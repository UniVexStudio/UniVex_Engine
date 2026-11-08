// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include "uve/component/entity_uve.h"
#include "uve/math/matrix3x3_uve.h"
#include "uve/math/quaternion_uve.h"
#include "uve/math/vector3_uve.h"
#include "uve/physics/physics_material_uve.h"

namespace UVE::Physics {

/// Physical severity classification based on total impulse magnitude (N.s).
/// Minor: absorbable by upper-body stiffness and small ankle/spine compensation.
/// Moderate: requires corrective stepping to expand the base of support.
/// Severe: unrecoverable balance disturbance; transitions smoothly into falling ragdoll.
/// Extreme: full momentum transfer, airborne launch or violent tumble.
enum class ImpactSeverityUVE : std::uint8_t {
    None = 0,
    Minor,
    Moderate,
    Severe,
    Extreme,
};

/// Direction of impact relative to the character's local coordinate frame.
enum class ImpactDirectionUVE : std::uint8_t {
    Forward = 0,
    Rear,
    LateralLeft,
    LateralRight,
    VerticalUp,
    VerticalDown,
    Rotational,
};

/// Type of environmental interaction.
enum class ImpactCategoryUVE : std::uint8_t {
    CharacterGround = 0,
    CharacterWall,
    CharacterCharacter,
    CharacterVehicle,
    CharacterStaticObject,
};

/// Comprehensive physical metadata for a single contact point.
struct ImpactContactPointUVE final {
    Math::Vector3UVE contactPoint{};
    Math::Vector3UVE contactNormal{0.0F, 1.0F, 0.0F};
    float penetrationDepth = 0.0F;

    Math::Vector3UVE relativeVelocity{};
    Math::Vector3UVE linearVelocityA{};
    Math::Vector3UVE linearVelocityB{};
    Math::Vector3UVE angularVelocityA{};
    Math::Vector3UVE angularVelocityB{};

    Math::Vector3UVE appliedImpulse{};
    float effectiveMass = 0.0F;
    float impactEnergy = 0.0F;
    float contactDuration = 0.0F;

    Scene::EntityUVE firstEntity = Scene::kInvalidEntityUVE;
    Scene::EntityUVE secondEntity = Scene::kInvalidEntityUVE;
    std::uint32_t bodyIndex = 0U;

    float friction = 0.8F;
    float restitution = 0.0F;
    std::string surfaceTag = "default";

    [[nodiscard]] bool operator==(const ImpactContactPointUVE&) const noexcept = default;
};

/// Aggregate analysis of one or more simultaneous contacts.
struct ImpactAnalysisSummaryUVE final {
    std::vector<ImpactContactPointUVE> contacts;
    Math::Vector3UVE totalImpulse{};
    float netImpulseMagnitude = 0.0F;
    Math::Vector3UVE netContactNormal{0.0F, 1.0F, 0.0F};
    Math::Vector3UVE primaryContactPoint{};

    ImpactSeverityUVE severity = ImpactSeverityUVE::None;
    ImpactDirectionUVE direction = ImpactDirectionUVE::Forward;
    ImpactCategoryUVE category = ImpactCategoryUVE::CharacterStaticObject;

    bool isGroundContact = false;
    bool isWallContact = false;
    float surfaceSlopeDegrees = 0.0F;
    std::size_t contactCount = 0U;

    [[nodiscard]] bool HasImpactUVE() const noexcept {
        return severity != ImpactSeverityUVE::None && netImpulseMagnitude > 1.0e-3F;
    }
};

/// Computes the effective collision mass along a contact normal for two bodies with rotational inertia.
[[nodiscard]] float ComputeEffectiveCollisionMassUVE(
    float invMassA, float invMassB,
    const Math::Matrix3x3UVE& invInertiaA, const Math::Matrix3x3UVE& invInertiaB,
    const Math::Vector3UVE& rA, const Math::Vector3UVE& rB,
    const Math::Vector3UVE& normal) noexcept;

/// Classifies impact severity based on impulse (N.s) with realistic character thresholds.
[[nodiscard]] ImpactSeverityUVE ClassifyImpactSeverityUVE(float impulseMagnitude) noexcept;

/// Classifies impact direction relative to character forward and right orientation.
[[nodiscard]] ImpactDirectionUVE ClassifyImpactDirectionUVE(
    const Math::Vector3UVE& impulse, const Math::QuaternionUVE& characterRotation) noexcept;

/// Analyzes an array of contacts and produces an aggregate physical summary.
[[nodiscard]] ImpactAnalysisSummaryUVE AnalyzeImpactsUVE(
    std::span<const ImpactContactPointUVE> contacts,
    const Math::QuaternionUVE& characterOrientation) noexcept;

} // namespace UVE::Physics
