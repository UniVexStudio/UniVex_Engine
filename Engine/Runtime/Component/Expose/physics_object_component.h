// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cstdint>

namespace UVE::Scene {

inline constexpr float kDefaultPhysicsCollisionPriority = 1.0F;

/// What happens to a PhysicsObject3D's body while its Process mode stops it from running.
enum class PhysicsObjectDisableMode : std::uint8_t {
    /// Taken out of the physics world: nothing collides with it until it runs again.
    Remove = 0,
    /// Kept in the world as an immovable obstacle.
    MakeStatic,
    /// Left exactly as it is, still simulated.
    KeepActive,
};

/// The shared state of PhysicsObject3D, the abstract base of every object that takes part in
/// collision - areas and bodies. No object is a PhysicsObject3D on its own; its kinds carry this
/// component and add their own.
///
/// The object's shape and its collision layer and mask are its ColliderComponent, which the
/// collision systems read and the Inspector draws inside this section. They are not repeated
/// here: two copies of a layer would be two answers to "what does this collide with".
struct PhysicsObjectComponent final {
    PhysicsObjectDisableMode disableMode = PhysicsObjectDisableMode::Remove;
    /// How strongly this object is pushed out when it overlaps another; higher yields less.
    float collisionPriority = kDefaultPhysicsCollisionPriority;
    /// Whether a pointer ray (a mouse or touch pick) can hit this object.
    bool inputRayPickable = true;
    /// Whether a drag that started on this object keeps reporting to it after leaving it.
    bool inputCaptureOnDrag = false;

    [[nodiscard]] bool operator==(const PhysicsObjectComponent&) const = default;
};

/// True when `mode` is one of `Remove`, `MakeStatic`, or `KeepActive`.
[[nodiscard]] bool IsPhysicsObjectDisableModeValid(PhysicsObjectDisableMode mode) noexcept;

/// A known disable mode and a finite, non-negative priority.
[[nodiscard]] bool IsPhysicsObjectComponentValid(const PhysicsObjectComponent& component) noexcept;

/// Clamps negative/non-finite priorities and resets unknown disable modes to `Remove`.
[[nodiscard]] PhysicsObjectComponent SanitizePhysicsObjectComponent(
    const PhysicsObjectComponent& component) noexcept;

/// True when a disabled entity's body remains in the broadphase (`MakeStatic` or `KeepActive`).
[[nodiscard]] bool ShouldKeepPhysicsBodyInWorldWhenDisabled(
    const PhysicsObjectComponent& component) noexcept;

/// True when a disabled entity's body still steps dynamically (`KeepActive`).
[[nodiscard]] bool ShouldSimulatePhysicsBodyWhenDisabled(
    const PhysicsObjectComponent& component) noexcept;

/// True when a pointer ray is allowed to pick `component` given its enabled state.
[[nodiscard]] bool CanPickPhysicsObjectWithRay(
    const PhysicsObjectComponent& component, bool entityEnabled = true) noexcept;

/// Computes how much of a pair's overlap `self` absorbs in `[0, 1]` relative to `other`.
/// Higher priority yields less (absorbs a smaller share of the depenetration push).
[[nodiscard]] float ResolvePairDepenetrationShare(
    const PhysicsObjectComponent& self, const PhysicsObjectComponent& other) noexcept;

} // namespace UVE::Scene
