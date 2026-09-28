// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <set>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "Support/test_scratch_uve.h"

#include "uve/asset/animation_clip_asset_uve.h"
#include "uve/component/name_component_uve.h"
#include "uve/nodes/3d/skeleton_3d_uve.h"
#include "uve/component/animation_player_component_uve.h"
#include "uve/component/prefab_instance_component_uve.h"
#include "uve/component/transform_component_uve.h"
#include "uve/component/hierarchy_component_uve.h"
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
    EXPECT_EQ(EditorUVE::MakeUniqueContentPathUVE(root, "Hero", ".uventity"), root / "Hero.uventity");
    std::ofstream(root / "Hero.uventity") << "x";
    std::ofstream(root / "Hero 2.uventity") << "x";
    EXPECT_EQ(EditorUVE::MakeUniqueContentPathUVE(root, "Hero", ".uventity"), root / "Hero 3.uventity");

    const auto renamed = EditorUVE::RenameContentFileUVE(root / "Hero.uventity", "Player");
    ASSERT_TRUE(renamed.has_value());
    EXPECT_EQ(*renamed, root / "Player.uventity");
    EXPECT_FALSE(EditorUVE::RenameContentFileUVE(root / "Player.uventity", "Hero 2").has_value()); // taken
    EXPECT_FALSE(EditorUVE::RenameContentFileUVE(root / "Player.uventity", "a/b").has_value());
    EXPECT_FALSE(EditorUVE::RenameContentFileUVE(root / "Player.uventity", "").has_value());

    const auto copy = EditorUVE::DuplicateContentFileUVE(root / "Player.uventity");
    ASSERT_TRUE(copy.has_value());
    EXPECT_EQ(*copy, root / "Player 2.uventity");
    EXPECT_TRUE(std::filesystem::is_regular_file(root / "Player.uventity"));
}

