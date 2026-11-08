// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cstdint>

#include "uve/math/quaternion_uve.h"
#include "uve/math/vector3_uve.h"
#include "uve/physics/motion_trajectory_uve.h"

namespace UVE::Physics {

/// Physical classification of how the character will touch down.
enum class LandingClassificationUVE : std::uint8_t {
    FeetFirst = 0,
    Front,
    Back,
    Side,
    ShoulderFirst,
    Uncontrolled,
};

/// Complete analytical report for an impending landing.
struct LandingAnalysisReportUVE final {
    LandingClassificationUVE classification = LandingClassificationUVE::Uncontrolled;

    Math::Vector3UVE landingPosition{};
    Math::Vector3UVE surfaceNormal{0.0F, 1.0F, 0.0F};
    float surfaceSlopeDegrees = 0.0F;
    float impactSpeed = 0.0F;
    float timeToTouchdown = 0.0F;

    float recommendedKneeDamping = 80.0F;
    float recommendedHipDamping = 100.0F;
    bool willSlide = false;
    bool requiresRagdoll = false;
};

/// Evaluates pre-landing conditions from trajectory, orientation, and body velocities.
[[nodiscard]] LandingAnalysisReportUVE AnalyzeImpendingLandingUVE(
    const MotionTrajectoryPredictionUVE& trajectory,
    const Math::QuaternionUVE& bodyOrientation,
    const Math::Vector3UVE& linearVelocity,
    const Math::Vector3UVE& angularVelocity,
    float surfaceFriction = 0.8F) noexcept;

} // namespace UVE::Physics
