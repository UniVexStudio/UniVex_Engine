// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cstdint>

#include "uve/nodes/3d/node_3d_common_uve.h"

namespace UVE::Scene {

enum class NavigationAgentPathStatusUVE : std::uint8_t {
    Idle = 0,
    Searching,
    Following,
    Finished,
    Failed,
};

struct NavigationAgent3DNodeComponentUVE final {
    Math::Vector3UVE targetPosition{};
    Math::Vector3UVE nextPathPosition{};
    Math::Vector3UVE desiredVelocity{};
    float radius = 0.5F;
    float height = 1.8F;
    float maxSpeed = 4.0F;
    float pathUpdateInterval = 0.1F;
    std::uint32_t navigationLayers = 1U;
    NavigationAgentPathStatusUVE pathStatus = NavigationAgentPathStatusUVE::Idle;
    bool avoidanceEnabled = true;
    bool enabled = true;
    bool pathChanged = false;
    bool targetReached = false;
};

[[nodiscard]] bool IsNavigationAgent3DNodeComponentValidUVE(const NavigationAgent3DNodeComponentUVE& value) noexcept;

} // namespace UVE::Scene
