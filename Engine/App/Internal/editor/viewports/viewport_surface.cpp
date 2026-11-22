// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
#include "viewport_surface.h"

#include <algorithm>
#include <array>
#include <cctype>
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

// Single-pass Post-Composite FXAA 3.11 + AMD FidelityFX Contrast-Adaptive Sharpening (CAS).
// Runs ONCE after sky, PBR meshes, infinite grid, and GPU selection outline are composited so
// silhouettes, fine mesh edges, and grid lines are smoothed without double-blurring, then crisped
// with zero extra texture taps via adaptive anti-ringing CAS.
constexpr std::string_view kPostAaCasFragmentShaderUVE = R"(#version 330 core
uniform sampler2D uSceneColor;
uniform vec2 uInvSize;
uniform int uFxaaQuality;
uniform float uCasSharpen;
out vec4 FragColor;

float Luma(vec3 rgb) {
    return dot(rgb, vec3(0.299, 0.587, 0.114));
}

void main() {
    vec2 uv = gl_FragCoord.xy * uInvSize;
    vec4 centerSample = texture(uSceneColor, uv);
    vec3 color = centerSample.rgb;

    vec3 cN = texture(uSceneColor, uv + vec2(0.0, uInvSize.y)).rgb;
    vec3 cS = texture(uSceneColor, uv - vec2(0.0, uInvSize.y)).rgb;
    vec3 cE = texture(uSceneColor, uv + vec2(uInvSize.x, 0.0)).rgb;
    vec3 cW = texture(uSceneColor, uv - vec2(uInvSize.x, 0.0)).rgb;

    if (uFxaaQuality > 0) {
        float lumaM = Luma(color);
        float lumaN = Luma(cN);
        float lumaS = Luma(cS);
        float lumaE = Luma(cE);
        float lumaW = Luma(cW);

        float lumaMin = min(lumaM, min(min(lumaN, lumaS), min(lumaE, lumaW)));
        float lumaMax = max(lumaM, max(max(lumaN, lumaS), max(lumaE, lumaW)));
        float lumaRange = lumaMax - lumaMin;

        if (lumaRange >= max(0.0312, lumaMax * 0.125)) {
            float lumaNW = Luma(texture(uSceneColor, uv + vec2(-uInvSize.x, uInvSize.y)).rgb);
            float lumaNE = Luma(texture(uSceneColor, uv + vec2(uInvSize.x, uInvSize.y)).rgb);
            float lumaSW = Luma(texture(uSceneColor, uv + vec2(-uInvSize.x, -uInvSize.y)).rgb);
            float lumaSE = Luma(texture(uSceneColor, uv + vec2(uInvSize.x, -uInvSize.y)).rgb);

            float lumaNS = lumaN + lumaS;
            float lumaWE = lumaW + lumaE;
            float lumaNWSW = lumaNW + lumaSW;
            float lumaNENE = lumaNE + lumaSE;
            float lumaNWNE = lumaNW + lumaNE;
            float lumaSWSE = lumaSW + lumaSE;

            float edgeHorz = abs(-2.0 * lumaW + lumaNWSW) + abs(-2.0 * lumaM + lumaNS) * 2.0 +
                             abs(-2.0 * lumaE + lumaNENE);
            float edgeVert = abs(-2.0 * lumaN + lumaNWNE) + abs(-2.0 * lumaM + lumaWE) * 2.0 +
                             abs(-2.0 * lumaS + lumaSWSE);
            bool isHorz = (edgeHorz >= edgeVert);

            float luma1 = isHorz ? lumaS : lumaW;
            float luma2 = isHorz ? lumaN : lumaE;
            float grad1 = luma1 - lumaM;
            float grad2 = luma2 - lumaM;
            bool is1Steepest = abs(grad1) >= abs(grad2);
            float gradScaled = 0.25 * max(abs(grad1), abs(grad2));

            float stepLength = isHorz ? uInvSize.y : uInvSize.x;
            float lumaLocalAvg = 0.0;
            if (is1Steepest) {
                stepLength = -stepLength;
                lumaLocalAvg = 0.5 * (luma1 + lumaM);
            } else {
                lumaLocalAvg = 0.5 * (luma2 + lumaM);
            }

            vec2 currentUv = uv;
            if (isHorz) {
                currentUv.y += stepLength * 0.5;
            } else {
                currentUv.x += stepLength * 0.5;
            }

            vec2 offset = isHorz ? vec2(uInvSize.x, 0.0) : vec2(0.0, uInvSize.y);
            vec2 uv1 = currentUv - offset;
            vec2 uv2 = currentUv + offset;

            float lumaEnd1 = Luma(texture(uSceneColor, uv1).rgb) - lumaLocalAvg;
            float lumaEnd2 = Luma(texture(uSceneColor, uv2).rgb) - lumaLocalAvg;
            bool reached1 = abs(lumaEnd1) >= gradScaled;
            bool reached2 = abs(lumaEnd2) >= gradScaled;
            bool reachedBoth = reached1 && reached2;

            if (!reached1) {
                uv1 -= offset;
            }
            if (!reached2) {
                uv2 += offset;
            }

            int maxSteps = (uFxaaQuality >= 2) ? 6 : 3;
            for (int i = 0; i < 6; ++i) {
                if (i >= maxSteps || reachedBoth) {
                    break;
                }
                float stepMul = (i >= 3) ? 2.0 : 1.5;
                if (!reached1) {
                    lumaEnd1 = Luma(texture(uSceneColor, uv1).rgb) - lumaLocalAvg;
                    reached1 = abs(lumaEnd1) >= gradScaled;
                }
                if (!reached2) {
                    lumaEnd2 = Luma(texture(uSceneColor, uv2).rgb) - lumaLocalAvg;
                    reached2 = abs(lumaEnd2) >= gradScaled;
                }
                reachedBoth = reached1 && reached2;
                if (!reached1) {
                    uv1 -= offset * stepMul;
                }
                if (!reached2) {
                    uv2 += offset * stepMul;
                }
            }

            float dist1 = isHorz ? (uv.x - uv1.x) : (uv.y - uv1.y);
            float dist2 = isHorz ? (uv2.x - uv.x) : (uv2.y - uv.y);
            bool isDir1 = dist1 < dist2;
            float distFinal = min(dist1, dist2);
            float edgeThickness = max(dist1 + dist2, 1.0e-5);
            float pixelOffset = -distFinal / edgeThickness + 0.5;

            bool isLumaCenterSmaller = lumaM < lumaLocalAvg;
            bool correctVariation = ((isDir1 ? lumaEnd1 : lumaEnd2) < 0.0) != isLumaCenterSmaller;
            float finalOffset = correctVariation ? pixelOffset : 0.0;

            float lumaAvg = (1.0 / 12.0) * (2.0 * (lumaNS + lumaWE) + lumaNWSW + lumaNENE);
            float subpix1 = clamp(abs(lumaAvg - lumaM) / max(lumaRange, 1.0e-5), 0.0, 1.0);
            float subpix2 = (-2.0 * subpix1 + 3.0) * subpix1 * subpix1;
            float subpixOffset = subpix2 * subpix2 * 0.65;

            finalOffset = max(finalOffset, subpixOffset);
            vec2 finalUv = uv;
            if (isHorz) {
                finalUv.y += finalOffset * stepLength;
            } else {
                finalUv.x += finalOffset * stepLength;
            }
            color = texture(uSceneColor, finalUv).rgb;
        }
    }

    if (uCasSharpen > 0.001) {
        vec3 mn = min(color, min(min(cN, cS), min(cE, cW)));
        vec3 mx = max(color, max(max(cN, cS), max(cE, cW)));
        vec3 amp = clamp(min(mn, 1.0 - mx) / max(mx, vec3(1.0e-4)), 0.0, 1.0);
        amp = sqrt(amp);
        float peak = -1.0 / mix(8.0, 5.0, clamp(uCasSharpen, 0.0, 1.0));
        vec3 w = amp * peak;
        color = clamp((color + (cN + cS + cE + cW) * w) / (1.0 + 4.0 * w), 0.0, 1.0);
    }

    FragColor = vec4(color, centerSample.a);
}
)";

