// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "uve/component/entity_uve.h"
#include "uve/entity/i_entity_manager_uve.h"
#include "uve/rhi/render_resource_descs_uve.h"

namespace UVE::Render {

/// Culling controls for the 2D view.
///
/// `viewCullingEnabledUVE` false is the spec's "debug freeze", mirroring
/// CullingSettingsUVE: the renderer stops rejecting anything by view rect, so every sprite in
/// the scene is submitted for the frame regardless of where the camera looks. Deliberately
/// narrow in what it turns off: authored-hidden sprites, sprites without transforms, invalid
/// sprites and unresolvable textures keep being skipped with their own counters — a debug
/// freeze that also ignored an author's "hidden" flag would hide the very mistake it exists
/// to diagnose.
struct Renderer2DCullingSettingsUVE final {
    bool viewCullingEnabledUVE = true;
};

/// A copied, frame-local account of observable Renderer2DUVE work. Each field names evidence
/// that the renderer actually has: extraction and recorded-draw counts are CPU-side facts;
/// `spriteDrawsRecorded` means a draw was recorded into the frame command buffer, not that the
/// driver completed it or that a compositor displayed pixels. Presentation and pixel
/// verification belong to EngineCoreUVE and dedicated real-GL integration tests respectively.
/// Thread-safety: returned by value; RenderFrameToTargetUVE() and this accessor remain
/// main-thread only.
struct Renderer2DFrameDiagnosticsUVE final {
    std::uint32_t renderTargetWidth = 0U;
    std::uint32_t renderTargetHeight = 0U;
    /// Sprites the extraction walk visited (every Sprite2DComponentUVE in the scene).
    std::size_t spritesConsidered = 0U;
    /// Authored-hidden sprites (`visibleInHierarchy` false): skipped, never drawn. The freeze
    /// does not touch these — hiding them is the author's explicit choice.
    std::size_t hiddenSprites = 0U;
    /// Sprites with no WorldTransformComponentUVE: skipped, since a sprite with nowhere to
    /// stand cannot be placed. Distinct from invalid: this is "never attached", not "badly
    /// authored".
    std::size_t spritesWithoutTransforms = 0U;
    /// Sprites failing IsSprite2DComponentValidUVE, whose region escapes its texture, or whose
    /// texture asset is ready but unusable (zero size, pixel-size mismatch, Basis encoding):
    /// skipped rather than drawn wrong.
    std::size_t invalidSprites = 0U;
    /// Sprites whose texture load is pending or failed: skipped this frame. Pending loads appear
    /// on their own once the load lands, through the same non-blocking contract the mesh path
    /// has.
    std::size_t unresolvedTextures = 0U;
    /// Sprites past the per-frame batch cap: dropped and counted rather than growing the
    /// frame without bound.
    std::size_t spritesDroppedPastBatchCap = 0U;
    /// Sprites the view rect rejected (culling enabled only).
    std::size_t spritesCulled = 0U;
    /// Draws recorded into the frame command buffer this frame (one upload + draw per sprite
    /// in v1 — batching by texture arrives with the run-batcher).
    std::size_t spriteDrawsRecorded = 0U;
    /// True when the frame skipped every view rejection because
    /// Renderer2DCullingSettingsUVE::viewCullingEnabledUVE was false (the debug freeze).
    /// Reported because a frozen frame's draw counts are otherwise indistinguishable from a
    /// scene that simply has nothing off-screen.
    bool viewCullingFrozen = false;
    /// False when the camera entity's Camera2D parameters failed validation: the frame clears
    /// its target and records no sprites.
    bool cameraValid = true;
    /// True once the frame's render pass ran (even a clear-only one).
    bool mainPassRecorded = false;
};

/// IRenderer2DUVE is the engine's per-frame 2D render orchestrator, following the
/// IRenderer3DUVE shape at a 2D scale: for a given Camera2D entity, it computes the
/// view-projection, extracts the scene's sprites, culls and painter-sorts them, uploads each
/// sprite's textured quad, and records the draws into the frame command buffer — all into a
/// caller-owned color/depth target pair.
///
/// v1 renders ONLY into caller-owned targets (one call per editor viewport context — the main
/// editor and the entity editor each render their own camera into their own targets through
/// this same entry point). There is deliberately no RenderFrameUVE()/ResizeTargetsUVE() yet:
/// the renderer owns no targets and presents to no surface; those arrive with the 2D
/// game-window path.
/// Thread-safety: not thread-safe. RenderFrameToTargetUVE() must be called once per target
/// from the main engine thread only, after SceneGraphUVE::UpdateUVE() has run for this frame
/// (it needs current WorldTransformComponentUVE data), matching EngineCoreUVE's
/// single-threaded Update -> Render frame-loop contract.
class IRenderer2DUVE {
public:
    virtual ~IRenderer2DUVE() = default;

    /// Renders the scene as seen by `cameraEntity` into `colorTarget`/`depthTarget`
    /// (caller-owned textures of `width` x `height` pixels — an editor viewport, or any other
    /// host compositing the result itself, e.g. via ImGui::Image()). PresentUVE() is never
    /// implied by this call. `cameraEntity` must have both WorldTransformComponentUVE and
    /// Camera2DComponentUVE (the same contract ICamera2DSystemUVE already enforces). A zero
    /// width/height records nothing; an invalid camera clears the target and records no
    /// sprites.
    virtual void RenderFrameToTargetUVE(Scene::IEntityManagerUVE& entityManager, Scene::EntityUVE cameraEntity,
                                        TextureHandleUVE colorTarget, TextureHandleUVE depthTarget,
                                        std::uint32_t width, std::uint32_t height) = 0;

    /// Sets the scene clear color for later frames. The default is a no-op so lightweight
    /// renderers and test doubles need not retain the value.
    virtual void SetSceneClearColorUVE(const std::array<float, 4U>& color) noexcept {
        static_cast<void>(color);
    }

    /// Updates the culling controls used by later frames. The default implementation is a
    /// no-op so non-Renderer2D test doubles need not own culling state; a double that never
    /// culls already behaves the way a disabled setting asks for.
    virtual void SetCullingSettingsUVE(const Renderer2DCullingSettingsUVE& settings) noexcept {
        static_cast<void>(settings);
    }

    /// Returns the last frame's copied renderer evidence snapshot. The snapshot intentionally
    /// does not claim a completed GPU frame or visible window pixels; use the real-GL
    /// integration tests for that stronger presentation proof.
    [[nodiscard]] virtual Renderer2DFrameDiagnosticsUVE GetLastFrameDiagnosticsUVE() const noexcept = 0;
};

} // namespace UVE::Render
