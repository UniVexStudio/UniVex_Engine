// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

#include "uve/component/entity_uve.h"
#include "uve/math/vector2_uve.h"
#include "uve/math/vector3_uve.h"
#include "uve/objects/3d/object_3d_common_uve.h"

namespace UVE::Scene {

class IEntityManagerUVE;

enum class ReflectionProbeUpdateModeUVE : std::uint8_t {
    Once = 0,
    EveryFrame,
    OnDemand,
};

/// Resolution tiers map to square 2D textures for each of a probe's six cubemap faces. A tier is
/// authored per probe because nearby hero probes can afford more texels than background probes.
enum class ReflectionProbeResolutionUVE : std::uint8_t {
    Low = 0U,
    Medium,
    High,
    Ultra,
};

inline constexpr std::uint32_t kReflectionProbeCaptureResolutionUVE = 128U;

[[nodiscard]] constexpr std::uint32_t GetReflectionProbeResolutionPixelsUVE(
    const ReflectionProbeResolutionUVE resolution) noexcept {
    switch (resolution) {
        case ReflectionProbeResolutionUVE::Low:
            return 64U;
        case ReflectionProbeResolutionUVE::Medium:
            return kReflectionProbeCaptureResolutionUVE;
        case ReflectionProbeResolutionUVE::High:
            return 256U;
        case ReflectionProbeResolutionUVE::Ultra:
            return 512U;
    }
    return 0U;
}

[[nodiscard]] constexpr bool IsReflectionProbeResolutionValidUVE(
    const ReflectionProbeResolutionUVE resolution) noexcept {
    return GetReflectionProbeResolutionPixelsUVE(resolution) != 0U;
}

struct ReflectionProbe3DComponentUVE final {
    // `size` is the box's FULL extents on each axis (Godot's ReflectionProbe convention);
    // everything geometric works in half-extents internally.
    Math::Vector3UVE size{5.0F, 5.0F, 5.0F};
    std::uint32_t visibilityLayers = 0xFFFFFFFFU;
    ReflectionProbeUpdateModeUVE updateMode = ReflectionProbeUpdateModeUVE::Once;
    ReflectionProbeResolutionUVE resolution = ReflectionProbeResolutionUVE::Medium;
    // The OnDemand recapture latch: authoring (or an inspector "Recapture" button) sets it,
    // EngineCoreUVE::SyncReflectionProbe3DObjectsUVE() clears it the tick the capture resolves. In
    // the other update modes the engine owns the schedule and simply ignores this, so it keeps
    // no meaning there. Never serialized (see the ToJson/FromJson pair - authored payloads only).
    bool updateRequested = false;
    bool enabled = true;
    // Runtime-only state below, refreshed by EngineCoreUVE::SyncReflectionProbe3DObjectsUVE() each
    // tick (same authored-config/runtime-state split every other 3D object keeps).
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

[[nodiscard]] bool IsReflectionProbe3DObjectComponentValidUVE(const ReflectionProbe3DComponentUVE& value) noexcept;

struct ReflectionProbe3DObjectDefinitionUVE final {
    static constexpr std::string_view defaultName = "ReflectionProbe3D";
    ReflectionProbe3DComponentUVE probe{};
};

void ApplyReflectionProbe3DObjectDefinitionUVE(IEntityManagerUVE& entityManager, EntityUVE entity,
                                               const ReflectionProbe3DObjectDefinitionUVE& value);

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

// The verdict. The ONLY rules, each measured by the object tests:
//   * !enabled or an updateMode value beyond every listed enumerator      -> None
//   * EveryFrame && hasCameraViewer && cameraInfluenceWeight > 0          -> Capture
//     (a probe influencing nothing this tick re-renders nothing this tick -
//      Godot's Always mode cannot say that; the savings are the claim)
//   * OnDemand && updateRequested                                         -> Capture
//   * Once && !capturedOnce                                               -> Capture
//   * everything else                                                     -> None
[[nodiscard]] ReflectionProbe3DCaptureActionUVE ResolveReflectionProbe3DCaptureActionUVE(
    const ReflectionProbe3DCaptureFrameUVE& frame) noexcept;

inline constexpr std::size_t kReflectionProbeCubemapFaceCountUVE = 6U;
inline constexpr std::size_t kMaximumReflectionProbesPerFrameUVE = 4U;

enum class CubemapFaceUVE : std::uint8_t {
    PositiveX = 0,
    NegativeX,
    PositiveY,
    NegativeY,
    PositiveZ,
    NegativeZ,
};

[[nodiscard]] bool TryGetCubemapFaceBasisUVE(CubemapFaceUVE face, Math::Vector3UVE& outForward,
                                             Math::Vector3UVE& outUp) noexcept;

[[nodiscard]] bool TrySelectCubemapFaceUVE(const Math::Vector3UVE& direction, CubemapFaceUVE& outFace) noexcept;

[[nodiscard]] bool TryMakeCubemapFaceCameraRotationUVE(CubemapFaceUVE face,
                                                       Math::QuaternionUVE& outRotation) noexcept;

[[nodiscard]] bool TryMakeCubemapFaceUvUVE(const Math::Vector3UVE& direction, CubemapFaceUVE face,
                                           Math::Vector2UVE& outUv) noexcept;

[[nodiscard]] bool TryProjectCubemapDirectionUVE(const Math::Vector3UVE& direction, CubemapFaceUVE& outFace,
                                                 Math::Vector2UVE& outUv) noexcept;

struct ReflectionProbe3DFrameUVE final {
    EntityUVE entity = kInvalidEntityUVE;
    Math::Vector3UVE worldPosition{};
    Math::Vector3UVE axisX{1.0F, 0.0F, 0.0F};
    Math::Vector3UVE axisY{0.0F, 1.0F, 0.0F};
    Math::Vector3UVE axisZ{0.0F, 0.0F, 1.0F};
    Math::Vector3UVE halfExtents{2.5F, 2.5F, 2.5F};
    std::uint32_t captureResolution = kReflectionProbeCaptureResolutionUVE;
    float influenceWeight = 0.0F;
    std::uint32_t captureGeneration = 0;
    bool capturedOnce = false;
    bool enabled = true;
};

[[nodiscard]] bool TryMakeReflectionProbe3DFrameUVE(const ReflectionProbe3DComponentUVE& value,
                                                    const Math::Vector3UVE& worldPosition,
                                                    const Math::QuaternionUVE& worldRotation,
                                                    ReflectionProbe3DFrameUVE& out) noexcept;

[[nodiscard]] std::optional<Math::Vector3UVE> ReflectionProbe3DWorldToLocalUVE(
    const ReflectionProbe3DFrameUVE& frame, const Math::Vector3UVE& worldPoint) noexcept;

[[nodiscard]] float SampleReflectionProbe3DInfluenceUVE(const ReflectionProbe3DFrameUVE& frame,
                                                        const Math::Vector3UVE& worldPoint) noexcept;

[[nodiscard]] bool TryBoxProjectReflectionUVE(const ReflectionProbe3DFrameUVE& frame,
                                              const Math::Vector3UVE& worldOrigin,
                                              const Math::Vector3UVE& worldDirection,
                                              Math::Vector3UVE& outWorldPoint) noexcept;

[[nodiscard]] std::size_t CollectReflectionProbe3DFramesUVE(IEntityManagerUVE& entityManager,
                                                            const Math::Vector3UVE& viewPosition,
                                                            std::span<ReflectionProbe3DFrameUVE> out);

struct ReflectionProbe3DGizmoUVE final {
    Math::Vector3UVE origin{};
    Math::Vector3UVE axisX{1.0F, 0.0F, 0.0F};
    Math::Vector3UVE axisY{0.0F, 1.0F, 0.0F};
    Math::Vector3UVE axisZ{0.0F, 0.0F, 1.0F};
    Math::Vector3UVE halfExtents{2.5F, 2.5F, 2.5F};
    Math::Vector3UVE color{0.35F, 0.85F, 0.95F};
};

void CollectReflectionProbe3DGizmosUVE(IEntityManagerUVE& entityManager, std::vector<ReflectionProbe3DGizmoUVE>& out);

} // namespace UVE::Scene
