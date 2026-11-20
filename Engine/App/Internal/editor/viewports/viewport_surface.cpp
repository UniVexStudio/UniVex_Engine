// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
#include "viewport_surface.h"

#include <algorithm>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include "engine_services.h"
#include "gl_render_device.h"
#include "i_entity_manager.h"
#include "logging_macros.h"
#include "preview_lighting.h"
#include "sky_3d.h"
#include "vector3.h"
#include "world_environment_3d.h"

namespace UVE::App::Viewports {
namespace {

// Coverage comes from alpha, not depth: Renderer3DUVE's tone-mapping pass reports which pixels it
// actually drew by writing alpha 1 there and 0 elsewhere (see fullscreen_quad.glsl), because the
// depth attachment handed to RenderFrameToTargetUVE is cleared by that pass and never written,
// and the scene's own clear colour is indistinguishable from dark geometry.
constexpr std::string_view kMeshBlitFragmentShaderUVE = R"(#version 330 core
uniform sampler2D uMeshColor;
uniform sampler2D uSceneDepth;
uniform int uHasDepth;
uniform int uDirectSceneDepth;
uniform mat4 uSceneInvViewProj;
uniform mat4 uViewProj;
uniform vec2 uOutSize;
out vec4 FragColor;
void main() {
    vec4 meshColor = texelFetch(uMeshColor, ivec2(gl_FragCoord.xy), 0);
    if (meshColor.a < 0.01) {
        discard;
    }
    float coverageAlpha = meshColor.a >= 0.74 ? 1.0 : smoothstep(0.01, 0.74, meshColor.a);
    FragColor = vec4(meshColor.rgb, coverageAlpha);
    if (uHasDepth == 0) {
        gl_FragDepth = 0.0;
        return;
    }
    vec2 uv = gl_FragCoord.xy / max(uOutSize, vec2(1.0));
    float d = texture(uSceneDepth, uv).r;
    if (d >= 0.999999) {
        gl_FragDepth = 1.0;
        return;
    }
    if (uDirectSceneDepth != 0) {
        gl_FragDepth = clamp(d * 2.0 - 1.0, 0.0, 1.0);
        return;
    }
    vec4 world = uSceneInvViewProj * vec4(uv * 2.0 - 1.0, d * 2.0 - 1.0, 1.0);
    world /= max(abs(world.w), 1.0e-6);
    vec4 clip = uViewProj * vec4(world.xyz, 1.0);
    gl_FragDepth = clamp((clip.z / max(abs(clip.w), 1.0e-6)) * 0.5 + 0.5, 0.0, 1.0);
}
)";

} // namespace

ViewportSurfaceUVE::ViewportSurfaceUVE(Core::EngineServicesUVE& services)
    : m_services(services), m_entityManager(services.GetEntityManagerUVE()), m_meshLayer(services) {}

ViewportSurfaceUVE::~ViewportSurfaceUVE() {
    DestroyFramebuffersUVE();
    if (m_meshBlitVao != 0U) {
        glDeleteVertexArrays(1, &m_meshBlitVao);
        m_meshBlitVao = 0U;
    }
}

bool ViewportSurfaceUVE::PrepareFrameUVE(const int width, const int height) {
    return EnsureGlewInitializedUVE() && EnsureRenderPassUVE() && EnsureFramebuffersUVE(width, height);
}

const univex::render::StudioBackdropRenderer* ViewportSurfaceUVE::StudioBackdropUVE() {
    if (!m_studioTried) {
        m_studioTried = true;
        std::string error;
        m_studio = univex::render::StudioBackdropRenderer::Create(error);
        if (!m_studio.has_value()) {
            UVE_ERROR("uve_editor_app: studio backdrop unavailable: {}", error);
        }
    }
    return m_studio.has_value() ? &*m_studio : nullptr;
}

GLuint ViewportSurfaceUVE::FullscreenVaoUVE() {
    if (!EnsureMeshBlitResourcesUVE()) {
        return 0U;
    }
    return m_meshBlitVao;
}

bool ViewportSurfaceUVE::HasActiveWorldEnvironmentUVE() const {
    bool hasEnvironment = false;
    m_entityManager.ForEachUVE<Scene::WorldEnvironment3DComponentUVE>(
        [&hasEnvironment](Scene::EntityUVE, const Scene::WorldEnvironment3DComponentUVE& value) {
            hasEnvironment |= Scene::IsWorldEnvironment3DObjectComponentValidUVE(value);
        });
    m_entityManager.ForEachUVE<Scene::Sky3DComponentUVE>(
        [&hasEnvironment](Scene::EntityUVE, const Scene::Sky3DComponentUVE& sky) {
            if (!sky.enabled || !Scene::IsSky3DObjectComponentValidUVE(sky)) {
                return;
            }
            if (sky.source == Scene::SkySourceUVE::Panorama && sky.panoramaPath.empty()) {
                return;
            }
            hasEnvironment = true;
        });
    return hasEnvironment;
}

