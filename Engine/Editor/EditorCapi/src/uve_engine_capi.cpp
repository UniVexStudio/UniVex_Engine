// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

// GL/glew.h (pulled in via GlApi.h) must be included before anything that might transitively pull
// <GLFW/glfw3.h>, matching every other translation unit in Engine/Editor/Viewport that mixes the
// two (see that header's own comment) - nothing else in this file drags in GLFW directly, but
// this ordering is cheap insurance and keeps this file consistent with
// Engine/App/src/editor/main.cpp's ViewportPanelBackendUVE, which this file's render logic mirrors.
#include "univex/render/GlApi.h"

#include "uve/editor_capi/uve_engine_capi.h"

#include <algorithm>
#include <charconv>
#include <cstring>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "ViewportRenderPass.h"
#include "integration/EntityManagerEntitySource.h"
#include "univex/camera/OrbitCamera.h"

#include "uve/core/engine_core_uve.h"
#include "uve/debug/logging_macros_uve.h"

namespace {

// Mirrors Engine/App/src/editor/main.cpp's ViewportPanelBackendUVE, minus the ImGui-specific
// mouse-input and EditorUVE-selection/overlay wiring - this first slice has no EditorUVE at all,
// only EngineCoreUVE plus the same real grid/orbit-camera/proxy-cube renderer, to prove the
// native-hosting + shared-GL-context pipe works before EditorBridgeUVE enters the picture in a
// later slice.
class MinimalViewportBackendUVE final {
public:
    explicit MinimalViewportBackendUVE(UVE::Scene::IEntityManagerUVE& entityManager)
        : entitySource_(entityManager) {}

    ~MinimalViewportBackendUVE() { DestroyFramebuffersUVE(); }

    MinimalViewportBackendUVE(const MinimalViewportBackendUVE&) = delete;
    MinimalViewportBackendUVE& operator=(const MinimalViewportBackendUVE&) = delete;

    [[nodiscard]] UveViewportFrameUVE RenderUVE(const float availWidth, const float availHeight) {
        UveViewportFrameUVE result{};
        if (!EnsureGlewInitializedUVE() || !EnsureRenderPassUVE()) {
            return result;
        }

        const int width = std::max(1, static_cast<int>(availWidth));
        const int height = std::max(1, static_cast<int>(availHeight));
        if (!EnsureFramebuffersUVE(width, height)) {
            return result;
        }

        GLint previousFbo = 0;
        glGetIntegerv(GL_FRAMEBUFFER_BINDING, &previousFbo);
        glBindFramebuffer(GL_FRAMEBUFFER, msaaFbo_);
        glEnable(GL_MULTISAMPLE);
        renderPass_->RenderFrame(camera_, width, height);
        glBindFramebuffer(GL_READ_FRAMEBUFFER, msaaFbo_);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, resolveFbo_);
        glBlitFramebuffer(0, 0, width, height, 0, 0, width, height, GL_COLOR_BUFFER_BIT, GL_NEAREST);
        glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(previousFbo));

        result.gl_texture = resolveColorTexture_;
        result.used_width = static_cast<float>(width);
        result.used_height = static_cast<float>(height);
        return result;
    }

