// univex/render/OverlayRasterState.h
// -----------------------------------------------------------------------
// Foundation WP6 (candidate B, step 1): the depth/blend/cull state an editor
// overlay pass needs, expressed as DATA instead of as raw glDepthFunc /
// glDepthMask calls scattered through the renderers. The GL target applies a
// value through one table-driven function (OverlayRasterStateGl.h); the
// engine bridge maps the same value onto an RHI PipelineDescUVE
// (integration/OverlayRasterStateToPipeline.h) so a host that renders
// overlays through IRenderDeviceUVE gets identical depth semantics.
//
// Pure data: no GL, no engine dependency (univex_viewport_core rules).
// -----------------------------------------------------------------------
#pragma once

#include <cstdint>

namespace univex::render {

enum class OverlayDepthCompare : std::uint8_t { Less, LessOrEqual, Always };
enum class OverlayBlendMode : std::uint8_t { Opaque, Alpha };
/// `Inherit` leaves the host's current cull mode alone (the pass never touched it before this
/// header existed, and keeping that exact behaviour is what makes the data step pixel-neutral).
enum class OverlayCullMode : std::uint8_t { Inherit, None, Back };

struct OverlayRasterState {
    bool depthTest = true;
    bool depthWrite = false;
    OverlayDepthCompare depthCompare = OverlayDepthCompare::Less;
    OverlayBlendMode blend = OverlayBlendMode::Alpha;
    OverlayCullMode cull = OverlayCullMode::Inherit;

    constexpr bool operator==(const OverlayRasterState&) const = default;
};

// ---- presets: one per overlay pass, named from the behaviour the GL sites implemented -----

/// Opaque gizmo solids: tested against the scene (when the widget asks), written so the widget
/// occludes itself, blended for the anti-aliased edge, visible from every side.
inline constexpr OverlayRasterState kGizmoOpaqueSolidState{true, true, OverlayDepthCompare::Less,
                                                           OverlayBlendMode::Alpha, OverlayCullMode::None};
/// Translucent gizmo solids: tested, never written - a half-transparent handle must not reject
/// the geometry showing through it.
inline constexpr OverlayRasterState kGizmoTranslucentSolidState{true, false, OverlayDepthCompare::Less,
                                                                OverlayBlendMode::Alpha, OverlayCullMode::None};
/// Gizmo strokes (axis lines, rings): tested, never written - an anti-aliased stroke edge must not
/// punch a hole into whatever is behind it.
inline constexpr OverlayRasterState kGizmoStrokeState{true, false, OverlayDepthCompare::Less,
                                                      OverlayBlendMode::Alpha, OverlayCullMode::None};
/// Infinite grid: tested against the scene, never occluding it, alpha-faded by the shader.
inline constexpr OverlayRasterState kInfiniteGridState{true, false, OverlayDepthCompare::Less,
                                                       OverlayBlendMode::Alpha, OverlayCullMode::Inherit};
/// Studio backdrop sky: drawn at the far plane behind everything (LessOrEqual against a cleared
/// depth of 1), opaque, never written.
inline constexpr OverlayRasterState kStudioBackdropSkyState{true, false, OverlayDepthCompare::LessOrEqual,
                                                            OverlayBlendMode::Opaque, OverlayCullMode::Inherit};
/// Screen-space composites (selection outline, 2D overlays): no depth at all.
inline constexpr OverlayRasterState kScreenSpaceCompositeState{false, false, OverlayDepthCompare::Always,
                                                               OverlayBlendMode::Alpha, OverlayCullMode::Inherit};

[[nodiscard]] constexpr OverlayRasterState WithDepthTest(OverlayRasterState state, const bool depthTest) {
    state.depthTest = depthTest;
    return state;
}
[[nodiscard]] constexpr OverlayRasterState WithDepthWrite(OverlayRasterState state, const bool depthWrite) {
    state.depthWrite = depthWrite;
    return state;
}

} // namespace univex::render
