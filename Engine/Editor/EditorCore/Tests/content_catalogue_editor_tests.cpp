// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <set>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "Support/test_scratch.h"

#include "animation_clip_asset.h"
#include "animation_library_asset.h"
#include "name_component.h"
#include "skeleton_3d.h"
#include "animation_sequencer_component.h"
#include "prefab_instance_component.h"
#include "transform_component.h"
#include "hierarchy_component.h"
#include "engine_core.h"
#include "engine_project_settings.h"
#include "editor_content_catalogue.h"
#include "editor.h"
#include "i_scene_graph.h"
#include "scene_folder.h"
#include "scene_object_registry.h"
#include "scene_object_type.h"

namespace UVE::Editor::Tests {
namespace {

using Kind = Scene::Objects::SceneObjectKind;

TEST(ContentCatalogueTest, EveryItemIsCreatableAndGroupsAreInOrder) {
    const auto items = GetContentCatalogueItems();
    const auto groups = GetContentCatalogueGroups();
    ASSERT_FALSE(items.empty());
    std::set<std::string_view> ids;
    std::size_t groupIndex = 0U;
    for (const ContentCatalogueItem& item : items) {
        EXPECT_TRUE(ids.insert(item.id).second) << item.id;
        EXPECT_FALSE(item.label.empty());
        EXPECT_FALSE(item.tooltip.empty()) << item.id;
        // Items of one group are adjacent and the groups come in their listed order.
        while (groupIndex < groups.size() && groups[groupIndex] != item.group) {
            ++groupIndex;
        }
        ASSERT_LT(groupIndex, groups.size()) << item.id << " is out of group order";
        // Folder and LibraryAsset make a file, not entities, so none of their items carry objects.
        if (item.action == ContentCatalogueAction::Folder ||
            item.action == ContentCatalogueAction::LibraryAsset ||
            item.action == ContentCatalogueAction::MaterialAsset ||
            item.action == ContentCatalogueAction::ShaderAsset) {
            EXPECT_TRUE(item.objects.empty());
            continue;
        }
        ASSERT_FALSE(item.objects.empty()) << item.id;
        for (std::size_t index = 0U; index < item.objects.size(); ++index) {
            const ContentCatalogueObject& object = item.objects[index];
            EXPECT_EQ(index == 0U, object.parent < 0) << item.id;
            EXPECT_LT(object.parent, static_cast<std::int32_t>(index)) << item.id;
            const Scene::Objects::SceneObjectDescriptor* const descriptor =
                Scene::Objects::FindSceneObjectDescriptor(object.kind);
            ASSERT_NE(descriptor, nullptr) << item.id;
            // Structural objects such as a scene's Viewport are valid scene-asset roots but are
            // intentionally not standalone library objects.
            EXPECT_TRUE(descriptor->libraryCreatable ||
                        (item.action == ContentCatalogueAction::SceneAsset &&
                         object.kind == Scene::Objects::SceneObjectKind::Viewport3D))
                << item.id;
        }
    }
    for (const std::string_view group : groups) {
        EXPECT_TRUE(std::any_of(items.begin(), items.end(),
                                [group](const ContentCatalogueItem& item) { return item.group == group; }))
            << group << " is empty";
    }
}

TEST(ContentCatalogueTest, EntityClassesAreBodiesTheMenuAlreadyHas) {
    const auto entries = GetContentCatalogueEntityClasses();
    ASSERT_FALSE(entries.empty());
    std::set<std::string_view> ids;
    for (const ContentCatalogueEntityClass& entry : entries) {
        EXPECT_TRUE(ids.insert(entry.itemId).second) << entry.itemId;
        const ContentCatalogueItem* const item = FindContentCatalogueItem(entry.itemId);
        ASSERT_NE(item, nullptr) << entry.itemId;
        EXPECT_EQ(item->group, "Entity") << entry.itemId;
        EXPECT_EQ(item->action, ContentCatalogueAction::EntityAsset) << entry.itemId;
        // A body is one object: the window promises what the item makes, and nothing rides along.
        EXPECT_EQ(item->objects.size(), 1U) << entry.itemId;
        EXPECT_FALSE(entry.summary.empty()) << entry.itemId;
        EXPECT_FALSE(entry.detail.empty()) << entry.itemId;
        EXPECT_TRUE(IsContentCatalogueEntityClass(entry.itemId)) << entry.itemId;
    }
    EXPECT_FALSE(IsContentCatalogueEntityClass(""));
    EXPECT_FALSE(IsContentCatalogueEntityClass("folder")) << "a folder is not a body";
    EXPECT_EQ(FindContentCatalogueItem("body-montage"), nullptr)
        << "+ Add has no Montage item: Entity Class... opens the window";
}

TEST(ContentCatalogueTest, SearchMatchesEveryWordAnywhere) {
    const ContentCatalogueItem* const character = FindContentCatalogueItem("character");
    ASSERT_NE(character, nullptr);
    EXPECT_TRUE(DoesContentCatalogueItemMatch(*character, ""));
    EXPECT_TRUE(DoesContentCatalogueItemMatch(*character, "CHAR"));
    EXPECT_TRUE(DoesContentCatalogueItemMatch(*character, "entity anim")); // group + tooltip
    EXPECT_FALSE(DoesContentCatalogueItemMatch(*character, "char light"));
    EXPECT_EQ(GetContentCatalogueIconKind(*character), Kind::Character3D);
    EXPECT_EQ(GetContentCatalogueIconKind(*FindContentCatalogueItem("folder")), Kind::Folder);
    EXPECT_EQ(FindContentCatalogueItem("nope"), nullptr);
    // Names beat descriptions: "li" puts Light above Collider, which only has it in its tooltip.
    EXPECT_EQ(RankContentCatalogueItem(*FindContentCatalogueItem("light"), "li"), 3);
    EXPECT_EQ(RankContentCatalogueItem(*FindContentCatalogueItem("collider"), "li"), 2);
    EXPECT_EQ(RankContentCatalogueItem(*FindContentCatalogueItem("static-body"), "never moves"), 1);
    EXPECT_EQ(RankContentCatalogueItem(*character, "xyz"), 0);
}

TEST(ContentCatalogueTest, RecentKeepsFiveNewestFirstWithoutRepeats) {
    std::vector<std::string> recent;
    for (const char* const id : {"a", "b", "c", "d", "e", "f", "c"}) {
        Editor::PushContentCreateRecent(recent, id);
    }
    EXPECT_EQ(recent, (std::vector<std::string>{"c", "f", "e", "d", "b"}));
}

TEST(ContentCatalogueTest, ContentFileNamesNeverCollide) {
    const std::filesystem::path root = ::UVE::Tests::MakeTestCaseDirectory("content_names");
    EXPECT_EQ(Editor::MakeUniqueContentPath(root, "Hero", ".uventity"), root / "Hero.uventity");
    std::ofstream(root / "Hero.uventity") << "x";
    std::ofstream(root / "Hero 2.uventity") << "x";
    EXPECT_EQ(Editor::MakeUniqueContentPath(root, "Hero", ".uventity"), root / "Hero 3.uventity");
    // Folders take the same helper with an empty extension: the picker's + Folder.
    EXPECT_EQ(Editor::MakeUniqueContentPath(root, "New Folder", ""), root / "New Folder");
    std::filesystem::create_directories(root / "New Folder");
    EXPECT_EQ(Editor::MakeUniqueContentPath(root, "New Folder", ""), root / "New Folder 2");

    const auto renamed = Editor::RenameContentFile(root / "Hero.uventity", "Player");
    ASSERT_TRUE(renamed.has_value());
    EXPECT_EQ(*renamed, root / "Player.uventity");
    EXPECT_FALSE(Editor::RenameContentFile(root / "Player.uventity", "Hero 2").has_value()); // taken
    EXPECT_FALSE(Editor::RenameContentFile(root / "Player.uventity", "a/b").has_value());
    EXPECT_FALSE(Editor::RenameContentFile(root / "Player.uventity", "").has_value());

    const auto copy = Editor::DuplicateContentFile(root / "Player.uventity");
    ASSERT_TRUE(copy.has_value());
    EXPECT_EQ(*copy, root / "Player 2.uventity");
    EXPECT_TRUE(std::filesystem::is_regular_file(root / "Player.uventity"));
}

[[nodiscard]] Core::EngineConfig MakeCatalogueEditorConfig(const std::filesystem::path& root) {
    Core::EngineConfig config{};
    config.headless = true;
    config.logFilePath = root / "log.txt";
    config.settingsFilePath = root / "settings.json";
    config.assetDatabaseFilePath = root / "assets.json";
    config.projectSettingsFilePath = root / "project.uvsettings";
    config.saveDirectoryPath = root / "saves";
    config.shaderCachePath = root / "shader_cache";
    config.shaderSourceRealDirectory = ::UVE::Tests::RepositoryRoot() / "Engine/Runtime/RHI/Shader/built_in";
    config.shaderSourceMountPrefix = "shaders";
    return config;
}

[[nodiscard]] std::vector<Kind> ChildKinds(Core::EngineServices& services, const Scene::Entity parent) {
    std::vector<Kind> kinds;
    Scene::IEntityManager& entityManager = services.GetEntityManager();
    for (const Scene::Entity child : services.GetSceneGraph().GetChildren(entityManager, parent)) {
        kinds.push_back(Scene::ResolveSceneObjectKind(entityManager, child));
    }
    return kinds;
}

TEST(ContentCatalogueEditorTest, CharacterAssetPlacesAsItsWholeTreeWithOneUndoStep) {
    const std::filesystem::path root = ::UVE::Tests::MakeTestCaseDirectory("content_catalogue_editor");
    const std::filesystem::path content = root / "Content";
    std::filesystem::create_directories(content);

    Core::EngineCore engine(MakeCatalogueEditorConfig(root));
    engine.Init();
    ASSERT_TRUE(engine.Load());
    {
        Editor editor(engine.GetServices(), root / "main.uvscene", 100U, &engine);
        editor.Init();
        Core::EngineServices& services = engine.GetServices();
        Scene::IEntityManager& entityManager = services.GetEntityManager();
        const std::size_t rootsBefore = editor.GetDocumentRoots().size();
        const bool dirtyBefore = editor.IsSceneDirty();

        const auto created = editor.CreateContentCatalogueItem("character", content);
        ASSERT_TRUE(created.has_value());
        EXPECT_EQ(*created, content / "Character.uventity");
        EXPECT_TRUE(std::filesystem::is_regular_file(*created));
        // Making the asset leaves the open scene alone.
        EXPECT_EQ(editor.GetDocumentRoots().size(), rootsBefore);
        EXPECT_EQ(editor.IsSceneDirty(), dirtyBefore);
        EXPECT_FALSE(editor.Undo());

        const auto second = editor.CreateContentCatalogueItem("character", content);
        ASSERT_TRUE(second.has_value());
        EXPECT_EQ(*second, content / "Character 2.uventity");
        const auto folder = editor.CreateContentCatalogueItem("folder", content);
        ASSERT_TRUE(folder.has_value());
        EXPECT_TRUE(std::filesystem::is_directory(*folder));
        EXPECT_FALSE(editor.CreateContentCatalogueItem("missing", content).has_value());

        const Scene::Entity placed = editor.PlaceEntityAsset(*created);
        ASSERT_NE(placed, Scene::kInvalidEntity);
        EXPECT_EQ(editor.GetSelectedEntity(), placed);
        EXPECT_EQ(Scene::ResolveSceneObjectKind(entityManager, placed), Kind::Character3D);
        EXPECT_EQ(entityManager.GetComponent<Scene::NameComponent>(placed).name, "Character");
        EXPECT_TRUE(entityManager.HasComponent<Scene::PrefabInstanceComponent>(placed));
        // Nothing inside it: an entity asset is the object the author picked and no more. A
        // Character that arrives with a Mesh is a child to look at, judge and delete before the
        // body is theirs - the same rule that keeps a new entity out of the way of its own editor.
        EXPECT_TRUE(ChildKinds(services, placed).empty());
        EXPECT_TRUE(editor.IsSceneDirty());

        // A second placement is named apart from the first.
        const Scene::Entity again = editor.PlaceEntityAsset(*created);
        ASSERT_NE(again, Scene::kInvalidEntity);
        EXPECT_NE(entityManager.GetComponent<Scene::NameComponent>(again).name, "Character");

        ASSERT_TRUE(editor.Undo());
        EXPECT_FALSE(entityManager.IsAlive(again));
        ASSERT_TRUE(editor.Undo());
        EXPECT_FALSE(entityManager.IsAlive(placed));
        ASSERT_TRUE(editor.Redo());
        const Scene::Entity redone = editor.GetSelectedEntity();
        EXPECT_EQ(Scene::ResolveSceneObjectKind(entityManager, redone), Kind::Character3D);
        EXPECT_EQ(ChildKinds(services, redone).size(), 0U);

        EXPECT_EQ(editor.PlaceEntityAsset(root / "nothing.uventity"), Scene::kInvalidEntity);
        EXPECT_EQ(editor.PlaceEntityAsset(root / "log.txt"), Scene::kInvalidEntity);

        EXPECT_TRUE(editor.GetDefaultPlayerEntity().empty());
        ASSERT_TRUE(editor.SetDefaultPlayerEntity("Character.uventity"));
        EXPECT_EQ(editor.GetDefaultPlayerEntity(), "Character.uventity");
        EXPECT_EQ(std::get<std::string>(*services.GetProjectSettings().GetValue(
                      Core::EngineProjectSettingId::kDefaultPlayerEntity)),
                  "Character.uventity");
        ASSERT_TRUE(editor.SetDefaultPlayerEntity({}));
        EXPECT_TRUE(editor.GetDefaultPlayerEntity().empty());

        // Nothing is made while the game runs.
        ASSERT_TRUE(editor.EnterPlayMode());
        EXPECT_FALSE(editor.CreateContentCatalogueItem("prop", content).has_value());
        EXPECT_EQ(editor.PlaceEntityAsset(*created), Scene::kInvalidEntity);
        ASSERT_TRUE(editor.StopPlayMode());

        editor.Shutdown();
    }
    engine.Shutdown();
}

TEST(ContentCatalogueEditorTest, LibraryAssetCreatesAnEmptyAnimationLibrary) {
    const std::filesystem::path root = ::UVE::Tests::MakeTestCaseDirectory("content_catalogue_library");
    const std::filesystem::path content = root / "Content";
    std::filesystem::create_directories(content);

    Core::EngineCore engine(MakeCatalogueEditorConfig(root));
    engine.Init();
    ASSERT_TRUE(engine.Load());
    {
        Editor editor(engine.GetServices(), root / "main.uvscene", 100U, &engine);
        editor.Init();
        const auto created = editor.CreateContentCatalogueItem("animation-library", content);
        ASSERT_TRUE(created.has_value());
        EXPECT_EQ(*created, content / "Animation Library.uvanimlib");
        Asset::AnimationLibraryAsset library;
        EXPECT_TRUE(Asset::LoadAnimationLibraryAsset(*created, library));
        EXPECT_TRUE(library.entries.empty());
        const auto second = editor.CreateContentCatalogueItem("animation-library", content);
        ASSERT_TRUE(second.has_value());
        EXPECT_EQ(*second, content / "Animation Library 2.uvanimlib");
        editor.Shutdown();
    }
    engine.Shutdown();
}

TEST(ContentCatalogueEditorTest, EntityClassMakesOnlyTheBodyYouPick) {
    const std::filesystem::path root = ::UVE::Tests::MakeTestCaseDirectory("entity_class");
    const std::filesystem::path content = root / "Content";
    std::filesystem::create_directories(content);

    Core::EngineCore engine(MakeCatalogueEditorConfig(root));
    engine.Init();
    ASSERT_TRUE(engine.Load());
    {
        Editor editor(engine.GetServices(), root / "main.uvscene", 100U, &engine);
        editor.Init();
        Core::EngineServices& services = engine.GetServices();
        Scene::IEntityManager& entityManager = services.GetEntityManager();

        // Opening Entity Class writes nothing: it asks first.
        editor.OpenEntityClassPicker(content);
        ASSERT_TRUE(editor.IsEntityClassPickerOpen());
        EXPECT_EQ(std::distance(std::filesystem::directory_iterator(content), std::filesystem::directory_iterator{}),
                  0)
            << "the window costs the project nothing until Open";

        // Only the window's own bodies: the menu's other entries are not choices it can make.
        EXPECT_FALSE(editor.CreateEntityClass("folder"));
        EXPECT_FALSE(editor.CreateEntityClass("nothing"));
        EXPECT_TRUE(editor.IsEntityClassPickerOpen()) << "a refusal leaves the question standing";

        ASSERT_TRUE(editor.CreateEntityClass("character"));
        EXPECT_FALSE(editor.IsEntityClassPickerOpen());
        const std::filesystem::path created = content / "Character.uventity";
        ASSERT_TRUE(std::filesystem::is_regular_file(created));
        // Made and opened in one step: the body is a file in Content and the document in the editor.
        EXPECT_TRUE(editor.IsEntityEditorOpen());
        EXPECT_EQ(editor.GetEntityEditorAssetPath(), created);
        const Scene::Entity body = editor.GetEntityEditorRoot();
        ASSERT_NE(body, Scene::kInvalidEntity);
        EXPECT_EQ(Scene::ResolveSceneObjectKind(entityManager, body), Kind::Character3D);
        EXPECT_EQ(entityManager.GetComponent<Scene::NameComponent>(body).name, "Character");
        // The rule every "+ Add" follows, Entity Class included: nothing inside the body it did not
        // ask for.
        EXPECT_TRUE(ChildKinds(services, body).empty());
        editor.Shutdown();
    }
    engine.Shutdown();
}

TEST(ContentCatalogueEditorTest, EntityEditorEditsTheAssetAloneAndGivesTheSceneBack) {
    const std::filesystem::path root = ::UVE::Tests::MakeTestCaseDirectory("entity_editor");
    const std::filesystem::path content = root / "Content";
    std::filesystem::create_directories(content);

    Core::EngineCore engine(MakeCatalogueEditorConfig(root));
    engine.Init();
    ASSERT_TRUE(engine.Load());
    {
        Editor editor(engine.GetServices(), root / "main.uvscene", 100U, &engine);
        editor.Init();
        Core::EngineServices& services = engine.GetServices();
        Scene::IEntityManager& entityManager = services.GetEntityManager();
        const auto created = editor.CreateContentCatalogueItem("character", content);
        ASSERT_TRUE(created.has_value());
        const Scene::Entity placed = editor.PlaceEntityAsset(*created);
        ASSERT_NE(placed, Scene::kInvalidEntity);
        ASSERT_TRUE(editor.SaveScene());
        ASSERT_FALSE(editor.IsSceneDirty());

        const auto positionY = [&entityManager](const Scene::Entity entity) {
            return entityManager.GetComponent<Scene::TransformComponent>(entity).localPosition.y;
        };
        const auto moveTo = [&editor](const Scene::Entity entity, const float y) {
            editor.SelectEntity(entity);
            Scene::TransformComponent moved{};
            moved.localPosition = Math::Vector3{0.0F, y, 0.0F};
            return editor.SetSelectedLocalTransform(moved);
        };

        EXPECT_FALSE(editor.OpenEntityEditor(root / "nothing.uventity"));
        ASSERT_TRUE(editor.OpenEntityEditor(*created));
        EXPECT_TRUE(editor.IsEntityEditorOpen());
        EXPECT_EQ(editor.GetEntityEditorAssetPath(), *created);
        // The entity is the document now; the scene is put aside and undo starts fresh.
        EXPECT_FALSE(entityManager.IsAlive(placed));
        EXPECT_FALSE(editor.IsSceneDirty());
        EXPECT_FALSE(editor.Undo());
        const Scene::Entity entityRoot = editor.GetEntityEditorRoot();
        ASSERT_NE(entityRoot, Scene::kInvalidEntity);
        EXPECT_EQ(Scene::ResolveSceneObjectKind(entityManager, entityRoot), Kind::Character3D);
        EXPECT_FALSE(entityManager.HasComponent<Scene::PrefabInstanceComponent>(entityRoot))
            << "edited as itself, not as an instance of itself";
        EXPECT_EQ(ChildKinds(services, entityRoot).size(), 0U);
        EXPECT_FALSE(editor.EnterPlayMode()) << "the scene plays, not an open entity";
        EXPECT_EQ(engine.GetSimulationExecutionMode(), Core::SimulationExecutionMode::Paused)
            << "an entity is edited as authored, not simulated";
        EXPECT_FALSE(editor.OpenEntityEditor(content / "Other.uventity"));

        // The entity is the top of the tree: new objects go under it, and it cannot be removed.
        editor.ClearSelection();
        const Scene::Entity added = editor.CreateDocumentSceneObject(Kind::Object3D);
        ASSERT_NE(added, Scene::kInvalidEntity);
        EXPECT_EQ(entityManager.GetComponent<Scene::HierarchyComponent>(added).parent, entityRoot);
        EXPECT_EQ(editor.GetEntityEditorRoot(), entityRoot) << "still one root";
        editor.SelectEntity(entityRoot);
        EXPECT_FALSE(editor.DeleteSelectedEntity());
        EXPECT_EQ(editor.DuplicateSelectedEntity(), Scene::kInvalidEntity);
        ASSERT_TRUE(editor.RevertEntityEditor());
        const Scene::Entity root2 = editor.GetEntityEditorRoot(); // Revert makes fresh entities

        // Save (Ctrl+S saves the entity while it is open), then change it again and revert.
        ASSERT_TRUE(moveTo(root2, 2.0F));
        EXPECT_TRUE(editor.IsSceneDirty());
        ASSERT_TRUE(editor.SaveScene());
        EXPECT_FALSE(editor.IsSceneDirty());
        ASSERT_TRUE(moveTo(root2, 5.0F));
        ASSERT_TRUE(editor.RevertEntityEditor());
        EXPECT_FLOAT_EQ(positionY(editor.GetEntityEditorRoot()), 2.0F);
        EXPECT_FALSE(editor.Undo());

        // One root only: an object beside the root is refused on save.
        const Scene::Entity sceneRoot = editor.GetDocumentSceneRoot();
        const Scene::Entity stray = entityManager.CreateEntity();
        entityManager.AddComponent<Scene::TransformComponent>(stray, Scene::TransformComponent{});
        entityManager.AddComponent<Scene::HierarchyComponent>(stray, Scene::HierarchyComponent{});
        services.GetSceneGraph().SetParent(entityManager, stray, sceneRoot);
        EXPECT_EQ(editor.GetEntityEditorRoot(), Scene::kInvalidEntity);
        EXPECT_FALSE(editor.SaveEntityEditor());
        ASSERT_TRUE(editor.RevertEntityEditor());

        // An unsaved change closed without saving is not written.
        ASSERT_TRUE(moveTo(editor.GetEntityEditorRoot(), 9.0F));
        ASSERT_TRUE(editor.CloseEntityEditor(false));
        EXPECT_FALSE(editor.IsEntityEditorOpen());
        // Closing the entity gives the mode back as Paused, not Running. An editor does not
        // simulate: the clock stays off until Play turns it on, so the mode an entity is closed
        // into is the same frozen one it was opened from. This used to expect Running, because the
        // editor used to open with the simulation already going.
        EXPECT_EQ(engine.GetSimulationExecutionMode(), Core::SimulationExecutionMode::Paused)
            << "closing an entity leaves the editor frozen, not simulating";

        // The scene is back, with its placed Character in the level's object folder.
        Scene::Entity character = Scene::kInvalidEntity;
        entityManager.ForEach<Scene::NameComponent>(
            [&character](const Scene::Entity entity, const Scene::NameComponent& name) {
                if (name.name == "Character") {
                    character = entity;
                }
            });
        ASSERT_NE(character, Scene::kInvalidEntity);
        EXPECT_TRUE(entityManager.HasComponent<Scene::PrefabInstanceComponent>(character));
        const Scene::Entity folder = entityManager.GetComponent<Scene::HierarchyComponent>(character).parent;
        EXPECT_TRUE(entityManager.HasComponent<Scene::FolderComponent>(folder));

        // What was saved is in the file.
        ASSERT_TRUE(editor.OpenEntityEditor(*created));
        EXPECT_FLOAT_EQ(positionY(editor.GetEntityEditorRoot()), 2.0F);
        // Closing with save writes an unsaved change.
        ASSERT_TRUE(moveTo(editor.GetEntityEditorRoot(), 3.0F));
        ASSERT_TRUE(editor.CloseEntityEditor(true));
        ASSERT_TRUE(editor.OpenEntityEditor(*created));
        EXPECT_FLOAT_EQ(positionY(editor.GetEntityEditorRoot()), 3.0F);
        ASSERT_TRUE(editor.CloseEntityEditor(false));
        editor.Shutdown();
    }
    engine.Shutdown();
}

TEST(ContentCatalogueEditorTest, EntityEditorClonedAssetSwitchAndSafeReturnToLevel) {
    const std::filesystem::path root = ::UVE::Tests::MakeTestCaseDirectory("entity_clone_switch");
    const std::filesystem::path content = root / "Content";
    std::filesystem::create_directories(content);

    Core::EngineCore engine(MakeCatalogueEditorConfig(root));
    engine.Init();
    ASSERT_TRUE(engine.Load());
    {
        Editor editor(engine.GetServices(), root / "main.uvscene", 100U, &engine);
        editor.Init();
        const auto created = editor.CreateContentCatalogueItem("character", content);
        ASSERT_TRUE(created.has_value());
        const Scene::Entity placed = editor.PlaceEntityAsset(*created);
        ASSERT_NE(placed, Scene::kInvalidEntity);
        ASSERT_TRUE(editor.SaveScene());

        // Clone/duplicate the entity file in Content
        const std::optional<std::filesystem::path> clonePath = editor.DuplicateContentFile(*created);
        ASSERT_TRUE(clonePath.has_value());

        // Open the original entity
        ASSERT_TRUE(editor.OpenEntityEditor(*created));
        EXPECT_TRUE(editor.IsEntityEditorOpen());
        EXPECT_EQ(editor.GetEntityEditorAssetPath(), *created);

        // Switch to the clone directly while the entity editor is open
        ASSERT_TRUE(editor.OpenEntityEditor(*clonePath));
        EXPECT_TRUE(editor.IsEntityEditorOpen());
        EXPECT_EQ(editor.GetEntityEditorAssetPath(), *clonePath);

        // Save & return to the main level editor safely
        ASSERT_TRUE(editor.CloseEntityEditor(true));
        EXPECT_FALSE(editor.IsEntityEditorOpen());

        // Verify the level editor scene is alive and document roots are valid
        EXPECT_NE(editor.GetDocumentSceneRoot(), Scene::kInvalidEntity);
        EXPECT_FALSE(editor.GetDocumentRoots().empty());

        editor.Shutdown();
    }
    engine.Shutdown();
}

TEST(ContentCatalogueEditorTest, AnFbxsTakesAreImportedAsClipsBesideIt) {
    const std::filesystem::path root = ::UVE::Tests::MakeTestCaseDirectory("fbx_takes");
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

    Core::EngineCore engine(MakeCatalogueEditorConfig(root));
    engine.Init();
    ASSERT_TRUE(engine.Load());
    {
        Editor editor(engine.GetServices(), root / "main.uvscene", 100U, &engine);
        editor.Init();
        const std::vector<std::filesystem::path> written = editor.ImportModelAnimations(source);
        ASSERT_EQ(written.size(), 1U);
        // One take: the animation is named after its file, not the DCC tool's take label.
        EXPECT_EQ(written[0], content / "Anims" / "Hero.uvanim");
        Asset::AnimationClipAsset clip;
        ASSERT_TRUE(Asset::LoadAnimationClipAsset(written[0], clip));
        EXPECT_EQ(clip.clipId, "Hero");
        EXPECT_TRUE(clip.IsSkeletal());
        ASSERT_EQ(clip.bones.size(), 1U);
        EXPECT_EQ(clip.bones[0].bone, "Hips");
        EXPECT_TRUE(editor.ImportModelAnimations(source).empty()) << "up to date: nothing written again";
        editor.Shutdown();
    }
    engine.Shutdown();
}

TEST(ContentCatalogueEditorTest, PlacingAnAnimationFbxBuildsItsSkeletonAndPlayer) {
    const std::filesystem::path root = ::UVE::Tests::MakeTestCaseDirectory("place_model");
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
    Core::EngineCore engine(MakeCatalogueEditorConfig(root));
    engine.Init();
    ASSERT_TRUE(engine.Load());
    // Written where this project's Asset Table looks.
    const std::filesystem::path content = engine.GetServices().GetProjectFileIndex().GetSnapshot().contentRoot;
    std::filesystem::create_directories(content / "Anims");
    {
        std::ofstream out(content / "Anims" / "Hero.FBX", std::ios::binary);
        out << fbx;
    }
    {
        Editor editor(engine.GetServices(), root / "main.uvscene", 100U, &engine);
        editor.Init();
        Core::EngineServices& services = engine.GetServices();
        Scene::IEntityManager& entityManager = services.GetEntityManager();
        // Deliberately the plain index refresh, not the editor's, which also fills the model-source
        // cache. Placing must work without that cache: it is an optimisation for panels that draw
        // many entries, and when it was the only source of truth this placement saw "no information",
        // read a bones-only file as a model whose mesh was still converting, and built nothing.
        ASSERT_TRUE(services.GetProjectFileIndex().Refresh(services.GetAssetDatabase()));
        const Scene::Entity placed = editor.PlaceModelSource("Anims/Hero.FBX");
        ASSERT_NE(placed, Scene::kInvalidEntity);

        // Hero (Object3D) > { Armature (Object3D) > Skeleton3D, AnimationSequencer }
        const auto nameOf = [&entityManager](const Scene::Entity entity) {
            return entityManager.GetComponent<Scene::NameComponent>(entity).name;
        };
        EXPECT_EQ(nameOf(placed), "Hero");
        EXPECT_EQ(Scene::ResolveSceneObjectKind(entityManager, placed), Kind::Object3D);
        const std::vector<Scene::Entity> children = services.GetSceneGraph().GetChildren(entityManager, placed);
        ASSERT_EQ(children.size(), 2U);
        EXPECT_EQ(nameOf(children[0]), "Armature");
        EXPECT_EQ(Scene::ResolveSceneObjectKind(entityManager, children[0]), Kind::Object3D);
        const std::vector<Scene::Entity> skeletons = services.GetSceneGraph().GetChildren(entityManager, children[0]);
        ASSERT_EQ(skeletons.size(), 1U);
        ASSERT_TRUE(entityManager.HasComponent<Scene::Skeleton3DComponent>(skeletons[0]));
        const Scene::Skeleton3DComponent& skeleton =
            entityManager.GetComponent<Scene::Skeleton3DComponent>(skeletons[0]);
        EXPECT_EQ(skeleton.skeletonAssetPath, "Anims/Hero.FBX");
        ASSERT_EQ(skeleton.bones.size(), 1U);
        EXPECT_EQ(skeleton.bones[0].name, "Hips");
        EXPECT_TRUE(services.GetSceneGraph().GetChildren(entityManager, skeletons[0]).empty())
            << "an animation file has no mesh";
        ASSERT_TRUE(entityManager.HasComponent<Scene::AnimationSequencerComponent>(children[1]));
        const Scene::AnimationSequencerComponent& player =
            entityManager.GetComponent<Scene::AnimationSequencerComponent>(children[1]);
        EXPECT_NE(player.clip, Asset::kInvalidAssetGuid);
        EXPECT_EQ(player.loopMode, Scene::AnimationLoopMode::Loop);
        EXPECT_TRUE(std::filesystem::exists(content / "Anims" / "Hero.uvanim")) << "one take: named after its file";
        Asset::AnimationClipAsset imported;
        ASSERT_TRUE(Asset::LoadAnimationClipAsset(content / "Anims" / "Hero.uvanim", imported));
        EXPECT_EQ(imported.clipId, "Hero") << "not the DCC tool's take label";
        EXPECT_EQ(player.library, std::vector<Asset::AssetGuid>{player.clip}) << "every take is on the player";

        // Another clip from the project joins the player's list and plays; one undo takes it back.
        Asset::AnimationClipAsset walk;
        walk.clipId = "Walk";
        walk.durationSeconds = 1.0;
        walk.bones = {Asset::AnimationAssetBoneTrack{"Hips", {Asset::AnimationAssetSample{}}}};
        ASSERT_TRUE(Asset::SaveAnimationClipAsset(walk, content / "Anims" / "Walk.uvanim"));
        const Asset::AssetGuid run = player.clip;
        ASSERT_TRUE(editor.AddClipToAnimationSequencer(children[1], content / "Anims" / "Walk.uvanim"));
        ASSERT_EQ(player.library.size(), 2U);
        EXPECT_EQ(player.library[0], run);
        EXPECT_EQ(player.clip, player.library[1]) << "the added clip plays";
        EXPECT_FALSE(editor.AddClipToAnimationSequencer(children[1], content / "Anims" / "Hero.FBX"))
            << "only an animation can join";
        ASSERT_TRUE(editor.Undo());
        EXPECT_EQ(entityManager.GetComponent<Scene::AnimationSequencerComponent>(children[1]).library.size(), 1U);
        EXPECT_EQ(entityManager.GetComponent<Scene::AnimationSequencerComponent>(children[1]).clip, run);

        // One undo removes the whole placement.
        ASSERT_TRUE(editor.Undo());
        EXPECT_FALSE(entityManager.IsAlive(placed));
        editor.Shutdown();
    }
    engine.Shutdown();
}

TEST(ContentCatalogueEditorTest, EntityEditorPlacingModelSourceBuildsCompleteModelContainerUnderEntityRoot) {
    const std::filesystem::path root = ::UVE::Tests::MakeTestCaseDirectory("place_model_entity_editor");
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
    Core::EngineCore engine(MakeCatalogueEditorConfig(root));
    engine.Init();
    ASSERT_TRUE(engine.Load());
    const std::filesystem::path content = engine.GetServices().GetProjectFileIndex().GetSnapshot().contentRoot;
    std::filesystem::create_directories(content / "Anims");
    {
        std::ofstream out(content / "Anims" / "Hero.FBX", std::ios::binary);
        out << fbx;
    }
    {
        Editor editor(engine.GetServices(), root / "main.uvscene", 100U, &engine);
        editor.Init();
        Core::EngineServices& services = engine.GetServices();
        Scene::IEntityManager& entityManager = services.GetEntityManager();
        ASSERT_TRUE(services.GetProjectFileIndex().Refresh(services.GetAssetDatabase()));

        const auto created = editor.CreateContentCatalogueItem("character", content);
        ASSERT_TRUE(created.has_value());
        ASSERT_TRUE(editor.OpenEntityEditor(*created));
        EXPECT_TRUE(editor.IsEntityEditorOpen());
        const Scene::Entity entityRoot = editor.GetEntityEditorRoot();
        ASSERT_NE(entityRoot, Scene::kInvalidEntity);

        const Scene::Entity placed = editor.PlaceModelSource("Anims/Hero.FBX", entityRoot);
        ASSERT_NE(placed, Scene::kInvalidEntity);

        const auto nameOf = [&entityManager](const Scene::Entity entity) {
            return entityManager.GetComponent<Scene::NameComponent>(entity).name;
        };
        EXPECT_EQ(nameOf(placed), "Hero");
        EXPECT_EQ(Scene::ResolveSceneObjectKind(entityManager, placed), Kind::Object3D);
        EXPECT_EQ(entityManager.GetComponent<Scene::HierarchyComponent>(placed).parent, entityRoot);

        const std::vector<Scene::Entity> children = services.GetSceneGraph().GetChildren(entityManager, placed);
        ASSERT_EQ(children.size(), 2U);
        EXPECT_EQ(nameOf(children[0]), "Armature");
        EXPECT_EQ(Scene::ResolveSceneObjectKind(entityManager, children[0]), Kind::Object3D);
        const std::vector<Scene::Entity> skeletons = services.GetSceneGraph().GetChildren(entityManager, children[0]);
        ASSERT_EQ(skeletons.size(), 1U);
        EXPECT_TRUE(entityManager.HasComponent<Scene::Skeleton3DComponent>(skeletons[0]));
        EXPECT_TRUE(entityManager.HasComponent<Scene::AnimationSequencerComponent>(children[1]));

        editor.Shutdown();
    }
    engine.Shutdown();
    std::filesystem::remove_all(root);
}

TEST(ContentCatalogueEditorTest, OutlinerFolderLockToggle) {
    const std::filesystem::path root = ::UVE::Tests::MakeTestCaseDirectory("folder_lock");
    Core::EngineCore engine(MakeCatalogueEditorConfig(root));
    engine.Init();
    ASSERT_TRUE(engine.Load());
    {
        Editor editor(engine.GetServices(), root / "main.uvscene", 100U, &engine);
        editor.Init();
        const Scene::Entity folder =
            editor.CreateDocumentSceneObject(Scene::Objects::SceneObjectKind::Folder);
        ASSERT_NE(folder, Scene::kInvalidEntity);

        EXPECT_FALSE(editor.IsEntityLocked(folder));
        editor.ToggleEntityLock(folder);
        EXPECT_TRUE(editor.IsEntityLocked(folder));
        editor.ToggleEntityLock(folder);
        EXPECT_FALSE(editor.IsEntityLocked(folder));

        editor.SetEntityLocked(folder, true);
        EXPECT_TRUE(editor.IsEntityLocked(folder));
        editor.SetEntityLocked(folder, false);
        EXPECT_FALSE(editor.IsEntityLocked(folder));

        editor.Shutdown();
    }
    engine.Shutdown();
    std::filesystem::remove_all(root);
}


TEST(ContentCatalogueEditorTest, ANewSceneIsItsViewportAndTheWorldFolderWaitsForSomethingToHold) {
    const std::filesystem::path root = ::UVE::Tests::MakeTestCaseDirectory("content_catalogue_scene_empty");
    const std::filesystem::path content = root / "Content";
    std::filesystem::create_directories(content);

    Core::EngineCore engine(MakeCatalogueEditorConfig(root));
    engine.Init();
    ASSERT_TRUE(engine.Load());
    {
        Editor editor(engine.GetServices(), root / "main.uvscene", 100U, &engine);
        editor.Init();
        Scene::IEntityManager& entityManager = engine.GetServices().GetEntityManager();
        Scene::ISceneGraph& sceneGraph = engine.GetServices().GetSceneGraph();

        const auto created = editor.CreateContentCatalogueItem("viewport_3d", content);
        ASSERT_TRUE(created.has_value());
        EXPECT_EQ(created->extension(), ".uvscene");
        ASSERT_TRUE(editor.OpenSceneAsset(*created));

        // Root and Viewport, and nothing else: the empty "World" folder the old creation path made is
        // gone, so a brand-new level does not open with a folder holding nothing.
        const Scene::Entity sceneRoot = editor.GetDocumentSceneRoot();
        ASSERT_NE(sceneRoot, Scene::kInvalidEntity);
        const std::vector<Scene::Entity> rootChildren = sceneGraph.GetChildren(entityManager, sceneRoot);
        ASSERT_EQ(rootChildren.size(), 1U);
        const Scene::Entity viewport = editor.GetDocumentViewport();
        EXPECT_EQ(rootChildren[0], viewport);
        EXPECT_TRUE(sceneGraph.GetChildren(entityManager, viewport).empty());

        // The folder appears when it has something to hold, and only then.
        const Scene::Entity placed =
            editor.CreateDocumentSceneObject(Scene::Objects::SceneObjectKind::Object3D);
        ASSERT_NE(placed, Scene::kInvalidEntity);
        const Scene::Entity folder = entityManager.GetComponent<Scene::HierarchyComponent>(placed).parent;
        ASSERT_NE(folder, Scene::kInvalidEntity);
        EXPECT_TRUE(entityManager.HasComponent<Scene::FolderComponent>(folder));
        EXPECT_EQ(entityManager.GetComponent<Scene::NameComponent>(folder).name, "World");
        EXPECT_EQ(entityManager.GetComponent<Scene::HierarchyComponent>(folder).parent, viewport);
        editor.Shutdown();
    }
    engine.Shutdown();
    std::filesystem::remove_all(root);
}

TEST(ContentCatalogueEditorTest, Creating3DLevelsMakesUniqueFilesAndOmits2DSceneCatalogue) {
    const std::filesystem::path root = ::UVE::Tests::MakeTestCaseDirectory("content_catalogue_scene_3d");
    const std::filesystem::path content = root / "Content";
    std::filesystem::create_directories(content);

    Core::EngineCore engine(MakeCatalogueEditorConfig(root));
    engine.Init();
    ASSERT_TRUE(engine.Load());
    {
        Editor editor(engine.GetServices(), root / "main.uvscene", 100U, &engine);
        editor.Init();
        Scene::IEntityManager& entityManager = engine.GetServices().GetEntityManager();

        EXPECT_EQ(FindContentCatalogueItem("viewport_2d"), nullptr);

        const auto first = editor.CreateContentCatalogueItem("viewport_3d", content);
        ASSERT_TRUE(first.has_value());
        EXPECT_EQ(first->filename().string(), "Viewport3D.uvscene");

        const auto second = editor.CreateContentCatalogueItem("viewport_3d", content);
        ASSERT_TRUE(second.has_value());
        EXPECT_EQ(second->filename().string(), "Viewport3D 2.uvscene");
        EXPECT_TRUE(std::filesystem::is_regular_file(*second));

        std::ifstream solidFile(*first);
        ASSERT_TRUE(solidFile.is_open());
        const std::string solidText(std::istreambuf_iterator<char>(solidFile), {});
        EXPECT_EQ(solidText.find("\"dimension\""), std::string::npos);

        ASSERT_TRUE(editor.OpenSceneAsset(*first));
        const Scene::Entity solidViewport = editor.GetDocumentViewport();
        ASSERT_NE(solidViewport, Scene::kInvalidEntity);
        EXPECT_TRUE(entityManager.HasComponent<Scene::OutlinerViewport3DComponent>(solidViewport));
        editor.Shutdown();
    }
    engine.Shutdown();
    std::filesystem::remove_all(root);
}

TEST(ContentCatalogueEditorTest, FolderAutoDetectAnimationLibraryIncludesOnlyDirectClipsAndStreamsOneByOne) {
    const std::filesystem::path root = ::UVE::Tests::MakeTestCaseDirectory("anim_lib_folder_autodetect");
    const std::filesystem::path content = root / "Content";
    std::filesystem::create_directories(content / "Locomotion" / "Combat");
    std::filesystem::create_directories(content / "OnlySubfolders" / "Nested");
    std::filesystem::create_directories(content / "NoAnimations");

    const auto writeClip = [](const std::filesystem::path& path, const std::string& id, const double duration) {
        Asset::AnimationClipAsset clip;
        clip.clipId = id;
        clip.durationSeconds = duration;
        const Asset::AnimationAssetPose p0{{0.0F, 0.0F, 0.0F}, {}, {1.0F, 1.0F, 1.0F}};
        const Asset::AnimationAssetPose p1{{1.0F, 0.0F, 0.0F}, {}, {1.0F, 1.0F, 1.0F}};
        clip.bones = {Asset::AnimationAssetBoneTrack{"Hips", {{0.0, p0}, {duration, p1}}}};
        clip.rest = {Asset::AnimationAssetRestBone{"Hips", -1, {0.0F, 1.0F, 0.0F}, {}, {1.0F, 1.0F, 1.0F}}};
        return Asset::SaveAnimationClipAsset(clip, path);
    };

    ASSERT_TRUE(writeClip(content / "Locomotion" / "Idle.uvanim", "idle", 1.0));
    ASSERT_TRUE(writeClip(content / "Locomotion" / "Walk.uvanim", "walk", 0.8));
    ASSERT_TRUE(writeClip(content / "Locomotion" / "Run.uvanim", "run", 0.6));
    {
        std::ofstream notes(content / "Locomotion" / "Notes.txt");
        notes << "not an animation";
    }
    // Nested subfolder animations MUST NOT be included when creating a library from "Locomotion".
    ASSERT_TRUE(writeClip(content / "Locomotion" / "Combat" / "Slash.uvanim", "slash", 0.5));
    ASSERT_TRUE(writeClip(content / "Locomotion" / "Combat" / "Block.uvanim", "block", 0.4));
    // A folder whose animations only live inside a child subfolder MUST report 0 direct clips.
    ASSERT_TRUE(writeClip(content / "OnlySubfolders" / "Nested" / "Jump.uvanim", "jump", 0.7));

    Core::EngineCore engine(MakeCatalogueEditorConfig(root));
    engine.Init();
    ASSERT_TRUE(engine.Load());
    {
        Editor editor(engine.GetServices(), root / "main.uvscene", 100U, &engine);
        editor.Init();
        Core::EngineServices& services = engine.GetServices();
        ASSERT_TRUE(services.GetProjectFileIndex().Refresh(services.GetAssetDatabase()));

        EXPECT_TRUE(editor.CollectAnimationClipsInFolder("NoAnimations").empty());
        EXPECT_FALSE(editor.CreateAnimationLibraryFromFolder("NoAnimations").has_value());

        EXPECT_TRUE(editor.CollectAnimationClipsInFolder("OnlySubfolders").empty());
        EXPECT_FALSE(editor.CreateAnimationLibraryFromFolder("OnlySubfolders").has_value());

        const std::vector<std::filesystem::path> directClips = editor.CollectAnimationClipsInFolder("Locomotion");
        ASSERT_EQ(directClips.size(), 3U);
        EXPECT_EQ(directClips[0].filename().string(), "Idle.uvanim");
        EXPECT_EQ(directClips[1].filename().string(), "Run.uvanim");
        EXPECT_EQ(directClips[2].filename().string(), "Walk.uvanim");

        const auto created = editor.CreateAnimationLibraryFromFolder("Locomotion");
        ASSERT_TRUE(created.has_value());
        EXPECT_EQ(created->filename().string(), "Locomotion.uvanimlib");
        EXPECT_TRUE(editor.IsAnimationLibraryWindowOpen());
        EXPECT_TRUE(editor.IsAnimationLibraryAddQueueActive());

        // Paced one-by-one ingestion: 3 direct clips take 3 single-clip polls.
        EXPECT_TRUE(editor.PollAnimationLibraryAddQueue(1U, 1.0));
        EXPECT_EQ(editor.GetLoadingState().busy, true);
        EXPECT_TRUE(editor.PollAnimationLibraryAddQueue(1U, 1.0));
        EXPECT_FALSE(editor.PollAnimationLibraryAddQueue(1U, 1.0));
        EXPECT_FALSE(editor.IsAnimationLibraryAddQueueActive());

        Asset::AnimationLibraryAsset loaded;
        ASSERT_TRUE(Asset::LoadAnimationLibraryAsset(*created, loaded));
        ASSERT_EQ(loaded.entries.size(), 3U);
        EXPECT_EQ(loaded.entries[0].name, "Idle");
        EXPECT_EQ(loaded.entries[1].name, "Run");
        EXPECT_EQ(loaded.entries[2].name, "Walk");

        // Creating another library on the same folder picks a collision-free name ("Locomotion 2.uvanimlib").
        const auto second = editor.CreateAnimationLibraryFromFolder("Locomotion");
        ASSERT_TRUE(second.has_value());
        EXPECT_EQ(second->filename().string(), "Locomotion 2.uvanimlib");
        while (editor.PollAnimationLibraryAddQueue(1U, 1.0)) {
        }

        editor.Shutdown();
    }
    engine.Shutdown();
    std::filesystem::remove_all(root);
}

TEST(ContentCatalogueEditorTest, RapidSequencerClipSwitchingAcrossMixedClipsAndMissingFilesStaysSafe) {
    const std::filesystem::path root = ::UVE::Tests::MakeTestCaseDirectory("sequencer_clip_switch_safety");
    const std::filesystem::path content = root / "Content";
    std::filesystem::create_directories(content / "Clips");

    // 1. Skeletal clip with a very long animation name.
    const std::string longStem =
        "VeryLongCharacterLocomotionSprintForwardWithHeavyShieldAndSwordAnimation_Take001_MasterLoop";
    const std::filesystem::path skelPath = content / "Clips" / (longStem + ".uvanim");
    {
        Asset::AnimationClipAsset skel;
        skel.clipId = "sprint_heavy";
        skel.durationSeconds = 1.25;
        const Asset::AnimationAssetPose p0{{0.0F, 1.0F, 0.0F}, {}, {1.0F, 1.0F, 1.0F}};
        const Asset::AnimationAssetPose p1{{0.0F, 1.1F, 2.0F}, {}, {1.0F, 1.0F, 1.0F}};
        skel.bones = {
            Asset::AnimationAssetBoneTrack{"Hips", {{0.0, p0}, {1.25, p1}}},
            Asset::AnimationAssetBoneTrack{"Spine", {{0.0, p0}, {1.25, p0}}},
        };
        skel.rest = {
            Asset::AnimationAssetRestBone{"Hips", -1, {0.0F, 1.0F, 0.0F}, {}, {1.0F, 1.0F, 1.0F}},
            Asset::AnimationAssetRestBone{"Spine", 0, {0.0F, 0.4F, 0.0F}, {}, {1.0F, 1.0F, 1.0F}},
        };
        skel.events = {Asset::AnimationAssetEvent{0.5, "footstep"}};
        ASSERT_TRUE(Asset::SaveAnimationClipAsset(skel, skelPath));
    }
    // 2. Object-transform clip (no bones).
    const std::filesystem::path objPath = content / "Clips" / "DoorSwing.uvanim";
    {
        Asset::AnimationClipAsset obj;
        obj.clipId = "door_swing";
        obj.durationSeconds = 0.75;
        const Asset::AnimationAssetPose p0{{0.0F, 0.0F, 0.0F}, {}, {1.0F, 1.0F, 1.0F}};
        const Asset::AnimationAssetPose p1{{2.0F, 0.0F, 0.0F}, {}, {1.0F, 1.0F, 1.0F}};
        obj.samples = {{0.0, p0}, {0.75, p1}};
        ASSERT_TRUE(Asset::SaveAnimationClipAsset(obj, objPath));
    }
    // 3. Facial morph-only clip (no bone or transform samples).
    const std::filesystem::path morphPath = content / "Clips" / "Blink.uvanim";
    {
        Asset::AnimationClipAsset morph;
        morph.clipId = "blink";
        morph.durationSeconds = 0.4;
        morph.morphWeights = {
            Asset::AnimationAssetMorphTrack{"Head", {{0.0, {0.0F, 0.0F}}, {0.2, {1.0F, 0.5F}}, {0.4, {0.0F, 0.0F}}}},
        };
        ASSERT_TRUE(Asset::SaveAnimationClipAsset(morph, morphPath));
    }
    // 4. Ephemeral clip that will be deleted after registration to simulate a missing file.
    const std::filesystem::path vanishedPath = content / "Clips" / "Vanished.uvanim";
    {
        Asset::AnimationClipAsset temp;
        temp.clipId = "vanished";
        temp.durationSeconds = 0.5;
        const Asset::AnimationAssetPose p0{{0.0F, 0.0F, 0.0F}, {}, {1.0F, 1.0F, 1.0F}};
        temp.samples = {{0.0, p0}, {0.5, p0}};
        ASSERT_TRUE(Asset::SaveAnimationClipAsset(temp, vanishedPath));
    }

    Core::EngineCore engine(MakeCatalogueEditorConfig(root));
    engine.Init();
    ASSERT_TRUE(engine.Load());
    {
        Editor editor(engine.GetServices(), root / "main.uvscene", 100U, &engine);
        editor.Init();
        Core::EngineServices& services = engine.GetServices();
        Scene::IEntityManager& em = services.GetEntityManager();
        ASSERT_TRUE(services.GetProjectFileIndex().Refresh(services.GetAssetDatabase()));

        const auto entityFile = editor.CreateContentCatalogueItem("character", content);
        ASSERT_TRUE(entityFile.has_value());
        ASSERT_TRUE(editor.OpenEntityEditor(*entityFile));

        Scene::Entity player = Scene::kInvalidEntity;
        {
            const Editor::EntityDocumentScope scope(editor);
            const Scene::Entity entityRoot = editor.GetEntityEditorRoot();
            ASSERT_NE(entityRoot, Scene::kInvalidEntity);
            player = editor.CreateDocumentSceneObject(Kind::AnimationSequencer);
            ASSERT_NE(player, Scene::kInvalidEntity);

            EXPECT_TRUE(editor.AddClipToAnimationSequencer(player, skelPath));
            EXPECT_TRUE(editor.AddClipToAnimationSequencer(player, objPath));
            EXPECT_TRUE(editor.AddClipToAnimationSequencer(player, morphPath));
            EXPECT_TRUE(editor.AddClipToAnimationSequencer(player, vanishedPath));
            std::filesystem::remove(vanishedPath);

            const auto& seq = em.GetComponent<Scene::AnimationSequencerComponent>(player);
            ASSERT_EQ(seq.library.size(), 4U);
            const std::vector<Asset::AssetGuid> guids = seq.library;

            // Rapidly cycle across all 4 clips while holding the previous frame's view to verify
            // clipOwner keeps outgoing clip memory alive across mid-frame resyncs.
            for (int pass = 0; pass < 3; ++pass) {
                for (std::size_t idx = 0U; idx < guids.size(); ++idx) {
                    editor.ChooseAnimationTimelineClip(player, guids[idx]);
                    const auto viewA = editor.SyncAnimationTimeline();
                    editor.StepAnimationTimeline(0.016);
                    const auto viewB = editor.SyncAnimationTimeline();
                    if (idx < 3U) {
                        ASSERT_NE(viewA.clip, nullptr);
                        ASSERT_NE(viewA.clipOwner, nullptr);
                        EXPECT_EQ(viewA.clip, viewA.clipOwner.get());
                        EXPECT_FALSE(viewA.clip->clipId.empty());
                        EXPECT_EQ(viewB.clip, viewA.clip);
                    } else {
                        EXPECT_EQ(viewA.clip, nullptr);
                    }
                }
            }
        }
        editor.Shutdown();
    }
    engine.Shutdown();
    std::filesystem::remove_all(root);
}

} // namespace
} // namespace UVE::Editor::Tests
