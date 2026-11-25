// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

#include "entity.h"
#include "vector2.h"
#include "vector3.h"
#include "object_3d_common.h"

namespace UVE::Scene {

class IEntityManager;

enum class ReflectionProbeUpdateMode : std::uint8_t {
    Once = 0,
    EveryFrame,
    OnDemand,
};

/// Resolution tiers map to square 2D textures for each of a probe's six cubemap faces. A tier is
/// authored per probe because nearby hero probes can afford more texels than background probes.
enum class ReflectionProbeResolution : std::uint8_t {
    Low = 0U,
    Medium,
    High,
    Ultra,
};

inline constexpr std::uint32_t kReflectionProbeCaptureResolution = 128U;

[[nodiscard]] constexpr std::uint32_t GetReflectionProbeResolutionPixels(
    const ReflectionProbeResolution resolution) noexcept {
    switch (resolution) {
        case ReflectionProbeResolution::Low:
            return 64U;
        case ReflectionProbeResolution::Medium:
            return kReflectionProbeCaptureResolution;
        case ReflectionProbeResolution::High:
            return 256U;
        case ReflectionProbeResolution::Ultra:
            return 512U;
    }
    return 0U;
}

[[nodiscard]] constexpr bool IsReflectionProbeResolutionValid(
    const ReflectionProbeResolution resolution) noexcept {
    return GetReflectionProbeResolutionPixels(resolution) != 0U;
}

struct ReflectionProbe3DComponent final {
    // `size` is the box's FULL extents on each axis (Godot's ReflectionProbe convention);
    // everything geometric works in half-extents internally.
    Math::Vector3 size{5.0F, 5.0F, 5.0F};
    std::uint32_t visibilityLayers = 0xFFFFFFFFU;
    ReflectionProbeUpdateMode updateMode = ReflectionProbeUpdateMode::Once;
    ReflectionProbeResolution resolution = ReflectionProbeResolution::Medium;
    // The OnDemand recapture latch: authoring (or an inspector "Recapture" button) sets it,
    // EngineCore::SyncReflectionProbe3DObjects() clears it the tick the capture resolves. In
    // the other update modes the engine owns the schedule and simply ignores this, so it keeps
    // no meaning there. Never serialized (see the ToJson/FromJson pair - authored payloads only).
    bool updateRequested = false;
    bool enabled = true;
    // Runtime-only state below, refreshed by EngineCore::SyncReflectionProbe3DObjects() each
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

[[nodiscard]] bool IsReflectionProbe3DObjectComponentValid(const ReflectionProbe3DComponent& value) noexcept;

struct ReflectionProbe3DObjectDefinition final {
    static constexpr std::string_view defaultName = "ReflectionProbe3D";
    ReflectionProbe3DComponent probe{};
};

void ApplyReflectionProbe3DObjectDefinition(IEntityManager& entityManager, Entity entity,
                                               const ReflectionProbe3DObjectDefinition& value);

// The budget of probe captures serviced per tick. A cubemap capture costs six scene passes -
// Godot's Always mode re-renders EVERY probe every frame, and others block on capture; time-
// slicing in the Frostbite style instead starts at most this many captures per tick and lets
// the rest carry over, prioritized nearest-camera-first. Measured by the engine tests.
inline constexpr std::size_t kMaximumReflectionProbeCapturesPerTick = 2U;

// The per-tick verdict the core sync applies to one probe.
enum class ReflectionProbe3DCaptureAction : std::uint8_t {
    None,
    Capture,
};

// The complete decision input for one probe on one tick, already extracted by the core sync
// (which owns the viewer pose and world-space reads). Kept a plain value so the verdict below
// is a total, measurable function of it.
struct ReflectionProbe3DCaptureFrame final {
    // The weight this probe exerts on the eye this tick, pre-computed by
    // ResolveReflectionProbe3DInfluenceWeight() below. Only meaningful when hasCameraViewer.
    float cameraInfluenceWeight = 0.0F;
    bool hasCameraViewer = false;
    ReflectionProbeUpdateMode updateMode = ReflectionProbeUpdateMode::Once;
    bool updateRequested = false;
    bool capturedOnce = false;
    bool enabled = true;
};

// The influence weight a probe box exerts on a point, expressed in PROBE-LOCAL coordinates
// (translation and rotation already undone by the caller; scale is out of scope, see the sync's
// doc comment). The rule: normalize the offset per axis by the half extent, take the maximum -
// the box-shaped Chebyshev distance - and let weight fall linearly from 1 at the center to 0
// exactly AT the face. Godot's ReflectionProbe ends its influence with a hard clip at the face
// and the standard boxes fade the same normalized way on paper but keep it internal; here the rule
// is one total, directly measurable function: non-finite inputs and degenerate half extents
// answer 0, `1 - d` is exact at every boundary, and no point ever reports a negative weight.
[[nodiscard]] float ResolveReflectionProbe3DInfluenceWeight(
    const Math::Vector3& probeLocalPoint, const Math::Vector3& probeHalfExtents) noexcept;

// The verdict. The ONLY rules, each measured by the object tests:
//   * !enabled or an updateMode value beyond every listed enumerator      -> None
//   * EveryFrame && hasCameraViewer && cameraInfluenceWeight > 0          -> Capture
//     (a probe influencing nothing this tick re-renders nothing this tick -
//      Godot's Always mode cannot say that; the savings are the claim)
//   * OnDemand && updateRequested                                         -> Capture
//   * Once && !capturedOnce                                               -> Capture
//   * everything else                                                     -> None
[[nodiscard]] ReflectionProbe3DCaptureAction ResolveReflectionProbe3DCaptureAction(
    const ReflectionProbe3DCaptureFrame& frame) noexcept;

inline constexpr std::size_t kReflectionProbeCubemapFaceCount = 6U;
inline constexpr std::size_t kMaximumReflectionProbesPerFrame = 4U;

enum class CubemapFace : std::uint8_t {
    PositiveX = 0,
    NegativeX,
    PositiveY,
    NegativeY,
    PositiveZ,
    NegativeZ,
};

[[nodiscard]] bool TryGetCubemapFaceBasis(CubemapFace face, Math::Vector3& outForward,
                                             Math::Vector3& outUp) noexcept;

[[nodiscard]] bool TrySelectCubemapFace(const Math::Vector3& direction, CubemapFace& outFace) noexcept;

[[nodiscard]] bool TryMakeCubemapFaceCameraRotation(CubemapFace face,
                                                       Math::Quaternion& outRotation) noexcept;

[[nodiscard]] bool TryMakeCubemapFaceUv(const Math::Vector3& direction, CubemapFace face,
                                           Math::Vector2& outUv) noexcept;

[[nodiscard]] bool TryProjectCubemapDirection(const Math::Vector3& direction, CubemapFace& outFace,
                                                 Math::Vector2& outUv) noexcept;

struct ReflectionProbe3DFrame final {
    Entity entity = kInvalidEntity;
    Math::Vector3 worldPosition{};
    Math::Vector3 axisX{1.0F, 0.0F, 0.0F};
    Math::Vector3 axisY{0.0F, 1.0F, 0.0F};
    Math::Vector3 axisZ{0.0F, 0.0F, 1.0F};
    Math::Vector3 halfExtents{2.5F, 2.5F, 2.5F};
    std::uint32_t visibilityLayers = 0xFFFFFFFFU;
    std::uint32_t captureResolution = kReflectionProbeCaptureResolution;
    float influenceWeight = 0.0F;
    std::uint32_t captureGeneration = 0;
    bool capturedOnce = false;
    bool enabled = true;
};

[[nodiscard]] bool TryMakeReflectionProbe3DFrame(const ReflectionProbe3DComponent& value,
                                                    const Math::Vector3& worldPosition,
                                                    const Math::Quaternion& worldRotation,
                                                    ReflectionProbe3DFrame& out) noexcept;

[[nodiscard]] std::optional<Math::Vector3> ReflectionProbe3DWorldToLocal(
    const ReflectionProbe3DFrame& frame, const Math::Vector3& worldPoint) noexcept;

[[nodiscard]] float SampleReflectionProbe3DInfluence(const ReflectionProbe3DFrame& frame,
                                                        const Math::Vector3& worldPoint) noexcept;

[[nodiscard]] bool TryBoxProjectReflection(const ReflectionProbe3DFrame& frame,
                                              const Math::Vector3& worldOrigin,
                                              const Math::Vector3& worldDirection,
                                              Math::Vector3& outWorldPoint) noexcept;

[[nodiscard]] std::size_t CollectReflectionProbe3DFrames(IEntityManager& entityManager,
                                                            const Math::Vector3& viewPosition,
                                                            std::span<ReflectionProbe3DFrame> out);

struct ReflectionProbe3DGizmo final {
    Math::Vector3 origin{};
    Math::Vector3 axisX{1.0F, 0.0F, 0.0F};
    Math::Vector3 axisY{0.0F, 1.0F, 0.0F};
    Math::Vector3 axisZ{0.0F, 0.0F, 1.0F};
    Math::Vector3 halfExtents{2.5F, 2.5F, 2.5F};
    Math::Vector3 color{0.35F, 0.85F, 0.95F};
};

void CollectReflectionProbe3DGizmos(IEntityManager& entityManager, std::vector<ReflectionProbe3DGizmo>& out);

} // namespace UVE::Scene
