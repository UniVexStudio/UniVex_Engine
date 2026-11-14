// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "univex/render/gl_api.h"

#include <android_native_app_glue.h>
#include <android/asset_manager.h>
#include <android/input.h>
#include <android/keycodes.h>
#include <android/log.h>
#include <android/native_window.h>
#include <jni.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <system_error>
#include <vector>

#include "android_editor_bridge.h"
#include "android_window_manager.h"
#include "camera_3d.h"
#include "directional_light_3d.h"
#include "editor.h"
#include "editor_viewport_backends.h"
#include "engine_core.h"
#include "gl_render_device.h"
#include "scene_object_type.h"

namespace {

[[nodiscard]] bool IsSafeRelativePathUVE(const std::filesystem::path& path) {
    if (path.empty() || path.is_absolute() || path.has_root_name() || path.has_root_directory() ||
        path.lexically_normal() != path) {
        return false;
    }
    for (const auto& part : path) {
        if (part == ".." || part == ".") {
            return false;
        }
    }
    return true;
}

void CopyShadersUVE(android_app* const app, const std::filesystem::path& internalRoot) {
    const std::filesystem::path directory = internalRoot / "shaders";
    std::filesystem::create_directories(directory);
    AAssetDir* const assets = AAssetManager_openDir(app->activity->assetManager, "shaders");
    if (assets == nullptr) {
        throw std::runtime_error("Shader asset directory missing");
    }
    while (const char* const name = AAssetDir_getNextFileName(assets)) {
        const std::string assetPath = std::string("shaders/") + name;
        AAsset* const asset = AAssetManager_open(app->activity->assetManager, assetPath.c_str(), AASSET_MODE_BUFFER);
        if (asset == nullptr) {
            AAssetDir_close(assets);
            throw std::runtime_error("Shader asset missing");
        }
        if (AAsset_getLength(asset) > 0 && AAsset_getBuffer(asset) == nullptr) {
            AAsset_close(asset);
            AAssetDir_close(assets);
            throw std::runtime_error("Shader asset mapping failed");
        }
        std::ofstream output(directory / name, std::ios::binary);
        output.write(static_cast<const char*>(AAsset_getBuffer(asset)), AAsset_getLength(asset));
        AAsset_close(asset);
        if (!output) {
            AAssetDir_close(assets);
            throw std::runtime_error("Shader extraction failed");
        }
    }
    AAssetDir_close(assets);
}

[[nodiscard]] std::filesystem::path ExtractPrismAssetsUVE(android_app* const app,
                                                          const std::filesystem::path& internalRoot) {
    AAsset* const index =
        AAssetManager_open(app->activity->assetManager, "prism-assets-files.txt", AASSET_MODE_BUFFER);
    if (index == nullptr) {
        throw std::runtime_error("PRISM asset index missing");
    }
    const auto length = AAsset_getLength64(index);
    if (length < 1 || length > 4 * 1024 * 1024) {
        AAsset_close(index);
        throw std::runtime_error("Invalid PRISM asset index");
    }
    std::string names(static_cast<std::size_t>(length), '\0');
    const auto read = AAsset_read(index, names.data(), names.size());
    AAsset_close(index);
    if (read != length) {
        throw std::runtime_error("Truncated PRISM asset index");
    }

    const std::filesystem::path stage = internalRoot / "prism-assets-extracting";
    const std::filesystem::path finalDir = internalRoot / "prism_assets";
    std::filesystem::remove_all(stage);
    std::filesystem::create_directories(stage);

    std::istringstream lines(names);
    std::string line;
    std::size_t count = 0U;
    std::uint64_t total = 0U;
    while (std::getline(lines, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (line.empty()) {
            continue;
        }
        const std::filesystem::path relative(line);
        if (!IsSafeRelativePathUVE(relative) || ++count > 10000U) {
            throw std::runtime_error("Invalid PRISM asset entry");
        }
        const std::string packaged = "prism_assets/" + relative.generic_string();
        AAsset* const asset =
            AAssetManager_open(app->activity->assetManager, packaged.c_str(), AASSET_MODE_STREAMING);
        if (asset == nullptr) {
            throw std::runtime_error("Missing PRISM asset: " + relative.generic_string());
        }
        const auto bytes = AAsset_getLength64(asset);
        if (bytes < 0 || bytes > 64LL * 1024 * 1024 || total + static_cast<std::uint64_t>(bytes) > 256ULL * 1024 * 1024) {
            AAsset_close(asset);
            throw std::runtime_error("PRISM asset budget exceeded");
        }
        total += static_cast<std::uint64_t>(bytes);
        const std::filesystem::path destination = stage / relative;
        std::filesystem::create_directories(destination.parent_path());
        std::ofstream output(destination, std::ios::binary);
        std::array<char, 65536> buffer{};
        std::int64_t copied = 0;
        int chunk = 0;
        while ((chunk = AAsset_read(asset, buffer.data(), buffer.size())) > 0) {
            output.write(buffer.data(), chunk);
            copied += chunk;
        }
        AAsset_close(asset);
        if (chunk < 0 || copied != bytes || !output) {
            throw std::runtime_error("PRISM asset write failed");
        }
    }

    if (!std::filesystem::is_regular_file(stage / "fonts" / "Inter-Regular.otf")) {
        throw std::runtime_error("Extracted PRISM font assets incomplete");
    }
    std::filesystem::remove_all(finalDir);
    std::filesystem::rename(stage, finalDir);
    return finalDir;
}

class AndroidJniBridgeUVE final {
public:
    ~AndroidJniBridgeUVE() { ShutdownUVE(); }

    void BindUVE(android_app* const app) { m_app = app; }

    void ShutdownUVE() {
        if (m_app == nullptr || m_app->activity == nullptr || m_app->activity->vm == nullptr) {
            return;
        }
        JNIEnv* env = nullptr;
        const bool needDetach = AttachUVE(env);
        if (env != nullptr && m_keyEventClass != nullptr) {
            env->DeleteGlobalRef(m_keyEventClass);
            m_keyEventClass = nullptr;
        }
        if (needDetach) {
            m_app->activity->vm->DetachCurrentThread();
        }
        m_keyEventCtor = nullptr;
        m_getUnicodeChar = nullptr;
    }

    [[nodiscard]] std::uint32_t TranslateKeyUnicodeUVE(const std::int32_t keyCode,
                                                       const std::int32_t metaState) {
        if (m_app == nullptr || m_app->activity == nullptr || m_app->activity->vm == nullptr) {
            return 0U;
        }
        JNIEnv* env = nullptr;
        const bool needDetach = AttachUVE(env);
        if (env == nullptr) {
            return 0U;
        }
        std::uint32_t codepoint = 0U;
        if (env->PushLocalFrame(16) == JNI_OK) {
            if (m_keyEventClass == nullptr) {
                if (jclass localCls = env->FindClass("android/view/KeyEvent")) {
                    m_keyEventClass = static_cast<jclass>(env->NewGlobalRef(localCls));
                    m_keyEventCtor = env->GetMethodID(m_keyEventClass, "<init>", "(II)V");
                    m_getUnicodeChar = env->GetMethodID(m_keyEventClass, "getUnicodeChar", "(I)I");
                }
            }
            if (m_keyEventClass != nullptr && m_keyEventCtor != nullptr && m_getUnicodeChar != nullptr) {
                if (jobject ev = env->NewObject(m_keyEventClass, m_keyEventCtor, AKEY_EVENT_ACTION_DOWN, keyCode)) {
                    const jint ch = env->CallIntMethod(ev, m_getUnicodeChar, metaState);
                    if (ch > 0) {
                        codepoint = static_cast<std::uint32_t>(ch);
                    }
                }
            }
            if (env->ExceptionCheck()) {
                env->ExceptionClear();
                codepoint = 0U;
            }
            env->PopLocalFrame(nullptr);
        }
        if (needDetach) {
            m_app->activity->vm->DetachCurrentThread();
        }
        return codepoint;
    }

    void SetSoftKeyboardVisibleUVE(const bool visible) {
        if (m_app == nullptr || m_app->activity == nullptr || m_app->activity->vm == nullptr) {
            return;
        }
        JNIEnv* env = nullptr;
        const bool needDetach = AttachUVE(env);
        if (env == nullptr) {
            return;
        }
        if (env->PushLocalFrame(32) == JNI_OK) {
            jobject activity = m_app->activity->clazz;
            jclass activityCls = env->GetObjectClass(activity);
            jclass contextCls = env->FindClass("android/content/Context");
            jclass immCls = env->FindClass("android/view/inputmethod/InputMethodManager");
            jclass windowCls = env->FindClass("android/view/Window");
            jclass viewCls = env->FindClass("android/view/View");
            if (activityCls && contextCls && immCls && windowCls && viewCls) {
                jfieldID immField = env->GetStaticFieldID(contextCls, "INPUT_METHOD_SERVICE", "Ljava/lang/String;");
                jobject immServiceName = immField ? env->GetStaticObjectField(contextCls, immField) : nullptr;
                jmethodID getSysSvc =
                    env->GetMethodID(activityCls, "getSystemService", "(Ljava/lang/String;)Ljava/lang/Object;");
                jobject imm = (immServiceName && getSysSvc)
                                  ? env->CallObjectMethod(activity, getSysSvc, immServiceName)
                                  : nullptr;
                jmethodID getWindow = env->GetMethodID(activityCls, "getWindow", "()Landroid/view/Window;");
                jobject win = getWindow ? env->CallObjectMethod(activity, getWindow) : nullptr;
                jmethodID getDecor = win ? env->GetMethodID(windowCls, "getDecorView", "()Landroid/view/View;") : nullptr;
                jobject decor = getDecor ? env->CallObjectMethod(win, getDecor) : nullptr;
                if (imm != nullptr && decor != nullptr) {
                    if (visible) {
                        jmethodID showSoftInput =
                            env->GetMethodID(immCls, "showSoftInput", "(Landroid/view/View;I)Z");
                        if (showSoftInput != nullptr) {
                            env->CallBooleanMethod(imm, showSoftInput, decor, 0);
                        }
                    } else {
                        jmethodID getToken = env->GetMethodID(viewCls, "getWindowToken", "()Landroid/os/IBinder;");
                        jobject token = getToken ? env->CallObjectMethod(decor, getToken) : nullptr;
                        jmethodID hideSoftInput =
                            env->GetMethodID(immCls, "hideSoftInputFromWindow", "(Landroid/os/IBinder;I)Z");
                        if (token != nullptr && hideSoftInput != nullptr) {
                            env->CallBooleanMethod(imm, hideSoftInput, token, 0);
                        }
                    }
                }
            }
            if (env->ExceptionCheck()) {
                env->ExceptionClear();
            }
            env->PopLocalFrame(nullptr);
        }
        if (needDetach) {
            m_app->activity->vm->DetachCurrentThread();
        }
    }

private:
    [[nodiscard]] bool AttachUVE(JNIEnv*& env) const {
        env = nullptr;
        if (m_app == nullptr || m_app->activity == nullptr || m_app->activity->vm == nullptr) {
            return false;
        }
        JavaVM* const vm = m_app->activity->vm;
        if (vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6) == JNI_OK) {
            return false;
        }
        if (vm->AttachCurrentThread(&env, nullptr) != JNI_OK) {
            env = nullptr;
            return false;
        }
        return true;
    }

    android_app* m_app = nullptr;
    jclass m_keyEventClass = nullptr;
    jmethodID m_keyEventCtor = nullptr;
    jmethodID m_getUnicodeChar = nullptr;
};

struct EditorHostStateUVE final {
    android_app* app = nullptr;
    std::filesystem::path internalRoot;
    std::filesystem::path prismAssetsDir;
    std::filesystem::path workspaceRoot;
    std::filesystem::path defaultScenePath;

