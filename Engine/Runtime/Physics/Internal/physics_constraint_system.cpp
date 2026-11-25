// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#include "physics_constraint_system.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

#include "rigid_3d_component.h"
#include "transform_component.h"
#include "world_transform_component.h"
#include "vector3.h"

namespace UVE::Physics {
namespace {

[[nodiscard]] float ConstraintLength(const Math::Vector3& value) noexcept {
    const float squared = Math::LengthSquared(value);
    if (std::isinf(squared)) {
        return std::numeric_limits<float>::infinity();
    }
    return std::isfinite(squared) && squared >= 0.0F ? std::sqrt(squared) : 0.0F;
}

[[nodiscard]] Math::Vector3 NormalizeOrDefault(const Math::Vector3& value,
                                                      Math::Vector3 fallback) noexcept {
    const float length = ConstraintLength(value);
    if (!std::isfinite(length) || length <= PhysicsConstraintSystem::kConstraintEpsilon) {
        return fallback;
    }
    return value * (1.0F / length);
}

void ApplyLocalDelta(Scene::IEntityManager& entityManager, Scene::ISceneGraph& sceneGraph,
                        Scene::Entity entity, Math::Vector3 delta) {
    Scene::TransformComponent transform = entityManager.GetComponent<Scene::TransformComponent>(entity);
    transform.localPosition += delta;
    sceneGraph.SetLocalTransform(entityManager, entity, transform);
}

[[nodiscard]] bool IsLocalDeltaValid(Scene::IEntityManager& entityManager,
                                        Scene::Entity entity,
                                        const Math::Vector3 delta) {
    if (!Math::IsFinite(delta)) {
        return false;
    }
    Scene::TransformComponent candidate =
        entityManager.GetComponent<Scene::TransformComponent>(entity);
    candidate.localPosition += delta;
    return Scene::IsTransformComponentValid(candidate);
}

[[nodiscard]] float InverseMass(Scene::IEntityManager& entityManager,
                                   Scene::Entity entity) noexcept {
    if (!entityManager.HasComponent<Scene::Rigid3DComponent>(entity)) {
        return 0.0F;
    }
    const Scene::Rigid3DComponent& rigidBody =
        entityManager.GetComponent<Scene::Rigid3DComponent>(entity);
    if (!Scene::IsRigid3DComponentValid(rigidBody) || rigidBody.isKinematic || rigidBody.mass <= 0.0F) {
        return 0.0F;
    }
    return 1.0F / rigidBody.mass;
}

} // namespace

PhysicsConstraintMutationResult PhysicsConstraintSystem::AddDistanceConstraint(
    const DistanceConstraint& constraint) {
    return Add(Kind::Distance, &constraint, nullptr, nullptr, nullptr);
}

PhysicsConstraintMutationResult PhysicsConstraintSystem::AddHingeConstraint(
    const HingeConstraint& constraint) {
    return Add(Kind::Hinge, nullptr, &constraint, nullptr, nullptr);
}

PhysicsConstraintMutationResult PhysicsConstraintSystem::AddBallSocketConstraint(
    const BallSocketConstraint& constraint) {
    return Add(Kind::BallSocket, nullptr, nullptr, &constraint, nullptr);
}

PhysicsConstraintMutationResult PhysicsConstraintSystem::AddFixedConstraint(
    const FixedConstraint& constraint) {
    return Add(Kind::Fixed, nullptr, nullptr, nullptr, &constraint);
}

PhysicsConstraintMutationResult PhysicsConstraintSystem::Add(
    Kind kind, const DistanceConstraint* distance, const HingeConstraint* hinge,
    const BallSocketConstraint* ballSocket, const FixedConstraint* fixed) {
    if ((kind == Kind::Distance && (distance == nullptr || !IsValidDistance(*distance))) ||
        (kind == Kind::Hinge && (hinge == nullptr || !IsValidHinge(*hinge))) ||
        (kind == Kind::BallSocket && (ballSocket == nullptr || !IsValidBallSocket(*ballSocket))) ||
        (kind == Kind::Fixed && (fixed == nullptr || !IsValidFixed(*fixed)))) {
        return PhysicsConstraintMutationResult{PhysicsConstraintCode::InvalidConstraint, {}};
    }
    for (std::size_t index = 0U; index < slots_.size(); ++index) {
        Slot& slot = slots_[index];
        if (slot.occupied) {
            continue;
        }
        slot.kind = kind;
        if (kind == Kind::Distance) {
            slot.distance = *distance;
        } else if (kind == Kind::Hinge) {
            slot.hinge = *hinge;
        } else if (kind == Kind::BallSocket) {
            slot.ballSocket = *ballSocket;
        } else if (kind == Kind::Fixed) {
            slot.fixed = *fixed;
        }
        slot.occupied = true;
        return PhysicsConstraintMutationResult{
            PhysicsConstraintCode::Accepted,
            PhysicsConstraintHandle{static_cast<std::uint32_t>(index), slot.generation}};
    }
    return PhysicsConstraintMutationResult{PhysicsConstraintCode::CapacityExceeded, {}};
}

PhysicsConstraintMutationResult PhysicsConstraintSystem::RemoveConstraint(
    PhysicsConstraintHandle handle) noexcept {
    if (!handle.IsValid() || handle.index >= slots_.size()) {
        return PhysicsConstraintMutationResult{PhysicsConstraintCode::NotFound, handle};
    }
    Slot& slot = slots_[handle.index];
    if (!slot.occupied) {
        return PhysicsConstraintMutationResult{PhysicsConstraintCode::NotFound, handle};
    }
    if (slot.generation != handle.generation) {
        return PhysicsConstraintMutationResult{PhysicsConstraintCode::StaleGeneration, handle};
    }
    slot.occupied = false;
    slot.generation = slot.generation == std::numeric_limits<std::uint32_t>::max() ? 1U : slot.generation + 1U;
    return PhysicsConstraintMutationResult{PhysicsConstraintCode::Accepted, handle};
}

void PhysicsConstraintSystem::Clear() noexcept {
    for (Slot& slot : slots_) {
        slot.occupied = false;
        slot.generation = slot.generation == std::numeric_limits<std::uint32_t>::max() ? 1U : slot.generation + 1U;
    }
}

std::size_t PhysicsConstraintSystem::GetConstraintCount() const noexcept {
    return static_cast<std::size_t>(std::count_if(slots_.begin(), slots_.end(), [](const Slot& slot) {
        return slot.occupied;
    }));
}

bool PhysicsConstraintSystem::IsValidDistance(const DistanceConstraint& constraint) noexcept {
    return constraint.firstEntity != Scene::kInvalidEntity &&
           constraint.secondEntity != Scene::kInvalidEntity &&
           constraint.firstEntity != constraint.secondEntity && Math::IsFinite(constraint.firstAnchor) &&
           Math::IsFinite(constraint.secondAnchor) && std::isfinite(constraint.restLength) &&
           constraint.restLength >= 0.0F;
}

bool PhysicsConstraintSystem::IsValidHinge(const HingeConstraint& constraint) noexcept {
    const float worldAxisLength = ConstraintLength(constraint.worldAxis);
    return constraint.firstEntity != Scene::kInvalidEntity &&
           constraint.secondEntity != Scene::kInvalidEntity &&
           constraint.firstEntity != constraint.secondEntity && Math::IsFinite(constraint.firstAnchor) &&
           Math::IsFinite(constraint.secondAnchor) && Math::IsFinite(constraint.worldAxis) &&
           std::isfinite(worldAxisLength) &&
           worldAxisLength > PhysicsConstraintSystem::kConstraintEpsilon;
}

bool PhysicsConstraintSystem::IsValidBallSocket(const BallSocketConstraint& constraint) noexcept {
    return constraint.firstEntity != Scene::kInvalidEntity &&
           constraint.secondEntity != Scene::kInvalidEntity &&
           constraint.firstEntity != constraint.secondEntity && Math::IsFinite(constraint.firstAnchor) &&
           Math::IsFinite(constraint.secondAnchor);
}

bool PhysicsConstraintSystem::IsValidFixed(const FixedConstraint& constraint) noexcept {
    return constraint.firstEntity != Scene::kInvalidEntity &&
           constraint.secondEntity != Scene::kInvalidEntity &&
           constraint.firstEntity != constraint.secondEntity && Math::IsFinite(constraint.firstAnchor) &&
           Math::IsFinite(constraint.secondAnchor);
}

PhysicsConstraintSolveResult PhysicsConstraintSystem::Solve(
    Scene::IEntityManager& entityManager, Scene::ISceneGraph& sceneGraph,
    std::size_t maximumIterations) const {
    PhysicsConstraintSolveResult result;
    const std::size_t constraintCount = GetConstraintCount();
    if (constraintCount == 0U) {
        result.islandPlanValid = true;
        return result;
    }

    std::array<ConstraintIslandEdge, kMaximumConstraints> edges{};
    std::size_t edgeCount = 0U;
    const auto getEntities = [](const Slot& slot) noexcept {
        switch (slot.kind) {
            case Kind::Distance:
                return std::pair<Scene::Entity, Scene::Entity>{slot.distance.firstEntity, slot.distance.secondEntity};
            case Kind::Hinge:
                return std::pair<Scene::Entity, Scene::Entity>{slot.hinge.firstEntity, slot.hinge.secondEntity};
            case Kind::BallSocket:
                return std::pair<Scene::Entity, Scene::Entity>{slot.ballSocket.firstEntity, slot.ballSocket.secondEntity};
            case Kind::Fixed:
                return std::pair<Scene::Entity, Scene::Entity>{slot.fixed.firstEntity, slot.fixed.secondEntity};
        }
        return std::pair<Scene::Entity, Scene::Entity>{Scene::kInvalidEntity, Scene::kInvalidEntity};
    };
    for (const Slot& slot : slots_) {
        if (!slot.occupied) {
            continue;
        }
        const auto [firstEntity, secondEntity] = getEntities(slot);
        edges[edgeCount++] = ConstraintIslandEdge{firstEntity, secondEntity};
    }

    ConstraintIslandPlan islandPlan;
    if (!BuildConstraintIslandPlan(std::span<const ConstraintIslandEdge>{edges.data(), edgeCount}, islandPlan)) {
        return result;
    }
    result.islandPlanValid = true;
    result.islandCount = islandPlan.islandCount;

    const std::size_t iterationLimit = std::min(maximumIterations, kMaximumSolverIterations);
    if (iterationLimit == 0U) {
        return result;
    }

    std::array<std::size_t, kMaximumConstraints> orderedIndices{};
    std::size_t orderedCount = 0U;
    for (std::size_t island = 0U; island < islandPlan.islandCount; ++island) {
        for (std::size_t slotIndex = 0U; slotIndex < slots_.size(); ++slotIndex) {
            const Slot& slot = slots_[slotIndex];
            if (!slot.occupied) {
                continue;
            }
            const auto [firstEntity, secondEntity] = getEntities(slot);
            std::size_t firstEntityIndex = ConstraintIslandPlan::kMaximumEntities;
            for (std::size_t entityIndex = 0U; entityIndex < islandPlan.entityCount; ++entityIndex) {
                if (islandPlan.entities[entityIndex] == firstEntity) {
                    firstEntityIndex = entityIndex;
                    break;
                }
            }
            if (firstEntityIndex != ConstraintIslandPlan::kMaximumEntities &&
                islandPlan.islandIndices[firstEntityIndex] == island) {
                orderedIndices[orderedCount++] = slotIndex;
            }
        }
    }

    std::array<bool, kMaximumConstraints> reportedSkipped{};
    sceneGraph.Update(entityManager);
    for (std::size_t iteration = 0U; iteration < iterationLimit; ++iteration) {
        bool changed = false;
        for (std::size_t orderIndex = 0U; orderIndex < orderedCount; ++orderIndex) {
            const std::size_t index = orderedIndices[orderIndex];
            const Slot& slot = slots_[index];
            if (!slot.occupied) {
                continue;
            }
            const auto [firstEntity, secondEntity] = getEntities(slot);
            if (!entityManager.IsAlive(firstEntity) || !entityManager.IsAlive(secondEntity) ||
                !entityManager.HasComponent<Scene::TransformComponent>(firstEntity) ||
                !entityManager.HasComponent<Scene::TransformComponent>(secondEntity) ||
                !entityManager.HasComponent<Scene::WorldTransformComponent>(firstEntity) ||
                !entityManager.HasComponent<Scene::WorldTransformComponent>(secondEntity)) {
                if (!reportedSkipped[index]) {
                    ++result.skippedConstraintCount;
                    reportedSkipped[index] = true;
                }
                continue;
            }

            const Scene::WorldTransformComponent& firstWorld =
                entityManager.GetComponent<Scene::WorldTransformComponent>(firstEntity);
            const Scene::WorldTransformComponent& secondWorld =
                entityManager.GetComponent<Scene::WorldTransformComponent>(secondEntity);
            const auto getAnchorOffsets = [](const Slot& s) noexcept {
                switch (s.kind) {
                    case Kind::Distance:
                        return std::pair<Math::Vector3, Math::Vector3>{s.distance.firstAnchor, s.distance.secondAnchor};
                    case Kind::Hinge:
                        return std::pair<Math::Vector3, Math::Vector3>{s.hinge.firstAnchor, s.hinge.secondAnchor};
                    case Kind::BallSocket:
                        return std::pair<Math::Vector3, Math::Vector3>{s.ballSocket.firstAnchor, s.ballSocket.secondAnchor};
                    case Kind::Fixed:
                        return std::pair<Math::Vector3, Math::Vector3>{s.fixed.firstAnchor, s.fixed.secondAnchor};
                }
                return std::pair<Math::Vector3, Math::Vector3>{Math::Vector3{}, Math::Vector3{}};
            };
            const auto [firstAnchorOffset, secondAnchorOffset] = getAnchorOffsets(slot);
            const Math::Vector3 firstAnchor = firstWorld.worldPosition + firstAnchorOffset;
            const Math::Vector3 secondAnchor = secondWorld.worldPosition + secondAnchorOffset;
            const Math::Vector3 delta = secondAnchor - firstAnchor;
            if (!Math::IsFinite(delta)) {
                if (!reportedSkipped[index]) {
                    ++result.skippedConstraintCount;
                    reportedSkipped[index] = true;
                }
                continue;
            }

            Math::Vector3 correction{};
            if (slot.kind == Kind::Distance) {
                const float distance = ConstraintLength(delta);
                const float error = distance - slot.distance.restLength;
                if (!std::isfinite(error)) {
                    if (!reportedSkipped[index]) {
                        ++result.skippedConstraintCount;
                        reportedSkipped[index] = true;
                    }
                    continue;
                }
                if (std::fabs(error) <= kConstraintEpsilon) {
                    continue;
                }
                const Math::Vector3 direction =
                    NormalizeOrDefault(delta, Math::Vector3{1.0F, 0.0F, 0.0F});
                correction = direction * error;
            } else {
                if (ConstraintLength(delta) <= kConstraintEpsilon) {
                    continue;
                }
                correction = delta;
            }

            const float firstInverseMass = InverseMass(entityManager, firstEntity);
            const float secondInverseMass = InverseMass(entityManager, secondEntity);
            const float totalInverseMass = firstInverseMass + secondInverseMass;
            if (!std::isfinite(firstInverseMass) || !std::isfinite(secondInverseMass) ||
                !std::isfinite(totalInverseMass)) {
                if (!reportedSkipped[index]) {
                    ++result.skippedConstraintCount;
                    reportedSkipped[index] = true;
                }
                continue;
            }
            if (totalInverseMass <= 0.0F) {
                continue;
            }
            const Math::Vector3 firstDelta = correction * (firstInverseMass / totalInverseMass);
            const Math::Vector3 secondDelta = correction * (-secondInverseMass / totalInverseMass);
            if (!IsLocalDeltaValid(entityManager, firstEntity, firstDelta) ||
                !IsLocalDeltaValid(entityManager, secondEntity, secondDelta)) {
                if (!reportedSkipped[index]) {
                    ++result.skippedConstraintCount;
                    reportedSkipped[index] = true;
                }
                continue;
            }
            ApplyLocalDelta(entityManager, sceneGraph, firstEntity, firstDelta);
            ApplyLocalDelta(entityManager, sceneGraph, secondEntity, secondDelta);
            ++result.solvedConstraintCount;
            changed = true;
            sceneGraph.Update(entityManager);
        }
        result.iterations = iteration + 1U;
        if (!changed) {
            return result;
        }
    }
    result.iterationCapReached = true;
    return result;
}

} // namespace UVE::Physics
