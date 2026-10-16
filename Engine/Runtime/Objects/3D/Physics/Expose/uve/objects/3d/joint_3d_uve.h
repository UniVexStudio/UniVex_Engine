// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

#include "uve/component/entity_uve.h"
#include "uve/objects/3d/object_3d_common_uve.h"
#include "uve/physics/hinge_motor_limit_uve.h"
#include "uve/physics/physics_constraint_system_uve.h"
#include "uve/scene/i_scene_graph_uve.h"

namespace UVE::Scene {

class IEntityManagerUVE;

enum class Joint3DKindUVE : std::uint8_t {
    Pin = 0,
    Hinge = 1,
    Slider = 2,
    ConeTwist = 3,
    Generic6DOF = 4,
};

/// Unified 3D physics joint/constraint component (`Pin`, `Hinge`, `Slider`, `ConeTwist`,
/// `Generic6DOF`) connecting `nodeA` and `nodeB` at world/local anchors `anchorA` and `anchorB`
/// along `axis`. Synced with `Physics::PhysicsConstraintSystemUVE` and
/// `Physics::EvaluateHingeMotorLimitUVE`.
struct Joint3DComponentUVE final {
    Joint3DKindUVE kind = Joint3DKindUVE::Pin;
    EntityUVE nodeA = kInvalidEntityUVE;
    EntityUVE nodeB = kInvalidEntityUVE;
    Math::Vector3UVE anchorA{0.0F, 0.0F, 0.0F};
    Math::Vector3UVE anchorB{0.0F, 0.0F, 0.0F};
    Math::Vector3UVE axis{0.0F, 1.0F, 0.0F};
    float restLength = 0.0F;
    bool limitsEnabled = false;
    float lowerLimit = -1.0F;
    float upperLimit = 1.0F;
    bool motorEnabled = false;
    float motorTargetVelocity = 0.0F;
    float motorMaxImpulse = 10.0F;
    bool enabled = true;

    // Runtime state, never persisted:
    float lastAppliedAngleCorrectionRadians = 0.0F;
    float lastAppliedAngularSpeedDeltaRadians = 0.0F;
    bool solvedLastStep = false;

    [[nodiscard]] bool operator==(const Joint3DComponentUVE& other) const noexcept {
        return kind == other.kind && nodeA == other.nodeA && nodeB == other.nodeB &&
               anchorA == other.anchorA && anchorB == other.anchorB && axis == other.axis &&
               restLength == other.restLength && limitsEnabled == other.limitsEnabled &&
               lowerLimit == other.lowerLimit && upperLimit == other.upperLimit &&
               motorEnabled == other.motorEnabled &&
               motorTargetVelocity == other.motorTargetVelocity &&
               motorMaxImpulse == other.motorMaxImpulse && enabled == other.enabled;
    }
};

[[nodiscard]] bool IsJoint3DComponentValidUVE(const Joint3DComponentUVE& value) noexcept;

[[nodiscard]] Joint3DComponentUVE SanitizeJoint3DComponentUVE(
    const Joint3DComponentUVE& value) noexcept;

struct PinJoint3DObjectDefinitionUVE final {
    static constexpr std::string_view defaultName = "PinJoint3D";
    Joint3DComponentUVE joint{Joint3DKindUVE::Pin};
};

struct HingeJoint3DObjectDefinitionUVE final {
    static constexpr std::string_view defaultName = "HingeJoint3D";
    Joint3DComponentUVE joint{Joint3DKindUVE::Hinge};
};

struct SliderJoint3DObjectDefinitionUVE final {
    static constexpr std::string_view defaultName = "SliderJoint3D";
    Joint3DComponentUVE joint{Joint3DKindUVE::Slider};
};

struct ConeTwistJoint3DObjectDefinitionUVE final {
    static constexpr std::string_view defaultName = "ConeTwistJoint3D";
    Joint3DComponentUVE joint{Joint3DKindUVE::ConeTwist};
};

struct Generic6DOFJoint3DObjectDefinitionUVE final {
    static constexpr std::string_view defaultName = "Generic6DOFJoint3D";
    Joint3DComponentUVE joint{Joint3DKindUVE::Generic6DOF};
};

[[nodiscard]] bool IsPinJoint3DObjectDefinitionValidUVE(
    const PinJoint3DObjectDefinitionUVE& value) noexcept;
[[nodiscard]] bool IsHingeJoint3DObjectDefinitionValidUVE(
    const HingeJoint3DObjectDefinitionUVE& value) noexcept;
[[nodiscard]] bool IsSliderJoint3DObjectDefinitionValidUVE(
    const SliderJoint3DObjectDefinitionUVE& value) noexcept;
[[nodiscard]] bool IsConeTwistJoint3DObjectDefinitionValidUVE(
    const ConeTwistJoint3DObjectDefinitionUVE& value) noexcept;
[[nodiscard]] bool IsGeneric6DOFJoint3DObjectDefinitionValidUVE(
    const Generic6DOFJoint3DObjectDefinitionUVE& value) noexcept;

void ApplyPinJoint3DObjectDefinitionUVE(IEntityManagerUVE& entityManager, EntityUVE entity,
                                        const PinJoint3DObjectDefinitionUVE& value);
void ApplyHingeJoint3DObjectDefinitionUVE(IEntityManagerUVE& entityManager, EntityUVE entity,
                                          const HingeJoint3DObjectDefinitionUVE& value);
void ApplySliderJoint3DObjectDefinitionUVE(IEntityManagerUVE& entityManager, EntityUVE entity,
                                           const SliderJoint3DObjectDefinitionUVE& value);
void ApplyConeTwistJoint3DObjectDefinitionUVE(IEntityManagerUVE& entityManager, EntityUVE entity,
                                              const ConeTwistJoint3DObjectDefinitionUVE& value);
void ApplyGeneric6DOFJoint3DObjectDefinitionUVE(IEntityManagerUVE& entityManager, EntityUVE entity,
                                                const Generic6DOFJoint3DObjectDefinitionUVE& value);

/// Evaluates hinge motor/limit response for a `Joint3DComponentUVE` using
/// `Physics::EvaluateHingeMotorLimitUVE` and records the runtime correction fields on `joint`.
[[nodiscard]] bool EvaluateJoint3DHingeMotorLimitStepUVE(
    Joint3DComponentUVE& joint, float currentAngleRadians, float currentRelativeSpeedRadians,
    float effectiveInverseInertia, float deltaTimeSeconds,
    Physics::HingeMotorLimitResultUVE& outResult) noexcept;

/// Populates a `Physics::PhysicsConstraintSystemUVE` from all enabled `Joint3DComponentUVE`
/// entities, solves positional constraints via `constraintSystem.SolveUVE(entityManager, sceneGraph)`,
/// and steps linear/hinge limits. Returns the number of active joints solved.
std::size_t UpdateJoint3DObjectsUVE(IEntityManagerUVE& entityManager, ISceneGraphUVE& sceneGraph,
                                    float deltaTimeSeconds);

} // namespace UVE::Scene
