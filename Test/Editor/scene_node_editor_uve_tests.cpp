// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include <algorithm>
#include <array>

#include <filesystem>
#include <gtest/gtest.h>

#include "Support/test_scratch_uve.h"

#include "uve/core/engine_core_uve.h"
#include "uve/editor/editor_uve.h"
#include "uve/component/animation_player_component_uve.h"
#include "uve/component/audio_source_component_uve.h"
#include "uve/component/camera_component_uve.h"
#include "uve/component/collider_component_uve.h"
#include "uve/component/solid_body_component_uve.h"
#include "uve/component/character_controller_component_uve.h"
#include "uve/nodes/3d/all_nodes_3d_uve.h"
#include "uve/component/light_component_uve.h"
#include "uve/component/mesh_component_uve.h"
#include "uve/component/name_component_uve.h"
#include "uve/component/particle_emitter_component_uve.h"
#include "uve/component/rigid_body_component_uve.h"
#include "uve/component/script_component_uve.h"
#include "uve/component/hierarchy_component_uve.h"
#include "uve/scene/nodes/scene_folder_uve.h"
#include "uve/scene/nodes/scene_node_registry_uve.h"
#include "uve/scene/nodes/scene_node_type_uve.h"
#include "uve/scene/nodes/scene_root_uve.h"

