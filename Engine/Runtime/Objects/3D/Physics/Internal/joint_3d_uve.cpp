// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/objects/3d/joint_3d_uve.h"

#include <algorithm>
#include <cmath>
#include <vector>

#include "uve/component/disabled_component_uve.h"
#include "uve/component/rigid_3d_component_uve.h"
#include "uve/component/transform_component_uve.h"
#include "uve/component/world_transform_component_uve.h"
#include "uve/entity/i_entity_manager_uve.h"
#include "uve/objects/3d/object_3d_uve.h"

namespace UVE::Scene {

bool IsJoint3DComponentValidUVE(const Joint3DComponentUVE& value) noexcept {
    if (static_cast<std::uint8_t>(value.kind) >
        static_cast<std::uint8_t>(Joint3DKindUVE::Generic6DOF)) {
        return false;
    }
    if (!IsFinite3DObjectVectorUVE(value.anchorA) || !IsFinite3DObjectVectorUVE(value.anchorB) ||
        !IsFinite3DObjectVectorUVE(value.axis) || Math::LengthSquaredUVE(value.axis) <= 1.0e-8F ||
        !std::isfinite(value.restLength) || value.restLength < 0.0F ||
        !std::isfinite(value.lowerLimit) || !std::isfinite(value.upperLimit) ||
        value.lowerLimit > value.upperLimit || !std::isfinite(value.motorTargetVelocity) ||
        !std::isfinite(value.motorMaxImpulse) || value.motorMaxImpulse < 0.0F) {
        return false;
    }
    return true;
}

Joint3DComponentUVE SanitizeJoint3DComponentUVE(const Joint3DComponentUVE& value) noexcept {
    Joint3DComponentUVE out = value;
    if (static_cast<std::uint8_t>(out.kind) >
        static_cast<std::uint8_t>(Joint3DKindUVE::Generic6DOF)) {
        out.kind = Joint3DKindUVE::Pin;
    }
    if (!IsFinite3DObjectVectorUVE(out.anchorA)) {
        out.anchorA = Math::Vector3UVE{0.0F, 0.0F, 0.0F};
    }
    if (!IsFinite3DObjectVectorUVE(out.anchorB)) {
        out.anchorB = Math::Vector3UVE{0.0F, 0.0F, 0.0F};
    }
    if (!IsFinite3DObjectVectorUVE(out.axis) || Math::LengthSquaredUVE(out.axis) <= 1.0e-8F) {
        out.axis = Math::Vector3UVE{0.0F, 1.0F, 0.0F};
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

bool IsPinJoint3DObjectDefinitionValidUVE(const PinJoint3DObjectDefinitionUVE& value) noexcept {
    return value.joint.kind == Joint3DKindUVE::Pin && IsJoint3DComponentValidUVE(value.joint);
}
bool IsHingeJoint3DObjectDefinitionValidUVE(const HingeJoint3DObjectDefinitionUVE& value) noexcept {
    return value.joint.kind == Joint3DKindUVE::Hinge && IsJoint3DComponentValidUVE(value.joint);
}
bool IsSliderJoint3DObjectDefinitionValidUVE(
    const SliderJoint3DObjectDefinitionUVE& value) noexcept {
    return value.joint.kind == Joint3DKindUVE::Slider && IsJoint3DComponentValidUVE(value.joint);
}
bool IsConeTwistJoint3DObjectDefinitionValidUVE(
    const ConeTwistJoint3DObjectDefinitionUVE& value) noexcept {
    return value.joint.kind == Joint3DKindUVE::ConeTwist && IsJoint3DComponentValidUVE(value.joint);
}
bool IsGeneric6DOFJoint3DObjectDefinitionValidUVE(
    const Generic6DOFJoint3DObjectDefinitionUVE& value) noexcept {
    return value.joint.kind == Joint3DKindUVE::Generic6DOF &&
           IsJoint3DComponentValidUVE(value.joint);
}

void ApplyPinJoint3DObjectDefinitionUVE(IEntityManagerUVE& entityManager, const EntityUVE entity,
                                        const PinJoint3DObjectDefinitionUVE& value) {
    EnsureObject3DBaselineUVE(entityManager, entity, PinJoint3DObjectDefinitionUVE::defaultName);
    Joint3DComponentUVE sanitized = SanitizeJoint3DComponentUVE(value.joint);
    sanitized.kind = Joint3DKindUVE::Pin;
    entityManager.AddComponentUVE<Joint3DComponentUVE>(entity, sanitized);
}

void ApplyHingeJoint3DObjectDefinitionUVE(IEntityManagerUVE& entityManager, const EntityUVE entity,
                                          const HingeJoint3DObjectDefinitionUVE& value) {
    EnsureObject3DBaselineUVE(entityManager, entity, HingeJoint3DObjectDefinitionUVE::defaultName);
    Joint3DComponentUVE sanitized = SanitizeJoint3DComponentUVE(value.joint);
    sanitized.kind = Joint3DKindUVE::Hinge;
    entityManager.AddComponentUVE<Joint3DComponentUVE>(entity, sanitized);
}

void ApplySliderJoint3DObjectDefinitionUVE(IEntityManagerUVE& entityManager, const EntityUVE entity,
                                           const SliderJoint3DObjectDefinitionUVE& value) {
    EnsureObject3DBaselineUVE(entityManager, entity, SliderJoint3DObjectDefinitionUVE::defaultName);
    Joint3DComponentUVE sanitized = SanitizeJoint3DComponentUVE(value.joint);
    sanitized.kind = Joint3DKindUVE::Slider;
    entityManager.AddComponentUVE<Joint3DComponentUVE>(entity, sanitized);
}

void ApplyConeTwistJoint3DObjectDefinitionUVE(IEntityManagerUVE& entityManager,
                                              const EntityUVE entity,
                                              const ConeTwistJoint3DObjectDefinitionUVE& value) {
    EnsureObject3DBaselineUVE(entityManager, entity,
                              ConeTwistJoint3DObjectDefinitionUVE::defaultName);
    Joint3DComponentUVE sanitized = SanitizeJoint3DComponentUVE(value.joint);
    sanitized.kind = Joint3DKindUVE::ConeTwist;
    entityManager.AddComponentUVE<Joint3DComponentUVE>(entity, sanitized);
}

void ApplyGeneric6DOFJoint3DObjectDefinitionUVE(
    IEntityManagerUVE& entityManager, const EntityUVE entity,
    const Generic6DOFJoint3DObjectDefinitionUVE& value) {
    EnsureObject3DBaselineUVE(entityManager, entity,
                              Generic6DOFJoint3DObjectDefinitionUVE::defaultName);
    Joint3DComponentUVE sanitized = SanitizeJoint3DComponentUVE(value.joint);
    sanitized.kind = Joint3DKindUVE::Generic6DOF;
    entityManager.AddComponentUVE<Joint3DComponentUVE>(entity, sanitized);
}

bool EvaluateJoint3DHingeMotorLimitStepUVE(
    Joint3DComponentUVE& joint, const float currentAngleRadians,
    const float currentRelativeSpeedRadians, const float effectiveInverseInertia,
    const float deltaTimeSeconds, Physics::HingeMotorLimitResultUVE& outResult) noexcept {
    if (!IsJoint3DComponentValidUVE(joint)) {
        return false;
    }
    Physics::HingeMotorLimitInputUVE input{};
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
    if (!Physics::EvaluateHingeMotorLimitUVE(input, outResult)) {
        return false;
    }
    joint.lastAppliedAngleCorrectionRadians = outResult.angleCorrectionRadians;
    joint.lastAppliedAngularSpeedDeltaRadians = outResult.angularSpeedDeltaRadians;
    return true;
}

std::size_t UpdateJoint3DObjectsUVE(IEntityManagerUVE& entityManager, ISceneGraphUVE& sceneGraph,
                                    const float deltaTimeSeconds) {
    Physics::PhysicsConstraintSystemUVE constraintSystem{};
    std::vector<EntityUVE> activeJoints;
    entityManager.ForEachUVE<Joint3DComponentUVE>(
        [&](const EntityUVE entity, Joint3DComponentUVE& joint) {
            joint.solvedLastStep = false;
            if (!joint.enabled || entityManager.HasComponentUVE<DisabledComponentUVE>(entity) ||
                !IsJoint3DComponentValidUVE(joint) || joint.nodeA == kInvalidEntityUVE ||
                joint.nodeB == kInvalidEntityUVE || joint.nodeA == joint.nodeB ||
                !entityManager.IsAliveUVE(joint.nodeA) || !entityManager.IsAliveUVE(joint.nodeB)) {
                return;
            }
            if (joint.kind == Joint3DKindUVE::Hinge || joint.kind == Joint3DKindUVE::ConeTwist) {
                Physics::HingeConstraintUVE hinge{};
                hinge.firstEntity = joint.nodeA;
                hinge.secondEntity = joint.nodeB;
                hinge.firstAnchor = joint.anchorA;
                hinge.secondAnchor = joint.anchorB;
                hinge.worldAxis = Math::NormalizeUVE(joint.axis);
                if (constraintSystem.AddHingeConstraintUVE(hinge).IsAcceptedUVE()) {
                    activeJoints.push_back(entity);
                }
            } else {
                Physics::DistanceConstraintUVE dist{};
                dist.firstEntity = joint.nodeA;
                dist.secondEntity = joint.nodeB;
                dist.firstAnchor = joint.anchorA;
                dist.secondAnchor = joint.anchorB;
                dist.restLength = joint.restLength;
                if (constraintSystem.AddDistanceConstraintUVE(dist).IsAcceptedUVE()) {
                    activeJoints.push_back(entity);
                }
            }
        });

    if (activeJoints.empty()) {
        return 0U;
    }
    const Physics::PhysicsConstraintSolveResultUVE solveResult =
        constraintSystem.SolveUVE(entityManager, sceneGraph);
    for (const EntityUVE jointEntity : activeJoints) {
        Joint3DComponentUVE& joint = entityManager.GetComponentUVE<Joint3DComponentUVE>(jointEntity);
        joint.solvedLastStep = solveResult.islandPlanValid;
        if (joint.kind == Joint3DKindUVE::Hinge && deltaTimeSeconds > 0.0F) {
            Physics::HingeMotorLimitResultUVE motorResult{};
            static_cast<void>(EvaluateJoint3DHingeMotorLimitStepUVE(
                joint, 0.0F, 0.0F, 1.0F, deltaTimeSeconds, motorResult));
        }
    }
    return activeJoints.size();
}

} // namespace UVE::Scene
