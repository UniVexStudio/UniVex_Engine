// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include <algorithm>
#include <typeindex>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <vector>

#include <gtest/gtest.h>

#include "Support/test_scratch.h"

#include "all_objects_3d.h"
#include "bone_attachment_3d.h"
#include "editor.h"
#include "engine_core.h"
#include "hierarchy_component.h"
#include "ragdoll_3d.h"
#include "scene_component_metadata.h"
#include "scene_object_type.h"
#include "skeleton_3d.h"

namespace UVE::Editor::Tests {

struct EditorAccess final {
    [[nodiscard]] static bool SetSelectedComponentProperty(Editor& editor, const Core::TypeMetadataEntry& entry,
                                                           const Core::TypeMetadataProperty& property,
                                                           const void* value) {
        return editor.SetSelectedComponentProperty(entry, property, value);
    }
    static void BuildColliderOverlay(const Editor& editor,
                                     std::vector<Editor::ViewportColliderLine>& outLines) {
        editor.BuildColliderOverlay(outLines);
    }
};

namespace {

[[nodiscard]] Core::EngineConfig MakeEntityEditorRagdollTestConfig(const std::filesystem::path& scratch,
                                                                   const std::filesystem::path& content) {
    Core::EngineConfig config{};
    config.headless = true;
    config.projectContentRoot = content;
    config.projectSettingsFilePath = scratch / "project.uvsettings";
    config.assetDatabaseFilePath = scratch / "assets.json";
    config.logFilePath = scratch / "log.txt";
    config.settingsFilePath = scratch / "settings.json";
    config.inputMapFilePath = scratch / "input_map.json";
    config.saveDirectoryPath = scratch / "saves";
    config.shaderCachePath = scratch / "shader_cache";
    config.shaderSourceRealDirectory = ::UVE::Tests::RepositoryRoot() / "Engine/Runtime/RHI/Shader/built_in";
    config.shaderSourceMountPrefix = "shaders";
    return config;
}

void PopulateTestHumanoidSkeleton(Scene::Skeleton3DComponent& skeleton) {
    skeleton.bones.clear();
    auto add = [&](const char* name, const std::int32_t parent, const Math::Vector3 pos) {
        Scene::SkeletonBone bone{};
        bone.name = name;
        bone.parentIndex = parent;
        bone.localPosition = pos;
        skeleton.bones.push_back(bone);
    };
    add("hips", -1, {0.0F, 1.05F, 0.0F});
    add("spine", 0, {0.0F, 0.25F, 0.0F});
    add("chest", 1, {0.0F, 0.25F, 0.0F});
    add("head", 2, {0.0F, 0.24F, 0.0F});
    add("thigh_l", 0, {-0.12F, -0.08F, 0.0F});
    add("shin_l", 4, {0.0F, -0.42F, 0.0F});
    add("upperarm_l", 2, {-0.22F, 0.14F, 0.0F});
    skeleton.pose.resize(skeleton.bones.size());
    for (std::size_t i = 0U; i < skeleton.bones.size(); ++i) {
        skeleton.pose[i].position = skeleton.bones[i].localPosition;
        skeleton.pose[i].rotation = skeleton.bones[i].localRotation;
        skeleton.pose[i].scale = skeleton.bones[i].localScale;
    }
}

TEST(EntityEditorParentingRagdollTest, CreateDocumentSceneObject_ParentsUnderSelectedChildAndAutoLinksRagdoll) {
    const std::filesystem::path scratch = ::UVE::Tests::MakeTestCaseDirectory("entity_editor_parenting_ragdoll");
    const std::filesystem::path content = scratch / "Content";
    std::filesystem::create_directories(content);

    Core::EngineCore engine(MakeEntityEditorRagdollTestConfig(scratch, content));
    engine.Init();
    ASSERT_TRUE(engine.Load());
    {
        Editor editor(engine.GetServices(), scratch / "main.uvscene", 100U, &engine);
        editor.SetDocumentSceneFactory([&engine] { return engine.CreateDocumentScene(); });
        editor.Init();

        const auto created = editor.CreateContentCatalogueItem("character", content);
        ASSERT_TRUE(created.has_value());
        ASSERT_TRUE(editor.OpenEntityEditor(*created));
        const Scene::Entity entityRoot = editor.GetEntityEditorRoot();
        ASSERT_NE(entityRoot, Scene::kInvalidEntity);

        {
            const Editor::EntityDocumentScope scope(editor);
            ASSERT_TRUE(scope.Active());
            Scene::IEntityManager& entityEm = editor.GetActiveServices().GetEntityManager();

            // 1. Adding Skeleton3D while entityRoot is selected parents under entityRoot.
            editor.SelectEntity(entityRoot);
            const Scene::Entity skeletonEntity =
                editor.CreateDocumentSceneObject(Scene::Objects::SceneObjectKind::Skeleton3D);
            ASSERT_NE(skeletonEntity, Scene::kInvalidEntity);
            EXPECT_EQ(entityEm.GetComponent<Scene::HierarchyComponent>(skeletonEntity).parent, entityRoot);
            PopulateTestHumanoidSkeleton(entityEm.GetComponent<Scene::Skeleton3DComponent>(skeletonEntity));

            // 2. Selecting the Skeleton3D child and adding Ragdoll3D MUST parent under Skeleton3D,
            //    NOT snap back to entityRoot, AND auto-link + initialize the ragdoll.
            editor.SelectEntity(skeletonEntity);
            const Scene::Entity ragdollEntity =
                editor.CreateDocumentSceneObject(Scene::Objects::SceneObjectKind::Ragdoll3D);
            ASSERT_NE(ragdollEntity, Scene::kInvalidEntity);
            EXPECT_EQ(entityEm.GetComponent<Scene::HierarchyComponent>(ragdollEntity).parent, skeletonEntity)
                << "Ragdoll3D added while Skeleton3D is selected must become a child of Skeleton3D, not entityRoot";
            ASSERT_TRUE(entityEm.HasComponent<Scene::Ragdoll3DComponent>(ragdollEntity));
            const auto& ragdoll = entityEm.GetComponent<Scene::Ragdoll3DComponent>(ragdollEntity);
            EXPECT_EQ(ragdoll.skeleton, skeletonEntity);
            EXPECT_GE(ragdoll.bones.size(), 6U);
            EXPECT_TRUE(ragdoll.initialized);

            // 3. Selecting Skeleton3D and adding BoneAttachment3D parents under Skeleton3D and links skeleton.
            editor.SelectEntity(skeletonEntity);
            const Scene::Entity boneAttachEntity =
                editor.CreateDocumentSceneObject(Scene::Objects::SceneObjectKind::BoneAttachment3D);
            ASSERT_NE(boneAttachEntity, Scene::kInvalidEntity);
            EXPECT_EQ(entityEm.GetComponent<Scene::HierarchyComponent>(boneAttachEntity).parent, skeletonEntity);
            ASSERT_TRUE(entityEm.HasComponent<Scene::BoneAttachment3DComponent>(boneAttachEntity));
            EXPECT_EQ(entityEm.GetComponent<Scene::BoneAttachment3DComponent>(boneAttachEntity).skeleton,
                      skeletonEntity);

            // 4. Explicit parent argument (used by Right-Click -> Add Child...) works even when root is selected.
            editor.SelectEntity(entityRoot);
            const Scene::Entity explicitChild =
                editor.CreateDocumentSceneObject(Scene::Objects::SceneObjectKind::Camera3D, skeletonEntity);
            ASSERT_NE(explicitChild, Scene::kInvalidEntity);
            EXPECT_EQ(entityEm.GetComponent<Scene::HierarchyComponent>(explicitChild).parent, skeletonEntity);
        }

        ASSERT_TRUE(editor.CloseEntityEditor(false));
        editor.Shutdown();
    }
    engine.Shutdown();
}

TEST(EntityEditorParentingRagdollTest, SimulateAndPreview_StepsRagdollOnStudioFloorAndBuildsXrayColliders) {
    const std::filesystem::path scratch = ::UVE::Tests::MakeTestCaseDirectory("entity_editor_ragdoll_sim");
    const std::filesystem::path content = scratch / "Content";
    std::filesystem::create_directories(content);

    Core::EngineCore engine(MakeEntityEditorRagdollTestConfig(scratch, content));
    engine.Init();
    ASSERT_TRUE(engine.Load());
    {
        Editor editor(engine.GetServices(), scratch / "main.uvscene", 100U, &engine);
        editor.SetDocumentSceneFactory([&engine] { return engine.CreateDocumentScene(); });
        editor.Init();

        const auto created = editor.CreateContentCatalogueItem("character", content);
        ASSERT_TRUE(created.has_value());
        ASSERT_TRUE(editor.OpenEntityEditor(*created));
        const Scene::Entity entityRoot = editor.GetEntityEditorRoot();
        ASSERT_NE(entityRoot, Scene::kInvalidEntity);

        Scene::Entity skeletonEntity = Scene::kInvalidEntity;
        Scene::Entity ragdollEntity = Scene::kInvalidEntity;
        float initialPelvisY = 0.0F;
        {
            const Editor::EntityDocumentScope scope(editor);
            Scene::IEntityManager& entityEm = editor.GetActiveServices().GetEntityManager();

            editor.SelectEntity(entityRoot);
            skeletonEntity = editor.CreateDocumentSceneObject(Scene::Objects::SceneObjectKind::Skeleton3D);
            ASSERT_NE(skeletonEntity, Scene::kInvalidEntity);
            PopulateTestHumanoidSkeleton(entityEm.GetComponent<Scene::Skeleton3DComponent>(skeletonEntity));
            initialPelvisY = entityEm.GetComponent<Scene::Skeleton3DComponent>(skeletonEntity).pose[0].position.y;

            editor.SelectEntity(skeletonEntity);
            ragdollEntity = editor.CreateDocumentSceneObject(Scene::Objects::SceneObjectKind::Ragdoll3D);
            ASSERT_NE(ragdollEntity, Scene::kInvalidEntity);

            // Simulate freshly-loaded state where runtime bodies haven't been initialized yet:
            // BuildColliderOverlay must lazily initialize and emit X-Ray colored capsule lines!
            auto& ragdoll = entityEm.GetComponent<Scene::Ragdoll3DComponent>(ragdollEntity);
            ragdoll.initialized = false;
            editor.SelectEntity(ragdollEntity);
            std::vector<Editor::ViewportColliderLine> lines;
            EditorAccess::BuildColliderOverlay(editor, lines);
            ASSERT_FALSE(lines.empty()) << "selecting Ragdoll3D must render visible bone capsule overlays";
            EXPECT_TRUE(ragdoll.initialized) << "overlay build must lazily initialize unloaded ragdolls";
            bool allXrayAndColored = true;
            for (const auto& line : lines) {
                if (!line.xray || !line.hasColour) {
                    allXrayAndColored = false;
                    break;
                }
            }
            EXPECT_TRUE(allXrayAndColored) << "ragdoll bone capsule lines must draw X-Ray through skinned meshes";

            // Also visible when selecting the owning Skeleton3D!
            editor.SelectEntity(skeletonEntity);
            lines.clear();
            EditorAccess::BuildColliderOverlay(editor, lines);
            EXPECT_FALSE(lines.empty()) << "selecting Skeleton3D must also display linked Ragdoll3D capsules";
        }

        // Entity Editor Simulate button: steps Ragdoll3D against the virtual y = 0 studio floor.
        ASSERT_TRUE(editor.PlayEntitySimulation());
        EXPECT_EQ(editor.GetEntitySimulationState(), Editor::EntitySimulationState::Running);
        for (int frame = 0; frame < 120; ++frame) {
            editor.TickEntitySimulation(1.0 / 60.0);
        }
        {
            const Editor::EntityDocumentScope scope(editor);
            Scene::IEntityManager& entityEm = editor.GetActiveServices().GetEntityManager();
            const float simulatedPelvisY =
                entityEm.GetComponent<Scene::Skeleton3DComponent>(skeletonEntity).pose[0].position.y;
            EXPECT_LT(simulatedPelvisY, initialPelvisY - 0.35F)
                << "Entity Editor Simulate must drop Ragdoll3D toward the studio floor";
            EXPECT_GT(simulatedPelvisY, -0.05F)
                << "Entity Editor virtual studio floor at y = 0 must catch the ragdoll";
        }

        ASSERT_TRUE(editor.StopEntitySimulation());
        EXPECT_EQ(editor.GetEntitySimulationState(), Editor::EntitySimulationState::Stopped);
        {
            const Editor::EntityDocumentScope scope(editor);
            Scene::IEntityManager& entityEm = editor.GetActiveServices().GetEntityManager();
            bool foundRestoredSkeleton = false;
            entityEm.ForEach<Scene::Skeleton3DComponent>(
                [&](const Scene::Entity, const Scene::Skeleton3DComponent& restoredSkeleton) {
                    if (!restoredSkeleton.pose.empty()) {
                        foundRestoredSkeleton = true;
                        EXPECT_NEAR(restoredSkeleton.pose[0].position.y, initialPelvisY, 1.0e-4F)
                            << "StopEntitySimulation must restore exact pre-simulation skeleton pose";
                    } else if (!restoredSkeleton.bones.empty()) {
                        foundRestoredSkeleton = true;
                        EXPECT_NEAR(restoredSkeleton.bones[0].localPosition.y, initialPelvisY, 1.0e-4F)
                            << "StopEntitySimulation must restore bind skeleton bones";
                    }
                });
            EXPECT_TRUE(foundRestoredSkeleton);
        }

        ASSERT_TRUE(editor.CloseEntityEditor(false));
        editor.Shutdown();
    }
    engine.Shutdown();
}

/// How far the bodies are from the bones the mesh is skinned to: the bodies are moved onto the pose (what the
/// overlay does while idle) and the distance each one travelled is the mismatch.
[[nodiscard]] float BodyToPoseMismatch(Scene::IEntityManager& entities, const Scene::Entity ragdollEntity) {
    Scene::Ragdoll3DComponent& ragdoll = entities.GetComponent<Scene::Ragdoll3DComponent>(ragdollEntity);
    std::vector<Math::Vector3> before;
    for (std::size_t i = 0U; i < ragdoll.solver.GetBodyCount(); ++i) {
        before.push_back(ragdoll.solver.GetBody(i).position);
    }
    const Scene::Ragdoll3DComponent saved = ragdoll;
    if (!Scene::SyncRagdollBodiesToSkeletonPose(entities, ragdollEntity)) return 1.0e9F;
    float worst = 0.0F;
    for (std::size_t i = 0U; i < before.size(); ++i) {
        worst = std::max(worst, std::sqrt(Math::LengthSquared(ragdoll.solver.GetBody(i).position - before[i])));
    }
    ragdoll = saved;
    return worst;
}

TEST(EntityEditorParentingRagdollTest, EditingModeOrPreset_LeavesTheCapsulesOnTheMesh) {
    const std::filesystem::path scratch = ::UVE::Tests::MakeTestCaseDirectory("entity_editor_ragdoll_edit");
    const std::filesystem::path content = scratch / "Content";
    std::filesystem::create_directories(content);

    Core::EngineCore engine(MakeEntityEditorRagdollTestConfig(scratch, content));
    engine.Init();
    ASSERT_TRUE(engine.Load());
    {
        Editor editor(engine.GetServices(), scratch / "main.uvscene", 100U, &engine);
        editor.SetDocumentSceneFactory([&engine] { return engine.CreateDocumentScene(); });
        editor.Init();
        const auto created = editor.CreateContentCatalogueItem("character", content);
        ASSERT_TRUE(created.has_value());
        ASSERT_TRUE(editor.OpenEntityEditor(*created));
        const Scene::Entity entityRoot = editor.GetEntityEditorRoot();

        Scene::Entity ragdollEntity = Scene::kInvalidEntity;
        {
            const Editor::EntityDocumentScope scope(editor);
            Scene::IEntityManager& entities = editor.GetActiveServices().GetEntityManager();
            editor.SelectEntity(entityRoot);
            const Scene::Entity skeletonEntity =
                editor.CreateDocumentSceneObject(Scene::Objects::SceneObjectKind::Skeleton3D);
            PopulateTestHumanoidSkeleton(entities.GetComponent<Scene::Skeleton3DComponent>(skeletonEntity));
            editor.SelectEntity(skeletonEntity);
            ragdollEntity = editor.CreateDocumentSceneObject(Scene::Objects::SceneObjectKind::Ragdoll3D);
            ASSERT_NE(ragdollEntity, Scene::kInvalidEntity);
            editor.SelectEntity(ragdollEntity);
        }
        const Core::TypeMetadataEntry* const entry =
            Scene::FindSceneComponentMetadata(std::type_index(typeid(Scene::Ragdoll3DComponent)));
        ASSERT_NE(entry, nullptr);
        const auto property = [&](const std::string& name) -> const Core::TypeMetadataProperty& {
            return *std::find_if(entry->properties.begin(), entry->properties.end(),
                                 [&](const auto& candidate) { return candidate.name == name; });
        };
        const auto edit = [&](const char* label, const bool previewing = false) {
            for (const std::int64_t mode : {0, 1, 2, 3, 2}) {
                for (const std::int64_t preset : {0, 1, 2}) {
                    if (previewing) {
                        const Editor::EntityDocumentScope scope(editor);
                        Scene::IEntityManager& entities = editor.GetActiveServices().GetEntityManager();
                        if (!entities.GetComponent<Scene::Ragdoll3DComponent>(ragdollEntity).editorPreviewSimulating) {
                            ASSERT_TRUE(Scene::StartRagdollEditorPreview(entities, ragdollEntity));
                        }
                        for (int frame = 0; frame < 40; ++frame) {
                            Scene::UpdateRagdoll3DObjects(entities, 1.0F / 60.0F, true, 0.0F);
                        }
                    }
                    {
                        const Editor::EntityDocumentScope scope(editor);
                        const auto modeValue = static_cast<Scene::RagdollMode>(mode);
                        const auto presetValue = static_cast<Scene::RagdollQualityPreset>(preset);
                        static_cast<void>(EditorAccess::SetSelectedComponentProperty(editor, *entry,
                                                                                      property("mode"), &modeValue));
                        static_cast<void>(EditorAccess::SetSelectedComponentProperty(
                            editor, *entry, property("qualityPreset"), &presetValue));
                        EXPECT_EQ(static_cast<std::int64_t>(
                                      editor.GetActiveServices().GetEntityManager()
                                          .GetComponent<Scene::Ragdoll3DComponent>(ragdollEntity).mode),
                                  mode)
                            << label << ": the edit must stick";
                    }
                    for (int frame = 0; frame < 20; ++frame) {
                        if (editor.GetEntitySimulationState() == Editor::EntitySimulationState::Running) {
                            editor.TickEntitySimulation(1.0 / 60.0);
                        } else {
                            const Editor::EntityDocumentScope tickScope(editor);
                            Scene::UpdateRagdoll3DObjects(editor.GetActiveServices().GetEntityManager(),
                                                          1.0F / 60.0F, /*editorPreviewOnly=*/true, 0.0F);
                        }
                    }
                    const Editor::EntityDocumentScope scope(editor);
                    Scene::IEntityManager& entities = editor.GetActiveServices().GetEntityManager();
                    const Scene::Ragdoll3DComponent& state = entities.GetComponent<Scene::Ragdoll3DComponent>(ragdollEntity);
                    EXPECT_LT(BodyToPoseMismatch(entities, ragdollEntity), 0.02F)
                        << label << ": mode " << mode << " preset " << preset << " now mode "
                        << static_cast<int>(state.mode) << " initialized " << state.initialized << " preview "
                        << state.editorPreviewSimulating << " bodies " << state.solver.GetBodyCount()
                        << " bindings " << state.bindings.size() << " error '" << state.lastSetupError << "'";
                }
            }
        };
        edit("idle");
        edit("while previewing", /*previewing=*/true);
        {
            const Editor::EntityDocumentScope scope(editor);
            static_cast<void>(Scene::StopRagdollEditorPreview(editor.GetActiveServices().GetEntityManager(), ragdollEntity));
        }
        ASSERT_TRUE(editor.PlayEntitySimulation());
        for (int frame = 0; frame < 60; ++frame) editor.TickEntitySimulation(1.0 / 60.0);
        edit("while simulating");
        ASSERT_TRUE(editor.StopEntitySimulation());
        {
            // Stopping restores the document, so the ragdoll is found again and selected again.
            const Editor::EntityDocumentScope scope(editor);
            Scene::IEntityManager& entities = editor.GetActiveServices().GetEntityManager();
            ragdollEntity = Scene::kInvalidEntity;
            entities.ForEach<Scene::Ragdoll3DComponent>(
                [&](const Scene::Entity found, const Scene::Ragdoll3DComponent&) { ragdollEntity = found; });
            ASSERT_NE(ragdollEntity, Scene::kInvalidEntity);
            editor.SelectEntity(ragdollEntity);
        }
        edit("after stop");

        ASSERT_TRUE(editor.CloseEntityEditor(false));
        editor.Shutdown();
    }
    engine.Shutdown();
}

TEST(EntityEditorParentingRagdollTest, SimulateWithEntityDrive_KeepsTheSkeletonStillAndRestoresEverythingOnStop) {
    const std::filesystem::path scratch = ::UVE::Tests::MakeTestCaseDirectory("entity_editor_ragdoll_drive");
    const std::filesystem::path content = scratch / "Content";
    std::filesystem::create_directories(content);

    Core::EngineCore engine(MakeEntityEditorRagdollTestConfig(scratch, content));
    engine.Init();
    ASSERT_TRUE(engine.Load());
    {
        Editor editor(engine.GetServices(), scratch / "main.uvscene", 100U, &engine);
        editor.SetDocumentSceneFactory([&engine] { return engine.CreateDocumentScene(); });
        editor.Init();
        const auto created = editor.CreateContentCatalogueItem("character", content);
        ASSERT_TRUE(created.has_value());
        ASSERT_TRUE(editor.OpenEntityEditor(*created));
        const Scene::Entity entityRoot = editor.GetEntityEditorRoot();

        Math::Vector3 skeletonBefore{};
        Math::Vector3 rootBefore{};
        {
            const Editor::EntityDocumentScope scope(editor);
            Scene::IEntityManager& entities = editor.GetActiveServices().GetEntityManager();
            editor.SelectEntity(entityRoot);
            const Scene::Entity mesh = editor.CreateDocumentSceneObject(Scene::Objects::SceneObjectKind::MeshInstance3D);
            ASSERT_NE(mesh, Scene::kInvalidEntity);
            editor.SelectEntity(entityRoot);
            const Scene::Entity skeletonEntity =
                editor.CreateDocumentSceneObject(Scene::Objects::SceneObjectKind::Skeleton3D);
            PopulateTestHumanoidSkeleton(entities.GetComponent<Scene::Skeleton3DComponent>(skeletonEntity));
            editor.SelectEntity(skeletonEntity);
            const Scene::Entity ragdollEntity =
                editor.CreateDocumentSceneObject(Scene::Objects::SceneObjectKind::Ragdoll3D);
            ASSERT_NE(ragdollEntity, Scene::kInvalidEntity);
            entities.GetComponent<Scene::Ragdoll3DComponent>(ragdollEntity).driveEntityTransform = true;
            skeletonBefore = entities.GetComponent<Scene::TransformComponent>(skeletonEntity).localPosition;
            rootBefore = entities.GetComponent<Scene::TransformComponent>(entityRoot).localPosition;
            EXPECT_EQ(Scene::ResolveRagdollDriveTarget(entities, skeletonEntity), entityRoot)
                << "the entity that holds the skeleton and the mesh is the one that follows";
        }
        ASSERT_TRUE(editor.PlayEntitySimulation());
        for (int frame = 0; frame < 120; ++frame) editor.TickEntitySimulation(1.0 / 60.0);
        ASSERT_TRUE(editor.StopEntitySimulation());
        {
            const Editor::EntityDocumentScope scope(editor);
            Scene::IEntityManager& entities = editor.GetActiveServices().GetEntityManager();
            Scene::Entity skeletonEntity = Scene::kInvalidEntity;
            entities.ForEach<Scene::Skeleton3DComponent>(
                [&](const Scene::Entity found, const Scene::Skeleton3DComponent&) { skeletonEntity = found; });
            ASSERT_NE(skeletonEntity, Scene::kInvalidEntity);
            EXPECT_EQ(entities.GetComponent<Scene::TransformComponent>(skeletonEntity).localPosition, skeletonBefore);
            EXPECT_EQ(entities.GetComponent<Scene::TransformComponent>(editor.GetEntityEditorRoot()).localPosition,
                      rootBefore)
                << "Stop restores the transforms the simulation moved";
        }
        ASSERT_TRUE(editor.CloseEntityEditor(false));
        editor.Shutdown();
    }
    engine.Shutdown();
}

} // namespace
} // namespace UVE::Editor::Tests
