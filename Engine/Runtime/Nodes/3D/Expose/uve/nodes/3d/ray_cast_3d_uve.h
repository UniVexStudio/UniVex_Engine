// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <array>
#include <cstdint>
#include <limits>

#include "uve/nodes/3d/node_3d_common_uve.h"
#include "uve/scene/entity_uve.h"

namespace UVE::Scene {

inline constexpr std::size_t kMaximumRayCastExclusionsUVE = 8U;

struct RayCast3DNodeComponentUVE final {
    Math::Vector3UVE direction{0.0F, -1.0F, 0.0F};
    float length = 100.0F;
    std::uint32_t collisionMask = 0xFFFFFFFFU;
    bool enabled = true;
    // Not yet consumed by EngineCoreUVE::SyncRayCast3DNodesUVE() - skipping a specific set of
    // other entities (rather than just this ray's own origin entity) would need
    // Physics::RaycastQueryUVE extended to accept more than one ignored entity, and a persistent,
    // save/load-stable way to reference another node (a raw EntityUVE is a runtime-only handle,
    // not something this engine's serializer can round-trip yet). Real, separate follow-up work.
    std::array<std::uint32_t, kMaximumRayCastExclusionsUVE> exclusions{};
    std::uint8_t exclusionCount = 0U;
    // Runtime-only result state below, refreshed every frame by SyncRayCast3DNodesUVE() - never
    // serialized (mirrors CharacterControllerComponentUVE's own authored-config/runtime-state
    // split).
    bool hit = false;
    Math::Vector3UVE hitPosition{};
    Math::Vector3UVE hitNormal{};
    EntityUVE hitEntity{};
};

[[nodiscard]] bool IsRayCast3DNodeComponentValidUVE(const RayCast3DNodeComponentUVE& value) noexcept;

} // namespace UVE::Scene