bool ViewportSurfaceUVE::HasAuthoredDirectionalLightUVE() const {
    return univex::integration::HasAuthoredDirectionalLightNodeUVE(m_entityManager);
}

void ViewportSurfaceUVE::SetShadingModeUVE(const std::int32_t mode) {
    m_services.GetRenderer3DUVE().SetShadingModeUVE(mode);
}

void ViewportSurfaceUVE::ApplyPreviewLightingUVE(const Editor::EditorUVE::ViewportOverlayStateUVE& overlayState,
                                                 const bool previewAllowed) {
    const bool hasAuthoredSun = HasAuthoredDirectionalLightUVE();
    const bool hasAuthoredEnv = HasActiveWorldEnvironmentUVE();

    Scene::PreviewLightingSettingsUVE previewSettings{};
    previewSettings.sunEnabled = overlayState.previewSun && previewAllowed;
    previewSettings.environmentEnabled = overlayState.previewEnvironment && previewAllowed;
    previewSettings.sunAzimuthDegrees = overlayState.previewSunAzimuth;
    previewSettings.sunAltitudeDegrees = overlayState.previewSunAltitude;
    previewSettings.sunEnergy = overlayState.previewSunEnergy;
    previewSettings.sunColor = Math::Vector3UVE{overlayState.previewSunColor[0],
                                                overlayState.previewSunColor[1],
                                                overlayState.previewSunColor[2]};
    previewSettings.skyColor = Math::Vector3UVE{overlayState.previewSkyColor[0],
                                                overlayState.previewSkyColor[1],
                                                overlayState.previewSkyColor[2]};
    previewSettings.horizonColor = Math::Vector3UVE{overlayState.previewHorizonColor[0],
                                                    overlayState.previewHorizonColor[1],
                                                    overlayState.previewHorizonColor[2]};
    previewSettings.groundColor = Math::Vector3UVE{overlayState.previewGroundColor[0],
                                                   overlayState.previewGroundColor[1],
                                                   overlayState.previewGroundColor[2]};
    previewSettings.ambientEnergy = overlayState.previewAmbientEnergy;

    const Scene::PreviewLightingFrameUVE previewFrame =
        Scene::ResolvePreviewLightingUVE(hasAuthoredSun, hasAuthoredEnv, previewSettings);
    m_meshLayer.SetPreviewLightingUVE(previewFrame);
    m_services.GetRenderer3DUVE().SetPreviewEnvironmentUVE(
        previewFrame.environmentActive
            ? std::optional<Scene::WorldEnvironmentFrameUVE>(previewFrame.environment)
            : std::nullopt);
}

void ViewportSurfaceUVE::ApplyAxisPaletteUVE(Editor::EditorUVE& editor,
                                             const Editor::EditorUVE::ViewportOverlayStateUVE& overlayState) {
    if (!m_renderPass.has_value()) {
        return;
    }
    if (overlayState.axisColorsValid) {
        static_cast<void>(m_renderPass->SetAxisPaletteUVE(univex::viewport::AxisPaletteUVE{
            univex::viewport::AxisRgbUVE{overlayState.axisColorX.r, overlayState.axisColorX.g,
                                         overlayState.axisColorX.b},
            univex::viewport::AxisRgbUVE{overlayState.axisColorY.r, overlayState.axisColorY.g,
                                         overlayState.axisColorY.b},
            univex::viewport::AxisRgbUVE{overlayState.axisColorZ.r, overlayState.axisColorZ.g,
                                         overlayState.axisColorZ.b}}));
    } else {
        SeedEditorAxisColorsFromDefaultsUVE(editor);
    }
}

void ViewportSurfaceUVE::ApplyGridAppearanceUVE(const Editor::EditorUVE& editor,
                                                const Editor::EditorUVE::ViewportOverlayStateUVE& overlayState,
                                                const bool visible,
                                                const univex::render::GridPlane plane) {
    if (!m_renderPass.has_value()) {
        return;
    }
    m_renderPass->Settings().viewGrid = visible;
    m_renderPass->SetGridOpacityUVE(overlayState.gridOpacity);
    m_renderPass->SetGridCellSizeUVE(overlayState.gridCellSize);
    const auto& prefs = editor.GetExtendedPreferencesUVE();
    if (prefs.gridFadeDistance > 0.0) {
        m_renderPass->Grid().Settings().fadeEndDistanceScale =
            static_cast<float>(std::clamp(prefs.gridFadeDistance * 0.25, 10.0, 500.0));
    }
    m_renderPass->SetGridPlaneUVE(plane);
}

