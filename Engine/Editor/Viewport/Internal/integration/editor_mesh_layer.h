// univex/integration/EditorMeshLayer.h (private to this module - not under include/)
// -----------------------------------------------------------------------
// Bridges the real, already-tested Renderer3D mesh/material/lit-shader pipeline into the
// editor Viewport panel, so what the panel shows is the engine's own render of the live scene.
//
// Owns two hidden, non-persisted entities (never serialized, never shown in the Scene panel) and
// one offscreen color+depth texture pair (recreated on resize) that
// Renderer3D::RenderFrameToTarget() renders the live scene into - reusing
// EngineServices's real, already-populated entity/asset/shader state directly, not a second
// renderer or a duplicate scene. The hidden entities are a camera kept in sync with the
// Viewport's own OrbitCamera, and a viewport headlight that turns itself on only while the scene
// has no light of its own, so authoring a first object does not start with a black shape.
// -----------------------------------------------------------------------
#pragma once

#include <cstdint>
#include <optional>

#include "engine_services.h"
#include "texture_handle.h"
#include "entity.h"
#include "preview_lighting.h"

namespace univex::camera {
class OrbitCamera;
}

namespace UVE::Scene {
class IEntityManager;
}

namespace univex::integration {

/// Native GL texture names of one EditorMeshLayer::Render() call's output. The colour
/// image carries per-pixel coverage in its alpha (1 where the renderer drew, 0 where it did not),
/// which is how a caller tells real geometry from empty background rather than treating the whole
/// frame as one opaque rectangle. The depth id is the attachment the render was given; the
/// tone-mapping pass clears it and never writes it, so it currently reports nothing useful.
struct EditorMeshLayerResult final {
    std::uint32_t colorTextureId = 0U;
    std::uint32_t depthTextureId = 0U;
};

/// True when the document lights something by itself: at least one light the light system would
/// hand the renderer, that is not one of this layer's own and that still has colour to give.
///
/// Deliberately not "does the document contain a light". A light the renderer drops - an invalid
/// component, a zero cull mask, a zeroed energy, a colour of pure black - reads
/// as "the scene has lighting" to a naive check, and the headlight then switches itself off for a
/// scene nothing lights: a black mesh with nothing on screen to explain it. Asking the light
/// system what it would actually light closes that gap at its source.
[[nodiscard]] bool HasContributingSceneLight(UVE::Scene::IEntityManager& entityManager);

/// True when the document's own DirectionalLight3D lights the scene: the same question as
/// HasContributingSceneLight(), narrowed to a sun, and asked for the same reason. The editor's
/// preview sun stands down for an authored sun - so it must not stand down for one the renderer
/// refuses, or the scene loses both lights at once.
[[nodiscard]] bool HasContributingDirectionalLight(UVE::Scene::IEntityManager& entityManager);

/// True when the document carries a valid authored DirectionalLight3D entity (even if currently
/// hidden via VisibilityComponent or with energy set to 0), so deliberately turning an authored
/// DirectionalLight3D off leaves the scene dark rather than re-enabling the editor preview sun.
[[nodiscard]] bool HasAuthoredDirectionalLightNode(UVE::Scene::IEntityManager& entityManager);

class EditorMeshLayer final {
public:
    explicit EditorMeshLayer(UVE::Core::EngineServices& services);
    ~EditorMeshLayer();

    EditorMeshLayer(const EditorMeshLayer&) = delete;
    EditorMeshLayer& operator=(const EditorMeshLayer&) = delete;

    /// Renders the live scene's real mesh/material entities into a `width`x`height` offscreen
    /// target (created or resized as needed). Returns {0, 0} if nothing could be rendered this
    /// frame (e.g. a zero-sized request). The caller composites the color id directly via
    /// a UI image or a custom shader, same as any other GL texture; the colour's alpha
    /// distinguishes real geometry from empty background per-pixel. See
    /// GlRenderDevice::GetNativeTextureId()'s own doc
    /// comment for why reading a native id out of a TextureHandle is a deliberate, narrowly
    /// scoped exception here rather than a general RHI capability.
    ///
    /// `camera` supplies the Edit-mode free-look view (eye/target/FOV/clip planes) via this
    /// layer's own hidden proxy camera entity, synced every call. `gameCameraOverride`, when set
    /// to a real entity carrying both WorldTransformComponent and CameraComponent, renders
    /// through THAT entity directly instead - the Play-mode "what a player would actually see"
    /// path (see main.cpp's own FindGameCameraEntity for how the entity is chosen). An override
    /// naming an entity that no longer has both components falls back to the Edit-mode path rather
    /// than failing the frame - a scene camera an author just deleted mid-Play shouldn't blank the
    /// panel.
    [[nodiscard]] EditorMeshLayerResult Render(
        const univex::camera::OrbitCamera& camera, std::uint32_t width, std::uint32_t height,
        std::optional<UVE::Scene::Entity> gameCameraOverride = std::nullopt);

    /// Editor preview lighting for this frame (see Scene::PreviewLightingFrame).
    ///
    /// Until this is called the layer keeps its historical behaviour - a warm-white light that
    /// follows the camera whenever the scene has none of its own - so the standalone demo and the
    /// headless capture tool are unaffected. Once the editor starts supplying frames, the preview
    /// sun is fully authoritative: its own colour, energy and world direction, and switching it off
    /// (or the scene gaining a real DirectionalLight3D) leaves the scene unlit exactly as Godot's
    /// preview does, instead of quietly substituting a different light.
    void SetPreviewLighting(const UVE::Scene::PreviewLightingFrame& frame);

private:
    [[nodiscard]] UVE::Scene::Entity CreateCameraProxyEntity();
    [[nodiscard]] UVE::Scene::Entity CreateHeadlightEntity();
    void SyncCameraFromOrbit(const univex::camera::OrbitCamera& camera, float aspectRatio);
    /// Returns whether it wrote a transform this frame (so the caller knows the scene graph
    /// needs updating when the camera-sync path that normally does it is skipped).
    [[nodiscard]] bool SyncHeadlight(const univex::camera::OrbitCamera& camera);
    [[nodiscard]] bool EnsureTargets(std::uint32_t width, std::uint32_t height);
    void DestroyTargets();

    UVE::Core::EngineServices& services_;
    UVE::Scene::Entity cameraEntity_ = UVE::Scene::kInvalidEntity;
    UVE::Scene::Entity headlightEntity_ = UVE::Scene::kInvalidEntity;
    UVE::Render::TextureHandle colorTarget_ = UVE::Render::kInvalidTextureHandle;
    UVE::Render::TextureHandle depthTarget_ = UVE::Render::kInvalidTextureHandle;
    std::uint32_t targetWidth_ = 0U;
    std::uint32_t targetHeight_ = 0U;
    UVE::Scene::PreviewLightingFrame previewLighting_{};
    bool previewLightingSet_ = false;
};

} // namespace univex::integration
