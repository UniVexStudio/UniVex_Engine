// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cstdint>

namespace UVE::Scene {

/// What happens to a PhysicsObject3D's body while its Process mode stops it from running.
enum class PhysicsObjectDisableModeUVE : std::uint8_t {
    /// Taken out of the physics world: nothing collides with it until it runs again.
    Remove = 0,
    /// Kept in the world as an immovable obstacle.
    MakeStatic,
    /// Left exactly as it is, still simulated.
    KeepActive,
};

/// The shared state of PhysicsObject3D, the abstract base of every node that takes part in
/// collision - areas and bodies. No node is a PhysicsObject3D on its own; its kinds carry this
/// component and add their own.
struct PhysicsObjectComponentUVE final {
    PhysicsObjectDisableModeUVE disableMode = PhysicsObjectDisableModeUVE::Remove;
    /// The layers this object is on - what others can find it on.
    std::uint32_t collisionLayer = 0x00000001U;
    /// The layers this object looks for - what it collides with or detects.
    std::uint32_t collisionMask = 0x00000001U;
    /// How strongly this object is pushed out when it overlaps another; higher yields less.
    float collisionPriority = 1.0F;
    /// Whether a pointer ray (a mouse or touch pick) can hit this object.
    bool inputRayPickable = true;
    /// Whether a drag that started on this object keeps reporting to it after leaving it.
    bool inputCaptureOnDrag = false;

    [[nodiscard]] bool operator==(const PhysicsObjectComponentUVE&) const = default;
};

/// A known disable mode and a finite, non-negative priority.
[[nodiscard]] bool IsPhysicsObjectComponentValidUVE(const PhysicsObjectComponentUVE& component) noexcept;

} // namespace UVE::Scene
