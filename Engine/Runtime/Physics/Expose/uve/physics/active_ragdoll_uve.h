// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include "uve/math/quaternion_uve.h"
#include "uve/math/vector2_uve.h"
#include "uve/math/vector3_uve.h"
#include "uve/physics/ragdoll_solver_uve.h"

namespace UVE::Physics {

/// Dynamic balance estimation state.
enum class BalanceStateUVE : std::uint8_t {
    Stable = 0,
    Disturbed,
    CorrectiveStepping,
    UnrecoverableFall,
};

/// 2D horizontal ground support polygon (convex hull of foot/body contacts on ground).
struct SupportPolygonUVE final {
    std::vector<Math::Vector2UVE> vertices;
    Math::Vector2UVE centroid{};
    float area = 0.0F;

    [[nodiscard]] bool ContainsPointUVE(const Math::Vector2UVE& point) const noexcept;
    [[nodiscard]] float SignedDistanceToBoundaryUVE(const Math::Vector2UVE& point) const noexcept;
};

/// Authored tuning parameters for active ragdoll joint drives and procedural balance.
struct ActiveRagdollConfigUVE final {
    float nominalComHeight = 0.95F;
    float maxStepReach = 0.65F;

    float stiffnessMinor = 650.0F;
    float dampingMinor = 65.0F;

    float stiffnessModerate = 400.0F;
    float dampingModerate = 45.0F;

    float stiffnessSevere = 120.0F;
    float dampingSevere = 20.0F;

    float headStabilizationGain = 0.8F;
    float spineCompensationGain = 0.6F;
    float fallRelaxationRate = 5.0F; // 1/s rate at which stiffness drops during unrecoverable fall
};

/// Runtime balance and procedural posture state.
struct ActiveRagdollStateUVE final {
    BalanceStateUVE balanceState = BalanceStateUVE::Stable;

    Math::Vector3UVE centerOfMass{};
    Math::Vector3UVE centerOfMassVelocity{};
    Math::Vector2UVE extrapolatedCenterOfMass{}; // ZMP on XZ horizontal plane
    SupportPolygonUVE supportPolygon{};
    float stabilityMargin = 0.0F;

    Math::Vector3UVE correctiveStepTarget{};
    Math::QuaternionUVE headTargetRotation{};
    Math::Vector3UVE spineRestorativeTorque{};
    float activeBlendWeight = 1.0F;
};

/// Computes the 2D convex hull of contact points on the ground (XZ plane).
[[nodiscard]] SupportPolygonUVE ComputeSupportPolygonUVE(
    std::span<const RagdollContactUVE> contacts) noexcept;

/// Active Ragdoll procedural controller evaluating balance and updating joint drives.
class ActiveRagdollControllerUVE final {
public:
    explicit ActiveRagdollControllerUVE(const ActiveRagdollConfigUVE& config = {}) noexcept
        : m_config(config) {}

    void UpdateBalanceAndPostureUVE(
        RagdollSolverUVE& solver,
        std::span<const RagdollContactUVE> contacts,
        const Math::QuaternionUVE& rootOrientation,
        float deltaTimeSeconds) noexcept;

    [[nodiscard]] const ActiveRagdollStateUVE& GetStateUVE() const noexcept { return m_state; }
    [[nodiscard]] ActiveRagdollStateUVE& GetStateUVE() noexcept { return m_state; }

    [[nodiscard]] const ActiveRagdollConfigUVE& GetConfigUVE() const noexcept { return m_config; }
    void SetConfigUVE(const ActiveRagdollConfigUVE& config) noexcept { m_config = config; }

    /// Sets the target pose for a specific joint from animation.
    void SetJointTargetPoseUVE(RagdollSolverUVE& solver, std::size_t jointIndex,
                               const Math::QuaternionUVE& targetRelativeRotation) noexcept;

    /// Disables all joint drives (instant drop to passive ragdoll on extreme impact).
    void ReleaseAllJointDrivesUVE(RagdollSolverUVE& solver) noexcept;

private:
    ActiveRagdollConfigUVE m_config{};
    ActiveRagdollStateUVE m_state{};
};

} // namespace UVE::Physics
