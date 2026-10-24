#include <android_native_app_glue.h>
#include <android/asset_manager.h>
#include <android/log.h>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <stdexcept>
#include "uve/core/engine_core_uve.h"
#include "uve/core/engine_services_uve.h"
#include "uve/entity/i_entity_manager_uve.h"
#include "uve/asset/i_asset_manager_uve.h"
#include "uve/scene/i_scene_graph_uve.h"
#include "uve/component/camera_component_uve.h"
#include "uve/component/transform_component_uve.h"
#include "uve/component/primitive_mesh_component_uve.h"
#include "uve/component/ui_text_component_uve.h"
#include "uve/input/android_motion_event_uve.h"
#include "uve/window/android_window_manager_uve.h"
#include "uve/rhi_opengl/gl_render_device_uve.h"
#include "uve/render_systems/i_renderer_3d_uve.h"

namespace {
struct Host {
    android_app* app = nullptr;
    std::unique_ptr<UVE::Core::EngineCoreUVE> engine;
    std::unique_ptr<UVE::Input::AndroidTouchRouterUVE> touch;
    UVE::Scene::EntityUVE cube = UVE::Scene::kInvalidEntityUVE;
    UVE::Scene::EntityUVE label = UVE::Scene::kInvalidEntityUVE;
    bool resumed = false;
    bool focused = false;
    bool failed = false;
    bool frameReported = false;
    std::uint64_t presentationBaseline = 0U;
    unsigned frameAttempts = 0U;
    bool previousTouch = false;
    void Cancel() { if (touch) touch->CancelUVE(); }
    void ResetPresentationProof() {
        frameReported = false;
        if (engine) {
            auto* window = dynamic_cast<UVE::Window::AndroidWindowManagerUVE*>(&engine->GetServicesUVE().GetWindowManagerUVE());
            if (window != nullptr) presentationBaseline = window->GetPresentationCountUVE();
        }
    }
    void Focus(bool value) {
        focused = value;
        __android_log_print(ANDROID_LOG_INFO, "UniVex", "UVE_ANDROID_FOCUS %d", value ? 1 : 0);
        if (engine) {
            auto* window = dynamic_cast<UVE::Window::AndroidWindowManagerUVE*>(&engine->GetServicesUVE().GetWindowManagerUVE());
            if (window != nullptr) window->SetFocusedUVE(value);
        }
        if (!value) Cancel();
    }
    void Stop() {
        Cancel(); touch.reset();
        if (engine) { engine->Shutdown(); engine.reset(); }
    }
    void UpdateDemo() {
        auto& services = engine->GetServicesUVE();
        auto& entities = services.GetEntityManagerUVE();
        const auto snapshot = services.GetMobileInputSystemUVE().GetSnapshotUVE();
        bool touched = false;
        for (const auto& contact : snapshot.touches) touched = touched || contact.active;
        if (previousTouch != touched) {
            __android_log_print(ANDROID_LOG_INFO, "UniVex", "UVE_ANDROID_TOUCH_APPLIED active=%d", touched ? 1 : 0);
            previousTouch = touched;
        }
        entities.GetComponentUVE<UVE::Scene::PrimitiveMeshComponentUVE>(cube).baseColor = touched
            ? UVE::Math::Vector3UVE{0.1F, 0.9F, 0.4F} : UVE::Math::Vector3UVE{0.2F, 0.5F, 0.9F};
        const auto diagnostics = services.GetRenderer3DUVE().GetLastFrameDiagnosticsUVE();
        if (frameAttempts++ < 3U) {
            __android_log_print(ANDROID_LOG_INFO, "UniVex", "UVE_ANDROID_FRAME primitives=%zu gl=%zu",
                diagnostics.primitiveDrawCallsRecorded, diagnostics.glDrawCallsIssued);
        }
        const auto* window = dynamic_cast<UVE::Window::AndroidWindowManagerUVE*>(&services.GetWindowManagerUVE());
        if (!frameReported && window != nullptr && window->GetPresentationCountUVE() > presentationBaseline &&
            diagnostics.primitiveDrawCallsRecorded > 0U && diagnostics.glDrawCallsIssued > 0U) {
            __android_log_print(ANDROID_LOG_INFO, "UniVex", "UVE_ANDROID_RENDERED");
            frameReported = true;
        }
        auto& text = entities.GetComponentUVE<UVE::Scene::UITextComponentUVE>(label);
        text.positionPixels = {16.0F, static_cast<float>(app->contentRect.top) + 28.0F};
    }
    void Start() {
        if (engine || app->window == nullptr || failed) return;
        try {
            UVE::Core::EngineConfigUVE config;
            config.nativeWindowHandleUVE = app->window;
            config.renderBackendPreferenceUVE = UVE::Core::RenderBackendPreferenceUVE::OpenGLUVE;
            config.shadowMapResolution = 512U;
            config.ssaoEnabledUVE = false;
            config.ambientColor = {0.7F, 0.7F, 0.7F};
            config.shaderSourceRealDirectoryUVE = "shaders";
            engine = std::make_unique<UVE::Core::EngineCoreUVE>(config);
            __android_log_print(ANDROID_LOG_INFO, "UniVex", "UVE_ANDROID_INIT_BEGIN");
            engine->Init();
            __android_log_print(ANDROID_LOG_INFO, "UniVex", "UVE_ANDROID_INIT_DONE");
            if (!engine->Load()) { Stop(); failed = true; return; }
            __android_log_print(ANDROID_LOG_INFO, "UniVex", "UVE_ANDROID_LOAD_DONE");
            auto* graphics = dynamic_cast<UVE::Render::GlRenderDeviceUVE*>(&engine->GetServicesUVE().GetRenderDeviceUVE());
            if (graphics == nullptr || !graphics->IsUsableUVE()) throw std::runtime_error("GLES renderer unavailable");
            auto* window = dynamic_cast<UVE::Window::AndroidWindowManagerUVE*>(&engine->GetServicesUVE().GetWindowManagerUVE());
            if (window != nullptr) window->SetFocusedUVE(focused);
            auto& entities = engine->GetServicesUVE().GetEntityManagerUVE();
            auto& graph = engine->GetServicesUVE().GetSceneGraphUVE();
            const auto camera = entities.CreateEntityUVE();
            UVE::Scene::TransformComponentUVE transform;
            transform.localPosition = {0.0F, 0.0F, 4.0F};
            graph.AttachTransformUVE(entities, camera, transform);
            entities.AddComponentUVE<UVE::Scene::CameraComponentUVE>(camera);
            engine->SetActiveCameraUVE(camera);
            cube = entities.CreateEntityUVE();
            graph.AttachTransformUVE(entities, cube, {});
            entities.AddComponentUVE<UVE::Scene::PrimitiveMeshComponentUVE>(cube);
            label = entities.CreateEntityUVE();
            UVE::Scene::UITextComponentUVE text;
            text.text = "UniVex Android: touch the screen";
            text.positionPixels = {16.0F, 48.0F};
            text.fontSize = 18.0F;
            entities.AddComponentUVE<UVE::Scene::UITextComponentUVE>(label, text);
            touch = std::make_unique<UVE::Input::AndroidTouchRouterUVE>(
                engine->GetServicesUVE().GetMobileInputSystemUVE());
            __android_log_print(ANDROID_LOG_INFO, "UniVex", "UVE_ANDROID_INITIALIZED");
        } catch (const std::exception& error) {
            __android_log_print(ANDROID_LOG_ERROR, "UniVex", "Startup failed: %s", error.what());
            Stop(); failed = true;
        }
    }
};
void Command(android_app* app, int32_t command) {
    auto& host = *static_cast<Host*>(app->userData);
    switch (command) {
    case APP_CMD_INIT_WINDOW:
        if (host.engine) {
            auto* window = dynamic_cast<UVE::Window::AndroidWindowManagerUVE*>(&host.engine->GetServicesUVE().GetWindowManagerUVE());
            if (window == nullptr || !window->AttachNativeWindowUVE(app->window)) {
                __android_log_print(ANDROID_LOG_ERROR, "UniVex", "EGL context recovery failed; runtime stopped");
                host.Stop(); host.failed = true;
            } else __android_log_print(ANDROID_LOG_INFO, "UniVex", "UVE_ANDROID_SURFACE_ATTACHED");
        } else host.Start();
        break;
    case APP_CMD_TERM_WINDOW:
        host.ResetPresentationProof();
        host.Cancel();
        if (host.engine) {
            auto* window = dynamic_cast<UVE::Window::AndroidWindowManagerUVE*>(&host.engine->GetServicesUVE().GetWindowManagerUVE());
            if (window != nullptr) window->DetachNativeWindowUVE();
            __android_log_print(ANDROID_LOG_INFO, "UniVex", "UVE_ANDROID_SURFACE_DETACHED");
        }
        break;
    case APP_CMD_RESUME: host.resumed = true; __android_log_print(ANDROID_LOG_INFO, "UniVex", "UVE_ANDROID_RESUMED"); break;
    case APP_CMD_PAUSE: host.resumed = false; host.ResetPresentationProof(); host.Cancel(); __android_log_print(ANDROID_LOG_INFO, "UniVex", "UVE_ANDROID_PAUSED"); break;
    case APP_CMD_GAINED_FOCUS: host.Focus(true); break;
    case APP_CMD_LOST_FOCUS: host.Focus(false); break;
    case APP_CMD_LOW_MEMORY:
        if (host.engine) host.engine->GetServicesUVE().GetAssetManagerUVE().CollectGarbageUVE();
        break;
    case APP_CMD_DESTROY: host.Stop(); __android_log_print(ANDROID_LOG_INFO, "UniVex", "UVE_ANDROID_DESTROYED"); break;
    default: break;
    }
}
int32_t Input(android_app* app, AInputEvent* event) {
    auto& host = *static_cast<Host*>(app->userData);
    const bool handled = host.touch && host.resumed && host.focused &&
        UVE::Input::RouteAndroidMotionEventUVE(*host.touch, event);
    if (handled) {
        const auto action = AMotionEvent_getAction(event) & AMOTION_EVENT_ACTION_MASK;
        if (action != AMOTION_EVENT_ACTION_MOVE)
            __android_log_print(ANDROID_LOG_INFO, "UniVex", "UVE_ANDROID_TOUCH action=%d", action);
    }
    return handled ? 1 : 0;
}
void CopyShaders(android_app* app) {
    const std::filesystem::path directory = std::filesystem::path(app->activity->internalDataPath) / "shaders";
    std::filesystem::create_directories(directory);
    AAssetDir* assets = AAssetManager_openDir(app->activity->assetManager, "shaders");
    if (assets == nullptr) throw std::runtime_error("Shader asset directory missing");
    while (const char* name = AAssetDir_getNextFileName(assets)) {
        const std::string path = std::string("shaders/") + name;
        AAsset* asset = AAssetManager_open(app->activity->assetManager, path.c_str(), AASSET_MODE_BUFFER);
        if (asset == nullptr) { AAssetDir_close(assets); throw std::runtime_error("Shader asset missing"); }
        if (AAsset_getLength(asset) > 0 && AAsset_getBuffer(asset) == nullptr) {
            AAsset_close(asset); AAssetDir_close(assets);
            throw std::runtime_error("Shader asset mapping failed");
        }
        std::ofstream output(directory / name, std::ios::binary);
        output.write(static_cast<const char*>(AAsset_getBuffer(asset)), AAsset_getLength(asset));
        AAsset_close(asset);
        if (!output) { AAssetDir_close(assets); throw std::runtime_error("Shader extraction failed"); }
    }
    AAssetDir_close(assets);
    std::filesystem::current_path(app->activity->internalDataPath);
}
}
extern "C" void android_main(android_app* app) {
    Host host; host.app = app;
    app->userData = &host; app->onAppCmd = Command; app->onInputEvent = Input;
    try { CopyShaders(app); }
    catch (const std::exception& error) {
        __android_log_print(ANDROID_LOG_ERROR, "UniVex", "Assets failed: %s", error.what());
        host.failed = true;
    }
    while (app->destroyRequested == 0) {
        const bool active = host.engine && host.resumed && app->window != nullptr;
        android_poll_source* source = nullptr;
        const int result = ALooper_pollOnce(active ? 0 : -1, nullptr, nullptr, reinterpret_cast<void**>(&source));
        if (result >= 0 && source != nullptr) source->process(app, source);
        if (app->destroyRequested != 0) break;
        if (host.engine && host.resumed && app->window != nullptr) {
            try {
                host.engine->TickFrameUVE(); host.UpdateDemo();
                if (!host.engine->GetServicesUVE().GetWindowManagerUVE().IsValidUVE()) {
                    __android_log_print(ANDROID_LOG_ERROR, "UniVex", "EGL context lost; runtime stopped safely");
                    host.Stop(); host.failed = true;
                }
            }
            catch (const std::exception& error) {
                __android_log_print(ANDROID_LOG_ERROR, "UniVex", "Frame failed: %s", error.what());
                host.Stop(); host.failed = true;
            }
        }
    }
    host.Stop();
}
