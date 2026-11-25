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
class EngineServices;
}

namespace UVE::Scene {
class IEntityManager;
}

namespace UVE::App::Viewports {

/// Owns the low-level OpenGL offscreen rendering surface bound to one `EngineServices` scene.
///
/// Manages lazy GLEW initialization, the `ViewportRenderPass`, MSAA + resolve framebuffers, the
/// fullscreen mesh-blit pass that composites `EditorMeshLayer` colour and depth into the shared
/// framebuffer, lazy `StudioBackdropRenderer`, preview lighting/environment resolution, and axis
/// palette seeding. Composed by every dedicated editor viewport without forcing any viewport to
/// inherit unrelated camera, gizmo, or Play-in-Editor state.
class ViewportSurface final {
public:
    explicit ViewportSurface(Core::EngineServices& services);
    ~ViewportSurface();

    ViewportSurface(const ViewportSurface&) = delete;
    ViewportSurface& operator=(const ViewportSurface&) = delete;
    ViewportSurface(ViewportSurface&&) = delete;
    ViewportSurface& operator=(ViewportSurface&&) = delete;

    /// Fullscreen-triangle vertex shader shared by fullscreen surface passes (mesh blit & depth).
    static constexpr std::string_view kFullscreenTriangleVertexShader = R"(#version 330 core
void main() {
    vec2 pos = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2);
    gl_Position = vec4(pos * 2.0 - 1.0, 0.0, 1.0);
}
)";

    [[nodiscard]] const Core::EngineServices* Services() const noexcept { return &m_services; }
    [[nodiscard]] Core::EngineServices& MutableServices() noexcept { return m_services; }
    [[nodiscard]] Scene::IEntityManager& EntityManager() noexcept { return m_entityManager; }
    [[nodiscard]] const Scene::IEntityManager& EntityManager() const noexcept { return m_entityManager; }

    /// Resolved viewport surface settings derived from the active effective RendererProfile.
    struct QualityConfig final {
        float renderScale = 1.0F;
        int msaaSamples = 0;
        int fxaaQuality = 1;
        float casSharpen = 0.20F;
        bool ssaoEnabled = false;
        std::uint32_t ssaoQuality = 0U;
        std::uint32_t shadowAtlasResolution = 1024U;
        float maxAnisotropy = 4.0F;
        std::int32_t scalabilityMode = 1;
        bool gpuOutlineOnly = false;
        bool bloomEnabled = false;
    };

    /// Resolves viewport surface settings from the active effective RendererProfile.
    [[nodiscard]] QualityConfig ResolveQualityConfig(int qualityPreset);

    /// Syncs this surface and `overlayState.resolvedRenderScale` to the renderer's effective profile.
    void ApplyQualityPreset(int qualityPreset,
                               const Editor::Editor::ViewportOverlayState& overlayState);

    /// True when the active profile prefers GPU-side selection outlines (avoiding glReadPixels).
    [[nodiscard]] bool PreferGpuSelectionOutline() const noexcept { return m_gpuOutlineOnly; }

    /// True when the renderer is running the ultra-lightweight Compatibility profile.
    [[nodiscard]] bool IsCompatibilityProfile() const noexcept {
        return m_appliedEffectiveProfile == 0;
    }

    /// Ensures GLEW, `ViewportRenderPass`, and sized scene/MSAA/resolve framebuffers are ready.
    [[nodiscard]] bool PrepareFrame(int width, int height);

    [[nodiscard]] univex::app::ViewportRenderPass& RenderPass() noexcept { return *m_renderPass; }
    [[nodiscard]] const univex::app::ViewportRenderPass& RenderPass() const noexcept { return *m_renderPass; }
    [[nodiscard]] univex::integration::EditorMeshLayer& MeshLayer() noexcept { return m_meshLayer; }
    [[nodiscard]] const univex::render::StudioBackdropRenderer* StudioBackdrop();

    /// Returns the active drawing framebuffer (MSAA FBO when multisampled, otherwise single-sample scene FBO).
    [[nodiscard]] GLuint ActiveDrawFbo() const noexcept { return m_msaaFbo != 0U ? m_msaaFbo : m_sceneFbo; }
    [[nodiscard]] GLuint MsaaFbo() const noexcept { return ActiveDrawFbo(); }
    [[nodiscard]] GLuint ResolveFbo() const noexcept { return m_resolveFbo; }
    [[nodiscard]] GLuint ResolveColorTexture() const noexcept { return m_resolveColorTexture; }
    [[nodiscard]] GLuint FullscreenVao();

    [[nodiscard]] bool HasActiveWorldEnvironment() const;
    [[nodiscard]] bool HasAuthoredDirectionalLight() const;

    void SetShadingMode(std::int32_t mode);
    void ApplyPreviewLighting(const Editor::Editor::ViewportOverlayState& overlayState,
                                 bool previewAllowed);
    void ApplyAxisPalette(Editor::Editor& editor,
                             const Editor::Editor::ViewportOverlayState& overlayState);
    void ApplyGridAppearance(const Editor::Editor& editor,
                                const Editor::Editor::ViewportOverlayState& overlayState,
                                bool visible,
                                univex::render::GridPlane plane);

    [[nodiscard]] univex::integration::EditorMeshLayerResult RenderSceneMeshLayer(
        const univex::camera::OrbitCamera& camera,
        int width,
        int height,
        std::optional<Scene::Entity> lookThroughCamera = std::nullopt);

    /// Binds the active draw framebuffer, configures multisampling, clears it, and returns the previous FBO.
    [[nodiscard]] GLint BeginPass(int width, int height);

    /// Writes `EditorMeshLayer` colour and real scene depth into the currently bound framebuffer.
    void BlitMeshLayer(const univex::integration::EditorMeshLayerResult& meshResult,
                          const univex::camera::OrbitCamera& camera,
                          int width,
                          int height);

    /// Resolves the scene framebuffer (directly returning `m_sceneColorTexture` when MSAA/FXAA/CAS
    /// are disabled in Compatibility mode), restores `previousFbo`, and returns the native GL texture id.
    [[nodiscard]] std::uint64_t EndPassAndResolve(int width, int height, GLint previousFbo);

