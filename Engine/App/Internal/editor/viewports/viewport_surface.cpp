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

// Coverage comes from alpha, not depth: Renderer3D's tone-mapping pass reports which pixels it
// actually drew by writing alpha 1 there and 0 elsewhere (see fullscreen_quad.glsl), because the
// depth attachment handed to RenderFrameToTarget is cleared by that pass and never written,
// and the scene's own clear colour is indistinguishable from dark geometry.
constexpr std::string_view kMeshBlitFragmentShader = R"(#version 330 core
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
    ivec2 depthSize = textureSize(uSceneDepth, 0);
    vec2 uv = gl_FragCoord.xy / max(uOutSize, vec2(1.0));
    ivec2 depthCoord = clamp(ivec2(uv * vec2(depthSize)), ivec2(0), max(depthSize - ivec2(1), ivec2(0)));
    float d = texelFetch(uSceneDepth, depthCoord, 0).r;
    if (d >= 0.999999) {
        gl_FragDepth = 1.0;
        return;
    }
    if (uDirectSceneDepth != 0) {
        gl_FragDepth = clamp(d, 0.0, 1.0);
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
constexpr std::string_view kPostAaCasFragmentShader = R"(#version 330 core
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

} // namespace

ViewportSurface::ViewportSurface(Core::EngineServices& services)
    : m_services(services), m_entityManager(services.GetEntityManager()), m_meshLayer(services) {}

ViewportSurface::~ViewportSurface() {
    DestroyFramebuffers();
    if (m_meshBlitVao != 0U) {
        glDeleteVertexArrays(1, &m_meshBlitVao);
        m_meshBlitVao = 0U;
    }
}

ViewportSurface::QualityConfig ViewportSurface::ResolveQualityConfig(const int /*qualityPreset*/) {
    const Platform::RendererProfile effective =
        m_services.GetRenderer3D().GetRendererProfileResolution().effective;
    switch (effective) {
        case Platform::RendererProfile::Compatibility:
            // Zero MSAA, zero post-AA resolve pass, 512px shadows, 4 lights, RGBA8 color target.
            return QualityConfig{0.85F, 0, 0, 0.0F, false, 0U, 512U, 2.0F, 0, true, false};
        case Platform::RendererProfile::Mobile:
            // Balanced mobile PBR: 0x MSAA, lightweight FXAA + subtle CAS, 1024px shadows, 8 lights.
            return QualityConfig{0.95F, 0, 1, 0.22F, false, 0U, 1024U, 4.0F, 1, true, false};
        case Platform::RendererProfile::ForwardPlus:
        default:
            // Full desktop PBR: native 1.0x, 2x MSAA, FXAA high + CAS, SSAO + Bloom, 2048px shadows.
            return QualityConfig{1.00F, 2, 2, 0.20F, true, 1U, 2048U, 8.0F, 2, false, true};
    }
}

void ViewportSurface::ApplyQualityPreset(const int qualityPreset,
                                               const Editor::Editor::ViewportOverlayState& overlayState) {
    Render::IRenderer3D& renderer = m_services.GetRenderer3D();
    const Platform::RendererProfile effective = renderer.GetRendererProfileResolution().effective;
    const int effectiveIndex = static_cast<int>(effective);
    const QualityConfig cfg = ResolveQualityConfig(qualityPreset);
    overlayState.resolvedRenderScale = cfg.renderScale;

    if (m_appliedEffectiveProfile == effectiveIndex) {
        return;
    }
    m_appliedEffectiveProfile = effectiveIndex;
    m_desiredMsaaSamples = cfg.msaaSamples;
    m_fxaaQuality = cfg.fxaaQuality;
    m_casSharpen = cfg.casSharpen;
    m_gpuOutlineOnly = cfg.gpuOutlineOnly;

    Render::PostProcessSettings post{};
    post.bloomEnabled = cfg.bloomEnabled;
    post.ssaoEnabled = cfg.ssaoEnabled;
    post.ssaoQuality = cfg.ssaoQuality;
    post.ssaoBlurEnabled = cfg.ssaoEnabled;
    post.clusteredLightingEnabled =
        effective == Platform::RendererProfile::ForwardPlus &&
        m_services.GetRenderDevice().GetCapabilities().storageBuffers != Render::RenderFeatureSupport::Unsupported;
    // Post-composite FXAA + CAS runs once in EndPassAndResolve (or is skipped in Compatibility).
    post.fastApproximateAAEnabled = false;
    post.sharpeningAmount = 0.0F;
    post.acesToneMappingEnabled = true;
    post.toneMappingMethod = 2U;
    post.ditheringEnabled = effective != Platform::RendererProfile::Compatibility;
    renderer.SetPostProcessSettings(post);
    renderer.SetShadowAtlasTileResolution(cfg.shadowAtlasResolution);
    renderer.SetMaterialSamplerSettings(true, cfg.maxAnisotropy);
}

