// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cstddef>
#include <cstdint>

#include "uve/math/vector3_uve.h"
#include "uve/nodes/3d/node_3d_common_uve.h"

namespace UVE::Scene {

enum class ReflectionProbeUpdateModeUVE : std::uint8_t {
    Once = 0,
    EveryFrame,
    OnDemand,
};

struct ReflectionProbe3DNodeComponentUVE final {
    // `size` is the box's FULL extents on each axis (Godot's ReflectionProbe convention);
    // everything geometric works in half-extents internally.
    Math::Vector3UVE size{5.0F, 5.0F, 5.0F};
    std::uint32_t visibilityLayers = 0xFFFFFFFFU;
    ReflectionProbeUpdateModeUVE updateMode = ReflectionProbeUpdateModeUVE::Once;
    // The OnDemand recapture latch: authoring (or an inspector "Recapture" button) sets it,
    // EngineCoreUVE::SyncReflectionProbe3DNodesUVE() clears it the tick the capture resolves. In
    // the other update modes the engine owns the schedule and simply ignores this, so it keeps
    // no meaning there. Never serialized (see the ToJson/FromJson pair - authored payloads only).
    bool updateRequested = false;
    bool enabled = true;
    // Runtime-only state below, refreshed by EngineCoreUVE::SyncReflectionProbe3DNodesUVE() each
    // tick (same authored-config/runtime-state split every other 3D node keeps).
    //
    // `capturedOnce` is what gives the Once update mode exactly-once semantics over the
    // component's own lifetime (a fresh component starts false, so Play/Stop re-captures -
    // deliberate: the scene the probe depicts may itself have changed); `captureGeneration` increments per resolved capture so a future shading
    // pass can bind "the freshest probe data" without timestamps; `cameraInfluenceWeight` is the
    // resolved [0,1] blend weight of this probe for the active camera this tick - first-class
    // blend information Godot simply never exposes.
    bool capturedOnce = false;
    std::uint32_t captureGeneration = 0;
    float cameraInfluenceWeight = 0.0F;
    // How many consecutive ticks this probe has DEMANDED a capture without being serviced,
    // because the per-tick budget went to others. The scheduler serves the OLDEST waiter first,
    // so a loud probe outside the budget ages in and is guaranteed service - the starvation-free
    // answer to nearest-first-only scheduling (which would keep a far-but-shinier probe parked
    // forever when the camera sits among shinier, nearer ones). Resets to 0 on the tick the
    // probe is serviced; runtime bookkeeping only.
    std::uint32_t captureWaitTicks = 0;
};

[[nodiscard]] bool IsReflectionProbe3DNodeComponentValidUVE(const ReflectionProbe3DNodeComponentUVE& value) noexcept;

// The budget of probe captures serviced per tick. A cubemap capture costs six scene passes -
// Godot's Always mode re-renders EVERY probe every frame, and Unreal blocks on capture; time-
// slicing in the Frostbite style instead starts at most this many captures per tick and lets
// the rest carry over, prioritized nearest-camera-first. Measured by the engine tests.
inline constexpr std::size_t kMaximumReflectionProbeCapturesPerTickUVE = 2U;

// The per-tick verdict the core sync applies to one probe.
enum class ReflectionProbe3DCaptureActionUVE : std::uint8_t {
    None,
    Capture,
};

// The complete decision input for one probe on one tick, already extracted by the core sync
// (which owns the viewer pose and world-space reads). Kept a plain value so the verdict below
// is a total, measurable function of it.
struct ReflectionProbe3DCaptureFrameUVE final {
    // The weight this probe exerts on the eye this tick, pre-computed by
    // ResolveReflectionProbe3DInfluenceWeightUVE() below. Only meaningful when hasCameraViewer.
    float cameraInfluenceWeight = 0.0F;
    bool hasCameraViewer = false;
    ReflectionProbeUpdateModeUVE updateMode = ReflectionProbeUpdateModeUVE::Once;
    bool updateRequested = false;
    bool capturedOnce = false;
    bool enabled = true;
};

// The influence weight a probe box exerts on a point, expressed in PROBE-LOCAL coordinates
// (translation and rotation already undone by the caller; scale is out of scope, see the sync's
// doc comment). The rule: normalize the offset per axis by the half extent, take the maximum -
// the box-shaped Chebyshev distance - and let weight fall linearly from 1 at the center to 0
// exactly AT the face. Godot's ReflectionProbe ends its influence with a hard clip at the face
// and Unreal's boxes fade the same normalized way on paper but keep it internal; here the rule
// is one total, directly measurable function: non-finite inputs and degenerate half extents
// answer 0, `1 - d` is exact at every boundary, and no point ever reports a negative weight.
[[nodiscard]] float ResolveReflectionProbe3DInfluenceWeightUVE(
    const Math::Vector3UVE& probeLocalPoint, const Math::Vector3UVE& probeHalfExtents) noexcept;

// The verdict. The ONLY rules, each measured by the node tests:
//   * !enabled or an updateMode value beyond every listed enumerator      -> None
//   * EveryFrame && hasCameraViewer && cameraInfluenceWeight > 0          -> Capture
//     (a probe influencing nothing this tick re-renders nothing this tick -
//      Godot's Always mode cannot say that; the savings are the claim)
//   * OnDemand && updateRequested                                         -> Capture
//   * Once && !capturedOnce                                               -> Capture
//   * everything else                                                     -> None
[[nodiscard]] ReflectionProbe3DCaptureActionUVE ResolveReflectionProbe3DCaptureActionUVE(
    const ReflectionProbe3DCaptureFrameUVE& frame) noexcept;

} // namespace UVE::Scene
