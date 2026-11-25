// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include <algorithm>
#include <array>

#include <filesystem>
#include <gtest/gtest.h>

#include "Support/test_scratch.h"

#include "engine_core.h"
#include "editor.h"
#include "animation_sequencer_component.h"
#include "audio_source_component.h"
#include "camera_component.h"
#include "collider_component.h"
#include "solid_body_component.h"
#include "character_controller_component.h"
#include "all_objects_3d.h"
#include "light_component.h"
#include "mesh_component.h"
#include "name_component.h"
#include "particle_emitter_component.h"
#include "rigid_3d_component.h"
#include "script_component.h"
#include "hierarchy_component.h"
#include "scene_folder.h"
#include "scene_object_registry.h"
#include "scene_object_type.h"
#include "scene_root.h"

namespace UVE::Editor::Tests {
namespace {

[[nodiscard]] Core::EngineConfig MakeSceneObjectEditorTestConfig() {
    Core::EngineConfig config{};
    config.headless = true;
    config.logFilePath = "uve_scene_object_editor_tests.log";
    config.settingsFilePath = "uve_scene_object_editor_tests_settings.json";
    config.assetDatabaseFilePath = "uve_scene_object_editor_tests_assets.json";
    config.saveDirectoryPath = "uve_scene_object_editor_tests_saves";
    config.shaderCachePath = "uve_scene_object_editor_tests_shader_cache";
    config.shaderSourceRealDirectory = ::UVE::Tests::RepositoryRoot() / "Engine/Runtime/RHI/Shader/built_in";
    config.shaderSourceMountPrefix = "shaders";
    return config;
}

TEST(SceneObjectEditorTest, CentralizedRegistryCreation_AttachesExpectedAuthoredComponents) {
    Core::EngineCore engine(MakeSceneObjectEditorTestConfig());
    engine.Init();
    ASSERT_TRUE(engine.Load());
    {
        Editor editor(engine.GetServices(), "uve_scene_object_editor_tests.uvscene");
        editor.Init();
        Scene::IEntityManager& entityManager = engine.GetServices().GetEntityManager();

        const Scene::Entity camera =
            editor.CreateDocumentSceneObject(Scene::Objects::SceneObjectKind::Camera3D);
        ASSERT_NE(camera, Scene::kInvalidEntity);
        EXPECT_TRUE(entityManager.HasComponent<Scene::CameraComponent>(camera));

        const Scene::Entity mesh =
            editor.CreateDocumentSceneObject(Scene::Objects::SceneObjectKind::MeshInstance3D);
        ASSERT_NE(mesh, Scene::kInvalidEntity);
        EXPECT_TRUE(entityManager.HasComponent<Scene::MeshComponent>(mesh));

        const Scene::Entity character =
            editor.CreateDocumentSceneObject(Scene::Objects::SceneObjectKind::Character3D);
        ASSERT_NE(character, Scene::kInvalidEntity);
        // A Character3D is a controller and no shape: the collider is the author's to add, so
        // adding one is the body's first shape instead of a second one in the same place.
        EXPECT_FALSE(entityManager.HasComponent<Scene::ColliderComponent>(character));
        EXPECT_TRUE(entityManager.HasComponent<Scene::CharacterControllerComponent>(character));
        // The controller owns all of its motion: no rigid body for gravity to fight over.
        EXPECT_FALSE(entityManager.HasComponent<Scene::Rigid3DComponent>(character));

        const Scene::Entity player =
            editor.CreateDocumentSceneObject(Scene::Objects::SceneObjectKind::Player3D);
        ASSERT_NE(player, Scene::kInvalidEntity);
        EXPECT_TRUE(entityManager.HasComponent<Scene::PlayerComponent>(player));
        EXPECT_TRUE(entityManager.HasComponent<Scene::CharacterControllerComponent>(player));
        EXPECT_EQ(Scene::ResolveSceneObjectKind(entityManager, player), Scene::Objects::SceneObjectKind::Player3D);

        const Scene::Entity animationPlayer =
            editor.CreateDocumentSceneObject(Scene::Objects::SceneObjectKind::AnimationSequencer);
        ASSERT_NE(animationPlayer, Scene::kInvalidEntity);
        EXPECT_TRUE(entityManager.HasComponent<Scene::AnimationSequencerComponent>(animationPlayer));

        const Scene::Entity audio =
            editor.CreateDocumentSceneObject(Scene::Objects::SceneObjectKind::AudioSource3D);
        ASSERT_NE(audio, Scene::kInvalidEntity);
        EXPECT_TRUE(entityManager.HasComponent<Scene::AudioSourceComponent>(audio));

        const Scene::Entity particles =
            editor.CreateDocumentSceneObject(Scene::Objects::SceneObjectKind::ParticleEmitter3D);
        ASSERT_NE(particles, Scene::kInvalidEntity);
        EXPECT_TRUE(entityManager.HasComponent<Scene::ParticleEmitterComponent>(particles));

        const Scene::Entity script =
            editor.CreateDocumentSceneObject(Scene::Objects::SceneObjectKind::Script);
        ASSERT_NE(script, Scene::kInvalidEntity);
        EXPECT_TRUE(entityManager.HasComponent<Scene::ScriptComponent>(script));

        const Scene::Entity rigidBody =
            editor.CreateDocumentSceneObject(Scene::Objects::SceneObjectKind::Rigid3D);
        ASSERT_NE(rigidBody, Scene::kInvalidEntity);
        EXPECT_TRUE(entityManager.HasComponent<Scene::Rigid3DComponent>(rigidBody));

        const Scene::Entity animationTree =
            editor.CreateDocumentSceneObject(Scene::Objects::SceneObjectKind::AnimationGraph);
        ASSERT_NE(animationTree, Scene::kInvalidEntity);
        EXPECT_TRUE(entityManager.HasComponent<Scene::AnimationGraphComponent>(animationTree));
        // Both animation objects are pure Objects: no transform of their own.
        EXPECT_FALSE(entityManager.HasComponent<Scene::TransformComponent>(animationTree));
        EXPECT_FALSE(entityManager.HasComponent<Scene::TransformComponent>(animationPlayer));
        editor.Shutdown();
    }
    engine.Shutdown();
}

TEST(SceneObjectEditorTest, CentralizedCreation_CreatesEveryExpandedObjectKind) {
    Core::EngineCore engine(MakeSceneObjectEditorTestConfig());
    engine.Init();
    ASSERT_TRUE(engine.Load());
    {
        Editor editor(engine.GetServices(), "uve_scene_object_editor_expanded_tests.uvscene");
        editor.Init();
        Scene::IEntityManager& entityManager = engine.GetServices().GetEntityManager();
        constexpr std::array<Scene::Objects::SceneObjectKind, 27U> expandedKinds{
            Scene::Objects::SceneObjectKind::Area3D,
            Scene::Objects::SceneObjectKind::RayCast3D,
            Scene::Objects::SceneObjectKind::Static3D,
            Scene::Objects::SceneObjectKind::Kinematic3D,
            Scene::Objects::SceneObjectKind::NavMeshVolume3D,
            Scene::Objects::SceneObjectKind::NavSeeker3D,
            Scene::Objects::SceneObjectKind::Skeleton3D,
            Scene::Objects::SceneObjectKind::BoneAttachment3D,
            Scene::Objects::SceneObjectKind::SpringArm3D,
            Scene::Objects::SceneObjectKind::Marker3D,
            Scene::Objects::SceneObjectKind::Hitbox3D,
            Scene::Objects::SceneObjectKind::Hurtbox3D,
            Scene::Objects::SceneObjectKind::Projectile3D,
            Scene::Objects::SceneObjectKind::InteractionArea3D,
            Scene::Objects::SceneObjectKind::ReflectionProbe3D,
            Scene::Objects::SceneObjectKind::Decal3D,
            Scene::Objects::SceneObjectKind::LODGroup3D,
            Scene::Objects::SceneObjectKind::Occluder3D,
            Scene::Objects::SceneObjectKind::VisibilityRegion3D,
            Scene::Objects::SceneObjectKind::SpawnPoint3D,
            Scene::Objects::SceneObjectKind::LevelStreamer3D,
            Scene::Objects::SceneObjectKind::WorldPartition3D,
            Scene::Objects::SceneObjectKind::Canvas,
            Scene::Objects::SceneObjectKind::UIText,
            Scene::Objects::SceneObjectKind::UIImage,
            Scene::Objects::SceneObjectKind::UIButton,
        };
        for (const Scene::Objects::SceneObjectKind kind : expandedKinds) {
            const Scene::Entity entity = editor.CreateDocumentSceneObject(kind);
            ASSERT_NE(entity, Scene::kInvalidEntity);
            EXPECT_TRUE(entityManager.IsAlive(entity));
            EXPECT_TRUE(entityManager.HasComponent<Scene::TransformComponent>(entity));
            EXPECT_TRUE(entityManager.HasComponent<Scene::NameComponent>(entity));
            EXPECT_GE(entityManager.GetComponentTypes(entity).size(), 4U);
        }
        editor.Shutdown();
    }
    engine.Shutdown();
}

TEST(SceneObjectEditorTest, CharacterBodyCreation_IsOneAtomicUndoRedoTransaction) {
    Core::EngineCore engine(MakeSceneObjectEditorTestConfig());
    engine.Init();
    ASSERT_TRUE(engine.Load());
    {
        Editor editor(engine.GetServices(), "uve_scene_object_editor_history_tests.uvscene");
        editor.Init();
        Scene::IEntityManager& entityManager = engine.GetServices().GetEntityManager();

        const Scene::Entity created =
            editor.CreateDocumentSceneObject(Scene::Objects::SceneObjectKind::Character3D);
        ASSERT_NE(created, Scene::kInvalidEntity);
        ASSERT_TRUE(editor.CanUndo());
        EXPECT_TRUE(editor.IsSceneDirty());

        ASSERT_TRUE(editor.Undo());
        EXPECT_FALSE(entityManager.IsAlive(created));
        EXPECT_EQ(editor.GetSelectedEntity(), Scene::kInvalidEntity);
        EXPECT_FALSE(editor.IsSceneDirty());
        EXPECT_TRUE(editor.CanRedo());

        ASSERT_TRUE(editor.Redo());
        const Scene::Entity restored = editor.GetSelectedEntity();
        ASSERT_NE(restored, Scene::kInvalidEntity);
        EXPECT_NE(restored, created);
        // Redo rebuilds the same object the creation built: a controller and no collider.
        EXPECT_FALSE(entityManager.HasComponent<Scene::ColliderComponent>(restored));
        EXPECT_TRUE(entityManager.HasComponent<Scene::CharacterControllerComponent>(restored));
        EXPECT_TRUE(entityManager.HasComponent<Scene::SolidBodyComponent>(restored));
        EXPECT_TRUE(editor.IsSceneDirty());
        editor.Shutdown();
    }
    engine.Shutdown();
}

TEST(SceneObjectEditorTest, CentralizedCreation_RejectsMultiSelectionAndPlayModeWithoutMutation) {
    Core::EngineCore engine(MakeSceneObjectEditorTestConfig());
    engine.Init();
    ASSERT_TRUE(engine.Load());
    {
        Editor editor(engine.GetServices(), "uve_scene_object_editor_safety_tests.uvscene", 100U, &engine);
        editor.Init();
        const Scene::Entity first =
            editor.CreateDocumentSceneObject(Scene::Objects::SceneObjectKind::Object3D);
        const Scene::Entity second =
            editor.CreateDocumentSceneObject(Scene::Objects::SceneObjectKind::Object3D);
        ASSERT_NE(first, Scene::kInvalidEntity);
        ASSERT_NE(second, Scene::kInvalidEntity);
        editor.SelectEntity(first);
        editor.ToggleEntitySelection(second);
        ASSERT_FALSE(editor.HasSingleDocumentSelection());
        const std::vector<Scene::Entity> rootsBefore = editor.GetDocumentRoots();
        EXPECT_EQ(editor.CreateDocumentSceneObject(Scene::Objects::SceneObjectKind::Camera3D),
                  Scene::kInvalidEntity);
        EXPECT_EQ(editor.GetDocumentRoots(), rootsBefore);

        ASSERT_TRUE(editor.EnterPlayMode());
        EXPECT_EQ(editor.CreateDocumentSceneObject(Scene::Objects::SceneObjectKind::Light3D),
                  Scene::kInvalidEntity);
        ASSERT_TRUE(editor.StopPlayMode());
        EXPECT_EQ(editor.GetDocumentRoots().size(), rootsBefore.size());
        editor.Shutdown();
    }
    engine.Shutdown();
}

[[nodiscard]] bool ContainsEntity(const std::vector<Scene::Entity>& entities,
                                    const Scene::Entity entity) {
    return std::find(entities.begin(), entities.end(), entity) != entities.end();
}

TEST(SceneObjectEditorTest, SceneRoot_FreshDocumentHasExactlyOneNamedRoot) {
    Core::EngineCore engine(MakeSceneObjectEditorTestConfig());
    engine.Init();
    ASSERT_TRUE(engine.Load());
    {
        Editor editor(engine.GetServices(), "uve_scene_object_editor_scene_root_tests.uvscene");
        editor.Init();
        Scene::IEntityManager& entityManager = engine.GetServices().GetEntityManager();

        const Scene::Entity root = editor.GetDocumentSceneRoot();
        ASSERT_NE(root, Scene::kInvalidEntity);
        EXPECT_TRUE(entityManager.HasComponent<Scene::SceneRootComponent>(root));
        EXPECT_EQ(entityManager.GetComponent<Scene::NameComponent>(root).name, "SceneRoot");
        EXPECT_EQ(editor.GetDocumentRoots().size(), 1U);
        EXPECT_EQ(editor.GetDocumentRoots()[0U], root);

        // The root is a library kind but never library-creatable: the Add-Object path refuses it.
        EXPECT_EQ(editor.CreateDocumentSceneObject(Scene::Objects::SceneObjectKind::SceneRoot),
                  Scene::kInvalidEntity);

        editor.Shutdown();
    }
    engine.Shutdown();
}

TEST(SceneObjectEditorTest, SceneRoot_NewObjectsJoinHierarchyUnderSelectionOrRoot) {
    Core::EngineCore engine(MakeSceneObjectEditorTestConfig());
    engine.Init();
    ASSERT_TRUE(engine.Load());
    {
        Editor editor(engine.GetServices(), "uve_scene_object_editor_scene_root_join_tests.uvscene");
        editor.Init();
        Scene::IEntityManager& entityManager = engine.GetServices().GetEntityManager();
        Scene::ISceneGraph& sceneGraph = engine.GetServices().GetSceneGraph();
        const Scene::Entity root = editor.GetDocumentSceneRoot();
        ASSERT_NE(root, Scene::kInvalidEntity);

        // No selection: a new object joins the level's object folder (a level object always lives in a
        // folder inside the Viewport), not a new document root.
        const Scene::Entity first =
            editor.CreateDocumentSceneObject(Scene::Objects::SceneObjectKind::Object3D);
        ASSERT_NE(first, Scene::kInvalidEntity);
        EXPECT_EQ(editor.GetDocumentRoots().size(), 1U);
        const Scene::Entity folder = entityManager.GetComponent<Scene::HierarchyComponent>(first).parent;
        ASSERT_TRUE(entityManager.HasComponent<Scene::FolderComponent>(folder));
        EXPECT_EQ(entityManager.GetComponent<Scene::NameComponent>(folder).name, "World");
        const Scene::Entity viewport = entityManager.GetComponent<Scene::HierarchyComponent>(folder).parent;
        EXPECT_TRUE(entityManager.HasComponent<Scene::OutlinerViewport3DComponent>(viewport));
        EXPECT_EQ(sceneGraph.GetChildren(entityManager, root), std::vector<Scene::Entity>{viewport});

        // With a single selection: the new object becomes that selection's child.
        editor.SelectEntity(first);
        const Scene::Entity second =
            editor.CreateDocumentSceneObject(Scene::Objects::SceneObjectKind::Camera3D);
        ASSERT_NE(second, Scene::kInvalidEntity);
        EXPECT_EQ(editor.GetDocumentRoots().size(), 1U);
        EXPECT_TRUE(ContainsEntity(sceneGraph.GetChildren(entityManager, first), second));
        EXPECT_FALSE(ContainsEntity(sceneGraph.GetChildren(entityManager, folder), second));

        editor.Shutdown();
    }
    engine.Shutdown();
}

TEST(SceneObjectEditorTest, OutlinerLayout_LevelTopIsViewportSunAndEnvironmentWithObjectsOnlyInFolders) {
    using Kind = Scene::Objects::SceneObjectKind;
    Core::EngineCore engine(MakeSceneObjectEditorTestConfig());
    engine.Init();
    ASSERT_TRUE(engine.Load());
    {
        Editor editor(engine.GetServices(), "uve_scene_object_editor_outliner_layout_tests.uvscene");
        editor.Init();
        Scene::IEntityManager& entityManager = engine.GetServices().GetEntityManager();
        Scene::ISceneGraph& sceneGraph = engine.GetServices().GetSceneGraph();
        const auto parentOf = [&entityManager](const Scene::Entity entity) {
            return entityManager.GetComponent<Scene::HierarchyComponent>(entity).parent;
        };
        const Scene::Entity root = editor.GetDocumentSceneRoot();

        // A fresh level: the Viewport at the top, holding nothing, and nothing to undo or save.
        // It used to hold an empty "World" folder made for it at open; a folder now appears only
        // when an object is placed and needs somewhere to live.
        const std::vector<Scene::Entity> top = sceneGraph.GetChildren(entityManager, root);
        ASSERT_EQ(top.size(), 1U);
        const Scene::Entity viewport = top[0U];
        EXPECT_TRUE(entityManager.HasComponent<Scene::OutlinerViewport3DComponent>(viewport));
        EXPECT_EQ(entityManager.GetComponent<Scene::NameComponent>(viewport).name, "uve_scene_object_editor_outliner_layout_tests"); // its asset's name
        EXPECT_TRUE(sceneGraph.GetChildren(entityManager, viewport).empty());
        EXPECT_FALSE(editor.IsSceneDirty());
        EXPECT_FALSE(editor.CanUndo());

        // The sun and the environment sit beside the Viewport, one of each, in the order added.
        const Scene::Entity sun = editor.CreateDocumentSceneObject(Kind::DirectionalLight3D);
        ASSERT_NE(sun, Scene::kInvalidEntity);
        const Scene::Entity environment = editor.CreateDocumentSceneObject(Kind::WorldEnvironment3D);
        ASSERT_NE(environment, Scene::kInvalidEntity);
        EXPECT_EQ(sceneGraph.GetChildren(entityManager, root),
                  (std::vector<Scene::Entity>{viewport, sun, environment}));
        EXPECT_EQ(editor.CreateDocumentSceneObject(Kind::DirectionalLight3D), Scene::kInvalidEntity);
        EXPECT_EQ(editor.CreateDocumentSceneObject(Kind::WorldEnvironment3D), Scene::kInvalidEntity);
        // Undoing one frees its place again.
        ASSERT_TRUE(editor.Undo());
        EXPECT_FALSE(entityManager.IsAlive(environment));
        ASSERT_NE(editor.CreateDocumentSceneObject(Kind::WorldEnvironment3D), Scene::kInvalidEntity);

        // Any other object goes into a folder, even with the sun selected - and that placement is
        // what makes the level's folder: it is created for the object, not left waiting empty.
        editor.SelectEntity(sun);
        const Scene::Entity mesh = editor.CreateDocumentSceneObject(Kind::BoxMesh3D);
        ASSERT_NE(mesh, Scene::kInvalidEntity);
        const Scene::Entity world = parentOf(mesh);
        ASSERT_NE(world, Scene::kInvalidEntity);
        EXPECT_TRUE(entityManager.HasComponent<Scene::FolderComponent>(world));
        EXPECT_EQ(entityManager.GetComponent<Scene::NameComponent>(world).name, "World");
        EXPECT_EQ(parentOf(world), viewport);
        // A new folder with nothing selected goes into the Viewport.
        editor.ClearSelection();
        const Scene::Entity props = editor.CreateDocumentSceneObject(Kind::Folder);
        ASSERT_NE(props, Scene::kInvalidEntity);
        EXPECT_EQ(parentOf(props), viewport);

        // Objects never leave the folders: not to the top, not straight into the Viewport.
        editor.SelectEntity(mesh);
        EXPECT_FALSE(editor.ReparentSelectedEntity(root));
        EXPECT_FALSE(editor.ReparentSelectedEntity(viewport));
        EXPECT_FALSE(editor.ReparentSelectedEntity(sun));
        EXPECT_TRUE(editor.ReparentSelectedEntity(props));
        EXPECT_EQ(parentOf(mesh), props);
        // "Move to top" keeps an object in a folder, and a folder in the Viewport.
        EXPECT_TRUE(editor.ReparentSelectedEntity(Scene::kInvalidEntity));
        EXPECT_TRUE(entityManager.HasComponent<Scene::FolderComponent>(parentOf(mesh)));
        editor.SelectEntity(props);
        EXPECT_FALSE(editor.ReparentSelectedEntity(root));
        EXPECT_TRUE(editor.ReparentSelectedEntity(world)); // a folder may nest in a folder
        EXPECT_TRUE(editor.ReparentSelectedEntity(Scene::kInvalidEntity));
        EXPECT_EQ(parentOf(props), viewport);

        // The Viewport is structure: it cannot be removed, copied or moved.
        editor.SelectEntity(viewport);
        EXPECT_FALSE(editor.DeleteSelectedEntity());
        EXPECT_EQ(editor.DuplicateSelectedEntity(), Scene::kInvalidEntity);
        EXPECT_FALSE(editor.ReparentSelectedEntity(world));
        EXPECT_TRUE(entityManager.IsAlive(viewport));

        // The layout survives a save and load as it is: no second Viewport or folder appears.
        ASSERT_TRUE(editor.SaveScene());
        ASSERT_TRUE(editor.LoadScene());
        EXPECT_FALSE(editor.IsSceneDirty());
        const std::vector<Scene::Entity> loadedTop =
            sceneGraph.GetChildren(entityManager, editor.GetDocumentSceneRoot());
        ASSERT_EQ(loadedTop.size(), 3U);
        EXPECT_TRUE(entityManager.HasComponent<Scene::OutlinerViewport3DComponent>(loadedTop[0U]));
        EXPECT_EQ(Scene::ResolveSceneObjectKind(entityManager, loadedTop[1U]), Kind::DirectionalLight3D);
        EXPECT_EQ(Scene::ResolveSceneObjectKind(entityManager, loadedTop[2U]), Kind::WorldEnvironment3D);
        EXPECT_EQ(sceneGraph.GetChildren(entityManager, loadedTop[0U]).size(), 2U);

        editor.Shutdown();
        std::filesystem::remove("uve_scene_object_editor_outliner_layout_tests.uvscene");
    }
    engine.Shutdown();
}

TEST(SceneObjectEditorTest, SceneRoot_RootCannotBeDeletedReparentedOrDuplicated) {
    Core::EngineCore engine(MakeSceneObjectEditorTestConfig());
    engine.Init();
    ASSERT_TRUE(engine.Load());
    {
        Editor editor(engine.GetServices(), "uve_scene_object_editor_scene_root_guard_tests.uvscene");
        editor.Init();
        Scene::IEntityManager& entityManager = engine.GetServices().GetEntityManager();

        const Scene::Entity root = editor.GetDocumentSceneRoot();
        ASSERT_NE(root, Scene::kInvalidEntity);
        const Scene::Entity other =
            editor.CreateDocumentSceneObject(Scene::Objects::SceneObjectKind::Object3D);
        ASSERT_NE(other, Scene::kInvalidEntity);

        editor.SelectEntity(root);
        EXPECT_FALSE(editor.DeleteSelectedEntity());
        EXPECT_EQ(editor.DuplicateSelectedEntity(), Scene::kInvalidEntity);
        EXPECT_FALSE(editor.ReparentSelectedEntity(other));
        EXPECT_TRUE(entityManager.IsAlive(root));
        EXPECT_TRUE(entityManager.HasComponent<Scene::SceneRootComponent>(root));
        EXPECT_EQ(editor.GetDocumentRoots().size(), 1U);
        EXPECT_EQ(editor.GetDocumentRoots()[0U], root);

        editor.Shutdown();
    }
    engine.Shutdown();
}

TEST(SceneObjectEditorTest, SceneRoot_UndoRedoKeepsCreatedObjectUnderItsParent) {
    Core::EngineCore engine(MakeSceneObjectEditorTestConfig());
    engine.Init();
    ASSERT_TRUE(engine.Load());
    {
        Editor editor(engine.GetServices(), "uve_scene_object_editor_scene_root_history_tests.uvscene");
        editor.Init();
        Scene::IEntityManager& entityManager = engine.GetServices().GetEntityManager();
        Scene::ISceneGraph& sceneGraph = engine.GetServices().GetSceneGraph();
        const Scene::Entity root = editor.GetDocumentSceneRoot();
        ASSERT_NE(root, Scene::kInvalidEntity);

        const Scene::Entity parent =
            editor.CreateDocumentSceneObject(Scene::Objects::SceneObjectKind::Object3D);
        ASSERT_NE(parent, Scene::kInvalidEntity);
        editor.SelectEntity(parent);
        const Scene::Entity child =
            editor.CreateDocumentSceneObject(Scene::Objects::SceneObjectKind::Light3D);
        ASSERT_NE(child, Scene::kInvalidEntity);
        ASSERT_TRUE(ContainsEntity(sceneGraph.GetChildren(entityManager, parent), child));

        ASSERT_TRUE(editor.Undo());
        EXPECT_FALSE(entityManager.IsAlive(child));
        ASSERT_TRUE(editor.Redo());
        const Scene::Entity restored = editor.GetSelectedEntity();
        ASSERT_NE(restored, Scene::kInvalidEntity);
        // Redo must re-home the object under its ORIGINAL parent, not drop it to document top
        // level now that documents have a single scene root.
        EXPECT_TRUE(ContainsEntity(sceneGraph.GetChildren(entityManager, parent), restored));
        EXPECT_EQ(editor.GetDocumentRoots().size(), 1U);

        editor.Shutdown();
    }
    engine.Shutdown();
}

TEST(SceneObjectEditorTest, SceneRoot_LegacyMultiRootSceneFileAutoMigratesUnderOneRoot) {
    Core::EngineCore engine(MakeSceneObjectEditorTestConfig());
    engine.Init();
    ASSERT_TRUE(engine.Load());
    {
        Scene::IEntityManager& entityManager = engine.GetServices().GetEntityManager();
        Scene::ISceneGraph& sceneGraph = engine.GetServices().GetSceneGraph();

        // Author a legacy-style file by hand: two top-level entities, NO scene-root marker -
        // exactly what every .uvscene saved before the root existed looks like.
        const Scene::Entity legacyA = entityManager.CreateEntity();
        sceneGraph.AttachTransform(entityManager, legacyA, Scene::TransformComponent{});
        entityManager.AddComponent<Scene::NameComponent>(legacyA, Scene::NameComponent{"LegacyA"});
        const Scene::Entity legacyB = entityManager.CreateEntity();
        sceneGraph.AttachTransform(entityManager, legacyB, Scene::TransformComponent{});
        entityManager.AddComponent<Scene::NameComponent>(legacyB, Scene::NameComponent{"LegacyB"});

        const std::string path = "uve_scene_object_editor_scene_root_migrate_tests.uvscene";
        std::filesystem::remove(path);
        ASSERT_TRUE(engine.GetServices().GetSceneSerializer().Save(
            entityManager, {legacyA, legacyB}, path, Asset::AssetKind::Scene));

        {
            Editor editor(engine.GetServices(), path);
            editor.Init();
            ASSERT_TRUE(editor.LoadScene());

            // The loaded document must hold the one-root invariant: a single SceneRoot at the
            // top with both legacy top-level entities re-parented under it.
            const Scene::Entity root = editor.GetDocumentSceneRoot();
            ASSERT_NE(root, Scene::kInvalidEntity);
            EXPECT_EQ(editor.GetDocumentRoots().size(), 1U);
            EXPECT_EQ(editor.GetDocumentRoots()[0U], root);
            EXPECT_EQ(entityManager.GetComponent<Scene::NameComponent>(root).name, "SceneRoot");
            // The restored entities are FRESH handles (load recreates entities from the
            // file), so identity is checked by name, not by handle.
            // Then the Outliner layout gave it a Viewport, and moved them into its object folder.
            const std::vector<Scene::Entity> topRows = sceneGraph.GetChildren(entityManager, root);
            ASSERT_EQ(topRows.size(), 1U);
            EXPECT_TRUE(entityManager.HasComponent<Scene::OutlinerViewport3DComponent>(topRows[0U]));
            const std::vector<Scene::Entity> folders = sceneGraph.GetChildren(entityManager, topRows[0U]);
            ASSERT_EQ(folders.size(), 1U);
            const std::vector<Scene::Entity> rootChildren = sceneGraph.GetChildren(entityManager, folders[0U]);
            ASSERT_EQ(rootChildren.size(), 2U);
            std::size_t matchedNames = 0U;
            for (const Scene::Entity child : rootChildren) {
                const std::string& name =
                    entityManager.GetComponent<Scene::NameComponent>(child).name;
                if (name == "LegacyA" || name == "LegacyB") {
                    ++matchedNames;
                }
            }
            EXPECT_EQ(matchedNames, 2U);
            // Migration is a real document change: the in-memory doc no longer matches the file.
            EXPECT_TRUE(editor.IsSceneDirty());

            editor.Shutdown();
        }
        std::filesystem::remove(path);
    }
    engine.Shutdown();
}

} // namespace
TEST(SceneObjectEditorTest, SceneRoot_FileCarryingTwoRootMarkersStillLoadsWithOneRoot) {
    // The migration test above covers a LEGACY file - no marker at all. This covers the other
    // direction: a file that carries two SceneRoot markers. That is not a file the editor can
    // currently produce, but .uvscene is plain JSON on disk, so it is a file the editor can be
    // handed - by a merge conflict resolved badly, a hand-edit, or a future tool.
    //
    // It matters more than it looks. The editor forbids deleting or reparenting a scene root, so
    // a second root that survives load is not a cosmetic wart: it is an entity the user is
    // structurally unable to remove.
    Core::EngineCore engine(MakeSceneObjectEditorTestConfig());
    engine.Init();
    ASSERT_TRUE(engine.Load());
    {
        Scene::IEntityManager& entityManager = engine.GetServices().GetEntityManager();
        Scene::ISceneGraph& sceneGraph = engine.GetServices().GetSceneGraph();

        // Two independently-marked roots, as a badly merged file would hold.
        const Scene::Entity firstRoot = entityManager.CreateEntity();
        sceneGraph.AttachTransform(entityManager, firstRoot, Scene::TransformComponent{});
        entityManager.AddComponent<Scene::NameComponent>(firstRoot, Scene::NameComponent{"SceneRoot"});
        entityManager.AddComponent<Scene::SceneRootComponent>(firstRoot, Scene::SceneRootComponent{});
        const Scene::Entity secondRoot = entityManager.CreateEntity();
        sceneGraph.AttachTransform(entityManager, secondRoot, Scene::TransformComponent{});
        entityManager.AddComponent<Scene::NameComponent>(secondRoot, Scene::NameComponent{"SurplusRoot"});
        entityManager.AddComponent<Scene::SceneRootComponent>(secondRoot, Scene::SceneRootComponent{});
        // The surplus root owns authored content. Repairing the structure must not discard it -
        // that would be trading the user's work for tidiness.
        const Scene::Entity surplusChild = entityManager.CreateEntity();
        sceneGraph.AttachTransform(entityManager, surplusChild, Scene::TransformComponent{});
        entityManager.AddComponent<Scene::NameComponent>(surplusChild, Scene::NameComponent{"KeepMe"});
        sceneGraph.SetParent(entityManager, surplusChild, secondRoot);

        const std::string path = "uve_scene_object_editor_two_root_markers_tests.uvscene";
        std::filesystem::remove(path);
        ASSERT_TRUE(engine.GetServices().GetSceneSerializer().Save(
            entityManager, {firstRoot, secondRoot}, path, Asset::AssetKind::Scene));

        {
            Editor editor(engine.GetServices(), path);
            editor.Init();
            ASSERT_TRUE(editor.LoadScene());

            // Exactly one entity may carry the marker afterwards. Counted directly rather than
            // through GetDocumentSceneRoot, which returns the first hit and would report
            // success even with a second marker buried in the hierarchy.
            std::size_t markerCount = 0U;
            entityManager.ForEach<Scene::SceneRootComponent>(
                [&markerCount](const Scene::Entity, const Scene::SceneRootComponent&) { ++markerCount; });
            EXPECT_EQ(markerCount, 1U)
                << "a second scene-root marker survived load - the editor refuses to delete or "
                   "reparent a root, so the user cannot remove it by hand";

            EXPECT_EQ(editor.GetDocumentRoots().size(), 1U);

            // The demoted root and its child both survive, as ordinary objects. Names are used for
            // identity because loading recreates entities with fresh handles.
            bool foundDemoted = false;
            bool foundGrandchild = false;
            entityManager.ForEach<Scene::NameComponent>(
                [&](const Scene::Entity, const Scene::NameComponent& nameComponent) {
                    if (nameComponent.name == "SurplusRoot") {
                        foundDemoted = true;
                    }
                    if (nameComponent.name == "KeepMe") {
                        foundGrandchild = true;
                    }
                });
            EXPECT_TRUE(foundDemoted) << "the surplus root must be demoted, not destroyed";
            EXPECT_TRUE(foundGrandchild) << "authored content under the surplus root must survive";

            // Repairing the file is a real document change, so the in-memory doc no longer
            // matches its bytes - same contract the legacy-migration path holds.
            EXPECT_TRUE(editor.IsSceneDirty());
            editor.Shutdown();
        }
        std::filesystem::remove(path);
    }
    engine.Shutdown();
}

} // namespace UVE::Editor::Tests
