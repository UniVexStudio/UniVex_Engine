// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
#pragma once

// GL/glew.h (pulled in via gl_api.h) must be included before anything that might transitively pull
// <GLFW/glfw3.h>, or GLFW's bundled GL header conflicts with GLEW's.
#include "univex/render/gl_api.h"

#include <cstdint>
#include <optional>
#include <string_view>

#include "editor.h"
#include "integration/editor_mesh_layer.h"
#include "univex/camera/orbit_camera.h"
#include "univex/render/infinite_grid_renderer.h"
#include "univex/render/shader_program.h"
#include "univex/render/studio_backdrop_renderer.h"
#include "viewport_render_pass.h"

namespace UVE::Core {
class EngineServicesUVE;
}

namespace UVE::Scene {
class IEntityManagerUVE;
}

namespace UVE::App::Viewports {

/// Owns the low-level OpenGL offscreen rendering surface bound to one `EngineServicesUVE` scene.
///
/// Manages lazy GLEW initialization, the `ViewportRenderPass`, MSAA + resolve framebuffers, the
/// fullscreen mesh-blit pass that composites `EditorMeshLayerUVE` colour and depth into the shared
/// framebuffer, lazy `StudioBackdropRenderer`, preview lighting/environment resolution, and axis
/// palette seeding. Composed by every dedicated editor viewport without forcing any viewport to
/// inherit unrelated camera, gizmo, or Play-in-Editor state.
class ViewportSurfaceUVE final {
public:
    explicit ViewportSurfaceUVE(Core::EngineServicesUVE& services);
    ~ViewportSurfaceUVE();

    ViewportSurfaceUVE(const ViewportSurfaceUVE&) = delete;
    ViewportSurfaceUVE& operator=(const ViewportSurfaceUVE&) = delete;
    ViewportSurfaceUVE(ViewportSurfaceUVE&&) = delete;
    ViewportSurfaceUVE& operator=(ViewportSurfaceUVE&&) = delete;

    /// Fullscreen-triangle vertex shader shared by fullscreen surface passes (mesh blit & depth).
    static constexpr std::string_view kFullscreenTriangleVertexShaderUVE = R"(#version 330 core
void main() {
    vec2 pos = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2);
    gl_Position = vec4(pos * 2.0 - 1.0, 0.0, 1.0);
}
)";

    [[nodiscard]] const Core::EngineServicesUVE* ServicesUVE() const noexcept { return &m_services; }
    [[nodiscard]] Core::EngineServicesUVE& MutableServicesUVE() noexcept { return m_services; }
    [[nodiscard]] Scene::IEntityManagerUVE& EntityManagerUVE() noexcept { return m_entityManager; }
    [[nodiscard]] const Scene::IEntityManagerUVE& EntityManagerUVE() const noexcept { return m_entityManager; }

    /// Resolved quality settings for a viewport surface preset (`0=Auto, 1=Low, 2=Medium, 3=High, 4=Ultra`).
    struct QualityConfigUVE final {
        float renderScale = 1.0F;
        int msaaSamples = 0;
        int fxaaQuality = 1;
        float casSharpen = 0.25F;
        bool ssaoEnabled = false;
        std::uint32_t ssaoQuality = 0U;
        std::uint32_t shadowAtlasResolution = 1024U;
        float maxAnisotropy = 4.0F;
        std::int32_t scalabilityMode = 1;
        bool gpuOutlineOnly = false;
    };

    /// Resolves quality settings for `qualityPreset` (with one-time GPU-tier auto-detection for Auto).
    [[nodiscard]] QualityConfigUVE ResolveQualityConfigUVE(int qualityPreset);

    /// Applies the selected quality preset to this surface, the 3D renderer, and publishes
    /// `overlayState.resolvedRenderScale`.
    void ApplyQualityPresetUVE(int qualityPreset,
                               const Editor::EditorUVE::ViewportOverlayStateUVE& overlayState);

    /// True when the active quality preset prefers GPU-side selection outlines (avoiding glReadPixels).
    [[nodiscard]] bool PreferGpuSelectionOutlineUVE() const noexcept { return m_gpuOutlineOnly; }

    /// Ensures GLEW, `ViewportRenderPass`, and sized scene/MSAA/resolve framebuffers are ready.
    [[nodiscard]] bool PrepareFrameUVE(int width, int height);

    [[nodiscard]] univex::app::ViewportRenderPass& RenderPassUVE() noexcept { return *m_renderPass; }
    [[nodiscard]] const univex::app::ViewportRenderPass& RenderPassUVE() const noexcept { return *m_renderPass; }
    [[nodiscard]] univex::integration::EditorMeshLayerUVE& MeshLayerUVE() noexcept { return m_meshLayer; }
    [[nodiscard]] const univex::render::StudioBackdropRenderer* StudioBackdropUVE();

    /// Returns the active drawing framebuffer (MSAA FBO when multisampled, otherwise single-sample scene FBO).
    [[nodiscard]] GLuint ActiveDrawFboUVE() const noexcept { return m_msaaFbo != 0U ? m_msaaFbo : m_sceneFbo; }
    [[nodiscard]] GLuint MsaaFboUVE() const noexcept { return ActiveDrawFboUVE(); }
    [[nodiscard]] GLuint ResolveFboUVE() const noexcept { return m_resolveFbo; }
    [[nodiscard]] GLuint ResolveColorTextureUVE() const noexcept { return m_resolveColorTexture; }
    [[nodiscard]] GLuint FullscreenVaoUVE();

    [[nodiscard]] bool HasActiveWorldEnvironmentUVE() const;
    [[nodiscard]] bool HasAuthoredDirectionalLightUVE() const;

    void SetShadingModeUVE(std::int32_t mode);
    void ApplyPreviewLightingUVE(const Editor::EditorUVE::ViewportOverlayStateUVE& overlayState,
                                 bool previewAllowed);
    void ApplyAxisPaletteUVE(Editor::EditorUVE& editor,
                             const Editor::EditorUVE::ViewportOverlayStateUVE& overlayState);
    void ApplyGridAppearanceUVE(const Editor::EditorUVE& editor,
                                const Editor::EditorUVE::ViewportOverlayStateUVE& overlayState,
                                bool visible,
                                univex::render::GridPlane plane);

    [[nodiscard]] univex::integration::EditorMeshLayerResultUVE RenderSceneMeshLayerUVE(
        const univex::camera::OrbitCamera& camera,
        int width,
        int height,
        std::optional<Scene::EntityUVE> lookThroughCamera = std::nullopt);

    /// Binds the active draw framebuffer, configures multisampling, clears it, and returns the previous FBO.
    [[nodiscard]] GLint BeginPassUVE(int width, int height);

    /// Writes `EditorMeshLayerUVE` colour and real scene depth into the currently bound framebuffer.
    void BlitMeshLayerUVE(const univex::integration::EditorMeshLayerResultUVE& meshResult,
                          const univex::camera::OrbitCamera& camera,
                          int width,
                          int height);

    /// Resolves the scene framebuffer through the Post-Composite FXAA 3.11 + CAS sharpen pass into
    /// the 2D display texture, restores `previousFbo`, and returns the resolved native GL texture id.
    [[nodiscard]] std::uint64_t EndPassAndResolveUVE(int width, int height, GLint previousFbo);

