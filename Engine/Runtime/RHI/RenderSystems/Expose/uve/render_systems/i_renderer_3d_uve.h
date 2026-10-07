// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include "uve/math/matrix4x4_uve.h"
#include "uve/math/vector3_uve.h"
#include "uve/rhi/render_resource_descs_uve.h"
#include "uve/component/entity_uve.h"
#include "uve/entity/i_entity_manager_uve.h"
#include "uve/scene/particle_runtime_uve.h"
#include "uve/ui/ui_runtime_uve.h"

namespace UVE::Render {

/// Post-process controls. The final tone-mapping pass supports bilinear or nearest-neighbour
/// spatial reconstruction and ACES or linear-clamp tone mapping; fast approximate AA is opt-in and
/// runs in that same shader. The final pass also supports edge-clamped unsharp masking. Bloom and
/// SSAO default to enabled; SSAO uses radius 0.5, intensity/power 1, and 12 kernel samples, matching
/// the renderer's existing behavior. Active WorldEnvironment components retain their enable/radius/
/// intensity overrides. SSAO quality is clamped to 0..2 (4/8/12 samples), power to [0.1, 4], and
/// the optional blur is applied during AO compositing. Fast-approximate-AA quality is clamped to
/// 0..2 (low/medium/high), selecting 3/5/7 edge-direction taps in addition to a fixed four-neighbour
/// contrast test; sharpening strength is clamped to [0, 1]. Dithering adds deterministic sub-LSB
/// screen-space noise before final output quantization.

struct PostProcessSettingsUVE final {
    bool bloomEnabledUVE = true;
    bool ssaoEnabledUVE = true;
    float ssaoRadiusUVE = 0.5F;
    float ssaoIntensityUVE = 1.0F;
    float ssaoPowerUVE = 1.0F;
    /// 0 = Low (4 samples), 1 = Medium (8), 2 = High (12).
    std::uint32_t ssaoQualityUVE = 2U;
    bool ssaoBlurEnabledUVE = false;
    bool fastApproximateAAEnabledUVE = false;
    std::uint32_t fastApproximateAAQualityUVE = 1U;
    bool nearestUpscalingEnabledUVE = false;
    bool acesToneMappingEnabledUVE = true;
    float sharpeningAmountUVE = 0.0F;
    bool ditheringEnabledUVE = true;
};

/// Culling controls for the main colour view.
///
/// `frustumCullingEnabledUVE` false is the spec's "debug freeze": the renderer stops rejecting
/// anything by frustum, so every placed mesh, primitive, surface instance and decal in the scene is
/// submitted for the frame regardless of where the camera looks. It is a debugging aid, not a
/// quality option - a frozen frame draws the whole scene.
///
/// Deliberately narrow in what it turns off. Visibility flags, world partitioning, visibility
/// regions, LodGroup3D draw distances, SurfaceInstance3D ranges, render layers and Occluder3D
/// occlusion are separate authoring/culling systems with their own controls and counters, and they
/// keep running: a debug freeze that also ignored an author's "hidden" flag would hide the very
/// mistake it exists to diagnose. Shadow cascades also keep culling against their own light frusta,
/// since freezing those multiplies shadow draw work without changing what the camera sees.
struct CullingSettingsUVE final {
    bool frustumCullingEnabledUVE = true;
};

/// A copied, frame-local account of observable Renderer3DUVE work. Each field names evidence that
/// the renderer actually has: extraction and recorded-pass/draw counts are CPU-side facts;
/// `glDrawCallsIssued` means a draw call was dispatched through the immediate OpenGL backend, not
/// that the driver completed it or that a compositor displayed pixels. Presentation and pixel
/// verification belong to EngineCoreUVE and dedicated real-GL integration tests respectively.
/// Thread-safety: returned by value; RenderFrameUVE() and this accessor remain main-thread only.
struct Renderer3DFrameDiagnosticsUVE final {
    std::uint32_t renderTargetWidth = 0U;
    std::uint32_t renderTargetHeight = 0U;
    std::size_t meshItemsExtracted = 0U;
    std::size_t invalidAssetReferences = 0U;
    std::size_t pendingAssetLoads = 0U;
    std::size_t failedAssetLoads = 0U;
    std::size_t textureFallbacks = 0U;
    std::size_t primitiveCandidates = 0U;