univex::integration::EditorMeshLayerResultUVE ViewportSurfaceUVE::RenderSceneMeshLayerUVE(
    const univex::camera::OrbitCamera& camera,
    const int width,
    const int height,
    const std::optional<Scene::EntityUVE> lookThroughCamera) {
    m_lastRenderUsedLookThroughCamera =
        lookThroughCamera.has_value() && *lookThroughCamera != Scene::kInvalidEntityUVE;
    return m_meshLayer.RenderUVE(camera, static_cast<std::uint32_t>(width),
                                 static_cast<std::uint32_t>(height), lookThroughCamera);
}

GLint ViewportSurfaceUVE::BeginPassUVE(const int width, const int height) {
    GLint previousFbo = 0;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &previousFbo);
    glBindFramebuffer(GL_FRAMEBUFFER, m_msaaFbo);
    glEnable(GL_MULTISAMPLE);
    m_renderPass->ClearUVE(width, height);
    return previousFbo;
}

void ViewportSurfaceUVE::BlitMeshLayerUVE(const univex::integration::EditorMeshLayerResultUVE& meshResult,
                                          const univex::camera::OrbitCamera& camera,
                                          const int width,
                                          const int height) {
    if (meshResult.colorTextureId == 0U || !EnsureMeshBlitResourcesUVE()) {
        return;
    }
    // GL_ALWAYS rather than disabling GL_DEPTH_TEST: OpenGL skips depth writes while depth testing
    // is disabled regardless of glDepthMask.
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_ALWAYS);
    glDepthMask(GL_TRUE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    m_meshBlitProgram->Use();
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, meshResult.colorTextureId);
    glUniform1i(m_meshBlitProgram->UniformLocation("uMeshColor"), 0);

    const Render::SceneDepthExportUVE depth = m_services.GetRenderer3DUVE().GetLastSceneDepthUVE();
    auto* const glDevice = dynamic_cast<Render::GlRenderDeviceUVE*>(&m_services.GetRenderDeviceUVE());
    const GLuint depthTexture =
        depth.valid && glDevice != nullptr ? static_cast<GLuint>(glDevice->GetNativeTextureIdUVE(depth.depthTexture))
                                           : 0U;
    glUniform1i(m_meshBlitProgram->UniformLocation("uHasDepth"), depthTexture != 0U ? 1 : 0);
    glUniform1i(m_meshBlitProgram->UniformLocation("uDirectSceneDepth"),
                m_lastRenderUsedLookThroughCamera ? 1 : 0);
    if (depthTexture != 0U) {
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, depthTexture);
        glUniform1i(m_meshBlitProgram->UniformLocation("uSceneDepth"), 1);
        glUniformMatrix4fv(m_meshBlitProgram->UniformLocation("uSceneInvViewProj"), 1, GL_TRUE,
                           &depth.inverseViewProjection.m[0][0]);
        const float aspect = static_cast<float>(width) / static_cast<float>(std::max(height, 1));
        m_meshBlitProgram->SetMat4("uViewProj", camera.ViewProjection(aspect).Data());
        glUniform2f(m_meshBlitProgram->UniformLocation("uOutSize"), static_cast<float>(width),
                    static_cast<float>(height));
        glActiveTexture(GL_TEXTURE0);
    }

    glBindVertexArray(m_meshBlitVao);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindVertexArray(0);
    glActiveTexture(GL_TEXTURE0);
    glDisable(GL_BLEND);
    glDepthFunc(GL_LESS);
}

std::uint64_t ViewportSurfaceUVE::EndPassAndResolveUVE(const int width, const int height, const GLint previousFbo) {
    glBindFramebuffer(GL_READ_FRAMEBUFFER, m_msaaFbo);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, m_resolveFbo);
    glBlitFramebuffer(0, 0, width, height, 0, 0, width, height, GL_COLOR_BUFFER_BIT, GL_NEAREST);
    glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(previousFbo));
    return static_cast<std::uint64_t>(m_resolveColorTexture);
}

bool ViewportSurfaceUVE::EnsureGlewInitializedUVE() {
    if (m_glewInitialized) {
        return true;
    }
    glewExperimental = GL_TRUE;
    if (const GLenum status = glewInit(); status != GLEW_OK) {
        UVE_ERROR("uve_editor_app: glewInit failed for editor viewport: {}",
                  reinterpret_cast<const char*>(glewGetErrorString(status)));
        return false;
    }
    glGetError(); // discard GLEW's benign core-profile extension-probe error
    m_glewInitialized = true;
    return true;
}

