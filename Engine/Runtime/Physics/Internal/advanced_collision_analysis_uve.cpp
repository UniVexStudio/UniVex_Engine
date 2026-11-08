// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/physics/advanced_collision_analysis_uve.h"

#include <cmath>

namespace UVE::Physics {

namespace {

[[nodiscard]] Math::Vector3UVE MultiplyUVE(const Math::Matrix3x3UVE& matrix,
                                          const Math::Vector3UVE& vector) noexcept {
    return Math::Vector3UVE{
        matrix.m[0][0] * vector.x + matrix.m[0][1] * vector.y + matrix.m[0][2] * vector.z,
        matrix.m[1][0] * vector.x + matrix.m[1][1] * vector.y + matrix.m[1][2] * vector.z,
        matrix.m[2][0] * vector.x + matrix.m[2][1] * vector.y + matrix.m[2][2] * vector.z,
    };
}

} // namespace

float ComputeEffectiveCollisionMassUVE(
    const float invMassA, const float invMassB,
    const Math::Matrix3x3UVE& invInertiaA, const Math::Matrix3x3UVE& invInertiaB,
    const Math::Vector3UVE& rA, const Math::Vector3UVE& rB,
    const Math::Vector3UVE& normal) noexcept {
    const float normalLenSq = Math::LengthSquaredUVE(normal);
    if (!std::isfinite(normalLenSq) || normalLenSq <= 1.0e-8F) {
        return 0.0F;
    }
    const Math::Vector3UVE unitNormal = normal * (1.0F / std::sqrt(normalLenSq));

    float denom = invMassA + invMassB;
    if (Math::LengthSquaredUVE(rA) > 1.0e-8F) {
        const Math::Vector3UVE armCrossNormalA = Math::CrossUVE(rA, unitNormal);
        const Math::Vector3UVE rotationalTermA = MultiplyUVE(invInertiaA, armCrossNormalA);
        denom += Math::DotUVE(armCrossNormalA, rotationalTermA);
    }
    if (Math::LengthSquaredUVE(rB) > 1.0e-8F) {
        const Math::Vector3UVE armCrossNormalB = Math::CrossUVE(rB, unitNormal);
        const Math::Vector3UVE rotationalTermB = MultiplyUVE(invInertiaB, armCrossNormalB);
        denom += Math::DotUVE(armCrossNormalB, rotationalTermB);
    }

    if (!std::isfinite(denom) || denom <= 1.0e-6F) {
        return 0.0F;
    }
    return 1.0F / denom;
}

ImpactSeverityUVE ClassifyImpactSeverityUVE(const float impulseMagnitude) noexcept {
    if (!std::isfinite(impulseMagnitude) || impulseMagnitude < 15.0F) {
        return ImpactSeverityUVE::None;
    }
    if (impulseMagnitude < 80.0F) {
        return ImpactSeverityUVE::Minor;
    }
    if (impulseMagnitude < 250.0F) {
        return ImpactSeverityUVE::Moderate;
    }
    if (impulseMagnitude < 600.0F) {
        return ImpactSeverityUVE::Severe;
    }
    return ImpactSeverityUVE::Extreme;
}

ImpactDirectionUVE ClassifyImpactDirectionUVE(
    const Math::Vector3UVE& impulse, const Math::QuaternionUVE& characterRotation) noexcept {
    const float lengthSq = Math::LengthSquaredUVE(impulse);
    if (!std::isfinite(lengthSq) || lengthSq <= 1.0e-8F) {
        return ImpactDirectionUVE::Forward;
    }

    // Inverse character rotation puts impulse into character-local coordinates.
    // In this engine's convention:
    // Local forward is -Z (or +Z depending on orientation; let's check local axes).
    // Forward is (0, 0, -1), Up is (0, 1, 0), Right is (1, 0, 0).
    const Math::QuaternionUVE invRot = Math::ConjugateUVE(characterRotation);
    const Math::Vector3UVE localImpulse = Math::RotateVectorUVE(invRot, impulse * (1.0F / std::sqrt(lengthSq)));

    // Vertical impacts (jumping on head or upward blast)
    if (localImpulse.y > 0.65F) {
        return ImpactDirectionUVE::VerticalUp;
    }
    if (localImpulse.y < -0.65F) {
        return ImpactDirectionUVE::VerticalDown;
    }

    // Check forward vs rear vs lateral
    // Impact from front pushes backwards (local +Z), impact from behind pushes forward (local -Z)
    // Note: impulse direction is the push on the character.
    // If pushed backwards (positive Z): impact came from the front!
    // If pushed forward (negative Z): impact came from the rear!
    const float absX = std::abs(localImpulse.x);
    const float absZ = std::abs(localImpulse.z);

    if (absX > absZ * 1.2F) {
        return localImpulse.x < 0.0F ? ImpactDirectionUVE::LateralRight : ImpactDirectionUVE::LateralLeft;
    }
    // Otherwise primarily longitudinal
    return localImpulse.z > 0.0F ? ImpactDirectionUVE::Forward : ImpactDirectionUVE::Rear;
}

ImpactAnalysisSummaryUVE AnalyzeImpactsUVE(
    const std::span<const ImpactContactPointUVE> contacts,
    const Math::QuaternionUVE& characterOrientation) noexcept {
    ImpactAnalysisSummaryUVE summary;
    summary.contactCount = contacts.size();
    if (contacts.empty()) {
        return summary;
    }

    summary.contacts.assign(contacts.begin(), contacts.end());

    Math::Vector3UVE totalImpulse{};
    Math::Vector3UVE weightedNormal{};
    Math::Vector3UVE weightedPosition{};
    float totalWeight = 0.0F;

    for (const ImpactContactPointUVE& cp : contacts) {
        totalImpulse += cp.appliedImpulse;

        const float weight = std::max(Math::LengthUVE(cp.appliedImpulse), cp.penetrationDepth * 100.0F + 1.0F);
        weightedNormal += cp.contactNormal * weight;
        weightedPosition += cp.contactPoint * weight;
        totalWeight += weight;

        // Ground detection: normal is predominantly vertical (+Y)
        if (cp.contactNormal.y > 0.5F) {
            summary.isGroundContact = true;
            const float slope = std::acos(std::clamp(cp.contactNormal.y, -1.0F, 1.0F)) * (180.0F / 3.14159265F);
            summary.surfaceSlopeDegrees = std::max(summary.surfaceSlopeDegrees, slope);
        } else if (std::abs(cp.contactNormal.y) <= 0.3F) {
            summary.isWallContact = true;
        }
    }

    summary.totalImpulse = totalImpulse;
    summary.netImpulseMagnitude = Math::LengthUVE(totalImpulse);

    if (totalWeight > 1.0e-6F) {
        const float invW = 1.0F / totalWeight;
        summary.primaryContactPoint = weightedPosition * invW;
        const float normLen = Math::LengthUVE(weightedNormal);
        if (normLen > 1.0e-6F) {
            summary.netContactNormal = weightedNormal * (1.0F / normLen);
        }
    }

    summary.severity = ClassifyImpactSeverityUVE(summary.netImpulseMagnitude);
    summary.direction = ClassifyImpactDirectionUVE(totalImpulse, characterOrientation);

    if (!contacts.empty() && contacts[0].surfaceTag == "vehicle") {
        summary.category = ImpactCategoryUVE::CharacterVehicle;
    } else if (!contacts.empty() && contacts[0].surfaceTag == "character") {
        summary.category = ImpactCategoryUVE::CharacterCharacter;
    } else if (summary.isGroundContact) {
        summary.category = ImpactCategoryUVE::CharacterGround;
    } else if (summary.isWallContact) {
        summary.category = ImpactCategoryUVE::CharacterWall;
    } else {
        summary.category = ImpactCategoryUVE::CharacterStaticObject;
    }

    return summary;
}

} // namespace UVE::Physics