[[nodiscard]] Core::EngineConfigUVE MakeCatalogueEditorConfigUVE(const std::filesystem::path& root) {
    Core::EngineConfigUVE config{};
    config.headlessUVE = true;
    config.logFilePath = root / "log.txt";
    config.settingsFilePath = root / "settings.json";
    config.assetDatabaseFilePath = root / "assets.json";
    config.projectSettingsFilePath = root / "project.uvsettings";
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
        EditorUVE editor(engine.GetServicesUVE(), root / "main.uvscene", 100U, &engine);
        editor.InitUVE();
        Core::EngineServicesUVE& services = engine.GetServicesUVE();
        Scene::IEntityManagerUVE& entityManager = services.GetEntityManagerUVE();
        const std::size_t rootsBefore = editor.GetDocumentRootsUVE().size();
        const bool dirtyBefore = editor.IsSceneDirtyUVE();

        const auto created = editor.CreateContentCatalogueItemUVE("character", content);
        ASSERT_TRUE(created.has_value());
        EXPECT_EQ(*created, content / "Character.uventity");
        EXPECT_TRUE(std::filesystem::is_regular_file(*created));
        // Making the asset leaves the open scene alone.
        EXPECT_EQ(editor.GetDocumentRootsUVE().size(), rootsBefore);
        EXPECT_EQ(editor.IsSceneDirtyUVE(), dirtyBefore);
        EXPECT_FALSE(editor.UndoUVE());

        const auto second = editor.CreateContentCatalogueItemUVE("character", content);
        ASSERT_TRUE(second.has_value());
        EXPECT_EQ(*second, content / "Character 2.uventity");
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

        EXPECT_EQ(editor.PlaceEntityAssetUVE(root / "nothing.uventity"), Scene::kInvalidEntityUVE);
        EXPECT_EQ(editor.PlaceEntityAssetUVE(root / "log.txt"), Scene::kInvalidEntityUVE);

        EXPECT_TRUE(editor.GetDefaultPlayerEntityUVE().empty());
        ASSERT_TRUE(editor.SetDefaultPlayerEntityUVE("Character.uventity"));
        EXPECT_EQ(editor.GetDefaultPlayerEntityUVE(), "Character.uventity");
        EXPECT_EQ(std::get<std::string>(*services.GetProjectSettingsUVE().GetValueUVE(
                      Core::EngineProjectSettingIdUVE::kDefaultPlayerEntityUVE)),
                  "Character.uventity");
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

TEST(ContentCatalogueEditorUVETest, EntityEditorEditsTheAssetAloneAndGivesTheSceneBack) {
    const std::filesystem::path root = ::UVE::Tests::MakeTestCaseDirectoryUVE("entity_editor");
    const std::filesystem::path content = root / "Content";
    std::filesystem::create_directories(content);

    Core::EngineCoreUVE engine(MakeCatalogueEditorConfigUVE(root));
    engine.Init();
    ASSERT_TRUE(engine.Load());
    {
        EditorUVE editor(engine.GetServicesUVE(), root / "main.uvscene", 100U, &engine);
        editor.InitUVE();
        Core::EngineServicesUVE& services = engine.GetServicesUVE();
        Scene::IEntityManagerUVE& entityManager = services.GetEntityManagerUVE();
        const auto created = editor.CreateContentCatalogueItemUVE("character", content);
        ASSERT_TRUE(created.has_value());
        const Scene::EntityUVE placed = editor.PlaceEntityAssetUVE(*created);
        ASSERT_NE(placed, Scene::kInvalidEntityUVE);
        ASSERT_TRUE(editor.SaveSceneUVE());
        ASSERT_FALSE(editor.IsSceneDirtyUVE());

        const auto positionY = [&entityManager](const Scene::EntityUVE entity) {
            return entityManager.GetComponentUVE<Scene::TransformComponentUVE>(entity).localPosition.y;
        };
        const auto moveTo = [&editor](const Scene::EntityUVE entity, const float y) {
            editor.SelectEntityUVE(entity);
            Scene::TransformComponentUVE moved{};
            moved.localPosition = Math::Vector3UVE{0.0F, y, 0.0F};
            return editor.SetSelectedLocalTransformUVE(moved);
        };

        EXPECT_FALSE(editor.OpenEntityEditorUVE(root / "nothing.uventity"));
        ASSERT_TRUE(editor.OpenEntityEditorUVE(*created));
        EXPECT_TRUE(editor.IsEntityEditorOpenUVE());
        EXPECT_EQ(editor.GetEntityEditorAssetPathUVE(), *created);
        // The entity is the document now; the scene is put aside and undo starts fresh.
        EXPECT_FALSE(entityManager.IsAliveUVE(placed));
        EXPECT_FALSE(editor.IsSceneDirtyUVE());
        EXPECT_FALSE(editor.UndoUVE());
        const Scene::EntityUVE entityRoot = editor.GetEntityEditorRootUVE();
        ASSERT_NE(entityRoot, Scene::kInvalidEntityUVE);
        EXPECT_EQ(Scene::ResolveSceneNodeKindUVE(entityManager, entityRoot), Kind::CharacterBody3D);
        EXPECT_FALSE(entityManager.HasComponentUVE<Scene::PrefabInstanceComponentUVE>(entityRoot))
            << "edited as itself, not as an instance of itself";
        EXPECT_EQ(ChildKindsUVE(services, entityRoot).size(), 3U);
        EXPECT_FALSE(editor.EnterPlayModeUVE()) << "the scene plays, not an open entity";
        EXPECT_EQ(engine.GetSimulationExecutionModeUVE(), Core::SimulationExecutionModeUVE::Paused)
            << "an entity is edited as authored, not simulated";
        EXPECT_FALSE(editor.OpenEntityEditorUVE(content / "Other.uventity"));

        // The entity is the top of the tree: new nodes go under it, and it cannot be removed.
        editor.ClearSelectionUVE();
        const Scene::EntityUVE added = editor.CreateDocumentSceneNodeUVE(Kind::Node3D);
        ASSERT_NE(added, Scene::kInvalidEntityUVE);
        EXPECT_EQ(entityManager.GetComponentUVE<Scene::HierarchyComponentUVE>(added).parent, entityRoot);
        EXPECT_EQ(editor.GetEntityEditorRootUVE(), entityRoot) << "still one root";
        editor.SelectEntityUVE(entityRoot);
        EXPECT_FALSE(editor.DeleteSelectedEntityUVE());
        EXPECT_EQ(editor.DuplicateSelectedEntityUVE(), Scene::kInvalidEntityUVE);
        ASSERT_TRUE(editor.RevertEntityEditorUVE());
        const Scene::EntityUVE root2 = editor.GetEntityEditorRootUVE(); // Revert makes fresh entities

        // Save (Ctrl+S saves the entity while it is open), then change it again and revert.
        ASSERT_TRUE(moveTo(root2, 2.0F));
        EXPECT_TRUE(editor.IsSceneDirtyUVE());
        ASSERT_TRUE(editor.SaveSceneUVE());
        EXPECT_FALSE(editor.IsSceneDirtyUVE());
        ASSERT_TRUE(moveTo(root2, 5.0F));
        ASSERT_TRUE(editor.RevertEntityEditorUVE());
        EXPECT_FLOAT_EQ(positionY(editor.GetEntityEditorRootUVE()), 2.0F);
        EXPECT_FALSE(editor.UndoUVE());

        // One root only: a node beside the root is refused on save.
        const Scene::EntityUVE sceneRoot = editor.GetDocumentSceneRootUVE();
        const Scene::EntityUVE stray = entityManager.CreateEntityUVE();
        entityManager.AddComponentUVE<Scene::TransformComponentUVE>(stray, Scene::TransformComponentUVE{});
        entityManager.AddComponentUVE<Scene::HierarchyComponentUVE>(stray, Scene::HierarchyComponentUVE{});
        services.GetSceneGraphUVE().SetParentUVE(entityManager, stray, sceneRoot);
        EXPECT_EQ(editor.GetEntityEditorRootUVE(), Scene::kInvalidEntityUVE);
        EXPECT_FALSE(editor.SaveEntityEditorUVE());
        ASSERT_TRUE(editor.RevertEntityEditorUVE());

        // An unsaved change closed without saving is not written.
        ASSERT_TRUE(moveTo(editor.GetEntityEditorRootUVE(), 9.0F));
        ASSERT_TRUE(editor.CloseEntityEditorUVE(false));
        EXPECT_FALSE(editor.IsEntityEditorOpenUVE());
        EXPECT_EQ(engine.GetSimulationExecutionModeUVE(), Core::SimulationExecutionModeUVE::Running);

        // The scene is back, with its placed Character.
        const std::vector<Scene::EntityUVE> sceneChildren =
            services.GetSceneGraphUVE().GetChildrenUVE(entityManager, editor.GetDocumentSceneRootUVE());
        const auto character = std::ranges::find_if(sceneChildren, [&entityManager](const Scene::EntityUVE child) {
            return entityManager.HasComponentUVE<Scene::NameComponentUVE>(child) &&
                   entityManager.GetComponentUVE<Scene::NameComponentUVE>(child).name == "Character";
        });
        ASSERT_NE(character, sceneChildren.end());
        EXPECT_TRUE(entityManager.HasComponentUVE<Scene::PrefabInstanceComponentUVE>(*character));

        // What was saved is in the file.
        ASSERT_TRUE(editor.OpenEntityEditorUVE(*created));
        EXPECT_FLOAT_EQ(positionY(editor.GetEntityEditorRootUVE()), 2.0F);
        // Closing with save writes an unsaved change.
        ASSERT_TRUE(moveTo(editor.GetEntityEditorRootUVE(), 3.0F));
        ASSERT_TRUE(editor.CloseEntityEditorUVE(true));
        ASSERT_TRUE(editor.OpenEntityEditorUVE(*created));
        EXPECT_FLOAT_EQ(positionY(editor.GetEntityEditorRootUVE()), 3.0F);
        ASSERT_TRUE(editor.CloseEntityEditorUVE(false));
        editor.ShutdownUVE();
    }
    engine.Shutdown();
}

TEST(ContentCatalogueEditorUVETest, EntityEditorCompilesScriptsListsSignalsAndGoesToProblems) {
    const std::filesystem::path root = ::UVE::Tests::MakeTestCaseDirectoryUVE("entity_editor_scripts");
    const std::filesystem::path content = root / "Content";
    std::filesystem::create_directories(content);

    Core::EngineCoreUVE engine(MakeCatalogueEditorConfigUVE(root));
    engine.Init();
    ASSERT_TRUE(engine.Load());
    {
        EditorUVE editor(engine.GetServicesUVE(), root / "main.uvscene", 100U, &engine);
        editor.InitUVE();
        const auto created = editor.CreateContentCatalogueItemUVE("character", content);
        ASSERT_TRUE(created.has_value());
        EXPECT_FALSE(editor.HasEntityEditorCompiledUVE());
        EXPECT_EQ(editor.CompileEntityEditorUVE(), 0U) << "nothing open, nothing to compile";
        ASSERT_TRUE(editor.OpenEntityEditorUVE(*created));
        EXPECT_EQ(editor.GetEntityEditorTabUVE(), EditorUVE::EntityEditorTabUVE::Viewport);

        // A fresh Character has no scripts: it compiles clean and answers to nothing.
        EXPECT_EQ(editor.CompileEntityEditorUVE(), 0U);
        EXPECT_TRUE(editor.HasEntityEditorCompiledUVE());
        EXPECT_TRUE(editor.GetEntityEditorSignalsUVE().empty());

        // New UVScript on the root: its handlers are the entity's signals.
        const Scene::EntityUVE entityRoot = editor.GetEntityEditorRootUVE();
        editor.SelectEntityUVE(entityRoot);
        ASSERT_TRUE(editor.CreateUVScriptForSelectedEntityUVE());
        ASSERT_TRUE(editor.GetOpenUVScriptUVE().has_value());
        std::vector<EditorUVE::EntitySignalRowUVE> signals = editor.GetEntityEditorSignalsUVE();
        ASSERT_EQ(signals.size(), 2U);
        EXPECT_EQ(signals[0].event, "ready");
        EXPECT_EQ(signals[1].event, "tick");
        EXPECT_EQ(signals[1].params, "(dt)");
        EXPECT_EQ(signals[1].entity, entityRoot);
        EXPECT_GT(signals[1].line, signals[0].line);

        // Unsaved text is what Compile checks: a mistake on line 3 is found there.
        editor.SetOpenUVScriptTextUVE("on ready:\n    pass\n\non tick(dt):\n    nope += 1\n");
        EXPECT_TRUE(editor.HasEntityEditorUnsavedChangesUVE());
        ASSERT_GE(editor.CompileEntityEditorUVE(), 1U);
        const EditorUVE::EntityCompileProblemUVE problem = editor.GetEntityEditorProblemsUVE().front();
        EXPECT_EQ(problem.entity, entityRoot);
        EXPECT_EQ(problem.at.line, 5U);
        EXPECT_FALSE(problem.scriptPath.empty());

        // Going to a problem selects the node and shows its script.
        editor.ClearSelectionUVE();
        ASSERT_TRUE(editor.GoToEntityScriptUVE(problem.entity, problem.at.line));
        EXPECT_EQ(editor.GetSelectedEntityUVE(), entityRoot);
        EXPECT_EQ(editor.GetEntityEditorTabUVE(), EditorUVE::EntityEditorTabUVE::Scripting);

        // A script file that is gone is a problem, not a crash.
        const Scene::EntityUVE child =
            engine.GetServicesUVE().GetSceneGraphUVE().GetChildrenUVE(engine.GetServicesUVE().GetEntityManagerUVE(), entityRoot).front();
        engine.GetServicesUVE().GetEntityManagerUVE().GetComponentUVE<Scene::ScriptComponentUVE>(child).scriptAssetPath =
            "scripts/missing.uvs";
        editor.SetOpenUVScriptTextUVE("on ready:\n    pass\n");
        ASSERT_EQ(editor.CompileEntityEditorUVE(), 1U);
        EXPECT_EQ(editor.GetEntityEditorProblemsUVE().front().entity, child);
        EXPECT_EQ(editor.GetEntityEditorProblemsUVE().front().at.line, 0U);

        // Save writes the script with the entity; Revert clears Compile's list.
        ASSERT_TRUE(editor.SaveEntityEditorUVE());
        EXPECT_FALSE(editor.HasEntityEditorUnsavedChangesUVE());
        ASSERT_TRUE(editor.RevertEntityEditorUVE());
        EXPECT_FALSE(editor.HasEntityEditorCompiledUVE());
        EXPECT_TRUE(editor.GetEntityEditorProblemsUVE().empty());
        ASSERT_TRUE(editor.CloseEntityEditorUVE(false));
        EXPECT_FALSE(editor.GetOpenUVScriptUVE().has_value()) << "the script went with the entity";
        editor.ShutdownUVE();
    }
    engine.Shutdown();
}

TEST(ContentCatalogueEditorUVETest, AnFbxsTakesAreImportedAsClipsBesideIt) {
    const std::filesystem::path root = ::UVE::Tests::MakeTestCaseDirectoryUVE("fbx_takes");
    const std::filesystem::path content = root / "Content";
    std::filesystem::create_directories(content / "Anims");
    // One bone and one take, "Armature|Idle", a second long (ASCII FBX 7.4).
    const std::string fbx = R"(; FBX 7.4.0 project file
FBXHeaderExtension:  {
	FBXVersion: 7400
}
Objects:  {
	Model: 3000, "Model::Hips", "LimbNode" {
		Version: 232
	}
	NodeAttribute: 3100, "NodeAttribute::Hips", "LimbNode" {
		TypeFlags: "Skeleton"
	}
	AnimationStack: 5000, "AnimStack::Armature|Idle", "" {
		Properties70:  {
			P: "LocalStart", "KTime", "Time", "",0
			P: "LocalStop", "KTime", "Time", "",46186158000
		}
	}
	AnimationLayer: 5100, "AnimLayer::Base", "" {
	}
}
Connections:  {
	C: "OO",3100,3000
	C: "OO",3000,0
	C: "OO",5100,5000
}
)";
    const std::filesystem::path source = content / "Anims" / "Hero.fbx";
    {
        std::ofstream out(source, std::ios::binary);
        out << fbx;
    }

    Core::EngineCoreUVE engine(MakeCatalogueEditorConfigUVE(root));
    engine.Init();
    ASSERT_TRUE(engine.Load());
    {
        EditorUVE editor(engine.GetServicesUVE(), root / "main.uvscene", 100U, &engine);
        editor.InitUVE();
        const std::vector<std::filesystem::path> written = editor.ImportModelAnimationsUVE(source);
        ASSERT_EQ(written.size(), 1U);
        EXPECT_EQ(written[0], content / "Anims" / "Hero_Idle.uvanim");
        Asset::AnimationClipAssetUVE clip;
        ASSERT_TRUE(Asset::LoadAnimationClipAssetUVE(written[0], clip));
        EXPECT_EQ(clip.clipId, "Idle");
        EXPECT_TRUE(clip.IsSkeletalUVE());
        ASSERT_EQ(clip.bones.size(), 1U);
        EXPECT_EQ(clip.bones[0].bone, "Hips");
        EXPECT_TRUE(editor.ImportModelAnimationsUVE(source).empty()) << "up to date: nothing written again";
        editor.ShutdownUVE();
    }
    engine.Shutdown();
}