[[nodiscard]] int DetectMobileGpuTierUVE() {
    const char* rawRenderer = reinterpret_cast<const char*>(glGetString(GL_RENDERER));
    if (rawRenderer == nullptr) {
        return 1; // Medium fallback
    }
    std::string gpu(rawRenderer);
    std::transform(gpu.begin(), gpu.end(), gpu.begin(),
                   [](const unsigned char ch) { return static_cast<char>(std::tolower(ch)); });

    const auto has = [&gpu](const std::string_view token) { return gpu.find(token) != std::string::npos; };
    if (has("swiftshader") || has("llvmpipe") || has("softpipe") || has("virgl") || has("emulator") ||
        has("mali-g31") || has("mali-g52") || has("mali-g57") || has("mali-t") || has("mali-4") ||
        has("powervr") || has("adreno (tm) 5") || has("adreno 5") || has("adreno (tm) 61") ||
        has("adreno 61") || has("adreno (tm) 62") || has("adreno 62") || has("adreno (tm) 63") ||
        has("adreno 63")) {
        return 0; // Low-tier mobile GPU
    }
    if (has("adreno (tm) 7") || has("adreno 7") || has("adreno (tm) 8") || has("adreno 8") ||
        has("immortalis") || has("mali-g715") || has("mali-g720") || has("xclipse") || has("apple")) {
        return 2; // High-tier flagship mobile GPU
    }
    return 1; // Mid-range mobile GPU
}

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

