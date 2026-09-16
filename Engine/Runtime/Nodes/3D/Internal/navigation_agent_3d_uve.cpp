// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/nodes/3d/navigation_agent_3d_uve.h"

namespace UVE::Scene {

bool IsNavigationAgent3DNodeComponentValidUVE(const NavigationAgent3DNodeComponentUVE& value) noexcept {
    return IsFinite3DNodeVectorUVE(value.targetPosition) && IsFinite3DNodeVectorUVE(value.nextPathPosition) &&
           IsFinite3DNodeVectorUVE(value.desiredVelocity) && std::isfinite(value.radius) && value.radius > 0.0F &&
           std::isfinite(value.height) && value.height >= value.radius * 2.0F && std::isfinite(value.maxSpeed) &&
           value.maxSpeed > 0.0F && std::isfinite(value.pathUpdateInterval) && value.pathUpdateInterval > 0.0F &&
           value.pathUpdateInterval <= 10.0F && value.navigationLayers != 0U &&
           value.pathStatus <= NavigationAgentPathStatusUVE::Failed;
}

} // namespace UVE::Scene
