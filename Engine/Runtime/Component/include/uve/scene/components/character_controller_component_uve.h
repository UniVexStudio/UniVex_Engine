// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cmath>

namespace UVE::Scene {

/// Marks an entity as a player/AI-style kinematic locomotion body, driven every fixed step by
/// `EngineCoreUVE::SyncCharacterControllersUVE()` via the stateless `Physics::CharacterControllerUVE`
/// utility (`MoveWithToIUVE`). Mixes authored config (`moveSpeed`/`jumpHeight`/`gravityScale`) with
/// persisted runtime state (`verticalVelocity`/`isGrounded`) in one component, matching
/// `RigidBodyComponentUVE`'s own established convention. Requires the entity to also carry a
/// `ColliderComponentUVE` and, if a `RigidBodyComponentUVE` is present, that it be `isKinematic`
/// (the same precondition `CharacterControllerUVE::MoveUVE`/`MoveWithToIUVE` already enforce).
struct CharacterControllerComponentUVE final {
    /// Horizontal move speed in world units/second, applied to WASD-style input.
    float moveSpeed = 5.0F;
    /// Peak jump height in world units; converted to an initial vertical launch velocity via
    /// v = sqrt(2 * g * h) using this engine's own gravity (`EngineConfigUVE::gravity`).
    float jumpHeight = 1.5F;
    /// Multiplies the engine's configured gravity before vertical integration - 1 = normal
    /// gravity, 0 = unaffected by gravity, matching `RigidBodyComponentUVE::gravityScale`.
    float gravityScale = 1.0F;
    /// Persisted runtime state: accumulated vertical speed (world units/second, +up).
    float verticalVelocity = 0.0F;
    /// Persisted runtime state: whether the last `MoveWithToIUVE` step reported ground contact.
    bool isGrounded = false;
};

/// Validates the value-only character-controller contract before scene persistence and stepping,
/// mirroring `IsRigidBodyComponentValidUVE`'s own per-field style.
[[nodiscard]] inline bool IsCharacterControllerComponentValidUVE(
    const CharacterControllerComponentUVE& characterController) noexcept {
    return std::isfinite(characterController.moveSpeed) && characterController.moveSpeed >= 0.0F &&
           std::isfinite(characterController.jumpHeight) && characterController.jumpHeight >= 0.0F &&
           std::isfinite(characterController.gravityScale) && characterController.gravityScale >= 0.0F &&
           std::isfinite(characterController.verticalVelocity);
}

} // namespace UVE::Scene