private:
    [[nodiscard]] bool EnsureGlewInitialized();
    [[nodiscard]] bool EnsureRenderPass();
    [[nodiscard]] bool EnsureFramebuffers(int width, int height);
    void DestroyFramebuffers();
    [[nodiscard]] bool EnsureMeshBlitResources();
    [[nodiscard]] bool EnsurePostAaResources();
    void SeedEditorAxisColorsFromDefaults(Editor::Editor& editor);

    Core::EngineServices& m_services;
    Scene::IEntityManager& m_entityManager;
    univex::integration::EditorMeshLayer m_meshLayer;
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

    int m_desiredMsaaSamples = 0;
    int m_fxaaQuality = 0;
    float m_casSharpen = 0.0F;
    bool m_gpuOutlineOnly = true;
    int m_appliedEffectiveProfile = -1;

    struct MeshBlitUniforms final {
        GLint meshColor = -1;
        GLint hasDepth = -1;
        GLint directSceneDepth = -1;
        GLint sceneDepth = -1;
        GLint sceneInvViewProj = -1;
        GLint viewProj = -1;
        GLint outSize = -1;
    };
    struct PostAaUniforms final {
        GLint sceneColor = -1;
        GLint invSize = -1;
        GLint fxaaQuality = -1;
        GLint casSharpen = -1;
    };

    std::optional<univex::render::ShaderProgram> m_meshBlitProgram;
    MeshBlitUniforms m_meshBlitUniforms{};
    std::optional<univex::render::ShaderProgram> m_postAaProgram;
    PostAaUniforms m_postAaUniforms{};
    bool m_postAaFailed = false;
    GLuint m_meshBlitVao = 0U;
    bool m_lastRenderUsedLookThroughCamera = false;
};

} // namespace UVE::App::Viewports