    AndroidJniBridgeUVE jni;
    std::unique_ptr<UVE::Core::EngineCoreUVE> engine;
    std::unique_ptr<UVE::Editor::EditorUVE> editor;
    std::unique_ptr<UVE::App::EditorViewportBackendsUVE> viewportBackends;

    bool resumed = false;
    bool focused = false;
    bool failed = false;
    bool frameReported = false;
    std::uint64_t presentationBaseline = 0U;

    void FailUVE(const char* const reason) {
        if (failed) {
            return;
        }
        failed = true;
        __android_log_print(ANDROID_LOG_ERROR, "UniVexEditor", "UVE_ANDROID_EDITOR_FAILED %s", reason);
    }

    void ResetPresentationProofUVE() {
        frameReported = false;
        if (engine) {
            auto* const window =
                dynamic_cast<UVE::Window::AndroidWindowManagerUVE*>(&engine->GetServicesUVE().GetWindowManagerUVE());
            if (window != nullptr) {
                presentationBaseline = window->GetPresentationCountUVE();
            }
        }
    }

    void FocusUVE(const bool value) {
        focused = value;
        __android_log_print(ANDROID_LOG_INFO, "UniVexEditor", "UVE_ANDROID_EDITOR_FOCUS %d", value ? 1 : 0);
        if (engine) {
            auto* const window =
                dynamic_cast<UVE::Window::AndroidWindowManagerUVE*>(&engine->GetServicesUVE().GetWindowManagerUVE());
            if (window != nullptr) {
                window->SetFocusedUVE(value);
            }
        }
        if (!value) {
            UVE::Editor::CancelAndroidEditorInputUVE();
        }
    }

