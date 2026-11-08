// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/physics/landing_system_uve.h"

#include <algorithm>
#include <cmath>

namespace UVE::Physics {

LandingAnalysisReportUVE AnalyzeImpendingLandingUVE(
    const MotionTrajectoryPredictionUVE& trajectory,
    const Math::QuaternionUVE& bodyOrientation,
    const Math::Vector3UVE& linearVelocity,
    const Math::Vector3UVE& angularVelocity,
    const float surfaceFriction) noexcept {
    LandingAnalysisReportUVE report;
    (void)linearVelocity;

    if (!trajectory.hasPredictedLanding) {
        return report;
    }

    report.landingPosition = trajectory.predictedLandingPosition;
    report.surfaceNormal = trajectory.predictedLandingNormal;
    report.surfaceSlopeDegrees = trajectory.landingSlopeDegrees;
    report.timeToTouchdown = trajectory.timeToLanding;

    const Math::Vector3UVE landVel = trajectory.predictedLandingVelocity;
    report.impactSpeed = Math::LengthUVE(landVel);

    // Evaluate sliding on inclined slope
    // Sliding occurs when tangential gravity/momentum exceeds static friction: tan(theta) > mu
    const float slopeRad = report.surfaceSlopeDegrees * (3.14159265F / 180.0F);
    report.willSlide = std::tan(slopeRad) > surfaceFriction || report.surfaceSlopeDegrees > 45.0F;

    // Body local axes in world coordinates
    const Math::Vector3UVE bodyUp = Math::RotateVectorUVE(bodyOrientation, Math::Vector3UVE{0.0F, 1.0F, 0.0F});
    const Math::Vector3UVE bodyForward = Math::RotateVectorUVE(bodyOrientation, Math::Vector3UVE{0.0F, 0.0F, -1.0F});
    const Math::Vector3UVE bodyRight = Math::RotateVectorUVE(bodyOrientation, Math::Vector3UVE{1.0F, 0.0F, 0.0F});

    const float spinRate = Math::LengthUVE(angularVelocity);
    const float upAlignment = Math::DotUVE(bodyUp, report.surfaceNormal);
    const float forwardAlignment = Math::DotUVE(bodyForward, report.surfaceNormal);
    const float rightAlignment = Math::DotUVE(bodyRight, report.surfaceNormal);

    // Classify landing
    if (report.impactSpeed > 14.0F || spinRate > 6.0F) {
        report.classification = LandingClassificationUVE::Uncontrolled;
        report.requiresRagdoll = true;
    } else if (upAlignment > 0.65F && report.impactSpeed < 10.0F && !report.willSlide) {
        report.classification = LandingClassificationUVE::FeetFirst;
        report.requiresRagdoll = false;
        // Moderate/high impact compression damping for shock absorption
        report.recommendedKneeDamping = 120.0F + report.impactSpeed * 10.0F;
        report.recommendedHipDamping = 150.0F + report.impactSpeed * 12.0F;
    } else if (forwardAlignment > 0.5F) {
        report.classification = LandingClassificationUVE::Back; // Back faces surface
        report.requiresRagdoll = true;
    } else if (forwardAlignment < -0.5F) {
        report.classification = LandingClassificationUVE::Front; // Chest faces surface
        report.requiresRagdoll = true;
    } else if (std::abs(rightAlignment) > 0.5F) {
        report.classification = LandingClassificationUVE::Side;
        report.requiresRagdoll = true;
    } else {
        report.classification = LandingClassificationUVE::ShoulderFirst;
        report.requiresRagdoll = true;
    }

    return report;
}

} // namespace UVE::Physics
