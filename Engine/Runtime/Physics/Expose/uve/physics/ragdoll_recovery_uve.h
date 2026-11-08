// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cstdint>
#include <span>

#include "uve/math/quaternion_uve.h"
#include "uve/math/vector3_uve.h"
#include "uve/physics/ragdoll_solver_uve.h"

namespace UVE::Physics {

/// The resting orientation of the ragdoll on the ground.
enum class RecoveryOrientationUVE : std::uint8_t {
    FaceUp = 0,
    FaceDown,
    SideLeft,
    SideRight,
};

/// Lifecycle phases of the intelligent recovery sequence.
enum class RecoveryPhaseUVE : std::uint8_t {
    None = 0,
    Stabilizing,
    Analyzing,
    GettingUp,
    Complete,
    Interrupted,
};

/// Configuration parameters for ragdoll recovery.
struct RagdollRecoveryConfigUVE final {
    float maxLinearSpeedForRest = 0.12F;
    float maxAngularSpeedForRest = 0.35F;
    float settleDelaySeconds = 0.3F;
    float getUpBlendDurationSeconds = 1.0F;
    float interruptionImpulseThreshold = 60.0F; // N.s
};

/// Runtime state for the ragdoll recovery system.
struct RagdollRecoveryStateUVE final {
    RecoveryPhaseUVE phase = RecoveryPhaseUVE::None;
    RecoveryOrientationUVE orientation = RecoveryOrientationUVE::FaceDown;

    float settleTimer = 0.0F;
    float getUpTimer = 0.0F;
    float blendWeight = 1.0F; // 1.0 = physics, 0.0 = get-up animation
    bool canRecover = false;
    bool isInterrupted = false;

    Math::Vector3UVE restingPelvisPosition{};
    Math::QuaternionUVE restingPelvisRotation{};
};

/// Intelligent recovery controller monitoring resting ragdoll state and driving the get-up sequence.
class RagdollRecoveryControllerUVE final {
public:
    explicit RagdollRecoveryControllerUVE(const RagdollRecoveryConfigUVE& config = {}) noexcept
        : m_config(config) {}

    /// Steps the recovery state machine.
    void UpdateRecoveryUVE(
        const RagdollSolverUVE& solver,
        std::span<const RagdollContactUVE> contacts,
        float deltaTimeSeconds) noexcept;

    /// Triggers immediate interruption if a significant new impact strikes during recovery.
    void NotifyImpactUVE(float impulseMagnitude) noexcept;

    /// Resets the recovery controller to idle.
    void ResetUVE() noexcept;

    [[nodiscard]] const RagdollRecoveryStateUVE& GetStateUVE() const noexcept { return m_state; }
    [[nodiscard]] RagdollRecoveryStateUVE& GetStateUVE() noexcept { return m_state; }

    [[nodiscard]] const RagdollRecoveryConfigUVE& GetConfigUVE() const noexcept { return m_config; }
    void SetConfigUVE(const RagdollRecoveryConfigUVE& config) noexcept { m_config = config; }

    [[nodiscard]] bool IsRecoveringUVE() const noexcept {
        return m_state.phase == RecoveryPhaseUVE::GettingUp;
    }

    [[nodiscard]] RecoveryOrientationUVE DetermineOrientationUVE(const RagdollSolverUVE& solver) const noexcept;

private:
    RagdollRecoveryConfigUVE m_config{};
    RagdollRecoveryStateUVE m_state{};
};

} // namespace UVE::Physics
