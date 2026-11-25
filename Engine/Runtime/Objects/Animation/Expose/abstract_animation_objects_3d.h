// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <string_view>

#include "animation_driver_component.h"
#include "bone_modifier_component.h"
#include "entity.h"

namespace UVE::Scene {

class IEntityManager;

/// Abstract animation bases used by animation scene objects. These are not directly creatable.
struct BoneModifier3DObjectDefinition final {
    static constexpr std::string_view typeName = "BoneModifier3D";
};

/// Pure-Object animation base shared by AnimationSequencer and AnimationGraph (no transform or visibility).
struct AnimationDriverObjectDefinition final {
    static constexpr std::string_view typeName = "AnimationDriver";
};

/// Applies the Object3D recipe and BoneModifier component where missing.
void ApplyBoneModifier3DBase(IEntityManager& entityManager, Entity entity,
                                std::string_view nameFallback);
/// Applies the pure Object baseline and AnimationDriver component where missing.
void ApplyAnimationDriverBase(IEntityManager& entityManager, Entity entity,
                                 std::string_view nameFallback);

// =================================================================================================
// What BoneModifier3D and AnimationDriver mean to the animation & IK passes.
// =================================================================================================

/// Resolves the effective `[0, 1]` blend influence of `entity`'s `BoneModifierComponent`.
/// An entity without `BoneModifierComponent` defaults to `1.0F`; an inactive or invalid
/// modifier returns `0.0F`.
[[nodiscard]] float ResolveBoneModifierInfluence(
    const IEntityManager& entityManager, Entity entity) noexcept;

/// True when `entity`'s `BoneModifierComponent` is active and has strictly positive influence
/// (or when the entity has no `BoneModifierComponent`, defaulting to active).
[[nodiscard]] bool IsBoneModifierActive(
    const IEntityManager& entityManager, Entity entity) noexcept;

/// Returns `entity`'s authored `AnimationDriverComponent`, or default `AnimationDriverComponent{}`
/// when the entity carries none (e.g. a legacy document).
[[nodiscard]] AnimationDriverComponent ResolveAnimationDriver(
    const IEntityManager& entityManager, Entity entity) noexcept;

/// True when `driver` is valid, active, and scheduled on `callback`.
[[nodiscard]] bool IsAnimationDriverRunningOnClock(
    const AnimationDriverComponent& driver, AnimationProcessCallback callback) noexcept;

/// Resolves the target entity `driver` animates: `driver.target` when alive, otherwise
/// `driverEntity`'s hierarchy parent when alive, otherwise `kInvalidEntity`.
[[nodiscard]] Entity ResolveAnimationDriverTargetEntity(
    const IEntityManager& entityManager, Entity driverEntity,
    const AnimationDriverComponent& driver) noexcept;

} // namespace UVE::Scene