bool ViewportSurfaceUVE::EnsureRenderPassUVE() {
    if (m_renderPass.has_value()) {
        return true;
    }
    std::string error;
    m_renderPass = univex::app::ViewportRenderPass::Create(error);
    if (!m_renderPass.has_value()) {
        UVE_ERROR("uve_editor_app: viewport render pass init failed: {}", error);
        return false;
    }
    return true;
}

bool ViewportSurfaceUVE::EnsureFramebuffersUVE(const int width, const int height) {
    if (width <= 0 || height <= 0) {
        return false;
    }
    if (width == m_framebufferWidth && height == m_framebufferHeight && m_resolveFbo != 0U) {
        return true;
    }
    DestroyFramebuffersUVE();
    m_framebufferWidth = width;
    m_framebufferHeight = height;

    GLint maxSamples = 0;
    glGetIntegerv(GL_MAX_SAMPLES, &maxSamples);
    const GLsizei samples = std::min(maxSamples, 4);

    glGenFramebuffers(1, &m_msaaFbo);
    glBindFramebuffer(GL_FRAMEBUFFER, m_msaaFbo);
    glGenRenderbuffers(1, &m_msaaColorRb);
    glBindRenderbuffer(GL_RENDERBUFFER, m_msaaColorRb);
    glRenderbufferStorageMultisample(GL_RENDERBUFFER, samples, GL_RGBA8, width, height);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, m_msaaColorRb);
    glGenRenderbuffers(1, &m_msaaDepthRb);
    glBindRenderbuffer(GL_RENDERBUFFER, m_msaaDepthRb);
    glRenderbufferStorageMultisample(GL_RENDERBUFFER, samples, GL_DEPTH24_STENCIL8, width, height);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, m_msaaDepthRb);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        UVE_ERROR("uve_editor_app: viewport MSAA framebuffer incomplete");
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        return false;
    }

    glGenFramebuffers(1, &m_resolveFbo);
    glBindFramebuffer(GL_FRAMEBUFFER, m_resolveFbo);
    glGenTextures(1, &m_resolveColorTexture);
    glBindTexture(GL_TEXTURE_2D, m_resolveColorTexture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_resolveColorTexture, 0);
    const bool complete = glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
    if (!complete) {
        UVE_ERROR("uve_editor_app: viewport resolve framebuffer incomplete");
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    return complete;
}

void ViewportSurfaceUVE::DestroyFramebuffersUVE() {
    if (m_resolveColorTexture != 0U) {
        glDeleteTextures(1, &m_resolveColorTexture);
    }
    if (m_resolveFbo != 0U) {
        glDeleteFramebuffers(1, &m_resolveFbo);
    }
    if (m_msaaColorRb != 0U) {
        glDeleteRenderbuffers(1, &m_msaaColorRb);
    }
    if (m_msaaDepthRb != 0U) {
        glDeleteRenderbuffers(1, &m_msaaDepthRb);
    }
    if (m_msaaFbo != 0U) {
        glDeleteFramebuffers(1, &m_msaaFbo);
    }
    m_resolveColorTexture = m_resolveFbo = m_msaaColorRb = m_msaaDepthRb = m_msaaFbo = 0U;
    m_framebufferWidth = m_framebufferHeight = 0;
}

bool ViewportSurfaceUVE::EnsureMeshBlitResourcesUVE() {
    if (m_meshBlitProgram.has_value()) {
        return true;
    }
    std::string error;
    m_meshBlitProgram = univex::render::ShaderProgram::Build(kFullscreenTriangleVertexShaderUVE,
                                                             kMeshBlitFragmentShaderUVE, error);
    if (!m_meshBlitProgram.has_value()) {
        UVE_ERROR("uve_editor_app: viewport mesh blit shader build failed: {}", error);
        return false;
    }
    glGenVertexArrays(1, &m_meshBlitVao);
    return true;
}

void ViewportSurfaceUVE::SeedEditorAxisColorsFromDefaultsUVE(Editor::EditorUVE& editor) {
    const univex::viewport::AxisPaletteUVE defaults;
    using EditorAxisColorUVE = Editor::EditorUVE::ViewportAxisColorUVE;
    const auto toEditor = [](const univex::viewport::AxisRgbUVE& color) {
        return EditorAxisColorUVE{color.r, color.g, color.b};
    };
    static_cast<void>(
        editor.SetViewportAxisColorsUVE(toEditor(defaults.x), toEditor(defaults.y), toEditor(defaults.z)));
}

} // namespace UVE::App::Viewports
