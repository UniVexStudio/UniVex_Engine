// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <set>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "Support/test_scratch_uve.h"

#include "uve/component/name_component_uve.h"
#include "uve/component/prefab_instance_component_uve.h"
#include "uve/core/engine_core_uve.h"
#include "uve/core/engine_project_settings_uve.h"
#include "uve/editor/editor_content_catalogue_uve.h"
#include "uve/editor/editor_uve.h"
#include "uve/scene/nodes/scene_node_registry_uve.h"
#include "uve/scene/nodes/scene_node_type_uve.h"

namespace UVE::Editor::Tests {
namespace {

using Kind = Scene::Nodes::SceneNodeKindUVE;

TEST(ContentCatalogueUVETest, EveryItemIsCreatableAndGroupsAreInOrder) {
    const auto items = GetContentCatalogueItemsUVE();
    const auto groups = GetContentCatalogueGroupsUVE();
    ASSERT_FALSE(items.empty());
    std::set<std::string_view> ids;
    std::size_t groupIndex = 0U;
    for (const ContentCatalogueItemUVE& item : items) {
        EXPECT_TRUE(ids.insert(item.id).second) << item.id;
        EXPECT_FALSE(item.label.empty());
        EXPECT_FALSE(item.tooltip.empty()) << item.id;
        // Items of one group are adjacent and the groups come in their listed order.
        while (groupIndex < groups.size() && groups[groupIndex] != item.group) {
            ++groupIndex;
        }
        ASSERT_LT(groupIndex, groups.size()) << item.id << " is out of group order";
        if (item.action == ContentCatalogueActionUVE::Folder) {
            EXPECT_TRUE(item.nodes.empty());
            continue;
        }
        ASSERT_FALSE(item.nodes.empty()) << item.id;
        for (std::size_t index = 0U; index < item.nodes.size(); ++index) {
            const ContentCatalogueNodeUVE& node = item.nodes[index];
            EXPECT_EQ(index == 0U, node.parent < 0) << item.id;
            EXPECT_LT(node.parent, static_cast<std::int32_t>(index)) << item.id;
            const Scene::Nodes::SceneNodeDescriptorUVE* const descriptor =
                Scene::Nodes::FindSceneNodeDescriptorUVE(node.kind);
            ASSERT_NE(descriptor, nullptr) << item.id;
            EXPECT_TRUE(descriptor->libraryCreatable) << item.id;
        }
    }
    for (const std::string_view group : groups) {
        EXPECT_TRUE(std::any_of(items.begin(), items.end(),
                                [group](const ContentCatalogueItemUVE& item) { return item.group == group; }))
            << group << " is empty";
    }
}

TEST(ContentCatalogueUVETest, SearchMatchesEveryWordAnywhere) {
    const ContentCatalogueItemUVE* const character = FindContentCatalogueItemUVE("character");
    ASSERT_NE(character, nullptr);
    EXPECT_TRUE(DoesContentCatalogueItemMatchUVE(*character, ""));
    EXPECT_TRUE(DoesContentCatalogueItemMatchUVE(*character, "CHAR"));
    EXPECT_TRUE(DoesContentCatalogueItemMatchUVE(*character, "entity anim")); // group + tooltip
    EXPECT_FALSE(DoesContentCatalogueItemMatchUVE(*character, "char light"));
    EXPECT_EQ(GetContentCatalogueIconKindUVE(*character), Kind::CharacterBody3D);
    EXPECT_EQ(GetContentCatalogueIconKindUVE(*FindContentCatalogueItemUVE("folder")), Kind::Folder);
    EXPECT_EQ(FindContentCatalogueItemUVE("nope"), nullptr);
    // Names beat descriptions: "li" puts Light above Collider, which only has it in its tooltip.
    EXPECT_EQ(RankContentCatalogueItemUVE(*FindContentCatalogueItemUVE("light"), "li"), 3);
    EXPECT_EQ(RankContentCatalogueItemUVE(*FindContentCatalogueItemUVE("collider"), "li"), 2);
    EXPECT_EQ(RankContentCatalogueItemUVE(*FindContentCatalogueItemUVE("static-body"), "never moves"), 1);
    EXPECT_EQ(RankContentCatalogueItemUVE(*character, "xyz"), 0);
}

TEST(ContentCatalogueUVETest, RecentKeepsFiveNewestFirstWithoutRepeats) {
    std::vector<std::string> recent;
    for (const char* const id : {"a", "b", "c", "d", "e", "f", "c"}) {
        EditorUVE::PushContentCreateRecentUVE(recent, id);
    }
    EXPECT_EQ(recent, (std::vector<std::string>{"c", "f", "e", "d", "b"}));
}

TEST(ContentCatalogueUVETest, ContentFileNamesNeverCollide) {
    const std::filesystem::path root = ::UVE::Tests::MakeTestCaseDirectoryUVE("content_names");
    EXPECT_EQ(EditorUVE::MakeUniqueContentPathUVE(root, "Hero", ".uveentity"), root / "Hero.uveentity");
    std::ofstream(root / "Hero.uveentity") << "x";
    std::ofstream(root / "Hero 2.uveentity") << "x";
    EXPECT_EQ(EditorUVE::MakeUniqueContentPathUVE(root, "Hero", ".uveentity"), root / "Hero 3.uveentity");

    const auto renamed = EditorUVE::RenameContentFileUVE(root / "Hero.uveentity", "Player");
    ASSERT_TRUE(renamed.has_value());
    EXPECT_EQ(*renamed, root / "Player.uveentity");
    EXPECT_FALSE(EditorUVE::RenameContentFileUVE(root / "Player.uveentity", "Hero 2").has_value()); // taken
    EXPECT_FALSE(EditorUVE::RenameContentFileUVE(root / "Player.uveentity", "a/b").has_value());
    EXPECT_FALSE(EditorUVE::RenameContentFileUVE(root / "Player.uveentity", "").has_value());

    const auto copy = EditorUVE::DuplicateContentFileUVE(root / "Player.uveentity");
    ASSERT_TRUE(copy.has_value());
    EXPECT_EQ(*copy, root / "Player 2.uveentity");
    EXPECT_TRUE(std::filesystem::is_regular_file(root / "Player.uveentity"));
}

[[nodiscard]] Core::EngineConfigUVE MakeCatalogueEditorConfigUVE(const std::filesystem::path& root) {
    Core::EngineConfigUVE config{};
    config.headlessUVE = true;
    config.logFilePath = root / "log.txt";
    config.settingsFilePath = root / "settings.json";
    config.assetDatabaseFilePath = root / "assets.json";
    config.projectSettingsFilePath = root / "project.uvesettings";
    config.saveDirectoryPath = root / "saves";
    config.shaderCachePath = root / "shader_cache";
    config.shaderSourceRealDirectoryUVE = ::UVE::Tests::RepositoryRootUVE() / "Engine/Runtime/RHI/Shader/built_in";
    config.shaderSourceMountPrefixUVE = "shaders";
    return config;
}

[[nodiscard]] std::vector<Kind> ChildKindsUVE(Core::EngineServicesUVE& services, const Scene::EntityUVE parent) {
    std::vector<Kind> kinds;
    Scene::IEntityManagerUVE& entityManager = services.GetEntityManagerUVE();
    for (const Scene::EntityUVE child : services.GetSceneGraphUVE().GetChildrenUVE(entityManager, parent)) {
        kinds.push_back(Scene::ResolveSceneNodeKindUVE(entityManager, child));
    }
    return kinds;
}

TEST(ContentCatalogueEditorUVETest, CharacterAssetPlacesAsItsWholeTreeWithOneUndoStep) {
    const std::filesystem::path root = ::UVE::Tests::MakeTestCaseDirectoryUVE("content_catalogue_editor");
    const std::filesystem::path content = root / "Content";
    std::filesystem::create_directories(content);

    Core::EngineCoreUVE engine(MakeCatalogueEditorConfigUVE(root));
    engine.Init();
    ASSERT_TRUE(engine.Load());
    {
        EditorUVE editor(engine.GetServicesUVE(), root / "main.uvescene", 100U, &engine);
        editor.InitUVE();
        Core::EngineServicesUVE& services = engine.GetServicesUVE();
        Scene::IEntityManagerUVE& entityManager = services.GetEntityManagerUVE();
        const std::size_t rootsBefore = editor.GetDocumentRootsUVE().size();
        const bool dirtyBefore = editor.IsSceneDirtyUVE();

        const auto created = editor.CreateContentCatalogueItemUVE("character", content);
        ASSERT_TRUE(created.has_value());
        EXPECT_EQ(*created, content / "Character.uveentity");
        EXPECT_TRUE(std::filesystem::is_regular_file(*created));
        // Making the asset leaves the open scene alone.
        EXPECT_EQ(editor.GetDocumentRootsUVE().size(), rootsBefore);
        EXPECT_EQ(editor.IsSceneDirtyUVE(), dirtyBefore);
        EXPECT_FALSE(editor.UndoUVE());

        const auto second = editor.CreateContentCatalogueItemUVE("character", content);
        ASSERT_TRUE(second.has_value());
        EXPECT_EQ(*second, content / "Character 2.uveentity");
        const auto folder = editor.CreateContentCatalogueItemUVE("folder", content);
        ASSERT_TRUE(folder.has_value());
        EXPECT_TRUE(std::filesystem::is_directory(*folder));
        EXPECT_FALSE(editor.CreateContentCatalogueItemUVE("missing", content).has_value());

        const Scene::EntityUVE placed = editor.PlaceEntityAssetUVE(*created);
        ASSERT_NE(placed, Scene::kInvalidEntityUVE);
        EXPECT_EQ(editor.GetSelectedEntityUVE(), placed);
        EXPECT_EQ(Scene::ResolveSceneNodeKindUVE(entityManager, placed), Kind::CharacterBody3D);
        EXPECT_EQ(entityManager.GetComponentUVE<Scene::NameComponentUVE>(placed).name, "Character");
        EXPECT_TRUE(entityManager.HasComponentUVE<Scene::PrefabInstanceComponentUVE>(placed));
        EXPECT_EQ(ChildKindsUVE(services, placed),
                  (std::vector<Kind>{Kind::MeshInstance3D, Kind::AnimationPlayer, Kind::AnimationTree}));
        EXPECT_TRUE(editor.IsSceneDirtyUVE());

        // A second placement is named apart from the first.
        const Scene::EntityUVE again = editor.PlaceEntityAssetUVE(*created);
        ASSERT_NE(again, Scene::kInvalidEntityUVE);
        EXPECT_NE(entityManager.GetComponentUVE<Scene::NameComponentUVE>(again).name, "Character");

        ASSERT_TRUE(editor.UndoUVE());
        EXPECT_FALSE(entityManager.IsAliveUVE(again));
        ASSERT_TRUE(editor.UndoUVE());
        EXPECT_FALSE(entityManager.IsAliveUVE(placed));
        ASSERT_TRUE(editor.RedoUVE());
        const Scene::EntityUVE redone = editor.GetSelectedEntityUVE();
        EXPECT_EQ(Scene::ResolveSceneNodeKindUVE(entityManager, redone), Kind::CharacterBody3D);
        EXPECT_EQ(ChildKindsUVE(services, redone).size(), 3U);

        EXPECT_EQ(editor.PlaceEntityAssetUVE(root / "nothing.uveentity"), Scene::kInvalidEntityUVE);
        EXPECT_EQ(editor.PlaceEntityAssetUVE(root / "log.txt"), Scene::kInvalidEntityUVE);

        EXPECT_TRUE(editor.GetDefaultPlayerEntityUVE().empty());
        ASSERT_TRUE(editor.SetDefaultPlayerEntityUVE("Character.uveentity"));
        EXPECT_EQ(editor.GetDefaultPlayerEntityUVE(), "Character.uveentity");
        EXPECT_EQ(std::get<std::string>(*services.GetProjectSettingsUVE().GetValueUVE(
                      Core::EngineProjectSettingIdUVE::kDefaultPlayerEntityUVE)),
                  "Character.uveentity");
        ASSERT_TRUE(editor.SetDefaultPlayerEntityUVE({}));
        EXPECT_TRUE(editor.GetDefaultPlayerEntityUVE().empty());

        // Nothing is made while the game runs.
        ASSERT_TRUE(editor.EnterPlayModeUVE());
        EXPECT_FALSE(editor.CreateContentCatalogueItemUVE("prop", content).has_value());
        EXPECT_EQ(editor.PlaceEntityAssetUVE(*created), Scene::kInvalidEntityUVE);
        ASSERT_TRUE(editor.StopPlayModeUVE());

        editor.ShutdownUVE();
    }
    engine.Shutdown();
}

} // namespace
} // namespace UVE::Editor::Tests