bool ViewportSurface::PrepareFrame(const int width, const int height) {
    return EnsureGlewInitialized() && EnsureRenderPass() && EnsureFramebuffers(width, height);
}

const univex::render::StudioBackdropRenderer* ViewportSurface::StudioBackdrop() {
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

GLuint ViewportSurface::FullscreenVao() {
    if (!EnsureMeshBlitResources()) {
        return 0U;
    }
    return m_meshBlitVao;
}

bool ViewportSurface::HasActiveWorldEnvironment() const {
    bool hasEnvironment = false;
    m_entityManager.ForEach<Scene::WorldEnvironment3DComponent>(
        [&hasEnvironment](Scene::Entity, const Scene::WorldEnvironment3DComponent& value) {
            hasEnvironment |= Scene::IsWorldEnvironment3DObjectComponentValid(value);
        });
    m_entityManager.ForEach<Scene::Sky3DComponent>(
        [&hasEnvironment](Scene::Entity, const Scene::Sky3DComponent& sky) {
            if (!sky.enabled || !Scene::IsSky3DObjectComponentValid(sky)) {
                return;
            }
            if (sky.source == Scene::SkySource::Panorama && sky.panoramaPath.empty()) {
                return;
            }
            hasEnvironment = true;
        });
    return hasEnvironment;
}

bool ViewportSurface::HasAuthoredDirectionalLight() const {
    return univex::integration::HasAuthoredDirectionalLightNode(m_entityManager);
}

void ViewportSurface::SetShadingMode(const std::int32_t mode) {
    m_services.GetRenderer3D().SetShadingMode(mode);
}

void ViewportSurface::ApplyPreviewLighting(const Editor::Editor::ViewportOverlayState& overlayState,
                                                 const bool previewAllowed) {
    const bool hasAuthoredSun = HasAuthoredDirectionalLight();
    const bool hasAuthoredEnv = HasActiveWorldEnvironment();

    Scene::PreviewLightingSettings previewSettings{};
    previewSettings.sunEnabled = overlayState.previewSun && previewAllowed;
    previewSettings.environmentEnabled = overlayState.previewEnvironment && previewAllowed;
    previewSettings.sunAzimuthDegrees = overlayState.previewSunAzimuth;
    previewSettings.sunAltitudeDegrees = overlayState.previewSunAltitude;
    previewSettings.sunEnergy = overlayState.previewSunEnergy;
    previewSettings.sunColor = Math::Vector3{overlayState.previewSunColor[0],
                                                overlayState.previewSunColor[1],
                                                overlayState.previewSunColor[2]};
    previewSettings.skyColor = Math::Vector3{overlayState.previewSkyColor[0],
                                                overlayState.previewSkyColor[1],
                                                overlayState.previewSkyColor[2]};
    previewSettings.horizonColor = Math::Vector3{overlayState.previewHorizonColor[0],
                                                    overlayState.previewHorizonColor[1],
                                                    overlayState.previewHorizonColor[2]};
    previewSettings.groundColor = Math::Vector3{overlayState.previewGroundColor[0],
                                                   overlayState.previewGroundColor[1],
                                                   overlayState.previewGroundColor[2]};
    previewSettings.ambientEnergy = overlayState.previewAmbientEnergy;

    const Scene::PreviewLightingFrame previewFrame =
        Scene::ResolvePreviewLighting(hasAuthoredSun, hasAuthoredEnv, previewSettings);
    m_meshLayer.SetPreviewLighting(previewFrame);
    m_services.GetRenderer3D().SetPreviewEnvironment(
        previewFrame.environmentActive
            ? std::optional<Scene::WorldEnvironmentFrame>(previewFrame.environment)
            : std::nullopt);
}

void ViewportSurface::ApplyAxisPalette(Editor::Editor& editor,
                                             const Editor::Editor::ViewportOverlayState& overlayState) {
    if (!m_renderPass.has_value()) {
        return;
    }
    if (overlayState.axisColorsValid) {
        static_cast<void>(m_renderPass->SetAxisPalette(univex::viewport::AxisPalette{
            univex::viewport::AxisRgb{overlayState.axisColorX.r, overlayState.axisColorX.g,
                                         overlayState.axisColorX.b},
            univex::viewport::AxisRgb{overlayState.axisColorY.r, overlayState.axisColorY.g,
                                         overlayState.axisColorY.b},
            univex::viewport::AxisRgb{overlayState.axisColorZ.r, overlayState.axisColorZ.g,
                                         overlayState.axisColorZ.b}}));
    } else {
        SeedEditorAxisColorsFromDefaults(editor);
    }
}

void ViewportSurface::ApplyGridAppearance(const Editor::Editor& editor,
                                                const Editor::Editor::ViewportOverlayState& overlayState,
                                                const bool visible,
                                                const univex::render::GridPlane plane) {
    if (!m_renderPass.has_value()) {
        return;
    }
    m_renderPass->Settings().viewGrid = visible;
    m_renderPass->SetGridOpacity(overlayState.gridOpacity);
    m_renderPass->SetGridCellSize(overlayState.gridCellSize);
    const auto& prefs = editor.GetExtendedPreferences();
    if (prefs.gridFadeDistance > 0.0) {
        auto& gridSettings = m_renderPass->Grid().Settings();
        const float minEnd = gridSettings.fadeStartDistanceScale + 1.0F;
        gridSettings.fadeEndDistanceScale =
            std::max(minEnd, static_cast<float>(std::clamp(prefs.gridFadeDistance * 0.25, 10.0, 500.0)));
    }
    m_renderPass->SetGridPlane(plane);
}

univex::integration::EditorMeshLayerResult ViewportSurface::RenderSceneMeshLayer(
    const univex::camera::OrbitCamera& camera,
    const int width,
    const int height,
    const std::optional<Scene::Entity> lookThroughCamera) {
    m_lastRenderUsedLookThroughCamera =
        lookThroughCamera.has_value() && *lookThroughCamera != Scene::kInvalidEntity;
    return m_meshLayer.Render(camera, static_cast<std::uint32_t>(width),
                                 static_cast<std::uint32_t>(height), lookThroughCamera);
}

GLint ViewportSurface::BeginPass(const int width, const int height) {
    GLint previousFbo = 0;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &previousFbo);
    glBindFramebuffer(GL_FRAMEBUFFER, ActiveDrawFbo());
    // Renderer3D may leave GL_SCISSOR_TEST or GL_CULL_FACE enabled at pass end; clear them so
    // the full viewport clears and fullscreen backdrop/mesh-blit/grid triangles are never clipped.
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_CULL_FACE);
#if !defined(UNIVEX_GL_ES)
    // OpenGL ES always rasterises multisampled targets with all their samples; desktop GL asks.
    if (m_msaaFbo != 0U) {
        glEnable(GL_MULTISAMPLE);
    } else {
        glDisable(GL_MULTISAMPLE);
    }