ViewportSurfaceUVE::QualityConfigUVE ViewportSurfaceUVE::ResolveQualityConfigUVE(const int qualityPreset) {
    const int clamped = std::clamp(qualityPreset, 0, 4);
#if defined(UNIVEX_GL_ES)
    if (!m_gpuTierProbed && EnsureGlewInitializedUVE()) {
        m_gpuTierProbed = true;
        m_detectedMobileTier = DetectMobileGpuTierUVE();
    }
    const int effective = clamped == 0 ? (m_detectedMobileTier == 0 ? 1 : (m_detectedMobileTier == 2 ? 3 : 2)) : clamped;
    switch (effective) {
        case 1: // Low (60-120 FPS)
            return QualityConfigUVE{0.65F, 0, 1, 0.32F, false, 0U, 512U, 2.0F, 0, true};
        case 2: // Medium (Balanced)
            return QualityConfigUVE{0.80F, 0, 1, 0.28F, false, 0U, 1024U, 4.0F, 1, true};
        case 3: // High (Crisp Native-Near + FXAA)
            return QualityConfigUVE{0.92F, 0, 2, 0.26F, false, 0U, 1024U, 8.0F, 2, true};
        case 4: // Ultra (Native + 2x MSAA + SSAO)
        default:
            return QualityConfigUVE{1.00F, 2, 2, 0.24F, true, 1U, 2048U, 16.0F, 2, true};
    }
#else
    const int effective = clamped == 0 ? 3 : clamped;
    switch (effective) {
        case 1: // Low
            return QualityConfigUVE{0.70F, 0, 1, 0.30F, false, 0U, 512U, 2.0F, 0, true};
        case 2: // Medium
            return QualityConfigUVE{0.85F, 0, 1, 0.26F, false, 0U, 1024U, 4.0F, 1, true};
        case 3: // High (Desktop default)
            return QualityConfigUVE{1.00F, 2, 2, 0.22F, true, 1U, 2048U, 8.0F, 2, false};
        case 4: // Ultra
        default:
            return QualityConfigUVE{1.00F, 4, 2, 0.20F, true, 2U, 2048U, 16.0F, 2, false};
    }
#endif
}

