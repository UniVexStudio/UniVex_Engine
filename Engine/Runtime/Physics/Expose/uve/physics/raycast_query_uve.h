// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#pragma once

#include <cstdint>
#include <span>

#include "uve/math/ray_uve.h"
#include "uve/component/entity_uve.h"

namespace UVE::Physics {

/// Bundles IRaycastSystemUVE::RaycastUVE()'s query parameters, matching this codebase's existing
/// `Desc`/`Settings`-struct convention (Render::BufferDescUVE, Asset::AssetImportSettingsUVE) —
/// growing this struct with a new field later never breaks the call signature, unlike growing a
/// parameter list would.
struct RaycastQueryUVE {
    Math::RayUVE ray;
    float maxDistance = 0.0F;

    /// Filters via `(collider.collisionLayer & layerMask) != 0` — defaults to "everything."
    std::uint32_t layerMask = 0xFFFFFFFFU;

    /// An entity to exclude from consideration entirely, regardless of layer or distance.
    /// Defaults to Scene::kInvalidEntityUVE (nothing excluded). Added so a future
    /// CharacterController's ground-check ray doesn't hit its own collider — a near-certain, not
    /// speculative, need; retrofitting this later would mean a breaking signature change.
    Scene::EntityUVE ignoreEntity = Scene::kInvalidEntityUVE;

    /// Further entities to exclude, for callers that have a list rather than one origin to skip
    /// (RayCast3DComponentUVE's authored `exclusions`). A span, not an owned array: the caller owns
    /// the storage, the query stays a cheap value to build per cast, and passing only the declared
    /// prefix means unused slots are never even looked at. An excluded entity is skipped before the
    /// layer mask is consulted — an exclusion is not a mask, so no layer can bring it back.
    std::span<const Scene::EntityUVE> excludedEntities{};
};

} // namespace UVE::Physics
