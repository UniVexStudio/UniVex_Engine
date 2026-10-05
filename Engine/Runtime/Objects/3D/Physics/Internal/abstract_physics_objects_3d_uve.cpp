// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/objects/3d/abstract_physics_objects_3d_uve.h"

#include "uve/component/physics_object_component_uve.h"
#include "uve/component/process_component_uve.h"
#include "uve/component/solid_body_component_uve.h"
#include "uve/entity/i_entity_manager_uve.h"
#include <cmath>

#include "uve/objects/3d/abstract_objects_3d_uve.h"

namespace UVE::Scene {
namespace {

template <typename BaseComponentT>
void ApplyPhysicsBaseUVE(IEntityManagerUVE& entityManager, const EntityUVE entity,
                         const std::string_view nameFallback) {
    if (!entityManager.IsAliveUVE(entity)) {
        return;
    }
    ApplyObject3DRecipeUVE(entityManager, entity, nameFallback);
    if (!entityManager.HasComponentUVE<BaseComponentT>(entity)) {
        entityManager.AddComponentUVE<BaseComponentT>(entity, BaseComponentT{});
    }
}

} // namespace

void ApplyPhysicsObject3DBaseUVE(IEntityManagerUVE& entityManager, const EntityUVE entity,
                                 const std::string_view nameFallback) {
    ApplyPhysicsBaseUVE<PhysicsObjectComponentUVE>(entityManager, entity, nameFallback);
}

void ApplySolidBody3DBaseUVE(IEntityManagerUVE& entityManager, const EntityUVE entity,
                             const std::string_view nameFallback) {
    ApplyPhysicsObject3DBaseUVE(entityManager, entity, nameFallback);
    if (entityManager.IsAliveUVE(entity) && !entityManager.HasComponentUVE<SolidBodyComponentUVE>(entity)) {
        entityManager.AddComponentUVE<SolidBodyComponentUVE>(entity, SolidBodyComponentUVE{});
    }
}

// =================================================================================================
// Participation: what Process mode and disableMode together mean to the physics world.
// =================================================================================================

PhysicsObjectParticipationUVE ResolvePhysicsObjectParticipationUVE(const IEntityManagerUVE& entityManager,
                                                                  const EntityUVE entity,
                                                                  const bool simulationPaused) noexcept {
    // An entity that is not a physics object - or is not one any more, because the component was
    // removed - has no answer to give and nothing here to take out of the world. Everything that
    // is not a physics object participates, which is the only reading that cannot silently disable
    // collision for a body someone assembled by hand.
    if (!entityManager.IsAliveUVE(entity) ||
        !entityManager.HasComponentUVE<PhysicsObjectComponentUVE>(entity)) {
        return PhysicsObjectParticipationUVE::Active;
    }

    // No Process component is the same as running: the schedule had nothing to say about it.
    if (!entityManager.HasComponentUVE<ProcessComponentUVE>(entity)) {
        return PhysicsObjectParticipationUVE::Active;
    }
    const ProcessComponentUVE& process = entityManager.GetComponentUVE<ProcessComponentUVE>(entity);
    if (IsTickingUVE(process.resolvedModeInHierarchy, simulationPaused)) {
        return PhysicsObjectParticipationUVE::Active;
    }

    // Not running: the disability mode is what decides whether anyone notices.
    switch (entityManager.GetComponentUVE<PhysicsObjectComponentUVE>(entity).disableMode) {
        case PhysicsObjectDisableModeUVE::KeepActive:
            return PhysicsObjectParticipationUVE::Active;
        case PhysicsObjectDisableModeUVE::MakeStatic:
            return PhysicsObjectParticipationUVE::StaticOnly;
        case PhysicsObjectDisableModeUVE::Remove:
        default:
            return PhysicsObjectParticipationUVE::Removed;
    }
}

bool IsPhysicsObjectInWorldUVE(const IEntityManagerUVE& entityManager, const EntityUVE entity) noexcept {
    // The fixed step's own answer: it does not run while the simulation is paused, so a paused
    // object is only out of the world if its mode says it never runs at all.
    return ResolvePhysicsObjectParticipationUVE(entityManager, entity, /*simulationPaused=*/false) !=
           PhysicsObjectParticipationUVE::Removed;
}

bool IsPhysicsObjectSimulatedUVE(const IEntityManagerUVE& entityManager, const EntityUVE entity) noexcept {
    return ResolvePhysicsObjectParticipationUVE(entityManager, entity, /*simulationPaused=*/false) ==
           PhysicsObjectParticipationUVE::Active;
}

float GetPhysicsObjectCollisionPriorityUVE(const IEntityManagerUVE& entityManager,
                                           const EntityUVE entity) noexcept {
    if (!entityManager.IsAliveUVE(entity) ||
        !entityManager.HasComponentUVE<PhysicsObjectComponentUVE>(entity)) {
        return 1.0F;
    }
    const float priority = entityManager.GetComponentUVE<PhysicsObjectComponentUVE>(entity).collisionPriority;
    // Hand-authored scenes go through the validator before they get here, and a scene assembled by
    // a script has not: a priority that cannot be honoured falls back to the neutral one rather
    // than to something the contact solver would have to special-case.
    return std::isfinite(priority) && priority >= 0.0F ? priority : 1.0F;
}

float GetPhysicsObjectYieldWeightUVE(const IEntityManagerUVE& entityManager, const EntityUVE entity,
                                     const float inverseMass) noexcept {
    if (!std::isfinite(inverseMass) || inverseMass <= 0.0F) {
        return 0.0F;
    }
    const float priority = GetPhysicsObjectCollisionPriorityUVE(entityManager, entity);
    // Priority 0 is the authored "never yields" - the limit of "higher yields less", and the way an
    // author pins something in place without making it kinematic.
    return priority <= 0.0F ? 0.0F : inverseMass / priority;
}

} // namespace UVE::Scene
