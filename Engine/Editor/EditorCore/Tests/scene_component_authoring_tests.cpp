// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include <filesystem>
#include <limits>
#include <string>

#include <gtest/gtest.h>

#include "Support/test_scratch.h"

#include "animation_library_asset.h"
#include "asset_guid.h"
#include "engine_core.h"
#include "editor.h"
#include "animation_sequencer_component.h"
#include "audio_source_component.h"
#include "camera_component.h"
#include "character_controller_component.h"
#include "collider_component.h"
#include "light_component.h"
#include "mesh_component.h"
#include "particle_emitter_component.h"
#include "rigid_3d_component.h"
#include "script_component.h"
#include "canvas_component.h"
#include "ui_button_component.h"
#include "ui_image_component.h"
#include "ui_text_component.h"

namespace UVE::Editor::Tests {
namespace {

[[nodiscard]] Core::EngineConfig MakeSceneComponentAuthoringTestConfig() {
    Core::EngineConfig config{};
    config.headless = true;
    config.logFilePath = "uve_scene_component_authoring_tests.log";
    config.settingsFilePath = "uve_scene_component_authoring_tests_settings.json";
    config.assetDatabaseFilePath = "uve_scene_component_authoring_tests_assets.json";
    config.saveDirectoryPath = "uve_scene_component_authoring_tests_saves";
    config.shaderCachePath = "uve_scene_component_authoring_tests_shader_cache";
    config.shaderSourceRealDirectory = ::UVE::Tests::RepositoryRoot() / "Engine/Runtime/RHI/Shader/built_in";
    config.shaderSourceMountPrefix = "shaders";
    return config;
}

TEST(SceneComponentAuthoringTest, SetSelectedSceneComponent_AddsAllSupportedComponentKindsAndReplaysHistory) {
    Core::EngineCore engine(MakeSceneComponentAuthoringTestConfig());
    engine.Init();
    ASSERT_TRUE(engine.Load());

    {
        Editor editor(engine.GetServices(), "uve_scene_component_authoring.uvscene");
        editor.Init();
        Scene::IEntityManager& entityManager = engine.GetServices().GetEntityManager();
        const Scene::Entity entity = editor.CreateDocumentEntity(EditorEntityKind::Empty);
        ASSERT_TRUE(entityManager.IsAlive(entity));

        const Scene::CameraComponent camera{75.0F, 0.05F, 500.0F};
        ASSERT_TRUE(editor.SetSelectedSceneComponent(EditorSceneComponentKind::Camera, camera));
        EXPECT_EQ(entityManager.GetComponent<Scene::CameraComponent>(entity).fieldOfViewDegrees, 75.0F);

        const Scene::MeshComponent mesh{Asset::AssetGuid{0x1010U}, Asset::AssetGuid{0x2020U}};
        ASSERT_TRUE(editor.SetSelectedSceneComponent(EditorSceneComponentKind::Mesh, mesh));
        EXPECT_EQ(entityManager.GetComponent<Scene::MeshComponent>(entity).meshGuid.value, 0x1010U);

        Scene::LightComponent light{};
        light.type = Scene::LightType::Spot;
        light.intensity = 4.0F;
        ASSERT_TRUE(editor.SetSelectedSceneComponent(EditorSceneComponentKind::Light, light));
        EXPECT_EQ(entityManager.GetComponent<Scene::LightComponent>(entity).type, Scene::LightType::Spot);

        Scene::ColliderComponent collider{};
        collider.shapeType = Scene::ColliderShapeType::Capsule;
        collider.radius = 0.35F;
        collider.height = 1.8F;
        ASSERT_TRUE(editor.SetSelectedSceneComponent(EditorSceneComponentKind::Collider, collider));
        EXPECT_EQ(entityManager.GetComponent<Scene::ColliderComponent>(entity).shapeType,
                  Scene::ColliderShapeType::Capsule);

        Scene::Rigid3DComponent rigidBody{};
        rigidBody.isKinematic = true;
        rigidBody.mass = 2.0F;
        rigidBody.gravityScale = 0.0F;
        ASSERT_TRUE(editor.SetSelectedSceneComponent(EditorSceneComponentKind::Rigid3D, rigidBody));
        EXPECT_TRUE(entityManager.GetComponent<Scene::Rigid3DComponent>(entity).isKinematic);

        Scene::AudioSourceComponent audio{};
        audio.audioAssetPath = "audio/impact.wav";
        audio.mixerGroup = "SFX";
        audio.playOnAwake = false;
        ASSERT_TRUE(editor.SetSelectedSceneComponent(EditorSceneComponentKind::AudioSource, audio));
        EXPECT_EQ(entityManager.GetComponent<Scene::AudioSourceComponent>(entity).audioAssetPath,
                  "audio/impact.wav");

        const Scene::ParticleEmitterComponent particles{2048U};
        ASSERT_TRUE(editor.SetSelectedSceneComponent(EditorSceneComponentKind::ParticleEmitter, particles));
        EXPECT_EQ(entityManager.GetComponent<Scene::ParticleEmitterComponent>(entity).maxParticles, 2048U);

        const Scene::ScriptComponent script{"scripts/player.uvs"};
        ASSERT_TRUE(editor.SetSelectedSceneComponent(EditorSceneComponentKind::Script, script));
        EXPECT_EQ(entityManager.GetComponent<Scene::ScriptComponent>(entity).scriptAssetPath,
                  "scripts/player.uvs");

        Scene::AnimationSequencerComponent animation;
        animation.clip = Asset::AssetGuid{77U};
        animation.speed = 1.25F;
        ASSERT_TRUE(editor.SetSelectedSceneComponent(EditorSceneComponentKind::AnimationSequencer, animation));
        EXPECT_EQ(entityManager.GetComponent<Scene::AnimationSequencerComponent>(entity).clip.value, 77U);

        ASSERT_TRUE(editor.RemoveSelectedSceneComponent(EditorSceneComponentKind::AnimationSequencer));
        EXPECT_FALSE(entityManager.HasComponent<Scene::AnimationSequencerComponent>(entity));
        ASSERT_TRUE(editor.Undo());
        EXPECT_TRUE(entityManager.HasComponent<Scene::AnimationSequencerComponent>(entity));
        EXPECT_EQ(entityManager.GetComponent<Scene::AnimationSequencerComponent>(entity).speed, 1.25F);
        ASSERT_TRUE(editor.Redo());
        EXPECT_FALSE(entityManager.HasComponent<Scene::AnimationSequencerComponent>(entity));

        editor.Shutdown();
    }
    engine.Shutdown();
}

TEST(SceneComponentAuthoringTest, SetSelectedSceneComponent_CharacterControllerAddEditRemoveUndoRedo) {
    Core::EngineCore engine(MakeSceneComponentAuthoringTestConfig());
    engine.Init();
    ASSERT_TRUE(engine.Load());

    {
        Editor editor(engine.GetServices(), "uve_scene_component_authoring_character_controller.uvscene");
        editor.Init();
        Scene::IEntityManager& entityManager = engine.GetServices().GetEntityManager();
        const Scene::Entity entity = editor.CreateDocumentEntity(EditorEntityKind::Empty);
        ASSERT_TRUE(entityManager.IsAlive(entity));

        Scene::CharacterControllerComponent characterController{};
        characterController.moveSpeed = 7.5F;
        characterController.jumpHeight = 2.0F;
        ASSERT_TRUE(editor.SetSelectedSceneComponent(EditorSceneComponentKind::CharacterController,
                                                        characterController));
        EXPECT_FLOAT_EQ(
            entityManager.GetComponent<Scene::CharacterControllerComponent>(entity).moveSpeed, 7.5F);

        ASSERT_TRUE(editor.RemoveSelectedSceneComponent(EditorSceneComponentKind::CharacterController));
        EXPECT_FALSE(entityManager.HasComponent<Scene::CharacterControllerComponent>(entity));
        ASSERT_TRUE(editor.Undo());
        EXPECT_TRUE(entityManager.HasComponent<Scene::CharacterControllerComponent>(entity));
        EXPECT_FLOAT_EQ(
            entityManager.GetComponent<Scene::CharacterControllerComponent>(entity).jumpHeight, 2.0F);
        ASSERT_TRUE(editor.Redo());
        EXPECT_FALSE(entityManager.HasComponent<Scene::CharacterControllerComponent>(entity));

        editor.Shutdown();
    }
    engine.Shutdown();
}

TEST(SceneComponentAuthoringTest, SetSelectedSceneComponent_UIComponentsAddEditRemoveUndoRedo) {
    Core::EngineCore engine(MakeSceneComponentAuthoringTestConfig());
    engine.Init();
    ASSERT_TRUE(engine.Load());

    {
        Editor editor(engine.GetServices(), "uve_scene_component_authoring_ui.uvscene");
        editor.Init();
        Scene::IEntityManager& entityManager = engine.GetServices().GetEntityManager();
        const Scene::Entity entity = editor.CreateDocumentEntity(EditorEntityKind::Empty);
        ASSERT_TRUE(entityManager.IsAlive(entity));

        Scene::CanvasComponent canvas{};
        canvas.sortOrder = 3;
        ASSERT_TRUE(editor.SetSelectedSceneComponent(EditorSceneComponentKind::Canvas, canvas));
        EXPECT_EQ(entityManager.GetComponent<Scene::CanvasComponent>(entity).sortOrder, 3);

        Scene::UITextComponent text{};
        text.text = "Score: 0";
        text.fontSize = 24.0F;
        ASSERT_TRUE(editor.SetSelectedSceneComponent(EditorSceneComponentKind::UIText, text));
        EXPECT_EQ(entityManager.GetComponent<Scene::UITextComponent>(entity).text, "Score: 0");

        Scene::UIImageComponent image{};
        image.textureAssetGuid = Asset::AssetGuid{0x3030U};
        ASSERT_TRUE(editor.SetSelectedSceneComponent(EditorSceneComponentKind::UIImage, image));
        EXPECT_EQ(entityManager.GetComponent<Scene::UIImageComponent>(entity).textureAssetGuid.value, 0x3030U);

        Scene::UIButtonComponent button{};
        button.rect.size = Math::Vector2{200.0F, 48.0F};
        ASSERT_TRUE(editor.SetSelectedSceneComponent(EditorSceneComponentKind::UIButton, button));
        EXPECT_FLOAT_EQ(entityManager.GetComponent<Scene::UIButtonComponent>(entity).rect.size.x, 200.0F);

        ASSERT_TRUE(editor.RemoveSelectedSceneComponent(EditorSceneComponentKind::UIButton));
        EXPECT_FALSE(entityManager.HasComponent<Scene::UIButtonComponent>(entity));
        ASSERT_TRUE(editor.Undo());
        EXPECT_TRUE(entityManager.HasComponent<Scene::UIButtonComponent>(entity));
        EXPECT_FLOAT_EQ(entityManager.GetComponent<Scene::UIButtonComponent>(entity).rect.size.x, 200.0F);
        ASSERT_TRUE(editor.Redo());
        EXPECT_FALSE(entityManager.HasComponent<Scene::UIButtonComponent>(entity));

        editor.Shutdown();
    }
    engine.Shutdown();
}

TEST(SceneComponentAuthoringTest, SetSelectedSceneComponent_RejectsInvalidValuesWithoutMutation) {
    Core::EngineCore engine(MakeSceneComponentAuthoringTestConfig());
    engine.Init();
    ASSERT_TRUE(engine.Load());

    {
        Editor editor(engine.GetServices(), "uve_scene_component_authoring_invalid.uvscene");
        editor.Init();
        Scene::IEntityManager& entityManager = engine.GetServices().GetEntityManager();
        const Scene::Entity entity = editor.CreateDocumentEntity(EditorEntityKind::Empty);
        ASSERT_TRUE(entityManager.IsAlive(entity));

        Scene::CameraComponent invalidCamera{180.0F, 0.1F, 100.0F};
        EXPECT_FALSE(editor.SetSelectedSceneComponent(EditorSceneComponentKind::Camera, invalidCamera));
        EXPECT_FALSE(entityManager.HasComponent<Scene::CameraComponent>(entity));

        Scene::ColliderComponent invalidCollider{};
        invalidCollider.shapeType = Scene::ColliderShapeType::Capsule;
        invalidCollider.radius = 1.0F;
        invalidCollider.height = 1.0F;
        EXPECT_FALSE(editor.SetSelectedSceneComponent(EditorSceneComponentKind::Collider, invalidCollider));
        EXPECT_FALSE(entityManager.HasComponent<Scene::ColliderComponent>(entity));

        Scene::AudioSourceComponent invalidAudio{};
        invalidAudio.spatial = true;
        invalidAudio.minDistance = 5.0F;
        invalidAudio.maxDistance = 2.0F;
        EXPECT_FALSE(editor.SetSelectedSceneComponent(EditorSceneComponentKind::AudioSource, invalidAudio));
        EXPECT_FALSE(entityManager.HasComponent<Scene::AudioSourceComponent>(entity));

        const Scene::ParticleEmitterComponent invalidParticles{0U};
        EXPECT_FALSE(editor.SetSelectedSceneComponent(EditorSceneComponentKind::ParticleEmitter, invalidParticles));
        EXPECT_FALSE(entityManager.HasComponent<Scene::ParticleEmitterComponent>(entity));

        const Scene::ScriptComponent invalidScript{"../player.uvs"};
        EXPECT_FALSE(editor.SetSelectedSceneComponent(EditorSceneComponentKind::Script, invalidScript));
        EXPECT_FALSE(entityManager.HasComponent<Scene::ScriptComponent>(entity));

        Scene::AnimationSequencerComponent invalidAnimation;
        invalidAnimation.speed = std::numeric_limits<float>::quiet_NaN();
        EXPECT_FALSE(editor.SetSelectedSceneComponent(EditorSceneComponentKind::AnimationSequencer, invalidAnimation));
        EXPECT_FALSE(entityManager.HasComponent<Scene::AnimationSequencerComponent>(entity));

        editor.Shutdown();
    }
    engine.Shutdown();
}

TEST(SceneComponentAuthoringTest, AnimationLibraryExportImportRoundTripsThePlayerListInOneUndoStep) {
    const std::filesystem::path root = ::UVE::Tests::MakeTestCaseDirectory("sequencer_library_roundtrip");
    Core::EngineCore engine(MakeSceneComponentAuthoringTestConfig());
    engine.Init();
    ASSERT_TRUE(engine.Load());
    {
        Editor editor(engine.GetServices(), "uve_sequencer_library.uvscene");
        editor.Init();
        Scene::IEntityManager& entityManager = engine.GetServices().GetEntityManager();
        const Scene::Entity entity = editor.CreateDocumentEntity(EditorEntityKind::Empty);
        ASSERT_TRUE(entityManager.IsAlive(entity));

        Scene::AnimationSequencerComponent animation;
        animation.clip = Asset::AssetGuid{0xA11CEU};
        animation.library = {Asset::AssetGuid{0xA11CEU}, Asset::AssetGuid{0xB22CEU}};
        ASSERT_TRUE(editor.SetSelectedSceneComponent(EditorSceneComponentKind::AnimationSequencer, animation));

        const std::filesystem::path libraryPath = root / "Player Library.uvanimlib";
        EXPECT_TRUE(editor.ExportAnimationSequencerToLibrary(entity, libraryPath));
        Asset::AnimationLibraryAsset library;
        ASSERT_TRUE(Asset::LoadAnimationLibraryAsset(libraryPath, library));
        ASSERT_EQ(library.entries.size(), 2U);
        EXPECT_EQ(library.entries[0].clip.value, 0xA11CEU);
        EXPECT_EQ(library.entries[1].clip.value, 0xB22CEU);
        // The GUIDs are made up and resolve to nothing: they keep their GUIDs under a
        // "(missing)" name, so importing back restores the exact same list.
        EXPECT_EQ(library.entries[0].name, "(missing)");

        // Clear the player through the real edit funnel, import the file back, and prove the
        // whole import lands as a single undo step.
        ASSERT_TRUE(editor.EditAnimationSequencer(entity, [](Scene::AnimationSequencerComponent& component) {
            component.library.clear();
            component.clip = Asset::AssetGuid{};
        }));
        EXPECT_TRUE(editor.ImportAnimationLibraryIntoSequencer(entity, libraryPath));
        const Scene::AnimationSequencerComponent& imported =
            entityManager.GetComponent<Scene::AnimationSequencerComponent>(entity);
        EXPECT_EQ(imported.library.size(), 2U);
        EXPECT_EQ(imported.clip.value, 0xA11CEU);
        EXPECT_FALSE(editor.ImportAnimationLibraryIntoSequencer(entity, libraryPath)); // nothing new
        ASSERT_TRUE(editor.Undo());
        EXPECT_TRUE(entityManager.GetComponent<Scene::AnimationSequencerComponent>(entity).library.empty());

        editor.Shutdown();
    }
    engine.Shutdown();
}

TEST(SceneComponentAuthoringTest, AnimationLibraryLinkUnlinkAreUndoStepsAndMergeDedupesInOrder) {
    const std::filesystem::path root = ::UVE::Tests::MakeTestCaseDirectory("sequencer_library_link");
    Asset::AnimationLibraryAsset file;
    file.libraryId = "Link";
    file.entries.push_back({Asset::AssetGuid{0xB22CEU}, "Clip B"});
    file.entries.push_back({Asset::AssetGuid{0xC33CEU}, "Clip C"});
    const std::filesystem::path libraryPath = root / "Link.uvanimlib";
    ASSERT_TRUE(Asset::SaveAnimationLibraryAsset(file, libraryPath));

    Core::EngineCore engine(MakeSceneComponentAuthoringTestConfig());
    engine.Init();
    ASSERT_TRUE(engine.Load());
    {
        Editor editor(engine.GetServices(), "uve_sequencer_library_link.uvscene");
        editor.Init();
        Scene::IEntityManager& entityManager = engine.GetServices().GetEntityManager();
        const Scene::Entity entity = editor.CreateDocumentEntity(EditorEntityKind::Empty);
        ASSERT_TRUE(entityManager.IsAlive(entity));

        Scene::AnimationSequencerComponent animation;
        animation.clip = Asset::AssetGuid{0xA11CEU};
        animation.library = {Asset::AssetGuid{0xA11CEU}};
        ASSERT_TRUE(editor.SetSelectedSceneComponent(EditorSceneComponentKind::AnimationSequencer, animation));

        EXPECT_TRUE(editor.LinkAnimationLibraryToSequencer(entity, libraryPath));
        const Asset::AssetGuid linked =
            entityManager.GetComponent<Scene::AnimationSequencerComponent>(entity).libraryRef;
        EXPECT_NE(linked, Asset::AssetGuid{});
        EXPECT_EQ(engine.GetServices().GetAssetDatabase().Resolve(linked).filename().string(), "Link.uvanimlib");

        EXPECT_TRUE(editor.UnlinkAnimationLibraryFromSequencer(entity));
        EXPECT_EQ(entityManager.GetComponent<Scene::AnimationSequencerComponent>(entity).libraryRef,
                  Asset::AssetGuid{});
        EXPECT_FALSE(editor.UnlinkAnimationLibraryFromSequencer(entity)); // nothing to unlink

        ASSERT_TRUE(editor.Undo()); // the unlink goes: linked again
        EXPECT_NE(entityManager.GetComponent<Scene::AnimationSequencerComponent>(entity).libraryRef,
                  Asset::AssetGuid{});
        ASSERT_TRUE(editor.Undo()); // the link goes: unlinked again
        EXPECT_EQ(entityManager.GetComponent<Scene::AnimationSequencerComponent>(entity).libraryRef,
                  Asset::AssetGuid{});

        // The merge the picker offers: owned, the clip first when unlisted, then linked in order.
        const std::vector<Asset::AssetGuid> owned{Asset::AssetGuid{0xA11CEU}, Asset::AssetGuid{0xB22CEU}};
        const std::vector<Asset::AssetGuid> linkedClips{Asset::AssetGuid{0xB22CEU}, Asset::AssetGuid{0xD44DEU}};
        EXPECT_EQ(Editor::MergeSequencerClipLists(owned, Asset::AssetGuid{0xC33CEU}, linkedClips),
                  (std::vector<Asset::AssetGuid>{Asset::AssetGuid{0xC33CEU}, Asset::AssetGuid{0xA11CEU},
                                                    Asset::AssetGuid{0xB22CEU}, Asset::AssetGuid{0xD44DEU}}));
        EXPECT_TRUE(Editor::MergeSequencerClipLists({}, Asset::AssetGuid{}, {}).empty());

        editor.Shutdown();
    }
    engine.Shutdown();
}

} // namespace
} // namespace UVE::Editor::Tests
