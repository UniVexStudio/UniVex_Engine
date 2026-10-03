// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/objects/3d/navigation_agent_3d_uve.h"

namespace UVE::Scene {

bool IsNavigationAgent3DObjectComponentValidUVE(const NavigationAgent3DComponentUVE& value) noexcept {
    return IsFinite3DObjectVectorUVE(value.targetPosition) && IsFinite3DObjectVectorUVE(value.nextPathPosition) &&
           IsFinite3DObjectVectorUVE(value.desiredVelocity) && std::isfinite(value.radius) && value.radius > 0.0F &&
           std::isfinite(value.height) && value.height >= value.radius * 2.0F && std::isfinite(value.maxSpeed) &&
           value.maxSpeed > 0.0F && std::isfinite(value.pathUpdateInterval) && value.pathUpdateInterval > 0.0F &&
           value.pathUpdateInterval <= 10.0F && value.navigationLayers != 0U &&
           value.pathStatus <= NavigationAgentPathStatusUVE::Failed;
}

} // namespace UVE::Scene