void ViewportSurfaceUVE::ApplyQualityPresetUVE(const int qualityPreset,
                                               const Editor::EditorUVE::ViewportOverlayStateUVE& overlayState) {
    const int clamped = std::clamp(qualityPreset, 0, 4);
    const QualityConfigUVE cfg = ResolveQualityConfigUVE(clamped);
    overlayState.resolvedRenderScale = cfg.renderScale;

    if (m_appliedQualityPreset == clamped) {
        return;
    }
    m_appliedQualityPreset = clamped;
    m_desiredMsaaSamples = cfg.msaaSamples;
    m_fxaaQuality = cfg.fxaaQuality;
    m_casSharpen = cfg.casSharpen;
    m_gpuOutlineOnly = cfg.gpuOutlineOnly;

    Render::IRenderer3DUVE& renderer = m_services.GetRenderer3DUVE();
    Render::PostProcessSettingsUVE post{};
    post.bloomEnabledUVE = true;
    post.ssaoEnabledUVE = cfg.ssaoEnabled;
    post.ssaoQualityUVE = cfg.ssaoQuality;
    post.ssaoBlurEnabledUVE = cfg.ssaoEnabled;
    // Post-composite FXAA 3.11 + CAS runs once in EndPassAndResolveUVE over meshes, silhouettes,
    // sky, and grid together, avoiding double-AA blur and saving a full-screen pass on mobile.
    post.fastApproximateAAEnabledUVE = false;
    post.sharpeningAmountUVE = 0.0F;
    post.acesToneMappingEnabledUVE = true;
    post.toneMappingMethodUVE = 2U;
    post.ditheringEnabledUVE = true;
    renderer.SetPostProcessSettingsUVE(post);
    renderer.SetShadowAtlasTileResolutionUVE(cfg.shadowAtlasResolution);
    renderer.SetMaterialSamplerSettingsUVE(true, cfg.maxAnisotropy);
    renderer.SetScalabilityModeUVE(cfg.scalabilityMode);
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
    glBindFramebuffer(GL_FRAMEBUFFER, ActiveDrawFboUVE());
#if !defined(UNIVEX_GL_ES)
    // OpenGL ES always rasterises multisampled targets with all their samples; desktop GL asks.
    if (m_msaaFbo != 0U) {
        glEnable(GL_MULTISAMPLE);
    } else {
        glDisable(GL_MULTISAMPLE);
    }
#endif
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
    if (m_msaaFbo != 0U) {
        glBindFramebuffer(GL_READ_FRAMEBUFFER, m_msaaFbo);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, m_sceneFbo);
        glBlitFramebuffer(0, 0, width, height, 0, 0, width, height, GL_COLOR_BUFFER_BIT, GL_NEAREST);
    }

    if ((m_fxaaQuality > 0 || m_casSharpen > 0.001F) && EnsurePostAaResourcesUVE()) {
        std::array<GLint, 4> previousViewport{};
        glGetIntegerv(GL_VIEWPORT, previousViewport.data());
        const GLboolean depthTest = glIsEnabled(GL_DEPTH_TEST);
        const GLboolean blend = glIsEnabled(GL_BLEND);
        const GLboolean cullFace = glIsEnabled(GL_CULL_FACE);
        const GLboolean scissorTest = glIsEnabled(GL_SCISSOR_TEST);

        glBindFramebuffer(GL_FRAMEBUFFER, m_resolveFbo);
        glViewport(0, 0, width, height);
        glDisable(GL_DEPTH_TEST);
        glDisable(GL_BLEND);
        glDisable(GL_CULL_FACE);
        glDisable(GL_SCISSOR_TEST);

        m_postAaProgram->Use();
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, m_sceneColorTexture);
        glUniform1i(m_postAaProgram->UniformLocation("uSceneColor"), 0);
        glUniform2f(m_postAaProgram->UniformLocation("uInvSize"),
                    1.0F / static_cast<float>(std::max(width, 1)),
                    1.0F / static_cast<float>(std::max(height, 1)));
        glUniform1i(m_postAaProgram->UniformLocation("uFxaaQuality"), m_fxaaQuality);
        glUniform1f(m_postAaProgram->UniformLocation("uCasSharpen"), m_casSharpen);

        glBindVertexArray(m_meshBlitVao);
        glDrawArrays(GL_TRIANGLES, 0, 3);
        glBindVertexArray(0);
        glBindTexture(GL_TEXTURE_2D, 0);

        glViewport(previousViewport[0], previousViewport[1], previousViewport[2], previousViewport[3]);
        if (depthTest == GL_TRUE) {
            glEnable(GL_DEPTH_TEST);
        }
        if (blend == GL_TRUE) {
            glEnable(GL_BLEND);
        }
        if (cullFace == GL_TRUE) {
            glEnable(GL_CULL_FACE);
        }
        if (scissorTest == GL_TRUE) {
            glEnable(GL_SCISSOR_TEST);
        }
        glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(previousFbo));
        return static_cast<std::uint64_t>(m_resolveColorTexture);
    }

    glBindFramebuffer(GL_READ_FRAMEBUFFER, m_sceneFbo);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, m_resolveFbo);
    glBlitFramebuffer(0, 0, width, height, 0, 0, width, height, GL_COLOR_BUFFER_BIT, GL_NEAREST);
    glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(previousFbo));
    return static_cast<std::uint64_t>(m_resolveColorTexture);
}

