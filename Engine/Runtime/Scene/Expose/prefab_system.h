// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#pragma once

#include "i_prefab_system.h"
#include "scene_serializer.h"

namespace UVE::Scene {

/// PrefabSystem is the concrete, engine-standard implementation of IPrefabSystem. Owns a
/// private SceneSerializer by value (it needs no constructor arguments of its own), so
/// callers never have to separately fetch a serializer just to save/instantiate prefabs.
class PrefabSystem final : public IPrefabSystem {
public:
    PrefabSystem() = default;

    [[nodiscard]] Asset::AssetGuid SavePrefab(IEntityManager& entityManager,
                                                     Asset::IAssetDatabase& assetDatabase,
                                                     Entity rootEntity,
                                                     const std::filesystem::path& path) override;
    [[nodiscard]] Entity Instantiate(IEntityManager& entityManager, ISceneGraph& sceneGraph,
                                           Asset::IAssetDatabase& assetDatabase,
                                           Asset::AssetGuid prefabGuid, Entity parent) override;
    [[nodiscard]] Entity InstantiateWithRevision(
        IEntityManager& entityManager, ISceneGraph& sceneGraph,
        Asset::IAssetDatabase& assetDatabase, Asset::AssetGuid prefabGuid, Entity parent,
        std::uint64_t sourceRevision) override;
    [[nodiscard]] PrefabRefreshResult RefreshInstance(
        IEntityManager& entityManager, ISceneGraph& sceneGraph,
        Asset::IAssetDatabase& assetDatabase, Entity instanceRoot,
        bool forceRefresh = false) override;

private:
    SceneSerializer m_sceneSerializer;
};

} // namespace UVE::Scene
