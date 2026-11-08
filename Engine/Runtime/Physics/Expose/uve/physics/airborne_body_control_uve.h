// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cstdint>

#include "uve/math/quaternion_uve.h"
#include "uve/math/vector3_uve.h"
#include "uve/physics/advanced_collision_analysis_uve.h"
#include "uve/physics/motion_trajectory_uve.h"

namespace UVE::Physics {

/// Procedural joint target adjustments generated during flight.
struct AirbornePostureTargetsUVE final {
    Math::QuaternionUVE headTargetRotation{};
    Math::QuaternionUVE spineTargetRotation{};
    Math::QuaternionUVE leftArmTargetRotation{};
    Math::QuaternionUVE rightArmTargetRotation{};
    Math::QuaternionUVE leftLegTargetRotation{};
    Math::QuaternionUVE rightLegTargetRotation{};

    float legReadiness = 0.0F; // 0 (tucked) to 1 (extended for touchdown)
    float armProtection = 0.0F; // 0 (relaxed) to 1 (protective brace)
    bool isTucking = false;
};

/// Evaluates procedural joint posture adjustments during airborne movement.
[[nodiscard]] AirbornePostureTargetsUVE EvaluateAirbornePostureUVE(
    const Math::Vector3UVE& linearVelocity,
    const Math::Vector3UVE& angularVelocity,
    const Math::QuaternionUVE& currentOrientation,
    const MotionTrajectoryPredictionUVE& trajectory,
    ImpactDirectionUVE lastImpactDirection,
    float timeInAirSeconds) noexcept;

} // namespace UVE::Physics
