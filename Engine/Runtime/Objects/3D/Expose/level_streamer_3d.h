// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>

#include "entity.h"
#include "vector3.h"
#include "object_3d_common.h"
#include "object_3d.h"

namespace UVE::Scene {

class IEntityManager;

struct LevelStreamer3DComponent final {
    std::string levelPath;
    float loadDistance = 250.0F;
    float unloadDistance = 300.0F;
    bool enabled = false;
    // Runtime-only state below, refreshed by EngineCore::SyncLevelStreamer3DObjects() each
    // tick (same authored-config/runtime-state split every other 3D object keeps - InteractionArea3D
    // above is the reference). These fields are never serialized: the serializer explicitly pins
    // them to false on load.
    //
    // `loaded` is true exactly while the subtree restored from levelPath lives in the document.
    // `loadRequested` is reserved for the future asynchronous streaming path: under today's
    // synchronous loading it is set true only for the duration of the load call itself, so it
    // always reads false between ticks - a documented invariant, measured by the engine tests.
    bool loaded = false;
    bool loadRequested = false;

    [[nodiscard]] bool operator==(const LevelStreamer3DComponent&) const = default;
};

[[nodiscard]] bool IsLevelStreamer3DObjectComponentValid(const LevelStreamer3DComponent& value) noexcept;

/// LevelStreamer3D's creation recipe — the Object3D baseline plus a default
/// `LevelStreamer3DComponent`.
struct LevelStreamer3DObjectDefinition final {
    static constexpr std::string_view typeName = "LevelStreamer3D";
    static constexpr std::string_view defaultName = "LevelStreamer3D";

    LevelStreamer3DComponent streamer{};

    [[nodiscard]] bool operator==(const LevelStreamer3DObjectDefinition&) const = default;
};

[[nodiscard]] bool IsLevelStreamer3DObjectDefinitionValid(
    const LevelStreamer3DObjectDefinition& value) noexcept;

void ApplyLevelStreamer3DObjectDefinition(IEntityManager& entityManager, Entity entity,
                                             const LevelStreamer3DObjectDefinition& value);

// The budget of streaming loads serviced per tick. Godot gives a distant scene nothing at all;
// The standard streaming volumes issue loads immediately and let the renderer hitch. Time-sliced
// budgeting in the Frostbite style instead: at most this many streamers begin loading each tick
// and the rest carry over to the next one, so a teleport across the map can never hitch the
// engine past the budget. Measured by the engine tests (5 streamers demand load -> exactly 4
// after tick one, all 5 after tick two).
inline constexpr std::size_t kMaximumLevelStreamer3DLoadsPerTick = 4U;

// The per-tick verdict the core sync applies to one streamer.
enum class LevelStreamer3DAction : std::uint8_t {
    None,
    RequestLoad,
    RequestUnload,
};

// The complete decision input for one streamer on one tick, already extracted by the core sync
// (which owns the viewer point cloud and world-pose reads). Kept as a plain value so the verdict
// below is a total, measurable function of it.
struct LevelStreamer3DStreamingFrame final {
    // Squared distance from this streamer to the nearest VALID viewer position on this tick,
    // pre-computed by ResolveLevelStreamer3DNearestViewerDistanceSquared() below. Only
    // meaningful when hasViewer is true.
    float nearestViewerDistanceSquared = 0.0F;
    // False when no live camera/controller viewer with a finite world position exists; in that
    // case the frame answers no motion at all (see the verdict rules).
    bool hasViewer = false;
    // Authored streaming configuration, forwarded unchanged from the component.
    float loadDistance = 0.0F;
    float unloadDistance = 0.0F;
    bool enabled = false;
    // Runtime streaming state, forwarded unchanged from the component.
    bool loaded = false;
    bool loadRequested = false;
};

// Squared distance from `streamerWorldPosition` to the nearest viewer in `viewerPositions`,
// skipping every non-finite viewer (a NaN camera pose must never poison the entire decision),
// and no value at all when the streamer's own position is non-finite or every viewer was
// skipped. Squared: the streaming comparisons only need ordering, avoiding a sqrt for every
// streamer*viewer pair each tick (same discipline as the hitbox broad-phase, which also never
// touches sqrt).
[[nodiscard]] std::optional<float> ResolveLevelStreamer3DNearestViewerDistanceSquared(
    const Math::Vector3& streamerWorldPosition,
    std::span<const Math::Vector3> viewerPositions) noexcept;

// The verdict. The ONLY rules, each measured by the object tests:
//   * any non-finite input, loadDistance <= 0, or unloadDistance <= loadDistance
//         (an authored configuration with no valid hysteresis band)         -> None
//   * !enabled && loaded                                                    -> RequestUnload
//   * !enabled                                                              -> None
//   * enabled && !loaded && !loadRequested && hasViewer && d2 <= load^2     -> RequestLoad
//   * enabled && loaded && hasViewer && d2 >= unload^2                      -> RequestUnload
//   * everything else (the hysteresis band itself, viewer-driven no-ops)    -> None
//
// loadDistance is inclusive on approach, unloadDistance is inclusive on retreat: boundaries are
// part of exactly one verdict each and hysteresis stands in between, which is the entire point -
// the camera seesawing around a distance can never flick a level on and off every frame.
[[nodiscard]] LevelStreamer3DAction ResolveLevelStreamer3DStreamingAction(
    const LevelStreamer3DStreamingFrame& frame) noexcept;

/// Node policy for a `LevelStreamer3D` scene object.
class LevelStreamer3D final {
public:
    LevelStreamer3D() = delete;

    /// Builds a `LevelStreamer3DStreamingFrame` for `streamer` at `streamerWorldPosition`
    /// against `viewerPositions`.
    [[nodiscard]] static LevelStreamer3DStreamingFrame BuildStreamingFrame(
        const LevelStreamer3DComponent& streamer, const Math::Vector3& streamerWorldPosition,
        std::span<const Math::Vector3> viewerPositions) noexcept;

    /// Evaluates the per-tick streaming action for `streamer` at `streamerWorldPosition`.
    [[nodiscard]] static LevelStreamer3DAction EvaluateAction(
        const LevelStreamer3DComponent& streamer, const Math::Vector3& streamerWorldPosition,
        std::span<const Math::Vector3> viewerPositions) noexcept;

    /// Width of the hysteresis band (`unloadDistance - loadDistance`), or `0.0F` if invalid.
    [[nodiscard]] static float HysteresisBandWidth(
        const LevelStreamer3DComponent& streamer) noexcept;
};

} // namespace UVE::Scene
