// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include <filesystem>

#include <gtest/gtest.h>

#include "asset_database.h"
#include "asset_content_fingerprint.h"
#include "log_sink.h"
#include "event_system.h"
#include "memory_manager.h"
#include "mesh_component.h"
#include "prefab_instance_component.h"
#include "transform_component.h"
#include "entity_manager.h"
#include "prefab_system.h"
#include "scene_graph.h"

namespace UVE::Scene::Tests {
namespace {

class PrefabMaturityTest : public ::testing::Test {
protected:
    Memory::MemoryManager memoryManager;
    Events::EventSystem eventSystem;
    EntityManager entityManager{memoryManager.GetDefaultAllocator(), eventSystem};
    SceneGraph sceneGraph;
    Asset::AssetDatabase assetDatabase;
    PrefabSystem prefabSystem;
};

TEST_F(PrefabMaturityTest, SourceRevision_ChangesWhenPrefabEnvelopeChanges) {
    const Entity source = entityManager.CreateEntity();
    entityManager.AddComponent<MeshComponent>(source,
                                                     MeshComponent{Asset::AssetGuid{11U}, Asset::AssetGuid{12U}});
    const std::filesystem::path path = "uve_prefab_maturity_revision.uvprefab";
    std::filesystem::remove(path);

    ASSERT_NE(prefabSystem.SavePrefab(entityManager, assetDatabase, source, path), Asset::kInvalidAssetGuid);
    const std::optional<std::uint64_t> firstRevision = ComputePrefabSourceRevision(path);
    ASSERT_TRUE(firstRevision.has_value());

    entityManager.GetComponent<MeshComponent>(source).meshGuid = Asset::AssetGuid{99U};
    ASSERT_NE(prefabSystem.SavePrefab(entityManager, assetDatabase, source, path), Asset::kInvalidAssetGuid);
    const std::optional<std::uint64_t> secondRevision = ComputePrefabSourceRevision(path);
    ASSERT_TRUE(secondRevision.has_value());
    EXPECT_NE(*firstRevision, *secondRevision);

    std::filesystem::remove(path);
}

TEST_F(PrefabMaturityTest, RefreshInstance_ReplacesCleanInstanceAfterSourceMutation) {
    const Entity source = entityManager.CreateEntity();
    entityManager.AddComponent<MeshComponent>(source,
                                                     MeshComponent{Asset::AssetGuid{21U}, Asset::AssetGuid{22U}});
    const std::filesystem::path path = "uve_prefab_maturity_refresh.uvprefab";
    std::filesystem::remove(path);
    const Asset::AssetGuid guid = prefabSystem.SavePrefab(entityManager, assetDatabase, source, path);
    ASSERT_NE(guid, Asset::kInvalidAssetGuid);
    const Entity instance = prefabSystem.Instantiate(entityManager, sceneGraph, assetDatabase, guid,
                                                            kInvalidEntity);
    ASSERT_NE(instance, kInvalidEntity);

    entityManager.GetComponent<MeshComponent>(source).meshGuid = Asset::AssetGuid{77U};
    ASSERT_NE(prefabSystem.SavePrefab(entityManager, assetDatabase, source, path), Asset::kInvalidAssetGuid);

    const PrefabRefreshResult result =
        prefabSystem.RefreshInstance(entityManager, sceneGraph, assetDatabase, instance);
    ASSERT_EQ(result.code, PrefabRefreshCode::Refreshed);
    ASSERT_NE(result.rootEntity, kInvalidEntity);
    EXPECT_NE(result.rootEntity, instance);
    EXPECT_EQ(entityManager.GetComponent<MeshComponent>(result.rootEntity).meshGuid,
              Asset::AssetGuid{77U});
    EXPECT_FALSE(entityManager.IsAlive(instance));

    std::filesystem::remove(path);
}

TEST_F(PrefabMaturityTest, RefreshInstance_RejectsLocalOverridesWithMergeRequired) {
    const Entity source = entityManager.CreateEntity();
    entityManager.AddComponent<MeshComponent>(source,
                                                     MeshComponent{Asset::AssetGuid{31U}, Asset::AssetGuid{32U}});
    const std::filesystem::path path = "uve_prefab_maturity_merge_required.uvprefab";
    std::filesystem::remove(path);
    const Asset::AssetGuid guid = prefabSystem.SavePrefab(entityManager, assetDatabase, source, path);
    ASSERT_NE(guid, Asset::kInvalidAssetGuid);
    const Entity instance = prefabSystem.Instantiate(entityManager, sceneGraph, assetDatabase, guid,
                                                            kInvalidEntity);
    ASSERT_NE(instance, kInvalidEntity);

    PrefabInstanceComponent& instanceComponent =
        entityManager.GetComponent<PrefabInstanceComponent>(instance);
    instanceComponent.overrides = {{"Mesh.meshGuid", "99"}};
    entityManager.GetComponent<MeshComponent>(source).meshGuid = Asset::AssetGuid{88U};
    ASSERT_NE(prefabSystem.SavePrefab(entityManager, assetDatabase, source, path), Asset::kInvalidAssetGuid);

    const PrefabRefreshResult result =
        prefabSystem.RefreshInstance(entityManager, sceneGraph, assetDatabase, instance);
    EXPECT_EQ(result.code, PrefabRefreshCode::MergeRequired);
    EXPECT_EQ(result.rootEntity, instance);
    EXPECT_TRUE(entityManager.IsAlive(instance));

    std::filesystem::remove(path);
}

TEST_F(PrefabMaturityTest, RefreshInstance_ForceRefreshExplicitlyDiscardsOverrides) {
    const Entity source = entityManager.CreateEntity();
    entityManager.AddComponent<MeshComponent>(source,
                                                     MeshComponent{Asset::AssetGuid{41U}, Asset::AssetGuid{42U}});
    const std::filesystem::path path = "uve_prefab_maturity_force_refresh.uvprefab";
    std::filesystem::remove(path);
    const Asset::AssetGuid guid = prefabSystem.SavePrefab(entityManager, assetDatabase, source, path);
    ASSERT_NE(guid, Asset::kInvalidAssetGuid);
    const Entity instance = prefabSystem.Instantiate(entityManager, sceneGraph, assetDatabase, guid,
                                                            kInvalidEntity);
    ASSERT_NE(instance, kInvalidEntity);

    entityManager.GetComponent<PrefabInstanceComponent>(instance).overrides = {{"Mesh.meshGuid", "99"}};
    entityManager.GetComponent<MeshComponent>(source).meshGuid = Asset::AssetGuid{123U};
    ASSERT_NE(prefabSystem.SavePrefab(entityManager, assetDatabase, source, path), Asset::kInvalidAssetGuid);

    const PrefabRefreshResult result =
        prefabSystem.RefreshInstance(entityManager, sceneGraph, assetDatabase, instance, true);
    ASSERT_EQ(result.code, PrefabRefreshCode::Refreshed);
    ASSERT_NE(result.rootEntity, instance);
    EXPECT_EQ(entityManager.GetComponent<MeshComponent>(result.rootEntity).meshGuid,
              Asset::AssetGuid{123U});
    EXPECT_FALSE(entityManager.IsAlive(instance));

    std::filesystem::remove(path);
}


} // namespace
} // namespace UVE::Scene::Tests
