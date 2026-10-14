// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
#pragma once
#include <cstdint>
#include <functional>
#include <string>
#include <vector>
#include "uve/world/scene_uve.h"

namespace UVE::World {
class WorldUVE;
enum class WorldPhaseUVE : std::uint8_t { BeforeTransforms = 0, AfterTransforms };
enum class WorldStateUVE : std::uint8_t { Created = 0, Running, Failed, Stopped };
enum class WorldResultUVE : std::uint8_t {
    Succeeded = 0,
    InvalidArgument,
    InvalidState,
    DuplicateName,
    UnknownScene,
    MissingDependency,
    DependencyCycle,
    InitializationFailed,
    SystemFailed,
    Busy,
    CapacityExceeded
};
struct WorldSceneContextUVE final {
    WorldUVE &world;
    SceneUVE &scene;
};
/// Owned callback descriptor. tick is required; start/stop are optional when no lifecycle work
/// is needed. A failed/throwing start is paired with stop, then prior starts roll back in reverse
/// order. stop must tolerate partial initialization and must not throw. Captures must outlive
/// registration (or own their dependencies). Structural world changes during callbacks fail Busy.
/// Tick exceptions stop further work and put the world in Failed; gameplay mutations already
/// performed are not rolled back. This is deterministic serial scheduling, not parallel gameplay.
struct WorldSystemDescUVE final {
    std::string name;
    WorldPhaseUVE phase = WorldPhaseUVE::BeforeTransforms;
    std::int32_t priority = 0;
    std::vector<std::string> after;
    std::function<bool(WorldSceneContextUVE &)> start;
    std::function<void(WorldSceneContextUVE &, float)> tick;
    std::function<void(WorldSceneContextUVE &)> stop;
};
} // namespace UVE::World
