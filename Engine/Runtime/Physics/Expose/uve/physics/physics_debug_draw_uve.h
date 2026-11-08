// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cstdint>
#include <vector>

#include "uve/math/quaternion_uve.h"
#include "uve/math/vector3_uve.h"
#include "uve/physics/active_ragdoll_uve.h"
#include "uve/physics/advanced_collision_analysis_uve.h"
#include "uve/physics/motion_trajectory_uve.h"

namespace UVE::Physics {

/// Bitmask flags controlling which debug visualization categories are generated.
enum class PhysicsDebugCategoryUVE : std::uint32_t {
    None = 0,
    Contacts = 1 << 0,
    Impulses = 1 << 1,
    Trajectory = 1 << 2,
    CenterOfMass = 1 << 3,
    SupportPolygon = 1 << 4,
    Landing = 1 << 5,
    JointLimits = 1 << 6,
    All = 0xFFFFFFFF,
};

[[nodiscard]] constexpr PhysicsDebugCategoryUVE operator|(
    PhysicsDebugCategoryUVE a, PhysicsDebugCategoryUVE b) noexcept {
    return static_cast<PhysicsDebugCategoryUVE>(static_cast<std::uint32_t>(a) | static_cast<std::uint32_t>(b));
}

[[nodiscard]] constexpr bool HasCategoryUVE(
    PhysicsDebugCategoryUVE mask, PhysicsDebugCategoryUVE category) noexcept {
    return (static_cast<std::uint32_t>(mask) & static_cast<std::uint32_t>(category)) != 0;
}

struct PhysicsDebugLineUVE final {
    Math::Vector3UVE start{};
    Math::Vector3UVE end{};
    Math::Vector3UVE color{1.0F, 1.0F, 1.0F};
};

struct PhysicsDebugPointUVE final {
    Math::Vector3UVE position{};
    Math::Vector3UVE color{1.0F, 1.0F, 0.0F};
    float size = 0.05F;
};

/// Collector holding rendered debug geometry for physics and active ragdoll systems.
struct PhysicsDebugBufferUVE final {
    std::vector<PhysicsDebugLineUVE> lines;
    std::vector<PhysicsDebugPointUVE> points;

    void Clear() noexcept {
        lines.clear();
        points.clear();
    }
};

/// Populates a debug buffer from trajectory, active balance, and collision contacts.
void GeneratePhysicsDebugDrawUVE(
    const MotionTrajectoryPredictionUVE& trajectory,
    const ActiveRagdollStateUVE& activeRagdoll,
    const ImpactAnalysisSummaryUVE& impactSummary,
    PhysicsDebugCategoryUVE enabledCategories,
    PhysicsDebugBufferUVE& outBuffer) noexcept;

} // namespace UVE::Physics