private:
    [[nodiscard]] bool EnsureGlewInitializedUVE();
    [[nodiscard]] bool EnsureRenderPassUVE();
    [[nodiscard]] bool EnsureFramebuffersUVE(int width, int height);
    void DestroyFramebuffersUVE();
    [[nodiscard]] bool EnsureMeshBlitResourcesUVE();
    [[nodiscard]] bool EnsurePostAaResourcesUVE();
    void SeedEditorAxisColorsFromDefaultsUVE(Editor::EditorUVE& editor);

    Core::EngineServicesUVE& m_services;
    Scene::IEntityManagerUVE& m_entityManager;
    univex::integration::EditorMeshLayerUVE m_meshLayer;
    std::optional<univex::app::ViewportRenderPass> m_renderPass;
    std::optional<univex::render::StudioBackdropRenderer> m_studio;
    bool m_studioTried = false;

    bool m_glewInitialized = false;
    GLuint m_msaaFbo = 0U;
    GLuint m_msaaColorRb = 0U;
    GLuint m_msaaDepthRb = 0U;
    GLuint m_sceneFbo = 0U;
    GLuint m_sceneColorTexture = 0U;
    GLuint m_sceneDepthRb = 0U;
    GLuint m_resolveFbo = 0U;
    GLuint m_resolveColorTexture = 0U;
    int m_framebufferWidth = 0;
    int m_framebufferHeight = 0;
    int m_allocatedMsaaSamples = -1;

#if defined(UNIVEX_GL_ES)
    int m_desiredMsaaSamples = 0;
    int m_fxaaQuality = 1;
    float m_casSharpen = 0.28F;
    bool m_gpuOutlineOnly = true;
#else
    int m_desiredMsaaSamples = 2;
    int m_fxaaQuality = 2;
    float m_casSharpen = 0.22F;
    bool m_gpuOutlineOnly = false;
#endif
    int m_appliedQualityPreset = -1;
    bool m_gpuTierProbed = false;
    int m_detectedMobileTier = 1; // 0=Low, 1=Medium, 2=High

    std::optional<univex::render::ShaderProgram> m_meshBlitProgram;
    std::optional<univex::render::ShaderProgram> m_postAaProgram;
    bool m_postAaFailed = false;
    GLuint m_meshBlitVao = 0U;
    bool m_lastRenderUsedLookThroughCamera = false;
};

} // namespace UVE::App::Viewports
