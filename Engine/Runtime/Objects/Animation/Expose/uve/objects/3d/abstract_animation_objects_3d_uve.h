// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <string_view>

#include "uve/component/animation_driver_component_uve.h"
#include "uve/component/bone_modifier_component_uve.h"
#include "uve/component/entity_uve.h"

namespace UVE::Scene {

class IEntityManagerUVE;

/// Abstract animation bases used by animation scene objects. These are not directly creatable.
struct BoneModifier3DObjectDefinitionUVE final {
    static constexpr std::string_view typeName = "BoneModifier3D";
};

/// Pure-Object animation base shared by AnimationSequencer and AnimationGraph (no transform or visibility).
struct AnimationDriverObjectDefinitionUVE final {
    static constexpr std::string_view typeName = "AnimationDriver";
};

/// Applies the Object3D recipe and BoneModifier component where missing.
void ApplyBoneModifier3DBaseUVE(IEntityManagerUVE& entityManager, EntityUVE entity,
                                std::string_view nameFallback);
/// Applies the pure Object baseline and AnimationDriver component where missing.
void ApplyAnimationDriverBaseUVE(IEntityManagerUVE& entityManager, EntityUVE entity,
                                 std::string_view nameFallback);

// =================================================================================================
// What BoneModifier3D and AnimationDriver mean to the animation & IK passes.
// =================================================================================================

/// Resolves the effective `[0, 1]` blend influence of `entity`'s `BoneModifierComponentUVE`.
/// An entity without `BoneModifierComponentUVE` defaults to `1.0F`; an inactive or invalid
/// modifier returns `0.0F`.
[[nodiscard]] float ResolveBoneModifierInfluenceUVE(
    const IEntityManagerUVE& entityManager, EntityUVE entity) noexcept;

/// True when `entity`'s `BoneModifierComponentUVE` is active and has strictly positive influence
/// (or when the entity has no `BoneModifierComponentUVE`, defaulting to active).
[[nodiscard]] bool IsBoneModifierActiveUVE(
    const IEntityManagerUVE& entityManager, EntityUVE entity) noexcept;

/// Returns `entity`'s authored `AnimationDriverComponentUVE`, or default `AnimationDriverComponentUVE{}`
/// when the entity carries none (e.g. a legacy document).
[[nodiscard]] AnimationDriverComponentUVE ResolveAnimationDriverUVE(
    const IEntityManagerUVE& entityManager, EntityUVE entity) noexcept;

/// True when `driver` is valid, active, and scheduled on `callback`.
[[nodiscard]] bool IsAnimationDriverRunningOnClockUVE(
    const AnimationDriverComponentUVE& driver, AnimationProcessCallbackUVE callback) noexcept;

/// Resolves the target entity `driver` animates: `driver.target` when alive, otherwise
/// `driverEntity`'s hierarchy parent when alive, otherwise `kInvalidEntityUVE`.
[[nodiscard]] EntityUVE ResolveAnimationDriverTargetEntityUVE(
    const IEntityManagerUVE& entityManager, EntityUVE driverEntity,
    const AnimationDriverComponentUVE& driver) noexcept;

} // namespace UVE::Scene
