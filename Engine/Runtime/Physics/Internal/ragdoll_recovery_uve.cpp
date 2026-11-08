// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/physics/ragdoll_recovery_uve.h"

#include <algorithm>
#include <cmath>

namespace UVE::Physics {

RecoveryOrientationUVE RagdollRecoveryControllerUVE::DetermineOrientationUVE(
    const RagdollSolverUVE& solver) const noexcept {
    if (solver.GetBodyCountUVE() == 0U) {
        return RecoveryOrientationUVE::FaceDown;
    }

    std::size_t refIndex = 0U;
    if (solver.GetBodyCountUVE() > 1U) {
        const Math::Vector3UVE f0 = Math::RotateVectorUVE(solver.GetBodyUVE(0).rotation, Math::Vector3UVE{0.0F, 0.0F, 1.0F});
        const Math::Vector3UVE f1 = Math::RotateVectorUVE(solver.GetBodyUVE(1).rotation, Math::Vector3UVE{0.0F, 0.0F, 1.0F});
        refIndex = std::abs(f1.y) >= std::abs(f0.y) ? 1U : 0U;
    }
    const RagdollBodyUVE& rootBody = solver.GetBodyUVE(refIndex);
    // In standard capsule convention, local +Z is the chest/front facing vector
    const Math::Vector3UVE frontDir = Math::RotateVectorUVE(rootBody.rotation, Math::Vector3UVE{0.0F, 0.0F, 1.0F});
    const Math::Vector3UVE rightDir = Math::RotateVectorUVE(rootBody.rotation, Math::Vector3UVE{1.0F, 0.0F, 0.0F});

    // Check chest orientation against world up (+Y)
    if (frontDir.y > 0.4F) {
        return RecoveryOrientationUVE::FaceUp;
    }
    if (frontDir.y < -0.4F) {
        return RecoveryOrientationUVE::FaceDown;
    }

    // Side orientations
    return rightDir.y > 0.0F ? RecoveryOrientationUVE::SideLeft : RecoveryOrientationUVE::SideRight;
}

void RagdollRecoveryControllerUVE::UpdateRecoveryUVE(
    const RagdollSolverUVE& solver,
    const std::span<const RagdollContactUVE> contacts,
    const float deltaTimeSeconds) noexcept {
    if (solver.GetBodyCountUVE() == 0U) {
        return;
    }

    // If currently interrupted, hold until re-stabilization
    if (m_state.isInterrupted) {
        m_state.phase = RecoveryPhaseUVE::Interrupted;
        m_state.blendWeight = 1.0F; // Full physics
        m_state.settleTimer = 0.0F;
        m_state.getUpTimer = 0.0F;
        // Interruption resets to None once body settles
    }

    // Check motion speeds
    float maxLinearSq = 0.0F;
    float maxAngularSq = 0.0F;
    for (std::size_t i = 0U; i < solver.GetBodyCountUVE(); ++i) {
        const RagdollBodyUVE& b = solver.GetBodyUVE(i);
        maxLinearSq = std::max(maxLinearSq, Math::LengthSquaredUVE(b.linearVelocity));
        maxAngularSq = std::max(maxAngularSq, Math::LengthSquaredUVE(b.angularVelocity));
    }

    const float linearSpeed = std::sqrt(maxLinearSq);
    const float angularSpeed = std::sqrt(maxAngularSq);
    const bool isMovingFast = linearSpeed > m_config.maxLinearSpeedForRest ||
                              angularSpeed > m_config.maxAngularSpeedForRest;

    // Check ground contacts
    bool hasGroundContact = false;
    for (const RagdollContactUVE& c : contacts) {
        if (c.normal.y > 0.5F) {
            hasGroundContact = true;
            break;
        }
    }

    switch (m_state.phase) {
    case RecoveryPhaseUVE::None:
    case RecoveryPhaseUVE::Interrupted:
        if (!isMovingFast && hasGroundContact) {
            m_state.phase = RecoveryPhaseUVE::Stabilizing;
            m_state.settleTimer = 0.0F;
            m_state.isInterrupted = false;
        }
        break;

    case RecoveryPhaseUVE::Stabilizing:
        if (isMovingFast || !hasGroundContact) {
            m_state.phase = RecoveryPhaseUVE::None;
            m_state.settleTimer = 0.0F;
            break;
        }
        m_state.settleTimer += deltaTimeSeconds;
        if (m_state.settleTimer >= m_config.settleDelaySeconds) {
            m_state.phase = RecoveryPhaseUVE::Analyzing;
        }
        break;

    case RecoveryPhaseUVE::Analyzing:
        m_state.orientation = DetermineOrientationUVE(solver);
        m_state.restingPelvisPosition = solver.GetBodyUVE(0).position;
        m_state.restingPelvisRotation = solver.GetBodyUVE(0).rotation;
        m_state.canRecover = true;
        m_state.phase = RecoveryPhaseUVE::GettingUp;
        m_state.getUpTimer = 0.0F;
        m_state.blendWeight = 1.0F;
        break;

    case RecoveryPhaseUVE::GettingUp: {
        m_state.getUpTimer += deltaTimeSeconds;
        const float duration = std::max(0.1F, m_config.getUpBlendDurationSeconds);
        const float progress = std::clamp(m_state.getUpTimer / duration, 0.0F, 1.0F);

        // Smooth S-curve blend weight (1.0 = physics, 0.0 = animation)
        m_state.blendWeight = 1.0F - (progress * progress * (3.0F - 2.0F * progress));

        if (progress >= 1.0F) {
            m_state.phase = RecoveryPhaseUVE::Complete;
            m_state.blendWeight = 0.0F;
        }
        break;
    }

    case RecoveryPhaseUVE::Complete:
        // Ready for character controller handover
        m_state.blendWeight = 0.0F;
        break;
    }
}

void RagdollRecoveryControllerUVE::NotifyImpactUVE(const float impulseMagnitude) noexcept {
    if (impulseMagnitude >= m_config.interruptionImpulseThreshold) {
        m_state.isInterrupted = true;
        m_state.phase = RecoveryPhaseUVE::Interrupted;
        m_state.blendWeight = 1.0F;
        m_state.settleTimer = 0.0F;
        m_state.getUpTimer = 0.0F;
    }
}

void RagdollRecoveryControllerUVE::ResetUVE() noexcept {
    m_state = RagdollRecoveryStateUVE{};
}

} // namespace UVE::Physics