private:
    [[nodiscard]] bool EnsureGlewInitializedUVE() {
        if (glewInitialized_) {
            return true;
        }
        glewExperimental = GL_TRUE;
        if (const GLenum status = glewInit(); status != GLEW_OK) {
            UVE_ERROR("uve_engine_capi: glewInit failed for the viewport panel: {}",
                      reinterpret_cast<const char*>(glewGetErrorString(status)));
            return false;
        }
        glGetError(); // discard GLEW's benign core-profile extension-probe error
        glewInitialized_ = true;
        return true;
    }

    [[nodiscard]] bool EnsureRenderPassUVE() {
        if (renderPass_.has_value()) {
            return true;
        }
        std::string error;
        renderPass_ = univex::app::ViewportRenderPass::Create(error);
        if (!renderPass_.has_value()) {
            UVE_ERROR("uve_engine_capi: viewport render pass init failed: {}", error);
            return false;
        }
        renderPass_->SetEntitySource(&entitySource_);
        return true;
    }

    [[nodiscard]] bool EnsureFramebuffersUVE(const int width, const int height) {
        if (width == framebufferWidth_ && height == framebufferHeight_ && resolveFbo_ != 0U) {
            return true;
        }
        DestroyFramebuffersUVE();
        framebufferWidth_ = width;
        framebufferHeight_ = height;

        GLint maxSamples = 0;
        glGetIntegerv(GL_MAX_SAMPLES, &maxSamples);
        const GLsizei samples = std::min(maxSamples, 8);

        glGenFramebuffers(1, &msaaFbo_);
        glBindFramebuffer(GL_FRAMEBUFFER, msaaFbo_);
        glGenRenderbuffers(1, &msaaColorRb_);
        glBindRenderbuffer(GL_RENDERBUFFER, msaaColorRb_);
        glRenderbufferStorageMultisample(GL_RENDERBUFFER, samples, GL_RGBA8, width, height);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, msaaColorRb_);
        glGenRenderbuffers(1, &msaaDepthRb_);
        glBindRenderbuffer(GL_RENDERBUFFER, msaaDepthRb_);
        glRenderbufferStorageMultisample(GL_RENDERBUFFER, samples, GL_DEPTH24_STENCIL8, width, height);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, msaaDepthRb_);
        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
            UVE_ERROR("uve_engine_capi: viewport MSAA framebuffer incomplete");
            return false;
        }

        glGenFramebuffers(1, &resolveFbo_);
        glBindFramebuffer(GL_FRAMEBUFFER, resolveFbo_);
        glGenTextures(1, &resolveColorTexture_);
        glBindTexture(GL_TEXTURE_2D, resolveColorTexture_);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, resolveColorTexture_, 0);
        const bool complete = glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
        if (!complete) {
            UVE_ERROR("uve_engine_capi: viewport resolve framebuffer incomplete");
        }
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        return complete;
    }

    void DestroyFramebuffersUVE() {
        if (resolveColorTexture_ != 0U) {
            glDeleteTextures(1, &resolveColorTexture_);
        }
        if (resolveFbo_ != 0U) {
            glDeleteFramebuffers(1, &resolveFbo_);
        }
        if (msaaColorRb_ != 0U) {
            glDeleteRenderbuffers(1, &msaaColorRb_);
        }
        if (msaaDepthRb_ != 0U) {
            glDeleteRenderbuffers(1, &msaaDepthRb_);
        }
        if (msaaFbo_ != 0U) {
            glDeleteFramebuffers(1, &msaaFbo_);
        }
        resolveColorTexture_ = resolveFbo_ = msaaColorRb_ = msaaDepthRb_ = msaaFbo_ = 0U;
        framebufferWidth_ = framebufferHeight_ = 0;
    }

    univex::integration::EntityManagerEntitySource entitySource_;
    std::optional<univex::app::ViewportRenderPass> renderPass_;
    univex::camera::OrbitCamera camera_;
    bool glewInitialized_ = false;
    GLuint msaaFbo_ = 0U;
    GLuint msaaColorRb_ = 0U;
    GLuint msaaDepthRb_ = 0U;
    GLuint resolveFbo_ = 0U;
    GLuint resolveColorTexture_ = 0U;
    int framebufferWidth_ = 0;
    int framebufferHeight_ = 0;
};