namespace UVE::Editor::Tests {
namespace {

[[nodiscard]] Core::EngineConfigUVE MakeSceneNodeEditorTestConfigUVE() {
    Core::EngineConfigUVE config{};
    config.headlessUVE = true;
    config.logFilePath = "uve_scene_node_editor_tests.log";
    config.settingsFilePath = "uve_scene_node_editor_tests_settings.json";
    config.assetDatabaseFilePath = "uve_scene_node_editor_tests_assets.json";
    config.saveDirectoryPath = "uve_scene_node_editor_tests_saves";
    config.shaderCachePath = "uve_scene_node_editor_tests_shader_cache";
    config.shaderSourceRealDirectoryUVE = ::UVE::Tests::RepositoryRootUVE() / "Engine/Runtime/RHI/Shader/built_in";
    config.shaderSourceMountPrefixUVE = "shaders";
    return config;
}

TEST(SceneNodeEditorUVETest, CentralizedRegistryCreationUVE_AttachesExpectedAuthoredComponents) {
    Core::EngineCoreUVE engine(MakeSceneNodeEditorTestConfigUVE());
    engine.Init();
    ASSERT_TRUE(engine.Load());
    {
        EditorUVE editor(engine.GetServicesUVE(), "uve_scene_node_editor_tests.uvscene");
        editor.InitUVE();
        Scene::IEntityManagerUVE& entityManager = engine.GetServicesUVE().GetEntityManagerUVE();

        const Scene::EntityUVE camera =
            editor.CreateDocumentSceneNodeUVE(Scene::Nodes::SceneNodeKindUVE::Camera3D);
        ASSERT_NE(camera, Scene::kInvalidEntityUVE);
        EXPECT_TRUE(entityManager.HasComponentUVE<Scene::CameraComponentUVE>(camera));

        const Scene::EntityUVE mesh =
            editor.CreateDocumentSceneNodeUVE(Scene::Nodes::SceneNodeKindUVE::MeshInstance3D);
        ASSERT_NE(mesh, Scene::kInvalidEntityUVE);
        EXPECT_TRUE(entityManager.HasComponentUVE<Scene::MeshComponentUVE>(mesh));

        const Scene::EntityUVE character =
            editor.CreateDocumentSceneNodeUVE(Scene::Nodes::SceneNodeKindUVE::Character3D);
        ASSERT_NE(character, Scene::kInvalidEntityUVE);
        EXPECT_TRUE(entityManager.HasComponentUVE<Scene::ColliderComponentUVE>(character));
        EXPECT_TRUE(entityManager.HasComponentUVE<Scene::CharacterControllerComponentUVE>(character));
        // The controller owns all of its motion: no rigid body for gravity to fight over.
        EXPECT_FALSE(entityManager.HasComponentUVE<Scene::RigidBodyComponentUVE>(character));

        const Scene::EntityUVE animationPlayer =
            editor.CreateDocumentSceneNodeUVE(Scene::Nodes::SceneNodeKindUVE::AnimationSequencer);
        ASSERT_NE(animationPlayer, Scene::kInvalidEntityUVE);
        EXPECT_TRUE(entityManager.HasComponentUVE<Scene::AnimationPlayerComponentUVE>(animationPlayer));

        const Scene::EntityUVE audio =
            editor.CreateDocumentSceneNodeUVE(Scene::Nodes::SceneNodeKindUVE::AudioSource3D);
        ASSERT_NE(audio, Scene::kInvalidEntityUVE);
        EXPECT_TRUE(entityManager.HasComponentUVE<Scene::AudioSourceComponentUVE>(audio));

        const Scene::EntityUVE particles =
            editor.CreateDocumentSceneNodeUVE(Scene::Nodes::SceneNodeKindUVE::ParticleEmitter3D);
        ASSERT_NE(particles, Scene::kInvalidEntityUVE);
        EXPECT_TRUE(entityManager.HasComponentUVE<Scene::ParticleEmitterComponentUVE>(particles));

        const Scene::EntityUVE script =
            editor.CreateDocumentSceneNodeUVE(Scene::Nodes::SceneNodeKindUVE::Script);
        ASSERT_NE(script, Scene::kInvalidEntityUVE);
        EXPECT_TRUE(entityManager.HasComponentUVE<Scene::ScriptComponentUVE>(script));

        const Scene::EntityUVE rigidBody =
            editor.CreateDocumentSceneNodeUVE(Scene::Nodes::SceneNodeKindUVE::Rigid3D);
        ASSERT_NE(rigidBody, Scene::kInvalidEntityUVE);
        EXPECT_TRUE(entityManager.HasComponentUVE<Scene::RigidBodyComponentUVE>(rigidBody));

        const Scene::EntityUVE animationTree =
            editor.CreateDocumentSceneNodeUVE(Scene::Nodes::SceneNodeKindUVE::AnimationGraph);
        ASSERT_NE(animationTree, Scene::kInvalidEntityUVE);
        EXPECT_TRUE(entityManager.HasComponentUVE<Scene::AnimationTreeComponentUVE>(animationTree));
        // Both animation nodes are pure Nodes: no transform of their own.
        EXPECT_FALSE(entityManager.HasComponentUVE<Scene::TransformComponentUVE>(animationTree));
        EXPECT_FALSE(entityManager.HasComponentUVE<Scene::TransformComponentUVE>(animationPlayer));
        editor.ShutdownUVE();
    }
    engine.Shutdown();
}

TEST(SceneNodeEditorUVETest, CentralizedCreationUVE_CreatesEveryExpandedNodeKind) {
    Core::EngineCoreUVE engine(MakeSceneNodeEditorTestConfigUVE());
    engine.Init();
    ASSERT_TRUE(engine.Load());
    {
        EditorUVE editor(engine.GetServicesUVE(), "uve_scene_node_editor_expanded_tests.uvscene");
        editor.InitUVE();
        Scene::IEntityManagerUVE& entityManager = engine.GetServicesUVE().GetEntityManagerUVE();
        constexpr std::array<Scene::Nodes::SceneNodeKindUVE, 27U> expandedKinds{
            Scene::Nodes::SceneNodeKindUVE::Area3D,
            Scene::Nodes::SceneNodeKindUVE::RayCast3D,
            Scene::Nodes::SceneNodeKindUVE::Static3D,
            Scene::Nodes::SceneNodeKindUVE::Kinematic3D,
            Scene::Nodes::SceneNodeKindUVE::NavigationRegion3D,
            Scene::Nodes::SceneNodeKindUVE::NavigationAgent3D,
            Scene::Nodes::SceneNodeKindUVE::Skeleton3D,
            Scene::Nodes::SceneNodeKindUVE::BoneAttachment3D,
            Scene::Nodes::SceneNodeKindUVE::SpringArm3D,
            Scene::Nodes::SceneNodeKindUVE::Marker3D,
            Scene::Nodes::SceneNodeKindUVE::Hitbox3D,
            Scene::Nodes::SceneNodeKindUVE::Hurtbox3D,
            Scene::Nodes::SceneNodeKindUVE::Projectile3D,
            Scene::Nodes::SceneNodeKindUVE::InteractionArea3D,
            Scene::Nodes::SceneNodeKindUVE::ReflectionProbe3D,
            Scene::Nodes::SceneNodeKindUVE::Decal3D,
            Scene::Nodes::SceneNodeKindUVE::LODGroup3D,
            Scene::Nodes::SceneNodeKindUVE::Occluder3D,
            Scene::Nodes::SceneNodeKindUVE::VisibilityRegion3D,
            Scene::Nodes::SceneNodeKindUVE::SpawnPoint3D,
            Scene::Nodes::SceneNodeKindUVE::LevelStreamer3D,
            Scene::Nodes::SceneNodeKindUVE::WorldPartition3D,
            Scene::Nodes::SceneNodeKindUVE::Canvas,
            Scene::Nodes::SceneNodeKindUVE::UIText,
            Scene::Nodes::SceneNodeKindUVE::UIImage,
            Scene::Nodes::SceneNodeKindUVE::UIButton,
        };
        for (const Scene::Nodes::SceneNodeKindUVE kind : expandedKinds) {
            const Scene::EntityUVE entity = editor.CreateDocumentSceneNodeUVE(kind);
            ASSERT_NE(entity, Scene::kInvalidEntityUVE);
            EXPECT_TRUE(entityManager.IsAliveUVE(entity));
            EXPECT_TRUE(entityManager.HasComponentUVE<Scene::TransformComponentUVE>(entity));
            EXPECT_TRUE(entityManager.HasComponentUVE<Scene::NameComponentUVE>(entity));
            EXPECT_GE(entityManager.GetComponentTypesUVE(entity).size(), 4U);
        }
        editor.ShutdownUVE();
    }
    engine.Shutdown();
}

TEST(SceneNodeEditorUVETest, CharacterBodyCreationUVE_IsOneAtomicUndoRedoTransaction) {
    Core::EngineCoreUVE engine(MakeSceneNodeEditorTestConfigUVE());
    engine.Init();
    ASSERT_TRUE(engine.Load());
    {
        EditorUVE editor(engine.GetServicesUVE(), "uve_scene_node_editor_history_tests.uvscene");
        editor.InitUVE();
        Scene::IEntityManagerUVE& entityManager = engine.GetServicesUVE().GetEntityManagerUVE();

        const Scene::EntityUVE created =
            editor.CreateDocumentSceneNodeUVE(Scene::Nodes::SceneNodeKindUVE::Character3D);
        ASSERT_NE(created, Scene::kInvalidEntityUVE);
        ASSERT_TRUE(editor.CanUndoUVE());
        EXPECT_TRUE(editor.IsSceneDirtyUVE());

        ASSERT_TRUE(editor.UndoUVE());
        EXPECT_FALSE(entityManager.IsAliveUVE(created));
        EXPECT_EQ(editor.GetSelectedEntityUVE(), Scene::kInvalidEntityUVE);
        EXPECT_FALSE(editor.IsSceneDirtyUVE());
        EXPECT_TRUE(editor.CanRedoUVE());

        ASSERT_TRUE(editor.RedoUVE());
        const Scene::EntityUVE restored = editor.GetSelectedEntityUVE();
        ASSERT_NE(restored, Scene::kInvalidEntityUVE);
        EXPECT_NE(restored, created);
        EXPECT_TRUE(entityManager.HasComponentUVE<Scene::ColliderComponentUVE>(restored));
        EXPECT_TRUE(entityManager.HasComponentUVE<Scene::CharacterControllerComponentUVE>(restored));
        EXPECT_TRUE(entityManager.HasComponentUVE<Scene::SolidBodyComponentUVE>(restored));
        EXPECT_TRUE(editor.IsSceneDirtyUVE());
        editor.ShutdownUVE();
    }
    engine.Shutdown();
}

TEST(SceneNodeEditorUVETest, CentralizedCreationUVE_RejectsMultiSelectionAndPlayModeWithoutMutation) {
    Core::EngineCoreUVE engine(MakeSceneNodeEditorTestConfigUVE());
    engine.Init();
    ASSERT_TRUE(engine.Load());
    {
        EditorUVE editor(engine.GetServicesUVE(), "uve_scene_node_editor_safety_tests.uvscene", 100U, &engine);
        editor.InitUVE();
        const Scene::EntityUVE first =
            editor.CreateDocumentSceneNodeUVE(Scene::Nodes::SceneNodeKindUVE::Object3D);
        const Scene::EntityUVE second =
            editor.CreateDocumentSceneNodeUVE(Scene::Nodes::SceneNodeKindUVE::Object3D);
        ASSERT_NE(first, Scene::kInvalidEntityUVE);
        ASSERT_NE(second, Scene::kInvalidEntityUVE);
        editor.SelectEntityUVE(first);
        editor.ToggleEntitySelectionUVE(second);
        ASSERT_FALSE(editor.HasSingleDocumentSelectionUVE());
        const std::vector<Scene::EntityUVE> rootsBefore = editor.GetDocumentRootsUVE();
        EXPECT_EQ(editor.CreateDocumentSceneNodeUVE(Scene::Nodes::SceneNodeKindUVE::Camera3D),
                  Scene::kInvalidEntityUVE);
        EXPECT_EQ(editor.GetDocumentRootsUVE(), rootsBefore);

        ASSERT_TRUE(editor.EnterPlayModeUVE());
        EXPECT_EQ(editor.CreateDocumentSceneNodeUVE(Scene::Nodes::SceneNodeKindUVE::Light3D),
                  Scene::kInvalidEntityUVE);
        ASSERT_TRUE(editor.StopPlayModeUVE());
        EXPECT_EQ(editor.GetDocumentRootsUVE().size(), rootsBefore.size());
        editor.ShutdownUVE();
    }
    engine.Shutdown();
}

[[nodiscard]] bool ContainsEntityUVE(const std::vector<Scene::EntityUVE>& entities,
                                    const Scene::EntityUVE entity) {
    return std::find(entities.begin(), entities.end(), entity) != entities.end();
}

TEST(SceneNodeEditorUVETest, SceneRootUVE_FreshDocumentHasExactlyOneNamedRoot) {
    Core::EngineCoreUVE engine(MakeSceneNodeEditorTestConfigUVE());
    engine.Init();
    ASSERT_TRUE(engine.Load());
    {
        EditorUVE editor(engine.GetServicesUVE(), "uve_scene_node_editor_scene_root_tests.uvscene");
        editor.InitUVE();
        Scene::IEntityManagerUVE& entityManager = engine.GetServicesUVE().GetEntityManagerUVE();

        const Scene::EntityUVE root = editor.GetDocumentSceneRootUVE();
        ASSERT_NE(root, Scene::kInvalidEntityUVE);
        EXPECT_TRUE(entityManager.HasComponentUVE<Scene::SceneRootComponentUVE>(root));
        EXPECT_EQ(entityManager.GetComponentUVE<Scene::NameComponentUVE>(root).name, "SceneRoot");
        EXPECT_EQ(editor.GetDocumentRootsUVE().size(), 1U);
        EXPECT_EQ(editor.GetDocumentRootsUVE()[0U], root);

        // The root is a library kind but never library-creatable: the Add-Node path refuses it.
        EXPECT_EQ(editor.CreateDocumentSceneNodeUVE(Scene::Nodes::SceneNodeKindUVE::SceneRoot),
                  Scene::kInvalidEntityUVE);

        editor.ShutdownUVE();
    }
    engine.Shutdown();
}

TEST(SceneNodeEditorUVETest, SceneRootUVE_NewNodesJoinHierarchyUnderSelectionOrRoot) {
    Core::EngineCoreUVE engine(MakeSceneNodeEditorTestConfigUVE());
    engine.Init();
    ASSERT_TRUE(engine.Load());
    {
        EditorUVE editor(engine.GetServicesUVE(), "uve_scene_node_editor_scene_root_join_tests.uvscene");
        editor.InitUVE();
        Scene::IEntityManagerUVE& entityManager = engine.GetServicesUVE().GetEntityManagerUVE();
        Scene::ISceneGraphUVE& sceneGraph = engine.GetServicesUVE().GetSceneGraphUVE();
        const Scene::EntityUVE root = editor.GetDocumentSceneRootUVE();
        ASSERT_NE(root, Scene::kInvalidEntityUVE);

        // No selection: a new node joins the level's node folder (a level node always lives in a
        // folder inside the Viewport), not a new document root.
        const Scene::EntityUVE first =
            editor.CreateDocumentSceneNodeUVE(Scene::Nodes::SceneNodeKindUVE::Object3D);
        ASSERT_NE(first, Scene::kInvalidEntityUVE);
        EXPECT_EQ(editor.GetDocumentRootsUVE().size(), 1U);
        const Scene::EntityUVE folder = entityManager.GetComponentUVE<Scene::HierarchyComponentUVE>(first).parent;
        ASSERT_TRUE(entityManager.HasComponentUVE<Scene::FolderComponentUVE>(folder));
        EXPECT_EQ(entityManager.GetComponentUVE<Scene::NameComponentUVE>(folder).name, "World");
        const Scene::EntityUVE viewport = entityManager.GetComponentUVE<Scene::HierarchyComponentUVE>(folder).parent;
        EXPECT_TRUE(entityManager.HasComponentUVE<Scene::OutlinerViewportComponentUVE>(viewport));
        EXPECT_EQ(sceneGraph.GetChildrenUVE(entityManager, root), std::vector<Scene::EntityUVE>{viewport});

        // With a single selection: the new node becomes that selection's child.
        editor.SelectEntityUVE(first);
        const Scene::EntityUVE second =
            editor.CreateDocumentSceneNodeUVE(Scene::Nodes::SceneNodeKindUVE::Camera3D);
        ASSERT_NE(second, Scene::kInvalidEntityUVE);
        EXPECT_EQ(editor.GetDocumentRootsUVE().size(), 1U);
        EXPECT_TRUE(ContainsEntityUVE(sceneGraph.GetChildrenUVE(entityManager, first), second));
        EXPECT_FALSE(ContainsEntityUVE(sceneGraph.GetChildrenUVE(entityManager, folder), second));

        editor.ShutdownUVE();
    }
    engine.Shutdown();
}

TEST(SceneNodeEditorUVETest, OutlinerLayoutUVE_LevelTopIsViewportSunAndEnvironmentWithNodesOnlyInFolders) {
    using Kind = Scene::Nodes::SceneNodeKindUVE;
    Core::EngineCoreUVE engine(MakeSceneNodeEditorTestConfigUVE());
    engine.Init();
    ASSERT_TRUE(engine.Load());
    {
        EditorUVE editor(engine.GetServicesUVE(), "uve_scene_node_editor_outliner_layout_tests.uvscene");
        editor.InitUVE();
        Scene::IEntityManagerUVE& entityManager = engine.GetServicesUVE().GetEntityManagerUVE();
        Scene::ISceneGraphUVE& sceneGraph = engine.GetServicesUVE().GetSceneGraphUVE();
        const auto parentOf = [&entityManager](const Scene::EntityUVE entity) {
            return entityManager.GetComponentUVE<Scene::HierarchyComponentUVE>(entity).parent;
        };
        const Scene::EntityUVE root = editor.GetDocumentSceneRootUVE();

        // A fresh level: the Viewport at the top holding one folder, and nothing to undo or save.
        const std::vector<Scene::EntityUVE> top = sceneGraph.GetChildrenUVE(entityManager, root);
        ASSERT_EQ(top.size(), 1U);
        const Scene::EntityUVE viewport = top[0U];
        EXPECT_TRUE(entityManager.HasComponentUVE<Scene::OutlinerViewportComponentUVE>(viewport));
        EXPECT_EQ(entityManager.GetComponentUVE<Scene::NameComponentUVE>(viewport).name, "uve_scene_node_editor_outliner_layout_tests"); // its asset's name
        const std::vector<Scene::EntityUVE> folders = sceneGraph.GetChildrenUVE(entityManager, viewport);
        ASSERT_EQ(folders.size(), 1U);
        const Scene::EntityUVE world = folders[0U];
        EXPECT_TRUE(entityManager.HasComponentUVE<Scene::FolderComponentUVE>(world));
        EXPECT_FALSE(editor.IsSceneDirtyUVE());
        EXPECT_FALSE(editor.CanUndoUVE());

        // The sun and the environment sit beside the Viewport, one of each, in the order added.
        const Scene::EntityUVE sun = editor.CreateDocumentSceneNodeUVE(Kind::DirectionalLight3D);
        ASSERT_NE(sun, Scene::kInvalidEntityUVE);
        const Scene::EntityUVE environment = editor.CreateDocumentSceneNodeUVE(Kind::WorldEnvironment3D);
        ASSERT_NE(environment, Scene::kInvalidEntityUVE);
        EXPECT_EQ(sceneGraph.GetChildrenUVE(entityManager, root),
                  (std::vector<Scene::EntityUVE>{viewport, sun, environment}));
        EXPECT_EQ(editor.CreateDocumentSceneNodeUVE(Kind::DirectionalLight3D), Scene::kInvalidEntityUVE);
        EXPECT_EQ(editor.CreateDocumentSceneNodeUVE(Kind::WorldEnvironment3D), Scene::kInvalidEntityUVE);
        // Undoing one frees its place again.
        ASSERT_TRUE(editor.UndoUVE());
        EXPECT_FALSE(entityManager.IsAliveUVE(environment));
        ASSERT_NE(editor.CreateDocumentSceneNodeUVE(Kind::WorldEnvironment3D), Scene::kInvalidEntityUVE);

        // Any other node goes into a folder, even with the sun selected.
        editor.SelectEntityUVE(sun);
        const Scene::EntityUVE mesh = editor.CreateDocumentSceneNodeUVE(Kind::BoxMesh3D);
        ASSERT_NE(mesh, Scene::kInvalidEntityUVE);
        EXPECT_EQ(parentOf(mesh), world);
        // A new folder with nothing selected goes into the Viewport.
        editor.ClearSelectionUVE();
        const Scene::EntityUVE props = editor.CreateDocumentSceneNodeUVE(Kind::Folder);
        ASSERT_NE(props, Scene::kInvalidEntityUVE);
        EXPECT_EQ(parentOf(props), viewport);

        // Nodes never leave the folders: not to the top, not straight into the Viewport.
        editor.SelectEntityUVE(mesh);
        EXPECT_FALSE(editor.ReparentSelectedEntityUVE(root));
        EXPECT_FALSE(editor.ReparentSelectedEntityUVE(viewport));
        EXPECT_FALSE(editor.ReparentSelectedEntityUVE(sun));
        EXPECT_TRUE(editor.ReparentSelectedEntityUVE(props));
        EXPECT_EQ(parentOf(mesh), props);
        // "Move to top" keeps a node in a folder, and a folder in the Viewport.
        EXPECT_TRUE(editor.ReparentSelectedEntityUVE(Scene::kInvalidEntityUVE));
        EXPECT_TRUE(entityManager.HasComponentUVE<Scene::FolderComponentUVE>(parentOf(mesh)));
        editor.SelectEntityUVE(props);
        EXPECT_FALSE(editor.ReparentSelectedEntityUVE(root));
        EXPECT_TRUE(editor.ReparentSelectedEntityUVE(world)); // a folder may nest in a folder
        EXPECT_TRUE(editor.ReparentSelectedEntityUVE(Scene::kInvalidEntityUVE));
        EXPECT_EQ(parentOf(props), viewport);

        // The Viewport is structure: it cannot be removed, copied or moved.
        editor.SelectEntityUVE(viewport);
        EXPECT_FALSE(editor.DeleteSelectedEntityUVE());
        EXPECT_EQ(editor.DuplicateSelectedEntityUVE(), Scene::kInvalidEntityUVE);
        EXPECT_FALSE(editor.ReparentSelectedEntityUVE(world));
        EXPECT_TRUE(entityManager.IsAliveUVE(viewport));

        // The layout survives a save and load as it is: no second Viewport or folder appears.
        ASSERT_TRUE(editor.SaveSceneUVE());
        ASSERT_TRUE(editor.LoadSceneUVE());
        EXPECT_FALSE(editor.IsSceneDirtyUVE());
        const std::vector<Scene::EntityUVE> loadedTop =
            sceneGraph.GetChildrenUVE(entityManager, editor.GetDocumentSceneRootUVE());
        ASSERT_EQ(loadedTop.size(), 3U);
        EXPECT_TRUE(entityManager.HasComponentUVE<Scene::OutlinerViewportComponentUVE>(loadedTop[0U]));
        EXPECT_EQ(Scene::ResolveSceneNodeKindUVE(entityManager, loadedTop[1U]), Kind::DirectionalLight3D);
        EXPECT_EQ(Scene::ResolveSceneNodeKindUVE(entityManager, loadedTop[2U]), Kind::WorldEnvironment3D);
        EXPECT_EQ(sceneGraph.GetChildrenUVE(entityManager, loadedTop[0U]).size(), 2U);

        editor.ShutdownUVE();
        std::filesystem::remove("uve_scene_node_editor_outliner_layout_tests.uvscene");
    }
    engine.Shutdown();
}

TEST(SceneNodeEditorUVETest, SceneRootUVE_RootCannotBeDeletedReparentedOrDuplicated) {
    Core::EngineCoreUVE engine(MakeSceneNodeEditorTestConfigUVE());
    engine.Init();
    ASSERT_TRUE(engine.Load());
    {
        EditorUVE editor(engine.GetServicesUVE(), "uve_scene_node_editor_scene_root_guard_tests.uvscene");
        editor.InitUVE();
        Scene::IEntityManagerUVE& entityManager = engine.GetServicesUVE().GetEntityManagerUVE();

        const Scene::EntityUVE root = editor.GetDocumentSceneRootUVE();
        ASSERT_NE(root, Scene::kInvalidEntityUVE);
        const Scene::EntityUVE other =
            editor.CreateDocumentSceneNodeUVE(Scene::Nodes::SceneNodeKindUVE::Object3D);
        ASSERT_NE(other, Scene::kInvalidEntityUVE);

        editor.SelectEntityUVE(root);
        EXPECT_FALSE(editor.DeleteSelectedEntityUVE());
        EXPECT_EQ(editor.DuplicateSelectedEntityUVE(), Scene::kInvalidEntityUVE);
        EXPECT_FALSE(editor.ReparentSelectedEntityUVE(other));
        EXPECT_TRUE(entityManager.IsAliveUVE(root));
        EXPECT_TRUE(entityManager.HasComponentUVE<Scene::SceneRootComponentUVE>(root));
        EXPECT_EQ(editor.GetDocumentRootsUVE().size(), 1U);
        EXPECT_EQ(editor.GetDocumentRootsUVE()[0U], root);

        editor.ShutdownUVE();
    }
    engine.Shutdown();
}

TEST(SceneNodeEditorUVETest, SceneRootUVE_UndoRedoKeepsCreatedNodeUnderItsParent) {
    Core::EngineCoreUVE engine(MakeSceneNodeEditorTestConfigUVE());
    engine.Init();
    ASSERT_TRUE(engine.Load());
    {
        EditorUVE editor(engine.GetServicesUVE(), "uve_scene_node_editor_scene_root_history_tests.uvscene");
        editor.InitUVE();
        Scene::IEntityManagerUVE& entityManager = engine.GetServicesUVE().GetEntityManagerUVE();
        Scene::ISceneGraphUVE& sceneGraph = engine.GetServicesUVE().GetSceneGraphUVE();
        const Scene::EntityUVE root = editor.GetDocumentSceneRootUVE();
        ASSERT_NE(root, Scene::kInvalidEntityUVE);

        const Scene::EntityUVE parent =
            editor.CreateDocumentSceneNodeUVE(Scene::Nodes::SceneNodeKindUVE::Object3D);
        ASSERT_NE(parent, Scene::kInvalidEntityUVE);
        editor.SelectEntityUVE(parent);
        const Scene::EntityUVE child =
            editor.CreateDocumentSceneNodeUVE(Scene::Nodes::SceneNodeKindUVE::Light3D);
        ASSERT_NE(child, Scene::kInvalidEntityUVE);
        ASSERT_TRUE(ContainsEntityUVE(sceneGraph.GetChildrenUVE(entityManager, parent), child));

        ASSERT_TRUE(editor.UndoUVE());
        EXPECT_FALSE(entityManager.IsAliveUVE(child));
        ASSERT_TRUE(editor.RedoUVE());
        const Scene::EntityUVE restored = editor.GetSelectedEntityUVE();
        ASSERT_NE(restored, Scene::kInvalidEntityUVE);
        // Redo must re-home the node under its ORIGINAL parent, not drop it to document top
        // level now that documents have a single scene root.
        EXPECT_TRUE(ContainsEntityUVE(sceneGraph.GetChildrenUVE(entityManager, parent), restored));
        EXPECT_EQ(editor.GetDocumentRootsUVE().size(), 1U);

        editor.ShutdownUVE();
    }
    engine.Shutdown();
}

TEST(SceneNodeEditorUVETest, SceneRootUVE_LegacyMultiRootSceneFileAutoMigratesUnderOneRoot) {
    Core::EngineCoreUVE engine(MakeSceneNodeEditorTestConfigUVE());
    engine.Init();
    ASSERT_TRUE(engine.Load());
    {
        Scene::IEntityManagerUVE& entityManager = engine.GetServicesUVE().GetEntityManagerUVE();
        Scene::ISceneGraphUVE& sceneGraph = engine.GetServicesUVE().GetSceneGraphUVE();

        // Author a legacy-style file by hand: two top-level entities, NO scene-root marker -
        // exactly what every .uvscene saved before the root existed looks like.
        const Scene::EntityUVE legacyA = entityManager.CreateEntityUVE();
        sceneGraph.AttachTransformUVE(entityManager, legacyA, Scene::TransformComponentUVE{});
        entityManager.AddComponentUVE<Scene::NameComponentUVE>(legacyA, Scene::NameComponentUVE{"LegacyA"});
        const Scene::EntityUVE legacyB = entityManager.CreateEntityUVE();
        sceneGraph.AttachTransformUVE(entityManager, legacyB, Scene::TransformComponentUVE{});
        entityManager.AddComponentUVE<Scene::NameComponentUVE>(legacyB, Scene::NameComponentUVE{"LegacyB"});

        const std::string path = "uve_scene_node_editor_scene_root_migrate_tests.uvscene";
        std::filesystem::remove(path);
        ASSERT_TRUE(engine.GetServicesUVE().GetSceneSerializerUVE().SaveUVE(
            entityManager, {legacyA, legacyB}, path, Asset::AssetKindUVE::Scene));

        {
            EditorUVE editor(engine.GetServicesUVE(), path);
            editor.InitUVE();
            ASSERT_TRUE(editor.LoadSceneUVE());

            // The loaded document must hold the one-root invariant: a single SceneRoot at the
            // top with both legacy top-level entities re-parented under it.
            const Scene::EntityUVE root = editor.GetDocumentSceneRootUVE();
            ASSERT_NE(root, Scene::kInvalidEntityUVE);
            EXPECT_EQ(editor.GetDocumentRootsUVE().size(), 1U);
            EXPECT_EQ(editor.GetDocumentRootsUVE()[0U], root);
            EXPECT_EQ(entityManager.GetComponentUVE<Scene::NameComponentUVE>(root).name, "SceneRoot");
            // The restored entities are FRESH handles (load recreates entities from the
            // file), so identity is checked by name, not by handle.
            // Then the Outliner layout gave it a Viewport, and moved them into its node folder.
            const std::vector<Scene::EntityUVE> topRows = sceneGraph.GetChildrenUVE(entityManager, root);
            ASSERT_EQ(topRows.size(), 1U);
            EXPECT_TRUE(entityManager.HasComponentUVE<Scene::OutlinerViewportComponentUVE>(topRows[0U]));
            const std::vector<Scene::EntityUVE> folders = sceneGraph.GetChildrenUVE(entityManager, topRows[0U]);
            ASSERT_EQ(folders.size(), 1U);
            const std::vector<Scene::EntityUVE> rootChildren = sceneGraph.GetChildrenUVE(entityManager, folders[0U]);
            ASSERT_EQ(rootChildren.size(), 2U);
            std::size_t matchedNames = 0U;
            for (const Scene::EntityUVE child : rootChildren) {
                const std::string& name =
                    entityManager.GetComponentUVE<Scene::NameComponentUVE>(child).name;
                if (name == "LegacyA" || name == "LegacyB") {
                    ++matchedNames;
                }
            }
            EXPECT_EQ(matchedNames, 2U);
            // Migration is a real document change: the in-memory doc no longer matches the file.
            EXPECT_TRUE(editor.IsSceneDirtyUVE());

            editor.ShutdownUVE();
        }
        std::filesystem::remove(path);
    }
    engine.Shutdown();
}

} // namespace
TEST(SceneNodeEditorUVETest, SceneRootUVE_FileCarryingTwoRootMarkersStillLoadsWithOneRoot) {
    // The migration test above covers a LEGACY file - no marker at all. This covers the other
    // direction: a file that carries two SceneRoot markers. That is not a file the editor can
    // currently produce, but .uvscene is plain JSON on disk, so it is a file the editor can be
    // handed - by a merge conflict resolved badly, a hand-edit, or a future tool.
    //
    // It matters more than it looks. The editor forbids deleting or reparenting a scene root, so
    // a second root that survives load is not a cosmetic wart: it is an entity the user is
    // structurally unable to remove.
    Core::EngineCoreUVE engine(MakeSceneNodeEditorTestConfigUVE());
    engine.Init();
    ASSERT_TRUE(engine.Load());
    {
        Scene::IEntityManagerUVE& entityManager = engine.GetServicesUVE().GetEntityManagerUVE();
        Scene::ISceneGraphUVE& sceneGraph = engine.GetServicesUVE().GetSceneGraphUVE();

        // Two independently-marked roots, as a badly merged file would hold.
        const Scene::EntityUVE firstRoot = entityManager.CreateEntityUVE();
        sceneGraph.AttachTransformUVE(entityManager, firstRoot, Scene::TransformComponentUVE{});
        entityManager.AddComponentUVE<Scene::NameComponentUVE>(firstRoot, Scene::NameComponentUVE{"SceneRoot"});
        entityManager.AddComponentUVE<Scene::SceneRootComponentUVE>(firstRoot, Scene::SceneRootComponentUVE{});
        const Scene::EntityUVE secondRoot = entityManager.CreateEntityUVE();
        sceneGraph.AttachTransformUVE(entityManager, secondRoot, Scene::TransformComponentUVE{});
        entityManager.AddComponentUVE<Scene::NameComponentUVE>(secondRoot, Scene::NameComponentUVE{"SurplusRoot"});
        entityManager.AddComponentUVE<Scene::SceneRootComponentUVE>(secondRoot, Scene::SceneRootComponentUVE{});
        // The surplus root owns authored content. Repairing the structure must not discard it -
        // that would be trading the user's work for tidiness.
        const Scene::EntityUVE surplusChild = entityManager.CreateEntityUVE();
        sceneGraph.AttachTransformUVE(entityManager, surplusChild, Scene::TransformComponentUVE{});
        entityManager.AddComponentUVE<Scene::NameComponentUVE>(surplusChild, Scene::NameComponentUVE{"KeepMe"});
        sceneGraph.SetParentUVE(entityManager, surplusChild, secondRoot);

        const std::string path = "uve_scene_node_editor_two_root_markers_tests.uvscene";
        std::filesystem::remove(path);
        ASSERT_TRUE(engine.GetServicesUVE().GetSceneSerializerUVE().SaveUVE(
            entityManager, {firstRoot, secondRoot}, path, Asset::AssetKindUVE::Scene));

        {
            EditorUVE editor(engine.GetServicesUVE(), path);
            editor.InitUVE();
            ASSERT_TRUE(editor.LoadSceneUVE());

            // Exactly one entity may carry the marker afterwards. Counted directly rather than
            // through GetDocumentSceneRootUVE, which returns the first hit and would report
            // success even with a second marker buried in the hierarchy.
            std::size_t markerCount = 0U;
            entityManager.ForEachUVE<Scene::SceneRootComponentUVE>(
                [&markerCount](const Scene::EntityUVE, const Scene::SceneRootComponentUVE&) { ++markerCount; });
            EXPECT_EQ(markerCount, 1U)
                << "a second scene-root marker survived load - the editor refuses to delete or "
                   "reparent a root, so the user cannot remove it by hand";

            EXPECT_EQ(editor.GetDocumentRootsUVE().size(), 1U);

            // The demoted root and its child both survive, as ordinary nodes. Names are used for
            // identity because loading recreates entities with fresh handles.
            bool foundDemoted = false;
            bool foundGrandchild = false;
            entityManager.ForEachUVE<Scene::NameComponentUVE>(
                [&](const Scene::EntityUVE, const Scene::NameComponentUVE& nameComponent) {
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
            EXPECT_TRUE(editor.IsSceneDirtyUVE());
            editor.ShutdownUVE();
        }
        std::filesystem::remove(path);
    }
    engine.Shutdown();
}

} // namespace UVE::Editor::Tests