bool ViewportSurfaceUVE::EnsureGlewInitializedUVE() {
    if (m_glewInitialized) {
        return true;
    }
#if defined(UNIVEX_GL_ES)
    // OpenGL ES functions come straight from the platform library: there is nothing to load.
    m_glewInitialized = true;
#else
    glewExperimental = GL_TRUE;
    if (const GLenum status = glewInit(); status != GLEW_OK) {
        UVE_ERROR("uve_editor_app: glewInit failed for editor viewport: {}",
                  reinterpret_cast<const char*>(glewGetErrorString(status)));
        return false;
    }
    glGetError(); // discard GLEW's benign core-profile extension-probe error
    m_glewInitialized = true;
#endif
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
    GLint maxSamples = 0;
    glGetIntegerv(GL_MAX_SAMPLES, &maxSamples);
    const int targetSamples =
        m_desiredMsaaSamples > 1 ? std::min(static_cast<int>(maxSamples), m_desiredMsaaSamples) : 0;

    if (width == m_framebufferWidth && height == m_framebufferHeight && m_resolveFbo != 0U && m_sceneFbo != 0U &&
        targetSamples == m_allocatedMsaaSamples) {
        return true;
    }
    DestroyFramebuffersUVE();
    m_framebufferWidth = width;
    m_framebufferHeight = height;
    m_allocatedMsaaSamples = targetSamples;

    if (targetSamples > 1) {
        glGenFramebuffers(1, &m_msaaFbo);
        glBindFramebuffer(GL_FRAMEBUFFER, m_msaaFbo);
        glGenRenderbuffers(1, &m_msaaColorRb);
        glBindRenderbuffer(GL_RENDERBUFFER, m_msaaColorRb);
        glRenderbufferStorageMultisample(GL_RENDERBUFFER, static_cast<GLsizei>(targetSamples), GL_RGBA8, width, height);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, m_msaaColorRb);
        glGenRenderbuffers(1, &m_msaaDepthRb);
        glBindRenderbuffer(GL_RENDERBUFFER, m_msaaDepthRb);
        glRenderbufferStorageMultisample(GL_RENDERBUFFER, static_cast<GLsizei>(targetSamples), GL_DEPTH24_STENCIL8,
                                         width, height);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, m_msaaDepthRb);
        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
            UVE_ERROR("uve_editor_app: viewport MSAA framebuffer incomplete");
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
            return false;
        }
    }

    glGenFramebuffers(1, &m_sceneFbo);
    glBindFramebuffer(GL_FRAMEBUFFER, m_sceneFbo);
    glGenTextures(1, &m_sceneColorTexture);
    glBindTexture(GL_TEXTURE_2D, m_sceneColorTexture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_sceneColorTexture, 0);
    if (targetSamples <= 1) {
        glGenRenderbuffers(1, &m_sceneDepthRb);
        glBindRenderbuffer(GL_RENDERBUFFER, m_sceneDepthRb);
        glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, width, height);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, m_sceneDepthRb);
    }
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        UVE_ERROR("uve_editor_app: viewport scene framebuffer incomplete");
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
    if (m_sceneColorTexture != 0U) {
        glDeleteTextures(1, &m_sceneColorTexture);
    }
    if (m_sceneDepthRb != 0U) {
        glDeleteRenderbuffers(1, &m_sceneDepthRb);
    }
    if (m_sceneFbo != 0U) {
        glDeleteFramebuffers(1, &m_sceneFbo);
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
    m_resolveColorTexture = m_resolveFbo = m_sceneColorTexture = m_sceneDepthRb = m_sceneFbo = 0U;
    m_msaaColorRb = m_msaaDepthRb = m_msaaFbo = 0U;
    m_framebufferWidth = m_framebufferHeight = 0;
    m_allocatedMsaaSamples = -1;
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
    if (m_meshBlitVao == 0U) {
        glGenVertexArrays(1, &m_meshBlitVao);
    }
    return true;
}

bool ViewportSurfaceUVE::EnsurePostAaResourcesUVE() {
    if (m_postAaFailed) {
        return false;
    }
    if (!m_postAaProgram.has_value()) {
        std::string error;
        m_postAaProgram = univex::render::ShaderProgram::Build(kFullscreenTriangleVertexShaderUVE,
                                                               kPostAaCasFragmentShaderUVE, error);
        if (!m_postAaProgram.has_value()) {
            UVE_ERROR("uve_editor_app: viewport FXAA+CAS shader build failed: {}", error);
            m_postAaFailed = true;
            return false;
        }
    }
    if (m_meshBlitVao == 0U) {
        glGenVertexArrays(1, &m_meshBlitVao);
    }
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