    /// Primitive placement cache outcomes for the frame. Reported for the same reason the mesh
    /// path reports its own: a cache with no visibility into its hit rate is a cache nobody can
    /// tell is broken - a subtle key bug that misses every frame costs a comparison on top of the
    /// original work and otherwise looks identical from the outside.
    std::size_t primitivePlacementCacheHits = 0U;
    std::size_t primitivePlacementCacheMisses = 0U;
    std::size_t primitiveItemsExtracted = 0U;
    /// This frame's projected decals: how many decals were considered, how many produced a draw,
    /// and what those draws cost in polygons. Counted here rather than left to the pass's own
    /// struct because a decal that paints nothing has several entirely different causes - it
    /// expired, its layers matched no receiver, its volume reached no surface, its material has not
    /// loaded - and a single "decals drawn" number cannot tell them apart.
    std::size_t decalsConsidered = 0U;
    std::size_t decalDrawsExtracted = 0U;
    std::size_t decalPatchesExtracted = 0U;
    std::size_t decalTrianglesExtracted = 0U;
    std::size_t decalsWithoutReceivers = 0U;
    /// Decal draw calls the main pass actually recorded, and the draws the plan refused. The two
    /// together answer the question the extraction counters above cannot: a decal whose geometry was
    /// extracted and whose draw never reached the GPU (its material was collected between the two,
    /// the frame's decal budget ran out, the volume had no invertible world-to-unit map) shows up
    /// here and nowhere else.
    std::size_t decalDrawCallsRecorded = 0U;
    std::size_t decalDrawsDropped = 0U;
    /// Skinned meshes posed by a skeleton and drawn from their own vertex buffer this frame.
    std::size_t skinnedMeshesDrawn = 0U;
    std::size_t meshDrawCallsRecorded = 0U;
    /// How many of `meshDrawCallsRecorded` were instanced draws, and how many objects those draws
    /// covered. Both name what instancing ACTUALLY did this frame, not what it was offered: a
    /// scene whose materials predate the instancing contract reports zero, which is the honest
    /// answer and the one that makes "is instancing on?" answerable from a diagnostic rather than
    /// from reading the material assets.
    std::size_t instancedDrawCallsRecorded = 0U;
    std::size_t instancedObjectsRecorded = 0U;
    /// Placement-cache outcome for this frame's extraction walk. Surfaced because a cache whose
    /// hit rate nobody can see is a cache nobody can tell is broken: a key bug that misses every
    /// frame costs an extra comparison on top of the original work and otherwise looks identical
    /// to a working one. In a static scene hits should be everything and misses zero.
    std::size_t placementCacheHits = 0U;
    std::size_t placementCacheMisses = 0U;
    /// Instanced draws recorded across ALL shadow cascades this frame. Separate from the main-pass
    /// counter because the shadow passes run once per cascade, so they - not the main pass - were
    /// where an uninstanced scene spent most of its draw calls.
    std::size_t shadowInstancedDrawCallsRecorded = 0U;

    /// Shadow batches built across ALL cascades this frame, and the items they covered.
    ///
    /// These two together answer "is the shadow pass actually batching?", which nothing else can:
    /// a batch is one draw call, so `shadowBatchesRecorded` well below `shadowBatchedItems` means
    /// the batcher is merging, and the two being equal means every item became its own draw.
    /// Worth watching because the merging depends entirely on the ORDER the cascade queue is
    /// handed - BuildShadowBatchesUVE merges only adjacent same-mesh runs - so a change to the
    /// queue's sort silently multiplies draw calls while every image stays identical.
    std::size_t shadowBatchesRecorded = 0U;
    std::size_t shadowBatchedItems = 0U;

    /// Spatial clusters the visibility set built this frame, and how many of them the MAIN view's
    /// frustum rejected outright. A rejected cluster skips a plane test per candidate inside it,
    /// so the ratio is the clustering's whole return: near zero rejected means the clusters are
    /// not tight enough to be worth building, and that is a regression nothing else reports.
    ///
    /// Main view only. The shadow cascades cull the same set against much wider frusta and would
    /// average the number into meaninglessness.
    std::size_t visibilityClusters = 0U;
    std::size_t visibilityClustersRejected = 0U;

    /// True when the main colour view skipped every frustum rejection this frame because
    /// CullingSettingsUVE::frustumCullingEnabledUVE was false (the debug freeze). Reported because
    /// the frozen frame's draw counts are otherwise indistinguishable from a scene that simply has
    /// nothing off-screen - and a freeze left on by accident is a performance bug nobody can see.
    /// Shadow cascades keep culling, so this describes the colour view alone.
    bool frustumCullingFrozen = false;

