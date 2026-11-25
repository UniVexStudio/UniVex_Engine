// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include "disabled_component.h"
#include "i_entity_manager.h"
#include "engine_assert.h"

namespace UVE::Scene {

/// True when `entity` is alive and carries no `DisabledComponent`. Debug builds assert that
/// the entity is alive (matching `HasComponent`'s contract); release builds return false for
/// a stale or invalid handle rather than reporting a dead entity as enabled.
[[nodiscard]] inline bool IsEntityEnabled(const IEntityManager& manager, const Entity entity) {
    if (!manager.IsAlive(entity)) {
        UVE_ASSERT(manager.IsAlive(entity));
        return false;
    }
    return !manager.HasComponent<DisabledComponent>(entity);
}

/// Enables or disables `entity` by removing or attaching `DisabledComponent`. Idempotent:
/// setting the state the entity already has is a no-op, so callers never need to check first.
/// Debug builds assert that the entity is alive; release builds return without mutation for a
/// stale or invalid handle. Toggling migrates the entity's archetype, exactly like any other
/// component add/remove — correct to call at runtime, not something to do per frame.
inline void SetEntityEnabled(IEntityManager& manager, const Entity entity, const bool enabled) {
    UVE_ASSERT(manager.IsAlive(entity));
    if (!manager.IsAlive(entity)) {
        return;
    }
    const bool disabled = manager.HasComponent<DisabledComponent>(entity);
    if (enabled && disabled) {
        manager.RemoveComponent<DisabledComponent>(entity);
    } else if (!enabled && !disabled) {
        manager.AddComponent<DisabledComponent>(entity);
    }
}

} // namespace UVE::Scene
