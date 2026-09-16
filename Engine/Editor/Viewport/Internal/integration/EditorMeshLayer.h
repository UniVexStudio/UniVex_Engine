// univex/integration/EditorMeshLayer.h (private to this module - not under include/)
// -----------------------------------------------------------------------
// Bridges the real, already-tested Renderer3DUVE mesh/material/lit-shader pipeline into the
// editor Viewport panel. Before this, the panel only ever showed ViewportRenderPass's grid/gizmo/
// flat-proxy-cube geometry - real MeshComponentUVE-carrying entities never actually rendered
// on screen even though the engine's own mesh renderer has existed and been tested all along.
//
// Owns one hidden, non-persisted camera entity (never serialized, never shown in the Scene
// panel) kept in sync with the Viewport's own OrbitCamera every frame, and one offscreen
// color+depth texture pair (recreated on resize) that Renderer3DUVE::RenderFrameToTargetUVE()
// renders the live scene into - reusing EngineServicesUVE's real, already-populated
// entity/asset/shader state directly, not a second renderer or a duplicate scene.
// -----------------------------------------------------------------------
#pragma once

#include <cstdint>
#include <optional>

#include "uve/core/engine_services_uve.h"
#include "uve/render/texture_handle_uve.h"
#include "uve/scene/entity_uve.h"

namespace univex::camera {
class OrbitCamera;
}

namespace univex::integration {

/// Native GL texture names of one EditorMeshLayerUVE::RenderUVE() call's output - both the
/// tonemapped color image and its matching depth buffer, so a caller can composite this layer
/// against other content using real per-pixel depth (e.g. "was anything actually drawn here")
/// rather than treating the whole frame as one opaque rectangle.
struct EditorMeshLayerResultUVE final {
    std::uint32_t colorTextureId = 0U;
    std::uint32_t depthTextureId = 0U;
};

class EditorMeshLayerUVE final {
public:
    explicit EditorMeshLayerUVE(UVE::Core::EngineServicesUVE& services);
    ~EditorMeshLayerUVE();

    EditorMeshLayerUVE(const EditorMeshLayerUVE&) = delete;
    EditorMeshLayerUVE& operator=(const EditorMeshLayerUVE&) = delete;

    /// Renders the live scene's real mesh/material entities into a `width`x`height` offscreen
    /// target (created or resized as needed). Returns {0, 0} if nothing could be rendered this
    /// frame (e.g. a zero-sized request). The caller composites the color id directly via
    /// ImGui::Image() or a custom shader, same as any other GL texture; the depth id lets a caller
    /// distinguish real geometry from empty background per-pixel (depth < far-clear-value 1.0
    /// means something was drawn there). See GlRenderDeviceUVE::GetNativeTextureIdUVE()'s own doc
    /// comment for why reading a native id out of a TextureHandleUVE is a deliberate, narrowly
    /// scoped exception here rather than a general RHI capability.
    ///
    /// `camera` supplies the Edit-mode free-look view (eye/target/FOV/clip planes) via this
    /// layer's own hidden proxy camera entity, synced every call. `gameCameraOverride`, when set
    /// to a real entity carrying both WorldTransformComponentUVE and CameraComponentUVE, renders
    /// through THAT entity directly instead - the Play-mode "what a player would actually see"
    /// path (see main.cpp's own FindGameCameraEntityUVE for how the entity is chosen). An override
    /// naming an entity that no longer has both components falls back to the Edit-mode path rather
    /// than failing the frame - a scene camera an author just deleted mid-Play shouldn't blank the
    /// panel.
    [[nodiscard]] EditorMeshLayerResultUVE RenderUVE(
        const univex::camera::OrbitCamera& camera, std::uint32_t width, std::uint32_t height,
        std::optional<UVE::Scene::EntityUVE> gameCameraOverride = std::nullopt);

private:
    [[nodiscard]] UVE::Scene::EntityUVE CreateCameraProxyEntityUVE();
    void SyncCameraFromOrbitUVE(const univex::camera::OrbitCamera& camera, float aspectRatio);
    [[nodiscard]] bool EnsureTargetsUVE(std::uint32_t width, std::uint32_t height);
    void DestroyTargetsUVE();

    UVE::Core::EngineServicesUVE& services_;
    UVE::Scene::EntityUVE cameraEntity_ = UVE::Scene::kInvalidEntityUVE;
    UVE::Render::TextureHandleUVE colorTarget_ = UVE::Render::kInvalidTextureHandleUVE;
    UVE::Render::TextureHandleUVE depthTarget_ = UVE::Render::kInvalidTextureHandleUVE;
    std::uint32_t targetWidth_ = 0U;
    std::uint32_t targetHeight_ = 0U;
};

} // namespace univex::integration
