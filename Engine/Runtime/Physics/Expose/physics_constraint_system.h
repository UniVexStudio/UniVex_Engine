// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

#include "vector3.h"
#include "entity.h"
#include "i_entity_manager.h"
#include "i_scene_graph.h"

namespace UVE::Physics {

struct PhysicsConstraintHandle final {
    std::uint32_t index = UINT32_MAX;
    std::uint32_t generation = 0U;

    [[nodiscard]] bool IsValid() const noexcept {
        return index != UINT32_MAX && generation != 0U;
    }

    [[nodiscard]] constexpr bool operator==(const PhysicsConstraintHandle&) const noexcept = default;
};

enum class PhysicsConstraintCode : std::uint8_t {
    Accepted = 0,
    InvalidConstraint,
    CapacityExceeded,
    NotFound,
    StaleGeneration,
};

struct DistanceConstraint final {
    Scene::Entity firstEntity;
    Scene::Entity secondEntity;
    Math::Vector3 firstAnchor{};
    Math::Vector3 secondAnchor{};
    float restLength = 0.0F;
};

struct HingeConstraint final {
    Scene::Entity firstEntity;
    Scene::Entity secondEntity;
    Math::Vector3 firstAnchor{};
    Math::Vector3 secondAnchor{};
    Math::Vector3 worldAxis{0.0F, 1.0F, 0.0F};
};

struct BallSocketConstraint final {
    Scene::Entity firstEntity;
    Scene::Entity secondEntity;
    Math::Vector3 firstAnchor{};
    Math::Vector3 secondAnchor{};
};

struct FixedConstraint final {
    Scene::Entity firstEntity;
    Scene::Entity secondEntity;
    Math::Vector3 firstAnchor{};
    Math::Vector3 secondAnchor{};
};

struct PhysicsConstraintMutationResult final {
    PhysicsConstraintCode code = PhysicsConstraintCode::InvalidConstraint;
    PhysicsConstraintHandle handle;

    [[nodiscard]] bool IsAccepted() const noexcept {
        return code == PhysicsConstraintCode::Accepted;
    }
};

struct PhysicsConstraintSolveResult final {
    std::size_t iterations = 0U;
    std::size_t solvedConstraintCount = 0U;
    std::size_t skippedConstraintCount = 0U;
    /// Number of deterministic connected constraint islands consumed by this solve.
    std::size_t islandCount = 0U;
    /// False only if the bounded copied island plan could not be built; no solver mutation follows.
    bool islandPlanValid = false;
    bool iterationCapReached = false;
};

struct ConstraintIslandEdge final {
    Scene::Entity firstEntity;
    Scene::Entity secondEntity;
};

struct ConstraintIslandPlan final {
    static constexpr std::size_t kMaximumEntities = 2048U;
    std::array<Scene::Entity, kMaximumEntities> entities{};
    std::array<std::size_t, kMaximumEntities> islandIndices{};
    std::size_t entityCount = 0U;
    std::size_t islandCount = 0U;
};

/// Builds deterministic connected components from copied constraint edges. The planner owns no
/// constraints, entities, solver iterations, persistence, or backend state.
[[nodiscard]] inline bool BuildConstraintIslandPlan(
    const std::span<const ConstraintIslandEdge> edges, ConstraintIslandPlan& outPlan) noexcept {
    if (edges.size() > ConstraintIslandPlan::kMaximumEntities) {
        return false;
    }
    ConstraintIslandPlan plan;
    std::array<std::size_t, ConstraintIslandPlan::kMaximumEntities> parent{};
    for (std::size_t index = 0U; index < parent.size(); ++index) {
        parent[index] = index;
    }
    const auto addEntity = [&plan, &parent](const Scene::Entity entity) noexcept {
        const std::size_t existing = [&plan, entity]() noexcept {
            for (std::size_t index = 0U; index < plan.entityCount; ++index) {
                if (plan.entities[index] == entity) {
                    return index;
                }
            }
            return ConstraintIslandPlan::kMaximumEntities;
        }();
        if (existing != ConstraintIslandPlan::kMaximumEntities) {
            return existing;
        }
        if (plan.entityCount >= ConstraintIslandPlan::kMaximumEntities) {
            return ConstraintIslandPlan::kMaximumEntities;
        }
        const std::size_t index = plan.entityCount++;
        plan.entities[index] = entity;
        parent[index] = index;
        return index;
    };
    const auto findRoot = [&parent](std::size_t index) noexcept {
        while (parent[index] != index) {
            parent[index] = parent[parent[index]];
            index = parent[index];
        }
        return index;
    };
    for (const ConstraintIslandEdge& edge : edges) {
        if (edge.firstEntity == Scene::kInvalidEntity || edge.secondEntity == Scene::kInvalidEntity) {
            return false;
        }
        const std::size_t first = addEntity(edge.firstEntity);
        const std::size_t second = addEntity(edge.secondEntity);
        if (first == ConstraintIslandPlan::kMaximumEntities ||
            second == ConstraintIslandPlan::kMaximumEntities) {
            return false;
        }
        const std::size_t firstRoot = findRoot(first);
        const std::size_t secondRoot = findRoot(second);
        if (firstRoot != secondRoot) {
            parent[secondRoot] = firstRoot;
        }
    }
    for (std::size_t index = 0U; index < plan.entityCount; ++index) {
        const std::size_t root = findRoot(index);
        std::size_t islandIndex = ConstraintIslandPlan::kMaximumEntities;
        for (std::size_t prior = 0U; prior < index; ++prior) {
            if (findRoot(prior) == root) {
                islandIndex = plan.islandIndices[prior];
                break;
            }
        }
        if (islandIndex == ConstraintIslandPlan::kMaximumEntities) {
            islandIndex = plan.islandCount++;
        }
        plan.islandIndices[index] = islandIndex;
    }
    outPlan = plan;
    return true;
}

/// Bounded main-thread positional constraint seam above the existing PhysicsSystem.
/// Distance constraints preserve a configured anchor separation; hinge constraints preserve
/// coincident anchor positions and carry a validated world axis for future angular degrees of
/// freedom. v1 owns copied descriptors and generation-safe registry slots only: it does not own
/// entities, rotations, angular velocity, inertia, persistence, motors, limits, or backend state.
/// Solve consumes the bounded deterministic connected-component plan to order work by island,
/// preserving slot order within each island and leaving the positional constraint equations unchanged.
class PhysicsConstraintSystem final {
public:
    static constexpr std::size_t kMaximumConstraints = 1024U;
    static constexpr std::size_t kMaximumSolverIterations = 8U;
    static constexpr float kConstraintEpsilon = 1.0e-4F;

