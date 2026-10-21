// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#pragma once

#include <cstddef>
#include <optional>
#include <span>

#include "uve/math/aabb_uve.h"
#include "uve/math/vector3_uve.h"

namespace UVE::Physics {

/// Opt-in continuous collision settings, carried inside PhysicsSimulationSettingsUVE.
///
/// Off by default and gated by a motion threshold on purpose: a swept test is several times the
/// cost of the discrete integration it protects, and 99% of bodies in a scene move far less than
/// their own size per step, where the discrete path is already exact. Turning it on globally would
/// pay that cost to protect bodies that cannot tunnel.
struct ContinuousCollisionSettingsUVE final {
    bool enabled = false;

    /// Metres of travel in one step below which a body is integrated discretely. Zero disables the
    /// gate, so every moving body is swept - only sensible for a scene that is already known-small.
    float motionThreshold = 0.0F;

    /// Gap kept between a swept mover and the surface it hits, in metres. The contact is pulled
    /// back along the sweep by this much, which is what stops the body from STARTING the next step
    /// already overlapping (a discrete resolver that begins penetrated has to push out, and that
    /// push-out is exactly the jitter CCD is supposed to remove).
    float contactOffset = 0.001F;
};

/// A sphere swept along a straight segment: the shape CCD reasons about.
///
/// A sphere is the honest choice for a first CCD pass even when the collider is a box: it is
/// rotation-independent, so a spinning body cannot sweep a different volume on two machines. The
/// radius callers pass is the collider's own bounding radius, which makes the sweep CONSERVATIVE -
/// it can stop a body a little early, never late - and early is recoverable while late is a
/// tunneled hit.
struct SweptSphereUVE final {
    Math::Vector3UVE center{};
    float radius = 0.0F;
    /// Motion over the whole step, in metres. Not a direction: its length is the travel.
    Math::Vector3UVE displacement{};
};

/// First contact found along a sweep.
struct SweptContactUVE final {
    /// Fraction of `displacement` travelled before contact, in [0, 1].
    float timeOfImpact = 1.0F;
    /// Unit normal pointing from the target toward the mover - the direction the mover must not
    /// continue into. Deterministic for axis-aligned targets: the entry face's outward axis.
    Math::Vector3UVE normal{0.0F, 1.0F, 0.0F};
    /// Mover centre at the time of impact.
    Math::Vector3UVE position{};
};

/// Motion after a CCD pass, plus the contact that shortened it.
struct ContinuousMotionResultUVE final {
    Math::Vector3UVE displacement{};
    std::optional<SweptContactUVE> contact{};
};

/// Exact earliest time of impact between a moving sphere and a static sphere, in [0, 1].
///
/// Returns `std::nullopt` when the sweep misses, when the target is already behind the sweep, or
/// when either input is non-finite. A mover that starts already overlapping reports
/// `timeOfImpact == 0` with a normal pointing away from the target - the caller must resolve the
/// overlap, not treat the zero as "no contact".
[[nodiscard]] std::optional<SweptContactUVE> SweepSphereAgainstSphereUVE(
    const SweptSphereUVE& mover, const Math::Vector3UVE& targetCenter, float targetRadius) noexcept;

/// Exact earliest time of impact between a moving sphere and a static axis-aligned box.
///
/// The box is expanded by the mover's radius (Minkowski sum) and the mover's centre is then swept
/// as a ray - the standard slab test, which is exact for this pair rather than conservative. A
/// target of zero or negative extent on any axis is treated as invalid and ignored.
[[nodiscard]] std::optional<SweptContactUVE> SweepSphereAgainstAabbUVE(
    const SweptSphereUVE& mover, const Math::AabbUVE& target) noexcept;

/// Earliest contact over several boxes. Ties resolve to the LOWEST index, so a scene whose colliders
/// are enumerated in a stable order produces the same contact on every run - the determinism half
/// of a network-friendly step.
[[nodiscard]] std::optional<SweptContactUVE> SweepSphereAgainstAabbsUVE(
    const SweptSphereUVE& mover, std::span<const Math::AabbUVE> targets) noexcept;

/// Applies a CCD pass to one body's step motion.
///
/// Returns `displacement` unchanged (and no contact) when CCD is disabled, when the travel is at or
/// below `motionThreshold`, or when nothing is in the way. Otherwise the displacement is cut at the
/// first contact minus `contactOffset`, and the contact is reported so the caller can also clear the
/// velocity component heading into the surface.
[[nodiscard]] ContinuousMotionResultUVE ResolveContinuousMotionUVE(
    const SweptSphereUVE& mover, std::span<const Math::AabbUVE> targets,
    const ContinuousCollisionSettingsUVE& settings) noexcept;

} // namespace UVE::Physics