#endif
    m_renderPass->Clear(width, height);
    return previousFbo;
}

void ViewportSurface::BlitMeshLayer(const univex::integration::EditorMeshLayerResult& meshResult,
                                          const univex::camera::OrbitCamera& camera,
                                          const int width,
                                          const int height) {
    if (meshResult.colorTextureId == 0U || !EnsureMeshBlitResources()) {
        return;
    }
    // GL_ALWAYS rather than disabling GL_DEPTH_TEST: OpenGL skips depth writes while depth testing
    // is disabled regardless of glDepthMask.
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_CULL_FACE);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_ALWAYS);
    glDepthMask(GL_TRUE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    m_meshBlitProgram->Use();
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, meshResult.colorTextureId);
    if (m_meshBlitUniforms.meshColor >= 0) {
        glUniform1i(m_meshBlitUniforms.meshColor, 0);
    }

    const Render::SceneDepthExport depth = m_services.GetRenderer3D().GetLastSceneDepth();
    auto* const glDevice = dynamic_cast<Render::GlRenderDevice*>(&m_services.GetRenderDevice());
    const GLuint depthTexture =
        depth.valid && glDevice != nullptr ? static_cast<GLuint>(glDevice->GetNativeTextureId(depth.depthTexture))
                                           : 0U;
    if (m_meshBlitUniforms.hasDepth >= 0) {
        glUniform1i(m_meshBlitUniforms.hasDepth, depthTexture != 0U ? 1 : 0);
    }
    if (m_meshBlitUniforms.directSceneDepth >= 0) {
        glUniform1i(m_meshBlitUniforms.directSceneDepth, m_lastRenderUsedLookThroughCamera ? 1 : 0);
    }
    if (depthTexture != 0U) {
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, depthTexture);
        if (m_meshBlitUniforms.sceneDepth >= 0) {
            glUniform1i(m_meshBlitUniforms.sceneDepth, 1);
        }
        if (m_meshBlitUniforms.sceneInvViewProj >= 0) {
            std::array<float, 16> invColMajor{};
            for (std::size_t r = 0U; r < 4U; ++r) {
                for (std::size_t c = 0U; c < 4U; ++c) {
                    invColMajor[c * 4U + r] = depth.inverseViewProjection.m[r][c];
                }
            }
            glUniformMatrix4fv(m_meshBlitUniforms.sceneInvViewProj, 1, GL_FALSE, invColMajor.data());
        }
        const float aspect = static_cast<float>(width) / static_cast<float>(std::max(height, 1));
        if (m_meshBlitUniforms.viewProj >= 0) {
            glUniformMatrix4fv(m_meshBlitUniforms.viewProj, 1, GL_FALSE, camera.ViewProjection(aspect).Data());
        }
        if (m_meshBlitUniforms.outSize >= 0) {
            glUniform2f(m_meshBlitUniforms.outSize, static_cast<float>(width), static_cast<float>(height));
        }
        glActiveTexture(GL_TEXTURE0);
    }

    glBindVertexArray(m_meshBlitVao);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindVertexArray(0);
    glActiveTexture(GL_TEXTURE0);
    glDisable(GL_BLEND);
    glDepthFunc(GL_LESS);
}