    [[nodiscard]] PhysicsConstraintMutationResult AddDistanceConstraint(
        const DistanceConstraint& constraint);
    [[nodiscard]] PhysicsConstraintMutationResult AddHingeConstraint(
        const HingeConstraint& constraint);
    [[nodiscard]] PhysicsConstraintMutationResult AddBallSocketConstraint(
        const BallSocketConstraint& constraint);
    [[nodiscard]] PhysicsConstraintMutationResult AddFixedConstraint(
        const FixedConstraint& constraint);
    [[nodiscard]] PhysicsConstraintMutationResult RemoveConstraint(
        PhysicsConstraintHandle handle) noexcept;
    void Clear() noexcept;

    [[nodiscard]] std::size_t GetConstraintCount() const noexcept;

    [[nodiscard]] PhysicsConstraintSolveResult Solve(
        Scene::IEntityManager& entityManager, Scene::ISceneGraph& sceneGraph,
        std::size_t maximumIterations = kMaximumSolverIterations) const;

private:
    enum class Kind : std::uint8_t { Distance = 0, Hinge, BallSocket, Fixed };

    struct Slot final {
        Kind kind = Kind::Distance;
        DistanceConstraint distance;
        HingeConstraint hinge;
        BallSocketConstraint ballSocket;
        FixedConstraint fixed;
        std::uint32_t generation = 1U;
        bool occupied = false;
    };

    [[nodiscard]] PhysicsConstraintMutationResult Add(
        Kind kind, const DistanceConstraint* distance, const HingeConstraint* hinge,
        const BallSocketConstraint* ballSocket = nullptr, const FixedConstraint* fixed = nullptr);
    [[nodiscard]] static bool IsValidDistance(const DistanceConstraint& constraint) noexcept;
    [[nodiscard]] static bool IsValidHinge(const HingeConstraint& constraint) noexcept;
    [[nodiscard]] static bool IsValidBallSocket(const BallSocketConstraint& constraint) noexcept;
    [[nodiscard]] static bool IsValidFixed(const FixedConstraint& constraint) noexcept;

    std::array<Slot, kMaximumConstraints> slots_{};
};

} // namespace UVE::Physics
