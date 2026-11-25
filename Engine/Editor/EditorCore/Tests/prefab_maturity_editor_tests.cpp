// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include <filesystem>

#include <gtest/gtest.h>

#include "Support/test_scratch.h"

#include "engine_core.h"
#include "editor_bridge.h"
#include "editor.h"
#include "prefab_instance_component.h"
#include "transform_component.h"

namespace UVE::Editor::Tests {
namespace {

[[nodiscard]] Core::EngineConfig MakePrefabEditorTestConfig() {
    Core::EngineConfig config{};
    config.headless = true;
    config.logFilePath = "uve_prefab_editor_tests.log";
    config.settingsFilePath = "uve_prefab_editor_tests_settings.json";
    config.assetDatabaseFilePath = "uve_prefab_editor_tests_assets.json";
    config.saveDirectoryPath = "uve_prefab_editor_tests_saves";
    config.shaderCachePath = "uve_prefab_editor_tests_shader_cache";
    config.shaderSourceRealDirectory = ::UVE::Tests::RepositoryRoot() / "Engine/Runtime/RHI/Shader/built_in";
    config.shaderSourceMountPrefix = "shaders";
    return config;
}

TEST(PrefabMaturityEditorTest, SaveRefreshAndBridgeFacts_AreRevisionAwareAndPlayGuarded) {
    const std::filesystem::path prefabPath = "uve_prefab_editor_maturity.uvprefab";
    std::filesystem::remove(prefabPath);

    Core::EngineCore engine(MakePrefabEditorTestConfig());
    engine.Init();
    ASSERT_TRUE(engine.Load());
    {
        Editor editor(engine.GetServices(), "uve_prefab_editor_maturity.uvscene", 100U, &engine);
        editor.Init();
        EditorBridge bridge(editor);
        Core::EngineServices& services = engine.GetServices();
        Scene::IEntityManager& entityManager = services.GetEntityManager();

        const Scene::Entity source = editor.CreateDocumentEntity(EditorEntityKind::Empty);
        ASSERT_NE(source, Scene::kInvalidEntity);
        ASSERT_TRUE(editor.SaveSelectedPrefab(prefabPath));
        const Asset::AssetGuid guid = entityManager.HasComponent<Scene::PrefabInstanceComponent>(source)
                                             ? Asset::kInvalidAssetGuid
                                             : services.GetAssetDatabase().Register(prefabPath);
        ASSERT_NE(guid, Asset::kInvalidAssetGuid);

        const Scene::Entity instance = services.GetPrefabSystem().Instantiate(
            entityManager, services.GetSceneGraph(), services.GetAssetDatabase(), guid,
            Scene::kInvalidEntity);
        ASSERT_NE(instance, Scene::kInvalidEntity);
        editor.SelectEntity(instance);

        const EditorBridgeSnapshot currentSnapshot = bridge.GetSnapshot();
        ASSERT_TRUE(currentSnapshot.inspector.prefab.has_value());
        EXPECT_EQ(currentSnapshot.inspector.prefab->status, EditorBridgePrefabRevisionStatus::Current);
        EXPECT_FALSE(currentSnapshot.inspector.prefab->canRefresh);

        editor.SelectEntity(source);
        Scene::TransformComponent changed = entityManager.GetComponent<Scene::TransformComponent>(source);
        changed.localPosition.x = 3.0F;
        ASSERT_TRUE(editor.SetSelectedLocalTransform(changed));
        ASSERT_TRUE(editor.SaveSelectedPrefab(prefabPath));

        editor.SelectEntity(instance);
        const EditorBridgeSnapshot staleSnapshot = bridge.GetSnapshot();
        ASSERT_TRUE(staleSnapshot.inspector.prefab.has_value());
        EXPECT_EQ(staleSnapshot.inspector.prefab->status, EditorBridgePrefabRevisionStatus::Stale);
        EXPECT_TRUE(staleSnapshot.inspector.prefab->canRefresh);

        ASSERT_TRUE(editor.RefreshSelectedPrefab());
        const Scene::Entity refreshed = editor.GetSelectedEntity();
        EXPECT_NE(refreshed, instance);
        EXPECT_FALSE(entityManager.IsAlive(instance));
        ASSERT_TRUE(entityManager.IsAlive(refreshed));
        EXPECT_FLOAT_EQ(entityManager.GetComponent<Scene::TransformComponent>(refreshed).localPosition.x, 3.0F);

        ASSERT_TRUE(editor.EnterPlayMode());
        EXPECT_FALSE(editor.RefreshSelectedPrefab());
        ASSERT_TRUE(editor.StopPlayMode());

        editor.Shutdown();
    }
    engine.Shutdown();
    std::filesystem::remove(prefabPath);
}

} // namespace
} // namespace UVE::Editor::Tests
