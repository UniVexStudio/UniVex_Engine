// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "joint_3d.h"

#include <algorithm>
#include <cmath>
#include <vector>

#include "disabled_component.h"
#include "rigid_3d_component.h"
#include "transform_component.h"
#include "world_transform_component.h"
#include "i_entity_manager.h"
#include "object_3d.h"

namespace UVE::Scene {

bool IsJoint3DComponentValid(const Joint3DComponent& value) noexcept {
    if (static_cast<std::uint8_t>(value.kind) >
        static_cast<std::uint8_t>(Joint3DKind::Generic6DOF)) {
        return false;
    }
    if (!IsFinite3DObjectVector(value.anchorA) || !IsFinite3DObjectVector(value.anchorB) ||
        !IsFinite3DObjectVector(value.axis) || Math::LengthSquared(value.axis) <= 1.0e-8F ||
        !std::isfinite(value.restLength) || value.restLength < 0.0F ||
        !std::isfinite(value.lowerLimit) || !std::isfinite(value.upperLimit) ||
        value.lowerLimit > value.upperLimit || !std::isfinite(value.motorTargetVelocity) ||
        !std::isfinite(value.motorMaxImpulse) || value.motorMaxImpulse < 0.0F) {
        return false;
    }
    return true;
}

Joint3DComponent SanitizeJoint3DComponent(const Joint3DComponent& value) noexcept {
    Joint3DComponent out = value;
    if (static_cast<std::uint8_t>(out.kind) >
        static_cast<std::uint8_t>(Joint3DKind::Generic6DOF)) {
        out.kind = Joint3DKind::Pin;
    }
    if (!IsFinite3DObjectVector(out.anchorA)) {
        out.anchorA = Math::Vector3{0.0F, 0.0F, 0.0F};
    }
    if (!IsFinite3DObjectVector(out.anchorB)) {
        out.anchorB = Math::Vector3{0.0F, 0.0F, 0.0F};
    }
    if (!IsFinite3DObjectVector(out.axis) || Math::LengthSquared(out.axis) <= 1.0e-8F) {
        out.axis = Math::Vector3{0.0F, 1.0F, 0.0F};
    }
    if (!std::isfinite(out.restLength) || out.restLength < 0.0F) {
        out.restLength = 0.0F;
    }
    if (!std::isfinite(out.lowerLimit)) {
        out.lowerLimit = -1.0F;
    }
    if (!std::isfinite(out.upperLimit) || out.upperLimit < out.lowerLimit) {
        out.upperLimit = out.lowerLimit;
    }
    if (!std::isfinite(out.motorTargetVelocity)) {
        out.motorTargetVelocity = 0.0F;
    }
    if (!std::isfinite(out.motorMaxImpulse) || out.motorMaxImpulse < 0.0F) {
        out.motorMaxImpulse = 0.0F;
    }
    return out;
}

bool IsPinJoint3DObjectDefinitionValid(const PinJoint3DObjectDefinition& value) noexcept {
    return value.joint.kind == Joint3DKind::Pin && IsJoint3DComponentValid(value.joint);
}
bool IsHingeJoint3DObjectDefinitionValid(const HingeJoint3DObjectDefinition& value) noexcept {
    return value.joint.kind == Joint3DKind::Hinge && IsJoint3DComponentValid(value.joint);
}
bool IsSliderJoint3DObjectDefinitionValid(
    const SliderJoint3DObjectDefinition& value) noexcept {
    return value.joint.kind == Joint3DKind::Slider && IsJoint3DComponentValid(value.joint);
}
bool IsConeTwistJoint3DObjectDefinitionValid(
    const ConeTwistJoint3DObjectDefinition& value) noexcept {
    return value.joint.kind == Joint3DKind::ConeTwist && IsJoint3DComponentValid(value.joint);
}
bool IsGeneric6DOFJoint3DObjectDefinitionValid(
    const Generic6DOFJoint3DObjectDefinition& value) noexcept {
    return value.joint.kind == Joint3DKind::Generic6DOF &&
           IsJoint3DComponentValid(value.joint);
}

void ApplyPinJoint3DObjectDefinition(IEntityManager& entityManager, const Entity entity,
                                        const PinJoint3DObjectDefinition& value) {
    EnsureObject3DBaseline(entityManager, entity, PinJoint3DObjectDefinition::defaultName);
    Joint3DComponent sanitized = SanitizeJoint3DComponent(value.joint);
    sanitized.kind = Joint3DKind::Pin;
    entityManager.AddComponent<Joint3DComponent>(entity, sanitized);
}

void ApplyHingeJoint3DObjectDefinition(IEntityManager& entityManager, const Entity entity,
                                          const HingeJoint3DObjectDefinition& value) {
    EnsureObject3DBaseline(entityManager, entity, HingeJoint3DObjectDefinition::defaultName);
    Joint3DComponent sanitized = SanitizeJoint3DComponent(value.joint);
    sanitized.kind = Joint3DKind::Hinge;
    entityManager.AddComponent<Joint3DComponent>(entity, sanitized);
}

void ApplySliderJoint3DObjectDefinition(IEntityManager& entityManager, const Entity entity,
                                           const SliderJoint3DObjectDefinition& value) {
    EnsureObject3DBaseline(entityManager, entity, SliderJoint3DObjectDefinition::defaultName);
    Joint3DComponent sanitized = SanitizeJoint3DComponent(value.joint);
    sanitized.kind = Joint3DKind::Slider;
    entityManager.AddComponent<Joint3DComponent>(entity, sanitized);
}

void ApplyConeTwistJoint3DObjectDefinition(IEntityManager& entityManager,
                                              const Entity entity,
                                              const ConeTwistJoint3DObjectDefinition& value) {
    EnsureObject3DBaseline(entityManager, entity,
                              ConeTwistJoint3DObjectDefinition::defaultName);
    Joint3DComponent sanitized = SanitizeJoint3DComponent(value.joint);
    sanitized.kind = Joint3DKind::ConeTwist;
    entityManager.AddComponent<Joint3DComponent>(entity, sanitized);
}

void ApplyGeneric6DOFJoint3DObjectDefinition(
    IEntityManager& entityManager, const Entity entity,
    const Generic6DOFJoint3DObjectDefinition& value) {
    EnsureObject3DBaseline(entityManager, entity,
                              Generic6DOFJoint3DObjectDefinition::defaultName);
    Joint3DComponent sanitized = SanitizeJoint3DComponent(value.joint);
    sanitized.kind = Joint3DKind::Generic6DOF;
    entityManager.AddComponent<Joint3DComponent>(entity, sanitized);
}

bool EvaluateJoint3DHingeMotorLimitStep(
    Joint3DComponent& joint, const float currentAngleRadians,
    const float currentRelativeSpeedRadians, const float effectiveInverseInertia,
    const float deltaTimeSeconds, Physics::HingeMotorLimitResult& outResult) noexcept {
    if (!IsJoint3DComponentValid(joint)) {
        return false;
    }
    Physics::HingeMotorLimitInput input{};
    input.currentAngleRadians = currentAngleRadians;
    input.currentRelativeSpeedRadians = currentRelativeSpeedRadians;
    input.deltaTimeSeconds = deltaTimeSeconds;
    input.effectiveInverseInertia = effectiveInverseInertia;
    input.motorEnabled = joint.motorEnabled;
    input.targetSpeedRadians = joint.motorTargetVelocity;
    input.maximumMotorTorque = joint.motorMaxImpulse;
    input.limitsEnabled = joint.limitsEnabled;
    input.minimumAngleRadians = joint.lowerLimit;
    input.maximumAngleRadians = joint.upperLimit;
    if (!Physics::EvaluateHingeMotorLimit(input, outResult)) {
        return false;
    }
    joint.lastAppliedAngleCorrectionRadians = outResult.angleCorrectionRadians;
    joint.lastAppliedAngularSpeedDeltaRadians = outResult.angularSpeedDeltaRadians;
    return true;
}

std::size_t UpdateJoint3DObjects(IEntityManager& entityManager, ISceneGraph& sceneGraph,
                                    const float deltaTimeSeconds) {
    Physics::PhysicsConstraintSystem constraintSystem{};
    std::vector<Entity> activeJoints;
    entityManager.ForEach<Joint3DComponent>(
        [&](const Entity entity, Joint3DComponent& joint) {
            joint.solvedLastStep = false;
            if (!joint.enabled || entityManager.HasComponent<DisabledComponent>(entity) ||
                !IsJoint3DComponentValid(joint) || joint.nodeA == kInvalidEntity ||
                joint.nodeB == kInvalidEntity || joint.nodeA == joint.nodeB ||
                !entityManager.IsAlive(joint.nodeA) || !entityManager.IsAlive(joint.nodeB)) {
                return;
            }
            if (joint.kind == Joint3DKind::Hinge || joint.kind == Joint3DKind::ConeTwist) {
                Physics::HingeConstraint hinge{};
                hinge.firstEntity = joint.nodeA;
                hinge.secondEntity = joint.nodeB;
                hinge.firstAnchor = joint.anchorA;
                hinge.secondAnchor = joint.anchorB;
                hinge.worldAxis = Math::Normalize(joint.axis);
                if (constraintSystem.AddHingeConstraint(hinge).IsAccepted()) {
                    activeJoints.push_back(entity);
                }
            } else {
                Physics::DistanceConstraint dist{};
                dist.firstEntity = joint.nodeA;
                dist.secondEntity = joint.nodeB;
                dist.firstAnchor = joint.anchorA;
                dist.secondAnchor = joint.anchorB;
                dist.restLength = joint.restLength;
                if (constraintSystem.AddDistanceConstraint(dist).IsAccepted()) {
                    activeJoints.push_back(entity);
                }
            }
        });

    if (activeJoints.empty()) {
        return 0U;
    }
    const Physics::PhysicsConstraintSolveResult solveResult =
        constraintSystem.Solve(entityManager, sceneGraph);
    for (const Entity jointEntity : activeJoints) {
        Joint3DComponent& joint = entityManager.GetComponent<Joint3DComponent>(jointEntity);
        joint.solvedLastStep = solveResult.islandPlanValid;
        if (joint.kind == Joint3DKind::Hinge && deltaTimeSeconds > 0.0F) {
            Physics::HingeMotorLimitResult motorResult{};
            static_cast<void>(EvaluateJoint3DHingeMotorLimitStep(
                joint, 0.0F, 0.0F, 1.0F, deltaTimeSeconds, motorResult));
        }
    }
    return activeJoints.size();
}

} // namespace UVE::Scene