    /// Entities a LodGroup3D dropped for being past the end of its distance chain. Distinct from
    /// a hidden entity: this is "too far to matter", not "the author switched it off", and a scene
    /// that is mostly this wants its draw distances reviewed rather than its visibility flags.
    std::size_t distanceCulledEntities = 0U;
    std::size_t primitiveDrawCallsRecorded = 0U;
    std::size_t particleItemsExtracted = 0U;
    std::size_t particleDrawCommandsRecorded = 0U;
    std::size_t particleDrawCommandsSubmitted = 0U;
    std::size_t particleDrawCallsRecorded = 0U;
    std::size_t glDrawCallsIssued = 0U;
    bool primitiveProgramReady = false;
    bool particleProgramReady = false;
    bool mainPassRecorded = false;
    bool toneMappingProgramReady = false;
    bool toneMappingPassRecorded = false;
    /// True when the fast approximate edge filter was enabled and dispatched by tone mapping.
    bool fastApproximateAAPassApplied = false;
    bool particleItemsTruncated = false;
    bool particleDrawCommandsSubmissionTruncated = false;
    /// True only when SSAO was enabled (PostProcessSettingsUVE), its post-process targets and
    /// programs were valid, and the camera's projection matrix was invertible this frame - not
    /// merely that SSAO was requested.
    bool ssaoPassRecorded = false;
    /// True only when bloom was enabled by PostProcessSettingsUVE and the active WorldEnvironment,
    /// and the required targets/programs were valid this frame - not merely that bloom was requested.
    bool bloomPassRecorded = false;
    /// Number of bloom scales actually blurred and composited this frame, or zero when inactive.
    std::uint32_t bloomMipCountUsed = 0U;
};

/// IRenderer3DUVE is the engine's final per-frame render orchestrator (the spec's `Renderer3DUVE`,
/// Part 7.2): for a given camera entity, it computes the view-projection, culls and sorts the
/// scene into a RenderQueueUVE (via IMeshRendererUVE), lazily uploads each encountered mesh's/
/// material's GPU-shaped resources (cached, invalidated on hot reload), records resulting draw
/// calls into an ICommandBufferUVE, and renders to an internally owned HDR color/depth target.
/// The built-in tone-mapping pass then writes the color target to the backend default framebuffer;
/// EngineCoreUVE owns the subsequent overlay callback and explicit PresentUVE() request.
/// Thread-safety: not thread-safe. RenderFrameUVE() must be called once per frame from the main
/// engine thread only, after SceneGraphUVE::UpdateUVE() has run for this frame (it needs current
/// WorldTransformComponentUVE data), matching EngineCoreUVE's single-threaded Update -> Render
/// frame-loop contract.
class IRenderer3DUVE {
public:
    virtual ~IRenderer3DUVE() = default;

    /// Renders the scene as seen by `cameraEntity`: extract -> sort -> record -> submit ->
    /// tone-map to the current backend presentation surface. `cameraEntity` must have both
    /// WorldTransformComponentUVE and CameraComponentUVE (the same contract ICameraSystemUVE
    /// already enforces).
    virtual void RenderFrameUVE(Scene::IEntityManagerUVE& entityManager, Scene::EntityUVE cameraEntity) = 0;

    /// Recreates the owned color/depth targets at a validated adaptive resolution. The default is
    /// a safe no-op for lightweight renderers and test doubles that do not own offscreen targets.
    /// Must be called on the main render thread while no frame command buffer is active.
    [[nodiscard]] virtual bool ResizeTargetsUVE(std::uint32_t width, std::uint32_t height) {
        static_cast<void>(width);
        static_cast<void>(height);
        return false;
    }

    /// Sets the scene/presentation clear color used when no sky is active. The default is a no-op
    /// so lightweight renderers and test doubles need not retain the value.
    virtual void SetSceneClearColorUVE(const std::array<float, 4U>& color) noexcept {
        static_cast<void>(color);
    }

    /// Renders the scene while extracting a copied particle snapshot from the caller-owned runtime
    /// into the frame queue. The runtime reference is borrowed for this call only; implementations
    /// must not retain it after returning. The distinct name avoids hiding the legacy virtual in test doubles.
    virtual void RenderFrameWithParticleRuntimeUVE(Scene::IEntityManagerUVE& entityManager,
                                                   Scene::EntityUVE cameraEntity,
                                                   const Scene::ParticleRuntimeUVE& particleRuntime) {
        static_cast<void>(particleRuntime);
        RenderFrameUVE(entityManager, cameraEntity);
    }

    /// Renders exactly like RenderFrameUVE(), except the final presentation write targets `region`
    /// (a pixel sub-rect of the presentation surface) instead of its entire area - Phase 3's
    /// ViewportManagerUVE split-view support. `particleRuntime`, when non-null, extracts particles
    /// into this frame exactly like RenderFrameWithParticleRuntimeUVE() would (an editor viewport
    /// composing both capabilities at once - e.g. rendering a scene's authored particle emitters
    /// into its own on-screen sub-rect - needs both together, not a choice between them). The
    /// default implementation ignores both `region` and `particleRuntime` and falls back to a
    /// full-surface RenderFrameUVE(), matching this interface's existing safe-no-op-default
    /// convention for test doubles/lightweight renderers that don't own a resizable offscreen
    /// target (see ResizeTargetsUVE()'s own default above) - such a renderer has no sub-region
    /// concept to honor, so a full render is the closest correct behavior rather than silently
    /// dropping the frame.
    virtual void RenderFrameToRegionUVE(Scene::IEntityManagerUVE& entityManager, Scene::EntityUVE cameraEntity,
                                        const ViewportRectUVE& region,
                                        const Scene::ParticleRuntimeUVE* particleRuntime = nullptr) {
        static_cast<void>(region);
        static_cast<void>(particleRuntime);
        RenderFrameUVE(entityManager, cameraEntity);
    }

