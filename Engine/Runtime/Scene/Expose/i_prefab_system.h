// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>

#include "i_asset_database.h"
#include "entity.h"
#include "i_entity_manager.h"
#include "i_scene_graph.h"

namespace UVE::Scene {

enum class PrefabRefreshCode : std::uint8_t {
    InvalidInstance = 0,
    SourceUnavailable,
    SourceRevisionInvalid,
    NoOp,
    Refreshed,
    MergeRequired,
    RollbackFailed,
};

struct PrefabRefreshResult final {
    PrefabRefreshCode code = PrefabRefreshCode::InvalidInstance;
    Entity rootEntity = kInvalidEntity;
    std::uint64_t observedSourceRevision = 0U;
    std::string message;

    [[nodiscard]] bool IsSuccess() const noexcept {
        return code == PrefabRefreshCode::NoOp || code == PrefabRefreshCode::Refreshed;
    }
};

/// Computes a deterministic nonzero source revision from the complete prefab envelope bytes.
/// The revision is an identity/freshness fact, not a security digest.
[[nodiscard]] std::optional<std::uint64_t> ComputePrefabSourceRevision(
    const std::filesystem::path& path);

/// IPrefabSystem is a thin layer over ISceneSerializer providing the "reusable entity
/// template" workflow the master spec names (Part 7.3): save a subtree as a `.uvprefab`, then
/// instantiate independent copies of it later. Overrides are whole-component: an instantiated
/// entity is an ordinary live ECS entity, so any later AddComponent/RemoveComponent/
/// GetComponent-mutation on it *is* the override — there is no separate override-tracking
/// data structure (see docs/CODING_STANDARDS.md/the Increment 6 plan for the rationale). Nested
/// prefabs "just work": an instance's PrefabInstanceComponent is serialized/deserialized like
/// any other component, and is never re-resolved/re-instantiated during an ordinary load — only
/// an explicit Instantiate() call actually instantiates a prefab asset.
/// Thread-safety: not thread-safe, matching IEntityManager's own main-thread-only contract.
class IPrefabSystem {
public:
    virtual ~IPrefabSystem() = default;

    /// Serializes `rootEntity` (and its descendants) to `path` as a reusable prefab template,
    /// registers `path` in `assetDatabase` (idempotent — re-saving the same path keeps its GUID
    /// stable) and immediately persists the registry, and returns that GUID. Returns
    /// `Asset::kInvalidAssetGuid` if the underlying save fails (already logged).
    [[nodiscard]] virtual Asset::AssetGuid SavePrefab(IEntityManager& entityManager,
                                                             Asset::IAssetDatabase& assetDatabase,
                                                             Entity rootEntity,
                                                             const std::filesystem::path& path) = 0;

    /// Instantiates the prefab identified by `prefabGuid` (resolved to a file path via
    /// `assetDatabase`) into fresh live entities, tags the new root with
    /// PrefabInstanceComponent{prefabGuid}, and — if `parent != kInvalidEntity` —
    /// reparents it under `parent` via `sceneGraph.SetParent()`. Returns the new root entity,
    /// or `kInvalidEntity` on failure (unknown GUID, or the underlying load fails — both
    /// logged).
    [[nodiscard]] virtual Entity Instantiate(IEntityManager& entityManager,
                                                    ISceneGraph& sceneGraph,
                                                    Asset::IAssetDatabase& assetDatabase,
                                                    Asset::AssetGuid prefabGuid,
                                                    Entity parent) = 0;

    /// Instantiates and stamps a nonzero source revision into the new root's
    /// PrefabInstanceComponent. The default implementation preserves legacy callers and
    /// delegates to Instantiate without revision metadata; PrefabSystem overrides it.
    [[nodiscard]] virtual Entity InstantiateWithRevision(
        IEntityManager& entityManager, ISceneGraph& sceneGraph,
        Asset::IAssetDatabase& assetDatabase, Asset::AssetGuid prefabGuid, Entity parent,
        std::uint64_t sourceRevision) {
        static_cast<void>(sourceRevision);
        return Instantiate(entityManager, sceneGraph, assetDatabase, prefabGuid, parent);
    }

    /// Replaces one clean live prefab instance with a freshly loaded source snapshot while preserving
    /// its current parent. Local overrides require explicit merge resolution and are never discarded.
    [[nodiscard]] virtual PrefabRefreshResult RefreshInstance(
        IEntityManager& entityManager, ISceneGraph& sceneGraph,
        Asset::IAssetDatabase& assetDatabase, Entity instanceRoot,
        bool forceRefresh = false) = 0;

};

} // namespace UVE::Scene
