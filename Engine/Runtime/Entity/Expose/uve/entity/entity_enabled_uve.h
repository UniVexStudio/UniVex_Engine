// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include "uve/component/disabled_component_uve.h"
#include "uve/entity/i_entity_manager_uve.h"
#include "uve/logging/assert_uve.h"

namespace UVE::Scene {

/// True when `entity` is alive and carries no `DisabledComponentUVE`. Debug builds assert that
/// the entity is alive (matching `HasComponentUVE`'s contract); release builds return false for
/// a stale or invalid handle rather than reporting a dead entity as enabled.
[[nodiscard]] inline bool IsEntityEnabledUVE(const IEntityManagerUVE& manager, const EntityUVE entity) {
    if (!manager.IsAliveUVE(entity)) {
        UVE_ASSERT(manager.IsAliveUVE(entity));
        return false;
    }
    return !manager.HasComponentUVE<DisabledComponentUVE>(entity);
}

/// Enables or disables `entity` by removing or attaching `DisabledComponentUVE`. Idempotent:
/// setting the state the entity already has is a no-op, so callers never need to check first.
/// Debug builds assert that the entity is alive; release builds return without mutation for a
/// stale or invalid handle. Toggling migrates the entity's archetype, exactly like any other
/// component add/remove — correct to call at runtime, not something to do per frame.
inline void SetEntityEnabledUVE(IEntityManagerUVE& manager, const EntityUVE entity, const bool enabled) {
    UVE_ASSERT(manager.IsAliveUVE(entity));
    if (!manager.IsAliveUVE(entity)) {
        return;
    }
    const bool disabled = manager.HasComponentUVE<DisabledComponentUVE>(entity);
    if (enabled && disabled) {
        manager.RemoveComponentUVE<DisabledComponentUVE>(entity);
    } else if (!enabled && !disabled) {
        manager.AddComponentUVE<DisabledComponentUVE>(entity);
    }
}

} // namespace UVE::Scene
