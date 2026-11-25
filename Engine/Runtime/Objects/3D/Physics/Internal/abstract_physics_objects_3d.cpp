// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "abstract_physics_objects_3d.h"

#include "physics_object_component.h"
#include "process_component.h"
#include "solid_body_component.h"
#include "i_entity_manager.h"
#include <cmath>

#include "abstract_objects_3d.h"

namespace UVE::Scene {
namespace {

template <typename BaseComponentT>
void ApplyPhysicsBase(IEntityManager& entityManager, const Entity entity,
                         const std::string_view nameFallback) {
    if (!entityManager.IsAlive(entity)) {
        return;
    }
    ApplyObject3DRecipe(entityManager, entity, nameFallback);
    if (!entityManager.HasComponent<BaseComponentT>(entity)) {
        entityManager.AddComponent<BaseComponentT>(entity, BaseComponentT{});
    }
}

} // namespace

void ApplyPhysicsObject3DBase(IEntityManager& entityManager, const Entity entity,
                                 const std::string_view nameFallback) {
    ApplyPhysicsBase<PhysicsObjectComponent>(entityManager, entity, nameFallback);
}

void ApplySolidBody3DBase(IEntityManager& entityManager, const Entity entity,
                             const std::string_view nameFallback) {
    ApplyPhysicsObject3DBase(entityManager, entity, nameFallback);
    if (entityManager.IsAlive(entity) && !entityManager.HasComponent<SolidBodyComponent>(entity)) {
        entityManager.AddComponent<SolidBodyComponent>(entity, SolidBodyComponent{});
    }
}

// =================================================================================================
// Participation: what Process mode and disableMode together mean to the physics world.
// =================================================================================================

PhysicsObjectParticipation ResolvePhysicsObjectParticipation(const IEntityManager& entityManager,
                                                                  const Entity entity,
                                                                  const bool simulationPaused) noexcept {
    // An entity that is not a physics object - or is not one any more, because the component was
    // removed - has no answer to give and nothing here to take out of the world. Everything that
    // is not a physics object participates, which is the only reading that cannot silently disable
    // collision for a body someone assembled by hand.
    if (!entityManager.IsAlive(entity) ||
        !entityManager.HasComponent<PhysicsObjectComponent>(entity)) {
        return PhysicsObjectParticipation::Active;
    }

    // No Process component is the same as running: the schedule had nothing to say about it.
    if (!entityManager.HasComponent<ProcessComponent>(entity)) {
        return PhysicsObjectParticipation::Active;
    }
    const ProcessComponent& process = entityManager.GetComponent<ProcessComponent>(entity);
    if (IsTicking(process.resolvedModeInHierarchy, simulationPaused)) {
        return PhysicsObjectParticipation::Active;
    }

    // Not running: the disability mode is what decides whether anyone notices.
    switch (entityManager.GetComponent<PhysicsObjectComponent>(entity).disableMode) {
        case PhysicsObjectDisableMode::KeepActive:
            return PhysicsObjectParticipation::Active;
        case PhysicsObjectDisableMode::MakeStatic:
            return PhysicsObjectParticipation::StaticOnly;
        case PhysicsObjectDisableMode::Remove:
        default:
            return PhysicsObjectParticipation::Removed;
    }
}

bool IsPhysicsObjectInWorld(const IEntityManager& entityManager, const Entity entity) noexcept {
    // The fixed step's own answer: it does not run while the simulation is paused, so a paused
    // object is only out of the world if its mode says it never runs at all.
    return ResolvePhysicsObjectParticipation(entityManager, entity, /*simulationPaused=*/false) !=
           PhysicsObjectParticipation::Removed;
}

bool IsPhysicsObjectSimulated(const IEntityManager& entityManager, const Entity entity) noexcept {
    return ResolvePhysicsObjectParticipation(entityManager, entity, /*simulationPaused=*/false) ==
           PhysicsObjectParticipation::Active;
}

float GetPhysicsObjectCollisionPriority(const IEntityManager& entityManager,
                                           const Entity entity) noexcept {
    if (!entityManager.IsAlive(entity) ||
        !entityManager.HasComponent<PhysicsObjectComponent>(entity)) {
        return 1.0F;
    }
    const float priority = entityManager.GetComponent<PhysicsObjectComponent>(entity).collisionPriority;
    // Hand-authored scenes go through the validator before they get here, and a scene assembled by
    // a script has not: a priority that cannot be honoured falls back to the neutral one rather
    // than to something the contact solver would have to special-case.
    return std::isfinite(priority) && priority >= 0.0F ? priority : 1.0F;
}

float GetPhysicsObjectYieldWeight(const IEntityManager& entityManager, const Entity entity,
                                     const float inverseMass) noexcept {
    if (!std::isfinite(inverseMass) || inverseMass <= 0.0F) {
        return 0.0F;
    }
    const float priority = GetPhysicsObjectCollisionPriority(entityManager, entity);
    // Priority 0 is the authored "never yields" - the limit of "higher yields less", and the way an
    // author pins something in place without making it kinematic.
    return priority <= 0.0F ? 0.0F : inverseMass / priority;
}

} // namespace UVE::Scene