std::uint64_t ViewportSurface::EndPassAndResolve(const int width, const int height, const GLint previousFbo) {
    if (m_msaaFbo == 0U && m_fxaaQuality == 0 && m_casSharpen <= 0.001F) {
        glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(previousFbo));
        return static_cast<std::uint64_t>(m_sceneColorTexture);
    }

    if (m_msaaFbo != 0U) {
        glBindFramebuffer(GL_READ_FRAMEBUFFER, m_msaaFbo);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, m_sceneFbo);
        glBlitFramebuffer(0, 0, width, height, 0, 0, width, height, GL_COLOR_BUFFER_BIT, GL_NEAREST);
    }

    if ((m_fxaaQuality > 0 || m_casSharpen > 0.001F) && EnsurePostAaResources()) {
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
        if (m_postAaUniforms.sceneColor >= 0) {
            glUniform1i(m_postAaUniforms.sceneColor, 0);
        }
        if (m_postAaUniforms.invSize >= 0) {
            glUniform2f(m_postAaUniforms.invSize,
                        1.0F / static_cast<float>(std::max(width, 1)),
                        1.0F / static_cast<float>(std::max(height, 1)));
        }
        if (m_postAaUniforms.fxaaQuality >= 0) {
            glUniform1i(m_postAaUniforms.fxaaQuality, m_fxaaQuality);
        }
        if (m_postAaUniforms.casSharpen >= 0) {
            glUniform1f(m_postAaUniforms.casSharpen, m_casSharpen);
        }

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

bool ViewportSurface::EnsureGlewInitialized() {
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

bool ViewportSurface::EnsureRenderPass() {
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

bool ViewportSurface::EnsureFramebuffers(const int width, const int height) {
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
    DestroyFramebuffers();
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

void ViewportSurface::DestroyFramebuffers() {
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

bool ViewportSurface::EnsureMeshBlitResources() {
    if (m_meshBlitProgram.has_value()) {
        return true;
    }
    std::string error;
    m_meshBlitProgram = univex::render::ShaderProgram::Build(kFullscreenTriangleVertexShader,
                                                             kMeshBlitFragmentShader, error);
    if (!m_meshBlitProgram.has_value()) {
        UVE_ERROR("uve_editor_app: viewport mesh blit shader build failed: {}", error);
        return false;
    }
    m_meshBlitUniforms.meshColor = m_meshBlitProgram->UniformLocation("uMeshColor");
    m_meshBlitUniforms.hasDepth = m_meshBlitProgram->UniformLocation("uHasDepth");
    m_meshBlitUniforms.directSceneDepth = m_meshBlitProgram->UniformLocation("uDirectSceneDepth");
    m_meshBlitUniforms.sceneDepth = m_meshBlitProgram->UniformLocation("uSceneDepth");
    m_meshBlitUniforms.sceneInvViewProj = m_meshBlitProgram->UniformLocation("uSceneInvViewProj");
    m_meshBlitUniforms.viewProj = m_meshBlitProgram->UniformLocation("uViewProj");
    m_meshBlitUniforms.outSize = m_meshBlitProgram->UniformLocation("uOutSize");
    if (m_meshBlitVao == 0U) {
        glGenVertexArrays(1, &m_meshBlitVao);
    }
    return true;
}

bool ViewportSurface::EnsurePostAaResources() {
    if (m_postAaFailed) {
        return false;
    }
    if (!m_postAaProgram.has_value()) {
        std::string error;
        m_postAaProgram = univex::render::ShaderProgram::Build(kFullscreenTriangleVertexShader,
                                                               kPostAaCasFragmentShader, error);
        if (!m_postAaProgram.has_value()) {
            UVE_ERROR("uve_editor_app: viewport FXAA+CAS shader build failed: {}", error);
            m_postAaFailed = true;
            return false;
        }
        m_postAaUniforms.sceneColor = m_postAaProgram->UniformLocation("uSceneColor");
        m_postAaUniforms.invSize = m_postAaProgram->UniformLocation("uInvSize");
        m_postAaUniforms.fxaaQuality = m_postAaProgram->UniformLocation("uFxaaQuality");
        m_postAaUniforms.casSharpen = m_postAaProgram->UniformLocation("uCasSharpen");
    }
    if (m_meshBlitVao == 0U) {
        glGenVertexArrays(1, &m_meshBlitVao);
    }
    return true;
}

void ViewportSurface::SeedEditorAxisColorsFromDefaults(Editor::Editor& editor) {
    const univex::viewport::AxisPalette defaults;
    using EditorAxisColor = Editor::Editor::ViewportAxisColor;
    const auto toEditor = [](const univex::viewport::AxisRgb& color) {
        return EditorAxisColor{color.r, color.g, color.b};
    };
    static_cast<void>(
        editor.SetViewportAxisColors(toEditor(defaults.x), toEditor(defaults.y), toEditor(defaults.z)));
}

} // namespace UVE::App::Viewports
