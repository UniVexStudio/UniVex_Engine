// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/physics/motion_trajectory_uve.h"

#include <algorithm>
#include <cmath>

#include "uve/physics/detail/shape_narrow_phase_uve.h"

namespace UVE::Physics {
namespace {

constexpr float kPi = 3.14159265358979323846F;
constexpr float kRadToDeg = 180.0F / kPi;

[[nodiscard]] Math::QuaternionUVE IntegrateRotationUVE(
    const Math::QuaternionUVE& q, const Math::Vector3UVE& omega, const float dt) noexcept {
    const float omegaLenSq = Math::LengthSquaredUVE(omega);
    if (omegaLenSq <= 1.0e-8F) {
        return q;
    }
    const float omegaLen = std::sqrt(omegaLenSq);
    const float halfTheta = 0.5F * omegaLen * dt;
    const float sinHalf = std::sin(halfTheta);
    const float cosHalf = std::cos(halfTheta);
    const Math::Vector3UVE axis = omega * (sinHalf / omegaLen);

    const Math::QuaternionUVE deltaQ{axis.x, axis.y, axis.z, cosHalf};
    Math::QuaternionUVE result = Math::MultiplyUVE(q, deltaQ);
    Math::QuaternionUVE normalized;
    if (Math::TryNormalizeUVE(result, normalized)) {
        return normalized;
    }
    return q;
}

struct SweepHitResultUVE final {
    bool hit = false;
    float t = 1.0F; // [0, 1] along segment
    Math::Vector3UVE point{};
    Math::Vector3UVE normal{0.0F, 1.0F, 0.0F};
    bool isGround = false;
    bool isWall = false;
    float slopeDegrees = 0.0F;
};

[[nodiscard]] SweepHitResultUVE TestSegmentAgainstWorldUVE(
    const Math::Vector3UVE& start, const Math::Vector3UVE& end, float radius,
    const std::span<const Detail::ColliderWorldAabbUVE> worldColliders) noexcept {
    SweepHitResultUVE earliest;
    earliest.t = 1.0F;

    const Math::Vector3UVE segDir = end - start;
    const float segLenSq = Math::LengthSquaredUVE(segDir);
    if (segLenSq <= 1.0e-8F) {
        return earliest;
    }
    const float segLen = std::sqrt(segLenSq);
    const Math::Vector3UVE dir = segDir * (1.0F / segLen);
    const Math::RayUVE ray{start, dir};

    for (const Detail::ColliderWorldAabbUVE& collider : worldColliders) {
        // Fast AABB rejection
        const Math::AabbUVE expandedAabb{
            collider.worldAabb.min - Math::Vector3UVE{radius, radius, radius},
            collider.worldAabb.max + Math::Vector3UVE{radius, radius, radius}};
        const std::optional<Math::RayHitUVE> aabbHit = Math::IntersectRayUVE(ray, expandedAabb, segLen);
        if (!aabbHit.has_value() || aabbHit->distance > segLen || aabbHit->distance > earliest.t * segLen) {
            continue;
        }

        // Detailed shape narrow phase
        float hitDist = aabbHit->distance;
        Math::Vector3UVE hitNorm = aabbHit->normal;

        if (collider.shapeType == Scene::ColliderShapeTypeUVE::Sphere) {
            const Math::SphereUVE targetSphere{collider.worldAabb.GetCenterUVE(), collider.shapeRadius + radius};
            const auto sphereHit = Detail::IntersectRaySphereUVE(ray, targetSphere, segLen);
            if (sphereHit.has_value()) {
                hitDist = sphereHit->distance;
                hitNorm = sphereHit->normal;
            }
        }

        const float tFrac = hitDist / segLen;
        if (tFrac < earliest.t && tFrac >= 0.0F) {
            earliest.hit = true;
            earliest.t = tFrac;
            earliest.point = start + dir * hitDist;
            earliest.normal = Math::LengthSquaredUVE(hitNorm) > 1.0e-4F ? Math::NormalizeUVE(hitNorm)
                                                                        : -dir;
            earliest.isGround = earliest.normal.y >= 0.55F;
            earliest.isWall = std::abs(earliest.normal.y) < 0.3F;
            earliest.slopeDegrees = std::acos(std::clamp(earliest.normal.y, -1.0F, 1.0F)) * kRadToDeg;
        }
    }

    return earliest;
}

} // namespace

MotionTrajectoryPredictionUVE PredictMotionTrajectoryUVE(
    const TrajectoryInputUVE& input,
    const std::span<const Detail::ColliderWorldAabbUVE> worldColliders) noexcept {
    MotionTrajectoryPredictionUVE prediction;
    prediction.timeHorizonSeconds = std::clamp(input.horizonSeconds, 0.2F, 5.0F);
    prediction.timeStepSeconds = std::clamp(input.stepSeconds, 1.0F / 240.0F, 0.1F);

    prediction.currentPosition = input.position;
    prediction.currentVelocity = input.linearVelocity;
    prediction.currentAngularVelocity = input.angularVelocity;
    prediction.currentAcceleration = input.acceleration;
    prediction.currentOrientation = input.orientation;
    prediction.centerOfMass = input.position + Math::RotateVectorUVE(input.orientation, input.centerOfMassOffset);

    // Initial state determination
    const float horizontalSpeed = std::sqrt(input.linearVelocity.x * input.linearVelocity.x +
                                            input.linearVelocity.z * input.linearVelocity.z);
    const float verticalSpeed = input.linearVelocity.y;
    const float angularSpeed = Math::LengthUVE(input.angularVelocity);

    if (input.isGrounded) {
        if (input.balanceDisturbed) {
            const float impulseMag = Math::LengthUVE(input.disturbanceImpulse);
            if (impulseMag > 450.0F) {
                prediction.state = TrajectoryStateUVE::UnrecoverableBalanceLoss;
            } else if (impulseMag > 150.0F) {
                prediction.state = TrajectoryStateUVE::CorrectiveStepping;
            } else {
                prediction.state = TrajectoryStateUVE::BalanceDisturbance;
            }
        } else {
            prediction.state = TrajectoryStateUVE::StableLocomotion;
        }
    } else {
        if (verticalSpeed > 1.0F || horizontalSpeed > 3.0F || angularSpeed > 2.0F) {
            prediction.state = TrajectoryStateUVE::AirborneMovement;
        } else if (verticalSpeed < -1.0F) {
            prediction.state = TrajectoryStateUVE::ForwardFalling;
        } else {
            prediction.state = TrajectoryStateUVE::AirborneMovement;
        }
    }

    const std::size_t sampleCount =
        static_cast<std::size_t>(std::ceil(prediction.timeHorizonSeconds / prediction.timeStepSeconds));
    prediction.samples.reserve(sampleCount + 1U);

    Math::Vector3UVE simPos = input.position;
    Math::Vector3UVE simVel = input.linearVelocity;
    Math::Vector3UVE simOmega = input.angularVelocity;
    Math::QuaternionUVE simRot = input.orientation;
    float simTime = 0.0F;

    // First sample (t = 0)
    prediction.samples.push_back(TrajectoryPointUVE{
        .timeSeconds = 0.0F,
        .position = simPos,
        .linearVelocity = simVel,
        .angularVelocity = simOmega,
        .acceleration = input.acceleration,
        .orientation = simRot,
        .centerOfMass = simPos + Math::RotateVectorUVE(simRot, input.centerOfMassOffset),
    });

    const float dt = prediction.timeStepSeconds;
    bool inFlight = !input.isGrounded || input.position.y > 0.05F;

    for (std::size_t step = 0U; step < sampleCount; ++step) {
        simTime += dt;

        // Kinematic integration
        Math::Vector3UVE effectiveAccel = input.gravity;
        if (!inFlight) {
            // Grounded friction damping along motion direction
            const float speed = Math::LengthUVE(simVel);
            if (speed > 1.0e-3F) {
                const Math::Vector3UVE frictionDecel = (simVel * (-1.0F / speed)) * (input.friction * 9.81F);
                effectiveAccel = frictionDecel;
            } else {
                simVel = Math::Vector3UVE{};
                effectiveAccel = Math::Vector3UVE{};
            }
        } else {
            // Air drag
            effectiveAccel -= simVel * input.linearAirDrag;
        }

        const Math::Vector3UVE nextVel = simVel + effectiveAccel * dt;
        const Math::Vector3UVE nextPos = simPos + (simVel + nextVel) * (0.5F * dt);
        simOmega -= simOmega * (input.angularAirDamping * dt);
        const Math::QuaternionUVE nextRot = IntegrateRotationUVE(simRot, simOmega, dt);

        if (inFlight) {
            // Forward obstacle collision sweep
            SweepHitResultUVE hit = TestSegmentAgainstWorldUVE(simPos, nextPos, input.characterRadius, worldColliders);
            if (!hit.hit && nextPos.y <= 0.0F && simPos.y > 0.0F) {
                const float dy = nextPos.y - simPos.y;
                const float tFrac = std::abs(dy) > 1.0e-5F ? std::clamp(-simPos.y / dy, 0.0F, 1.0F) : 0.0F;
                hit.hit = true;
                hit.t = tFrac;
                hit.point = simPos + (nextPos - simPos) * tFrac;
                hit.point.y = 0.0F;
                hit.normal = Math::Vector3UVE{0.0F, 1.0F, 0.0F};
                hit.isGround = true;
                hit.isWall = false;
                hit.slopeDegrees = 0.0F;
            }

            if (hit.hit) {
                const Math::Vector3UVE impactVel = simVel + effectiveAccel * (dt * hit.t);
                PredictedContactEventUVE contactEvent{
                    .timeSeconds = simTime - dt * (1.0F - hit.t),
                    .contactPosition = hit.point,
                    .contactNormal = hit.normal,
                    .incomingVelocity = impactVel,
                    .outgoingVelocity = Math::Vector3UVE{},
                    .isWall = hit.isWall,
                    .isGround = hit.isGround,
                    .surfaceSlopeDegrees = hit.slopeDegrees,
                };

                if (hit.isGround && !prediction.hasPredictedLanding) {
                    prediction.hasPredictedLanding = true;
                    prediction.timeToLanding = contactEvent.timeSeconds;
                    prediction.predictedLandingPosition = hit.point;
                    prediction.predictedLandingNormal = hit.normal;
                    prediction.predictedLandingVelocity = impactVel;
                    prediction.predictedLandingOrientation = simRot;
                    prediction.landingSlopeDegrees = hit.slopeDegrees;

                    // If normal velocity is strong and restitution is active, calculate bounce
                    const float normalVel = Math::DotUVE(simVel, hit.normal);
                    if (normalVel < -2.0F && input.restitution > 0.05F) {
                        const Math::Vector3UVE normalPart = hit.normal * normalVel;
                        const Math::Vector3UVE tangentPart = simVel - normalPart;
                        simVel = tangentPart * (1.0F - input.friction) - normalPart * input.restitution;
                        simPos = hit.point + hit.normal * 0.02F;
                        contactEvent.outgoingVelocity = simVel;
                        prediction.predictedContacts.push_back(contactEvent);
                        prediction.state = TrajectoryStateUVE::ReboundAfterImpact;
                        continue;
                    }

                    // Otherwise, settle on ground
                    inFlight = false;
                    simVel = Math::Vector3UVE{};
                    simPos = hit.point;
                    contactEvent.outgoingVelocity = simVel;
                    prediction.predictedContacts.push_back(contactEvent);
                    prediction.state = TrajectoryStateUVE::GroundCollision;
                } else if (hit.isWall) {
                    if (!prediction.hasPredictedWallHit) {
                        prediction.hasPredictedWallHit = true;
                        prediction.timeToWallHit = contactEvent.timeSeconds;
                        prediction.predictedWallHitPosition = hit.point;
                        prediction.predictedWallHitNormal = hit.normal;
                    }

                    // Dynamic rebound off vertical wall: reflect velocity with restitution and friction
                    const float normalVel = Math::DotUVE(simVel, hit.normal);
                    if (normalVel < 0.0F) {
                        const Math::Vector3UVE normalPart = hit.normal * normalVel;
                        const Math::Vector3UVE tangentPart = simVel - normalPart;
                        simVel = tangentPart * std::clamp(1.0F - input.friction * 0.5F, 0.0F, 1.0F) -
                                 normalPart * input.restitution;

                        // Impart angular spin from wall impact arm
                        const Math::Vector3UVE arm = hit.point - simPos;
                        simOmega += Math::CrossUVE(arm, hit.normal * (-normalVel * 2.0F));
                    }
                    simPos = hit.point + hit.normal * (input.characterRadius + 0.01F);
                    contactEvent.outgoingVelocity = simVel;
                    prediction.predictedContacts.push_back(contactEvent);
                    prediction.state = TrajectoryStateUVE::WallCollisionDuringFlight;
                    continue;
                }
            }
        }

        simPos = nextPos;
        simVel = nextVel;
        simRot = nextRot;

        prediction.samples.push_back(TrajectoryPointUVE{
            .timeSeconds = simTime,
            .position = simPos,
            .linearVelocity = simVel,
            .angularVelocity = simOmega,
            .acceleration = effectiveAccel,
            .orientation = simRot,
            .centerOfMass = simPos + Math::RotateVectorUVE(simRot, input.centerOfMassOffset),
        });
    }

    return prediction;
}

TrajectoryStateUVE EvaluateTrajectoryStateTransitionUVE(
    const TrajectoryStateUVE currentState,
    const bool isGrounded,
    const float linearSpeed,
    const float angularSpeed,
    const Math::Vector3UVE& balanceError,
    const float balanceErrorThreshold,
    const bool hadWallHit) noexcept {
    if (hadWallHit) {
        return TrajectoryStateUVE::WallCollisionDuringFlight;
    }

    if (!isGrounded) {
        if (linearSpeed > 3.0F || angularSpeed > 2.0F) {
            return TrajectoryStateUVE::AirborneMovement;
        }
        return TrajectoryStateUVE::ForwardFalling;
    }

    // Grounded transitions
    if (currentState == TrajectoryStateUVE::GroundCollision ||
        currentState == TrajectoryStateUVE::AirborneMovement ||
        currentState == TrajectoryStateUVE::ReboundAfterImpact) {
        if (linearSpeed < 0.2F && angularSpeed < 0.3F) {
            return TrajectoryStateUVE::LandingStabilization;
        }
    }

    if (currentState == TrajectoryStateUVE::LandingStabilization) {
        if (linearSpeed < 0.05F && angularSpeed < 0.1F) {
            return TrajectoryStateUVE::Recovery;
        }
    }

    const float errorMag = Math::LengthUVE(balanceError);
    if (errorMag > balanceErrorThreshold * 2.5F) {
        // High disturbance - directional fall
        if (balanceError.z > 0.0F) {
            return TrajectoryStateUVE::ForwardFalling;
        }
        if (balanceError.z < 0.0F) {
            return TrajectoryStateUVE::BackwardFalling;
        }
        return TrajectoryStateUVE::SidewaysFalling;
    }
    if (errorMag > balanceErrorThreshold) {
        return TrajectoryStateUVE::CorrectiveStepping;
    }
    if (errorMag > balanceErrorThreshold * 0.3F) {
        return TrajectoryStateUVE::BalanceDisturbance;
    }

    return TrajectoryStateUVE::StableLocomotion;
}

} // namespace UVE::Physics