TEST(ContentCatalogueEditorUVETest, PlacingAnAnimationFbxBuildsItsSkeletonAndPlayer) {
    const std::filesystem::path root = ::UVE::Tests::MakeTestCaseDirectoryUVE("place_model");
    const std::string fbx = R"(; FBX 7.4.0 project file
FBXHeaderExtension:  {
	FBXVersion: 7400
}
Objects:  {
	Model: 3000, "Model::Hips", "LimbNode" {
		Version: 232
	}
	NodeAttribute: 3100, "NodeAttribute::Hips", "LimbNode" {
		TypeFlags: "Skeleton"
	}
	AnimationStack: 5000, "AnimStack::Armature|Run", "" {
		Properties70:  {
			P: "LocalStart", "KTime", "Time", "",0
			P: "LocalStop", "KTime", "Time", "",46186158000
		}
	}
	AnimationLayer: 5100, "AnimLayer::Base", "" {
	}
}
Connections:  {
	C: "OO",3100,3000
	C: "OO",3000,0
	C: "OO",5100,5000
}
)";
    Core::EngineCoreUVE engine(MakeCatalogueEditorConfigUVE(root));
    engine.Init();
    ASSERT_TRUE(engine.Load());
    // Written where this project's Content Browser looks.
    const std::filesystem::path content = engine.GetServicesUVE().GetProjectFileIndexUVE().GetSnapshotUVE().contentRoot;
    std::filesystem::create_directories(content / "Anims");
    {
        std::ofstream out(content / "Anims" / "Hero.FBX", std::ios::binary);
        out << fbx;
    }
    {
        EditorUVE editor(engine.GetServicesUVE(), root / "main.uvscene", 100U, &engine);
        editor.InitUVE();
        Core::EngineServicesUVE& services = engine.GetServicesUVE();
        Scene::IEntityManagerUVE& entityManager = services.GetEntityManagerUVE();
        ASSERT_TRUE(services.GetProjectFileIndexUVE().RefreshUVE(services.GetAssetDatabaseUVE()));
        const Scene::EntityUVE placed = editor.PlaceModelSourceUVE("Anims/Hero.FBX");
        ASSERT_NE(placed, Scene::kInvalidEntityUVE);

        // Hero (Node3D) > { Armature (Node3D) > Skeleton3D, AnimationPlayer }
        const auto nameOf = [&entityManager](const Scene::EntityUVE entity) {
            return entityManager.GetComponentUVE<Scene::NameComponentUVE>(entity).name;
        };
        EXPECT_EQ(nameOf(placed), "Hero");
        EXPECT_EQ(Scene::ResolveSceneNodeKindUVE(entityManager, placed), Kind::Node3D);
        const std::vector<Scene::EntityUVE> children = services.GetSceneGraphUVE().GetChildrenUVE(entityManager, placed);
        ASSERT_EQ(children.size(), 2U);
        EXPECT_EQ(nameOf(children[0]), "Armature");
        EXPECT_EQ(Scene::ResolveSceneNodeKindUVE(entityManager, children[0]), Kind::Node3D);
        const std::vector<Scene::EntityUVE> skeletons = services.GetSceneGraphUVE().GetChildrenUVE(entityManager, children[0]);
        ASSERT_EQ(skeletons.size(), 1U);
        ASSERT_TRUE(entityManager.HasComponentUVE<Scene::Skeleton3DNodeComponentUVE>(skeletons[0]));
        const Scene::Skeleton3DNodeComponentUVE& skeleton =
            entityManager.GetComponentUVE<Scene::Skeleton3DNodeComponentUVE>(skeletons[0]);
        EXPECT_EQ(skeleton.skeletonAssetPath, "Anims/Hero.FBX");
        ASSERT_EQ(skeleton.bones.size(), 1U);
        EXPECT_EQ(skeleton.bones[0].name, "Hips");
        EXPECT_TRUE(services.GetSceneGraphUVE().GetChildrenUVE(entityManager, skeletons[0]).empty())
            << "an animation file has no mesh";
        ASSERT_TRUE(entityManager.HasComponentUVE<Scene::AnimationPlayerComponentUVE>(children[1]));
        const Scene::AnimationPlayerComponentUVE& player =
            entityManager.GetComponentUVE<Scene::AnimationPlayerComponentUVE>(children[1]);
        EXPECT_NE(player.clip, Asset::kInvalidAssetGuidUVE);
        EXPECT_EQ(player.loopMode, Scene::AnimationLoopModeUVE::Loop);
        EXPECT_TRUE(std::filesystem::exists(content / "Anims" / "Hero_Run.uvanim"));
        EXPECT_EQ(player.library, std::vector<Asset::AssetGuidUVE>{player.clip}) << "every take is on the player";

        // Another clip from the project joins the player's list and plays; one undo takes it back.
        Asset::AnimationClipAssetUVE walk;
        walk.clipId = "Walk";
        walk.durationSeconds = 1.0;
        walk.bones = {Asset::AnimationAssetBoneTrackUVE{"Hips", {Asset::AnimationAssetSampleUVE{}}}};
        ASSERT_TRUE(Asset::SaveAnimationClipAssetUVE(walk, content / "Anims" / "Walk.uvanim"));
        const Asset::AssetGuidUVE run = player.clip;
        ASSERT_TRUE(editor.AddClipToAnimationPlayerUVE(children[1], content / "Anims" / "Walk.uvanim"));
        ASSERT_EQ(player.library.size(), 2U);
        EXPECT_EQ(player.library[0], run);
        EXPECT_EQ(player.clip, player.library[1]) << "the added clip plays";
        EXPECT_FALSE(editor.AddClipToAnimationPlayerUVE(children[1], content / "Anims" / "Hero.FBX"))
            << "only an animation can join";
        ASSERT_TRUE(editor.UndoUVE());
        EXPECT_EQ(entityManager.GetComponentUVE<Scene::AnimationPlayerComponentUVE>(children[1]).library.size(), 1U);
        EXPECT_EQ(entityManager.GetComponentUVE<Scene::AnimationPlayerComponentUVE>(children[1]).clip, run);

        // One undo removes the whole placement.
        ASSERT_TRUE(editor.UndoUVE());
        EXPECT_FALSE(entityManager.IsAliveUVE(placed));
        editor.ShutdownUVE();
    }
    engine.Shutdown();
}

} // namespace
} // namespace UVE::Editor::Tests