    void StopUVE() {
        UVE::Editor::CancelAndroidEditorInputUVE();
        UVE::Editor::SetAndroidEditorCallbacksUVE({});
        if (engine) {
            engine->SetPostRenderCallbackUVE({});
        }
        if (editor) {
            editor->SetViewportPanelRendererUVE({});
        }
        viewportBackends.reset();
        if (editor) {
            editor->ShutdownUVE();
            editor.reset();
        }
        if (engine) {
            engine->Shutdown();
            engine.reset();
        }
        jni.ShutdownUVE();
    }

    void StartUVE() {
        if (engine || app->window == nullptr || failed) {
            return;
        }
        try {
            if (internalRoot.empty() || prismAssetsDir.empty()) {
                throw std::runtime_error("Android editor assets are not prepared");
            }
            workspaceRoot = internalRoot / "UniVexProject";
            const std::filesystem::path contentDir = workspaceRoot / "Content";
            const std::filesystem::path viewportDir = contentDir / "Viewport";
            const std::filesystem::path derivedDir = workspaceRoot / "DerivedData" / "Import";
            const std::filesystem::path savesDir = workspaceRoot / "saves";
            std::filesystem::create_directories(viewportDir);
            std::filesystem::create_directories(derivedDir);
            std::filesystem::create_directories(savesDir);
            defaultScenePath = viewportDir / "Viewport.uvscene";

            std::filesystem::current_path(internalRoot);

            UVE::Core::EngineConfigUVE config{};
            config.nativeWindowHandleUVE = app->window;
            config.renderBackendPreferenceUVE = UVE::Core::RenderBackendPreferenceUVE::OpenGLUVE;
            config.shadowMapResolution = 512U;
            config.ssaoEnabledUVE = false;
            config.focusedFrameRateCapUVE = 60U;
            config.unfocusedFrameRateCapUVE = 15U;
            config.shaderSourceRealDirectoryUVE = "shaders";
            config.shaderCachePath = "shader_cache/";
            config.logFilePath = internalRoot / "uve_editor.log";
            config.projectRootDirectoryUVE = workspaceRoot;
            config.projectContentRootUVE = contentDir;
            config.assetDatabaseFilePath = workspaceRoot / ".uvassetdb";
            config.derivedArtifactCacheRootUVE = derivedDir;
            config.projectSettingsFilePath = workspaceRoot / "project.uvsettings";
            config.inputMapFilePath = workspaceRoot / "project.uvinput";
            config.settingsFilePath = workspaceRoot / ".uvsettings";
            config.saveDirectoryPath = savesDir;
            config.crashDumpDirectoryUVE = internalRoot / "crash-dumps";

            engine = std::make_unique<UVE::Core::EngineCoreUVE>(config);
            __android_log_print(ANDROID_LOG_INFO, "UniVexEditor", "UVE_ANDROID_EDITOR_INIT_BEGIN");
            engine->Init();
            if (!engine->Load()) {
                StopUVE();
                FailUVE("engine load failed");
                return;
            }

            auto* const graphics =
                dynamic_cast<UVE::Render::GlRenderDeviceUVE*>(&engine->GetServicesUVE().GetRenderDeviceUVE());
            if (graphics == nullptr || !graphics->IsUsableUVE()) {
                throw std::runtime_error("GLES3 renderer unavailable");
            }
            auto* const window =
                dynamic_cast<UVE::Window::AndroidWindowManagerUVE*>(&engine->GetServicesUVE().GetWindowManagerUVE());
            if (window != nullptr) {
                window->SetFocusedUVE(focused);
                presentationBaseline = window->GetPresentationCountUVE();
            }

            UVE::Editor::AndroidEditorCallbacksUVE callbacks{};
            callbacks.setTextInputActive = [this](const bool active) {
                jni.SetSoftKeyboardVisibleUVE(active);
            };
            callbacks.requestQuit = [this] {
                if (app != nullptr && app->activity != nullptr) {
                    ANativeActivity_finish(app->activity);
                }
            };
            UVE::Editor::SetAndroidEditorCallbacksUVE(std::move(callbacks));

            editor = std::make_unique<UVE::Editor::EditorUVE>(
                engine->GetServicesUVE(), defaultScenePath, 100U, engine.get());
            editor->SetPrismUiEnabledUVE(true, prismAssetsDir);
            editor->InitUVE();
            if (!editor->IsPrismUiActiveUVE()) {
                throw std::runtime_error("PRISM editor UI initialization failed");
            }

            if (std::filesystem::exists(defaultScenePath)) {
                static_cast<void>(editor->LoadSceneUVE());
            } else {
                static_cast<void>(
                    editor->CreateDocumentSceneObjectUVE(UVE::Scene::Objects::SceneObjectKindUVE::DirectionalLight3D));
                static_cast<void>(
                    editor->CreateDocumentSceneObjectUVE(UVE::Scene::Objects::SceneObjectKindUVE::Camera3D));
                static_cast<void>(
                    editor->CreateDocumentSceneObjectUVE(UVE::Scene::Objects::SceneObjectKindUVE::BoxMesh3D));
                static_cast<void>(editor->SaveSceneUVE());
            }

            editor->SetDocumentSceneFactoryUVE([this] { return engine->CreateDocumentSceneUVE(); });
            editor->SetDocumentSceneClosingUVE([this](const UVE::Core::EngineServicesUVE& services) {
                if (viewportBackends != nullptr) {
                    viewportBackends->ForgetServicesUVE(services);
                }
            });

            viewportBackends = std::make_unique<UVE::App::EditorViewportBackendsUVE>(*editor, *engine);
            editor->SetViewportPanelRendererUVE(
                [this](const UVE::Editor::EditorUVE::ViewportContextUVE context,
                       const UVE::Math::Vector2UVE& availableSize,
                       UVE::Math::Vector2UVE& outUsedSize,
                       const UVE::Editor::EditorUVE::ViewportOverlayStateUVE& overlayState) {
                    return viewportBackends->RenderUVE(context, availableSize, outUsedSize, overlayState);
                });
            engine->SetPostRenderCallbackUVE([this] {
                if (editor) {
                    editor->RenderOverlayUVE();
                }
            });

            const int width = ANativeWindow_getWidth(app->window);
            const int height = ANativeWindow_getHeight(app->window);
            UVE::Editor::NotifyAndroidWindowAttachedUVE(app->window, width, height);

            __android_log_print(ANDROID_LOG_INFO, "UniVexEditor", "UVE_ANDROID_EDITOR_INITIALIZED");
        } catch (const std::exception& error) {
            __android_log_print(ANDROID_LOG_ERROR, "UniVexEditor", "Startup failed: %s", error.what());
            StopUVE();
            FailUVE(error.what());
        }
    }
};

void OnAppCommandUVE(android_app* const app, const int32_t command) {
    auto& state = *static_cast<EditorHostStateUVE*>(app->userData);
    switch (command) {
        case APP_CMD_INIT_WINDOW:
            if (state.engine) {
                auto* const window = dynamic_cast<UVE::Window::AndroidWindowManagerUVE*>(
                    &state.engine->GetServicesUVE().GetWindowManagerUVE());
                if (window == nullptr || !window->AttachNativeWindowUVE(app->window)) {
                    __android_log_print(ANDROID_LOG_ERROR, "UniVexEditor",
                                        "EGL context recovery failed; editor stopped");
                    state.StopUVE();
                    state.FailUVE("EGL context recovery failed");
                } else {
                    const int width = ANativeWindow_getWidth(app->window);
                    const int height = ANativeWindow_getHeight(app->window);
                    UVE::Editor::NotifyAndroidWindowAttachedUVE(app->window, width, height);
                    __android_log_print(ANDROID_LOG_INFO, "UniVexEditor", "UVE_ANDROID_EDITOR_SURFACE_ATTACHED");
                }
            } else {
                state.StartUVE();
            }
            break;
        case APP_CMD_TERM_WINDOW:
            state.ResetPresentationProofUVE();
            UVE::Editor::NotifyAndroidWindowDetachedUVE();
            if (state.engine) {
                auto* const window = dynamic_cast<UVE::Window::AndroidWindowManagerUVE*>(
                    &state.engine->GetServicesUVE().GetWindowManagerUVE());
                if (window != nullptr) {
                    window->DetachNativeWindowUVE();
                }
                __android_log_print(ANDROID_LOG_INFO, "UniVexEditor", "UVE_ANDROID_EDITOR_SURFACE_DETACHED");
            }
            break;
        case APP_CMD_WINDOW_RESIZED:
        case APP_CMD_CONFIG_CHANGED:
        case APP_CMD_CONTENT_RECT_CHANGED:
            if (app->window != nullptr) {
                UVE::Editor::NotifyAndroidWindowResizedUVE(
                    ANativeWindow_getWidth(app->window), ANativeWindow_getHeight(app->window));
            }
            break;
        case APP_CMD_RESUME:
            state.resumed = true;
            __android_log_print(ANDROID_LOG_INFO, "UniVexEditor", "UVE_ANDROID_EDITOR_RESUMED");
            break;
        case APP_CMD_PAUSE:
            state.resumed = false;
            state.ResetPresentationProofUVE();
            UVE::Editor::CancelAndroidEditorInputUVE();
            __android_log_print(ANDROID_LOG_INFO, "UniVexEditor", "UVE_ANDROID_EDITOR_PAUSED");
            break;
        case APP_CMD_GAINED_FOCUS:
            state.FocusUVE(true);
            break;
        case APP_CMD_LOST_FOCUS:
            state.FocusUVE(false);
            break;
        case APP_CMD_LOW_MEMORY:
            if (state.engine) {
                state.engine->GetServicesUVE().GetAssetManagerUVE().CollectGarbageUVE();
            }
            break;
        case APP_CMD_DESTROY:
            state.StopUVE();
            __android_log_print(ANDROID_LOG_INFO, "UniVexEditor", "UVE_ANDROID_EDITOR_DESTROYED");
            break;
        default:
            break;
    }
}

int32_t OnInputEventUVE(android_app* const app, AInputEvent* const event) {
    auto& state = *static_cast<EditorHostStateUVE*>(app->userData);
    if (!state.editor || !state.resumed) {
        return 0;
    }
    const int32_t type = AInputEvent_getType(event);
    if (type == AINPUT_EVENT_TYPE_MOTION) {
        const int32_t rawAction = AMotionEvent_getAction(event);
        const int32_t masked = rawAction & AMOTION_EVENT_ACTION_MASK;
        const int32_t actionIndex =
            (rawAction & AMOTION_EVENT_ACTION_POINTER_INDEX_MASK) >> AMOTION_EVENT_ACTION_POINTER_INDEX_SHIFT;
        const std::size_t count = AMotionEvent_getPointerCount(event);
        std::vector<UVE::Editor::AndroidTouchContactUVE> contacts;
        contacts.reserve(count);
        for (std::size_t i = 0U; i < count; ++i) {
            contacts.push_back(UVE::Editor::AndroidTouchContactUVE{
                AMotionEvent_getPointerId(event, i),
                AMotionEvent_getX(event, i),
                AMotionEvent_getY(event, i),
            });
        }
        UVE::Editor::AndroidTouchActionUVE mapped = UVE::Editor::AndroidTouchActionUVE::Move;
        switch (masked) {
            case AMOTION_EVENT_ACTION_DOWN: mapped = UVE::Editor::AndroidTouchActionUVE::Down; break;
            case AMOTION_EVENT_ACTION_MOVE: mapped = UVE::Editor::AndroidTouchActionUVE::Move; break;
            case AMOTION_EVENT_ACTION_UP: mapped = UVE::Editor::AndroidTouchActionUVE::Up; break;
            case AMOTION_EVENT_ACTION_CANCEL: mapped = UVE::Editor::AndroidTouchActionUVE::Cancel; break;
            case AMOTION_EVENT_ACTION_POINTER_DOWN: mapped = UVE::Editor::AndroidTouchActionUVE::PointerDown; break;
            case AMOTION_EVENT_ACTION_POINTER_UP: mapped = UVE::Editor::AndroidTouchActionUVE::PointerUp; break;
            case AMOTION_EVENT_ACTION_HOVER_MOVE: mapped = UVE::Editor::AndroidTouchActionUVE::HoverMove; break;
            case AMOTION_EVENT_ACTION_SCROLL: mapped = UVE::Editor::AndroidTouchActionUVE::Scroll; break;
            default: return 0;
        }
        const float scrollX = (masked == AMOTION_EVENT_ACTION_SCROLL)
                                  ? AMotionEvent_getAxisValue(event, AMOTION_EVENT_AXIS_HSCROLL, 0)
                                  : 0.0F;
        const float scrollY = (masked == AMOTION_EVENT_ACTION_SCROLL)
                                  ? AMotionEvent_getAxisValue(event, AMOTION_EVENT_AXIS_VSCROLL, 0)
                                  : 0.0F;
        const std::int32_t buttons = AMotionEvent_getButtonState(event);
        return UVE::Editor::DispatchAndroidTouchEventUVE(mapped, actionIndex, contacts, buttons, scrollX, scrollY)
                   ? 1
                   : 0;
    }

    if (type == AINPUT_EVENT_TYPE_KEY) {
        const std::int32_t keyCode = AKeyEvent_getKeyCode(event);
        const std::int32_t action = AKeyEvent_getAction(event);
        const std::int32_t metaState = AKeyEvent_getMetaState(event);
        const bool repeat = AKeyEvent_getRepeatCount(event) > 0;
        std::uint32_t unicodeChar = 0U;
        if (action == AKEY_EVENT_ACTION_DOWN && !repeat) {
            unicodeChar = state.jni.TranslateKeyUnicodeUVE(keyCode, metaState);
        } else if (action == AKEY_EVENT_ACTION_MULTIPLE && keyCode == AKEYCODE_UNKNOWN) {
            // Multiple characters may arrive without keycode; rely on normal key events otherwise.
        }
        return UVE::Editor::DispatchAndroidKeyEventUVE(keyCode, action, metaState, unicodeChar, repeat) ? 1 : 0;
    }

    return 0;
}

} // namespace

