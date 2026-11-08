// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/physics/airborne_body_control_uve.h"

#include <algorithm>
#include <cmath>

namespace UVE::Physics {

AirbornePostureTargetsUVE EvaluateAirbornePostureUVE(
    const Math::Vector3UVE& linearVelocity,
    const Math::Vector3UVE& angularVelocity,
    const Math::QuaternionUVE& currentOrientation,
    const MotionTrajectoryPredictionUVE& trajectory,
    const ImpactDirectionUVE lastImpactDirection,
    const float timeInAirSeconds) noexcept {
    AirbornePostureTargetsUVE targets;
    (void)currentOrientation;
    (void)timeInAirSeconds;

    const float spinRate = Math::LengthUVE(angularVelocity);
    const float timeToLanding = trajectory.hasPredictedLanding ? trajectory.timeToLanding : 2.0F;

    // High tumble detection: pull limbs into tuck posture
    if (spinRate > 5.0F) {
        targets.isTucking = true;
        targets.armProtection = 0.9F;
        targets.legReadiness = 0.1F;

        // Tucking rotations
        targets.spineTargetRotation = Math::QuaternionUVE{0.2F, 0.0F, 0.0F, 0.98F};
        targets.leftArmTargetRotation = Math::QuaternionUVE{0.3F, 0.2F, 0.0F, 0.93F};
        targets.rightArmTargetRotation = Math::QuaternionUVE{0.3F, -0.2F, 0.0F, 0.93F};
        targets.leftLegTargetRotation = Math::QuaternionUVE{-0.4F, 0.0F, 0.0F, 0.91F};
        targets.rightLegTargetRotation = Math::QuaternionUVE{-0.4F, 0.0F, 0.0F, 0.91F};
        targets.headTargetRotation = Math::QuaternionUVE{0.15F, 0.0F, 0.0F, 0.98F};
        return targets;
    }

    // Landing preparation (when close to ground)
    if (trajectory.hasPredictedLanding && timeToLanding < 0.45F) {
        const Math::Vector3UVE landNormal = trajectory.predictedLandingNormal;
        const bool landingUpright = landNormal.y > 0.6F && linearVelocity.y < 0.0F;

        if (landingUpright) {
            // Extend legs towards landing surface
            targets.legReadiness = std::clamp(1.0F - timeToLanding / 0.45F, 0.0F, 1.0F);
            targets.leftLegTargetRotation = Math::QuaternionUVE{0.0F, 0.0F, 0.0F, 1.0F};
            targets.rightLegTargetRotation = Math::QuaternionUVE{0.0F, 0.0F, 0.0F, 1.0F};

            // Arms slightly flared for landing balance
            targets.leftArmTargetRotation = Math::QuaternionUVE{0.1F, 0.3F, 0.0F, 0.95F};
            targets.rightArmTargetRotation = Math::QuaternionUVE{0.1F, -0.3F, 0.0F, 0.95F};

            // Head oriented forward and level
            targets.headTargetRotation = Math::QuaternionUVE{0.0F, 0.0F, 0.0F, 1.0F};
            return targets;
        }

        // Falling forward / chest landing
        if (lastImpactDirection == ImpactDirectionUVE::Forward || linearVelocity.z < -2.0F) {
            targets.armProtection = 0.8F;
            targets.leftArmTargetRotation = Math::QuaternionUVE{-0.5F, 0.2F, 0.0F, 0.84F};
            targets.rightArmTargetRotation = Math::QuaternionUVE{-0.5F, -0.2F, 0.0F, 0.84F};
            targets.headTargetRotation = Math::QuaternionUVE{-0.2F, 0.0F, 0.0F, 0.97F}; // Pull head back
        } else {
            // Falling backward / back landing
            targets.armProtection = 0.8F;
            targets.headTargetRotation = Math::QuaternionUVE{0.3F, 0.0F, 0.0F, 0.95F}; // Tuck chin
            targets.spineTargetRotation = Math::QuaternionUVE{0.2F, 0.0F, 0.0F, 0.98F}; // Curl spine forward
        }
        return targets;
    }

    // Mid-flight orientation and head stabilization
    targets.legReadiness = 0.5F;
    targets.armProtection = 0.3F;
    targets.headTargetRotation = Math::QuaternionUVE{0.0F, 0.0F, 0.0F, 1.0F};

    return targets;
}

} // namespace UVE::Physics