    /// Renders exactly like RenderFrameUVE(), except the final tone-mapped image is written into
    /// `colorTarget`/`depthTarget` (caller-owned offscreen textures) instead of the backend's
    /// presentation surface - an editor viewport (or any other host compositing the result itself,
    /// e.g. via ImGui::Image()) rather than a real on-screen window region (that case is
    /// RenderFrameToRegionUVE() above). Both targets must be valid, matching-size textures created
    /// through this renderer's own IRenderDeviceUVE; PresentUVE() is never implied by this call.
    /// `width`/`height` name that matching size in pixels - TextureHandleUVE has no queryable size
    /// on IRenderDeviceUVE, so the caller (which just created these textures) supplies it directly;
    /// this is also what sizes the "UIOverlay" pass's orthographic projection for this target, so a
    /// zero width/height (the default, for callers that don't need UI overlaid) skips that pass
    /// exactly like the pre-Phase-U3b behavior. The default implementation ignores every parameter
    /// and falls back to a full-frame RenderFrameUVE(), matching this interface's existing
    /// safe-no-op-default convention for test doubles/lightweight renderers with no offscreen-target
    /// concept.
    virtual void RenderFrameToTargetUVE(Scene::IEntityManagerUVE& entityManager, Scene::EntityUVE cameraEntity,
                                        TextureHandleUVE colorTarget, TextureHandleUVE depthTarget,
                                        std::uint32_t width = 0U, std::uint32_t height = 0U) {
        static_cast<void>(colorTarget);
        static_cast<void>(depthTarget);
        static_cast<void>(width);
        static_cast<void>(height);
        RenderFrameUVE(entityManager, cameraEntity);
    }

    /// Updates the Phase 2b post-process quality-tier toggles for later render frames. The default
    /// implementation is intentionally a no-op so non-Renderer3D test doubles need not own
    /// post-process state.
    virtual void SetPostProcessSettingsUVE(const PostProcessSettingsUVE& settings) {
        static_cast<void>(settings);
    }

    /// Sets the authored depth-bias and angle-aware normal-bias defaults used by directional lights whose
    /// per-light values are negative (inherit). Implementations may clamp out-of-range direct API input;
    /// settings-system values are already validated to [0, 10].
    virtual void SetShadowBiasDefaultsUVE(float depthBias, float normalBias) noexcept {
        static_cast<void>(depthBias);
        static_cast<void>(normalBias);
    }

    /// Updates the culling controls used by later frames. The default implementation is a no-op so
    /// non-Renderer3D test doubles need not own culling state; a double that never culls already
    /// behaves the way a disabled setting asks for.
    virtual void SetCullingSettingsUVE(const CullingSettingsUVE& settings) noexcept {
        static_cast<void>(settings);
    }

    /// Points the renderer at UI::UIRuntimeUVE's latest draw batch/font atlas, drawn by a
    /// "UIOverlay" pass added right after tone-mapping in every RenderFrame* variant. Pass nullptr
    /// (the default) to draw no UI this frame. The pointer is read fresh each RenderFrame* call,
    /// never copied - the caller must keep it valid at least that long. The default implementation
    /// is a no-op so non-Renderer3D test doubles need not own UI-overlay state.
    virtual void SetUIRuntimeUVE(const UI::UIRuntimeUVE* uiRuntime) noexcept {
        static_cast<void>(uiRuntime);
    }

    /// Returns the last frame's copied renderer evidence snapshot. The snapshot intentionally does
    /// not claim a completed GPU frame or visible window pixels; use the real-GL integration tests
    /// for that stronger presentation proof.

    /// How far the frame being drawn sits between the last two fixed physics steps, in [0, 1].
    ///
    /// Set once per frame by whoever owns the fixed-step timer, before rendering. Defaults to
    /// zero, which means "draw the simulated pose" - so a host that never calls this keeps exactly
    /// the behaviour it had, and nothing is required to opt in.
    virtual void SetPhysicsInterpolationAlphaUVE(float alpha) noexcept = 0;
    [[nodiscard]] virtual Renderer3DFrameDiagnosticsUVE GetLastFrameDiagnosticsUVE() const noexcept = 0;
};

} // namespace UVE::Render