extern "C" void android_main(android_app* const app) {
    EditorHostStateUVE state{};
    state.app = app;
    state.jni.BindUVE(app);
    app->userData = &state;
    app->onAppCmd = OnAppCommandUVE;
    app->onInputEvent = OnInputEventUVE;

    try {
        state.internalRoot = std::filesystem::path(app->activity->internalDataPath);
        std::filesystem::create_directories(state.internalRoot);
        std::filesystem::current_path(state.internalRoot);
        CopyShadersUVE(app, state.internalRoot);
        state.prismAssetsDir = ExtractPrismAssetsUVE(app, state.internalRoot);
    } catch (const std::exception& error) {
        __android_log_print(ANDROID_LOG_ERROR, "UniVexEditor", "Assets failed: %s", error.what());
        state.FailUVE(error.what());
    }

    while (app->destroyRequested == 0) {
        const bool active = state.engine && state.editor && state.resumed && app->window != nullptr;
        android_poll_source* source = nullptr;
        const int result = ALooper_pollOnce(active ? 0 : -1, nullptr, nullptr, reinterpret_cast<void**>(&source));
        if (result >= 0 && source != nullptr) {
            source->process(app, source);
        }
        if (app->destroyRequested != 0) {
            break;
        }
        if (state.engine && state.editor && state.resumed && app->window != nullptr) {
            try {
                state.editor->TickUVE();
                state.engine->TickFrameUVE();
                if (!state.engine->GetServicesUVE().GetWindowManagerUVE().IsValidUVE()) {
                    __android_log_print(ANDROID_LOG_ERROR, "UniVexEditor",
                                        "EGL context lost; editor stopped safely");
                    state.StopUVE();
                    state.FailUVE("EGL context lost");
                } else if (!state.frameReported) {
                    const auto* const window = dynamic_cast<UVE::Window::AndroidWindowManagerUVE*>(
                        &state.engine->GetServicesUVE().GetWindowManagerUVE());
                    if (window != nullptr && window->GetPresentationCountUVE() > state.presentationBaseline) {
                        __android_log_print(ANDROID_LOG_INFO, "UniVexEditor", "UVE_ANDROID_EDITOR_RENDERED");
                        state.frameReported = true;
                    }
                }
            } catch (const std::exception& error) {
                __android_log_print(ANDROID_LOG_ERROR, "UniVexEditor", "Frame failed: %s", error.what());
                state.StopUVE();
                state.FailUVE(error.what());
            }
        }
    }
    state.StopUVE();
}
