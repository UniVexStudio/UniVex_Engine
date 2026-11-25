// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

#include "entity.h"
#include "object_3d_common.h"
#include "hinge_motor_limit.h"
#include "physics_constraint_system.h"
#include "i_scene_graph.h"

namespace UVE::Scene {

class IEntityManager;

enum class Joint3DKind : std::uint8_t {
    Pin = 0,
    Hinge = 1,
    Slider = 2,
    ConeTwist = 3,
    Generic6DOF = 4,
};

/// Unified 3D physics joint/constraint component (`Pin`, `Hinge`, `Slider`, `ConeTwist`,
/// `Generic6DOF`) connecting `nodeA` and `nodeB` at world/local anchors `anchorA` and `anchorB`
/// along `axis`. Synced with `Physics::PhysicsConstraintSystem` and
/// `Physics::EvaluateHingeMotorLimit`.
struct Joint3DComponent final {
    Joint3DKind kind = Joint3DKind::Pin;
    Entity nodeA = kInvalidEntity;
    Entity nodeB = kInvalidEntity;
    Math::Vector3 anchorA{0.0F, 0.0F, 0.0F};
    Math::Vector3 anchorB{0.0F, 0.0F, 0.0F};
    Math::Vector3 axis{0.0F, 1.0F, 0.0F};
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

    [[nodiscard]] bool operator==(const Joint3DComponent& other) const noexcept {
        return kind == other.kind && nodeA == other.nodeA && nodeB == other.nodeB &&
               anchorA == other.anchorA && anchorB == other.anchorB && axis == other.axis &&
               restLength == other.restLength && limitsEnabled == other.limitsEnabled &&
               lowerLimit == other.lowerLimit && upperLimit == other.upperLimit &&
               motorEnabled == other.motorEnabled &&
               motorTargetVelocity == other.motorTargetVelocity &&
               motorMaxImpulse == other.motorMaxImpulse && enabled == other.enabled;
    }
};

[[nodiscard]] bool IsJoint3DComponentValid(const Joint3DComponent& value) noexcept;

[[nodiscard]] Joint3DComponent SanitizeJoint3DComponent(
    const Joint3DComponent& value) noexcept;

struct PinJoint3DObjectDefinition final {
    static constexpr std::string_view defaultName = "PinJoint3D";
    Joint3DComponent joint{Joint3DKind::Pin};
};

struct HingeJoint3DObjectDefinition final {
    static constexpr std::string_view defaultName = "HingeJoint3D";
    Joint3DComponent joint{Joint3DKind::Hinge};
};

struct SliderJoint3DObjectDefinition final {
    static constexpr std::string_view defaultName = "SliderJoint3D";
    Joint3DComponent joint{Joint3DKind::Slider};
};

struct ConeTwistJoint3DObjectDefinition final {
    static constexpr std::string_view defaultName = "ConeTwistJoint3D";
    Joint3DComponent joint{Joint3DKind::ConeTwist};
};

struct Generic6DOFJoint3DObjectDefinition final {
    static constexpr std::string_view defaultName = "Generic6DOFJoint3D";
    Joint3DComponent joint{Joint3DKind::Generic6DOF};
};

[[nodiscard]] bool IsPinJoint3DObjectDefinitionValid(
    const PinJoint3DObjectDefinition& value) noexcept;
[[nodiscard]] bool IsHingeJoint3DObjectDefinitionValid(
    const HingeJoint3DObjectDefinition& value) noexcept;
[[nodiscard]] bool IsSliderJoint3DObjectDefinitionValid(
    const SliderJoint3DObjectDefinition& value) noexcept;
[[nodiscard]] bool IsConeTwistJoint3DObjectDefinitionValid(
    const ConeTwistJoint3DObjectDefinition& value) noexcept;
[[nodiscard]] bool IsGeneric6DOFJoint3DObjectDefinitionValid(
    const Generic6DOFJoint3DObjectDefinition& value) noexcept;

void ApplyPinJoint3DObjectDefinition(IEntityManager& entityManager, Entity entity,
                                        const PinJoint3DObjectDefinition& value);
void ApplyHingeJoint3DObjectDefinition(IEntityManager& entityManager, Entity entity,
                                          const HingeJoint3DObjectDefinition& value);
void ApplySliderJoint3DObjectDefinition(IEntityManager& entityManager, Entity entity,
                                           const SliderJoint3DObjectDefinition& value);
void ApplyConeTwistJoint3DObjectDefinition(IEntityManager& entityManager, Entity entity,
                                              const ConeTwistJoint3DObjectDefinition& value);
void ApplyGeneric6DOFJoint3DObjectDefinition(IEntityManager& entityManager, Entity entity,
                                                const Generic6DOFJoint3DObjectDefinition& value);

/// Evaluates hinge motor/limit response for a `Joint3DComponent` using
/// `Physics::EvaluateHingeMotorLimit` and records the runtime correction fields on `joint`.
[[nodiscard]] bool EvaluateJoint3DHingeMotorLimitStep(
    Joint3DComponent& joint, float currentAngleRadians, float currentRelativeSpeedRadians,
    float effectiveInverseInertia, float deltaTimeSeconds,
    Physics::HingeMotorLimitResult& outResult) noexcept;

/// Populates a `Physics::PhysicsConstraintSystem` from all enabled `Joint3DComponent`
/// entities, solves positional constraints via `constraintSystem.Solve(entityManager, sceneGraph)`,
/// and steps linear/hinge limits. Returns the number of active joints solved.
std::size_t UpdateJoint3DObjects(IEntityManager& entityManager, ISceneGraph& sceneGraph,
                                    float deltaTimeSeconds);

} // namespace UVE::Scene
