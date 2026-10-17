// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>

#include "uve/component/collider_component_uve.h"

namespace UVE::Physics {

/// Combine priority order: Average < Minimum < Multiply < Maximum.
enum class PhysicsCombineModeUVE : std::uint8_t {
    Average = 0,
    Minimum = 1,
    Multiply = 2,
    Maximum = 3,
};

/// Authored physics material asset definition (Part 6.5): friction, rolling friction, restitution,
/// combine modes, and surface type tag for audio/decal dispatch.
struct PhysicsMaterialAssetUVE final {
    float friction = 0.5F;
    float rollingFriction = 0.01F;
    float restitution = 0.0F;
    float density = 1.0F;
    PhysicsCombineModeUVE frictionCombine = PhysicsCombineModeUVE::Average;
    PhysicsCombineModeUVE restitutionCombine = PhysicsCombineModeUVE::Average;
    std::string surfaceTypeTag = "default";
};

[[nodiscard]] constexpr float CombineScalarByModeUVE(const float a, const float b,
                                                     const PhysicsCombineModeUVE modeA,
                                                     const PhysicsCombineModeUVE modeB) noexcept {
    const PhysicsCombineModeUVE effective =
        static_cast<std::uint8_t>(modeA) >= static_cast<std::uint8_t>(modeB) ? modeA : modeB;
    switch (effective) {
    case PhysicsCombineModeUVE::Minimum:
        return std::min(a, b);
    case PhysicsCombineModeUVE::Multiply:
        return a * b;
    case PhysicsCombineModeUVE::Maximum:
        return std::max(a, b);
    case PhysicsCombineModeUVE::Average:
    default:
        return (a + b) * 0.5F;
    }
}

/// PhysicsMaterialUVE is the spec's `PhysicsMaterialUVE` (Part 7.5): friction, restitution,
/// density. It is deliberately a transient, derived, read-only value — never stored in the ECS.
/// `Scene::ColliderComponentUVE` already owns the source-of-truth friction/restitution/density
/// fields as plain floats (storing a `Physics::PhysicsMaterialUVE` field there instead would
/// require `engine/scene` to depend on `engine/physics`, which already depends on `engine/scene`
/// — a circular module dependency this codebase has never had). This type exists purely so
/// PhysicsSystemUVE's collision resolution and RaycastHitUVE (Increment 16) have a small,
/// self-contained value to combine/return, matching CollisionPairUVE's precedent for a plain
/// physics-internal value type.
struct PhysicsMaterialUVE {
    float friction = 0.0F;
    float restitution = 0.0F;
    float density = 1.0F;
};

/// Combines two colliding surfaces' materials by simple average — the simplest well-defined rule.
/// No per-pair configurable combine mode (min/max/multiply) this increment, matching the "don't
/// build ahead of a real second consumer" discipline; revisitable later without an interface
/// change, since this is a pure implementation detail, not part of any public interface signature.
[[nodiscard]] constexpr PhysicsMaterialUVE CombineMaterialsUVE(const PhysicsMaterialUVE& a,
                                                                 const PhysicsMaterialUVE& b) noexcept {
    return PhysicsMaterialUVE{(a.friction + b.friction) * 0.5F, (a.restitution + b.restitution) * 0.5F,
                               (a.density + b.density) * 0.5F};
}

/// Builds a PhysicsMaterialUVE from a collider's friction/restitution/density fields, clamping
/// friction and restitution to [0,1] at this read boundary — a defensive guard against corrupted
/// or hand-edited scene data producing energy-gaining bounces (restitution > 1) or inverted
/// friction (negative), without a general NaN/validation framework (out of scope this increment).
/// Shared by PhysicsSystemUVE's resolution and RaycastSystemUVE's hit reporting so both read this
/// data through the exact same clamp, never two independently-maintained copies of it.
[[nodiscard]] inline PhysicsMaterialUVE MaterialOfUVE(const Scene::ColliderComponentUVE& collider) noexcept {
    const float friction = std::isfinite(collider.friction) ? std::clamp(collider.friction, 0.0F, 1.0F) : 0.0F;
    const float restitution = std::isfinite(collider.restitution)
                                  ? std::clamp(collider.restitution, 0.0F, 1.0F)
                                  : 0.0F;
    const float density = std::isfinite(collider.density) && collider.density > 0.0F ? collider.density : 1.0F;
    return PhysicsMaterialUVE{friction, restitution, density};
}

/// Resolves the effective material for `collider`, applying `overrideAsset` first, then
/// `projectDefaultAsset`, and falling back to the collider's inline values.
[[nodiscard]] inline PhysicsMaterialAssetUVE ResolveColliderPhysicsMaterialUVE(
    const Scene::ColliderComponentUVE& collider,
    const PhysicsMaterialAssetUVE* const overrideAsset = nullptr,
    const PhysicsMaterialAssetUVE* const projectDefaultAsset = nullptr) {
    if (overrideAsset != nullptr) {
        return *overrideAsset;
    }
    if (projectDefaultAsset != nullptr) {
        return *projectDefaultAsset;
    }
    const PhysicsMaterialUVE base = MaterialOfUVE(collider);
    PhysicsMaterialAssetUVE resolved{};
    resolved.friction = base.friction;
    resolved.restitution = base.restitution;
    resolved.density = base.density;
    return resolved;
}

} // namespace UVE::Physics