// Mirrors Engine/App/src/editor/main.cpp's own ParseGlVersionUVE helper: lets a caller (e.g. a
// test harness running under Mesa llvmpipe, which does not support every desktop GL version) pick
// an explicit context version the same way the C++ ImGui editor's own --gl-version flag already
// does, rather than always requesting EngineConfigUVE's default.
void ApplyGlVersionOverrideUVE(const int argc, const char* const* argv, UVE::Core::EngineConfigUVE& config) {
    for (int index = 1; index < argc; ++index) {
        const std::string_view argument{argv[index]};
        if (argument != "--gl-version" || index + 1 >= argc) {
            continue;
        }
        const std::string_view value{argv[index + 1]};
        const std::size_t separator = value.find('.');
        if (separator == std::string_view::npos || separator == 0U || separator + 1U >= value.size()) {
            continue;
        }
        std::uint32_t major = 0;
        std::uint32_t minor = 0;
        const std::string_view majorText = value.substr(0U, separator);
        const std::string_view minorText = value.substr(separator + 1U);
        const auto [majorEnd, majorError] =
            std::from_chars(majorText.data(), majorText.data() + majorText.size(), major);
        const auto [minorEnd, minorError] =
            std::from_chars(minorText.data(), minorText.data() + minorText.size(), minor);
        if (majorError == std::errc{} && minorError == std::errc{} &&
            majorEnd == majorText.data() + majorText.size() && minorEnd == minorText.data() + minorText.size()) {
            config.windowGlVersionMajor = major;
            config.windowGlVersionMinor = minor;
        }
        return;
    }
}

} // namespace

struct UveEngineHandleUVE final {
    UVE::Core::EngineConfigUVE config;
    std::unique_ptr<UVE::Core::EngineCoreUVE> engine;
    std::optional<MinimalViewportBackendUVE> viewportBackend;
};

extern "C" {

UveEngineHandleUVE* uve_capi_create(const int argc, const char* const* argv) {
    auto handle = std::make_unique<UveEngineHandleUVE>();
    handle->config.logFilePath = "uve_editor_cs.log";
    handle->config.commandLineArgs.assign(argv + (argc > 0 ? 1 : 0), argv + argc);
    ApplyGlVersionOverrideUVE(argc, argv, handle->config);

    handle->engine = std::make_unique<UVE::Core::EngineCoreUVE>(handle->config);
    try {
        handle->engine->Init();
        if (!handle->engine->Load()) {
            handle->engine->Shutdown();
            return nullptr;
        }
    } catch (const std::exception& exception) {
        UVE_FATAL("uve_engine_capi: unhandled exception during Init/Load: {}", exception.what());
        return nullptr;
    }

    handle->viewportBackend.emplace(handle->engine->GetServicesUVE().GetEntityManagerUVE());
    return handle.release();
}

void uve_capi_destroy(UveEngineHandleUVE* const handle) {
    if (handle == nullptr) {
        return;
    }
    // Destroyed here, before engine->Shutdown() tears down the window/GL context below - its
    // destructor deletes real GL objects (framebuffers/textures) that must still be valid,
    // matching Engine/App/src/editor/main.cpp's own teardown ordering.
    handle->viewportBackend.reset();
    handle->engine->Shutdown();
    delete handle;
}

int uve_capi_tick_frame(UveEngineHandleUVE* const handle) {
    if (handle == nullptr) {
        return 0;
    }
    handle->engine->TickFrameUVE();
    return handle->engine->GetServicesUVE().GetWindowManagerUVE().IsCloseRequestedUVE() ? 0 : 1;
}

UveViewportFrameUVE uve_capi_render_viewport(UveEngineHandleUVE* const handle, const float avail_width,
                                              const float avail_height) {
    if (handle == nullptr || !handle->viewportBackend.has_value()) {
        return UveViewportFrameUVE{};
    }
    return handle->viewportBackend->RenderUVE(avail_width, avail_height);
}

void uve_capi_log_info(const char* const utf8_message) {
    if (utf8_message == nullptr) {
        return;
    }
    constexpr std::size_t kMaximumLoggedBytesUVE = 1024U;
    const std::string bounded(utf8_message, std::min(std::strlen(utf8_message), kMaximumLoggedBytesUVE));
    UVE_INFO("{}", bounded);
}

} // extern "C"
