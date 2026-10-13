// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include <filesystem>
#include <limits>
#include <string>

#include <gtest/gtest.h>

#include "Support/test_scratch_uve.h"

#include "uve/asset/animation_library_asset_uve.h"
#include "uve/asset/asset_guid_uve.h"
#include "uve/core/engine_core_uve.h"
#include "uve/editor/editor_uve.h"
#include "uve/component/animation_sequencer_component_uve.h"
#include "uve/component/audio_source_component_uve.h"
#include "uve/component/camera_component_uve.h"
#include "uve/component/character_controller_component_uve.h"
#include "uve/component/collider_component_uve.h"
#include "uve/component/light_component_uve.h"
#include "uve/component/mesh_component_uve.h"
#include "uve/component/particle_emitter_component_uve.h"
#include "uve/component/rigid_3d_component_uve.h"
#include "uve/component/script_component_uve.h"
#include "uve/component/canvas_component_uve.h"
#include "uve/component/ui_button_component_uve.h"
#include "uve/component/ui_image_component_uve.h"
#include "uve/component/ui_text_component_uve.h"

namespace UVE::Editor::Tests {
namespace {

[[nodiscard]] Core::EngineConfigUVE MakeSceneComponentAuthoringTestConfigUVE() {
    Core::EngineConfigUVE config{};
    config.headlessUVE = true;
    config.logFilePath = "uve_scene_component_authoring_tests.log";
    config.settingsFilePath = "uve_scene_component_authoring_tests_settings.json";
    config.assetDatabaseFilePath = "uve_scene_component_authoring_tests_assets.json";
    config.saveDirectoryPath = "uve_scene_component_authoring_tests_saves";
    config.shaderCachePath = "uve_scene_component_authoring_tests_shader_cache";
    config.shaderSourceRealDirectoryUVE = ::UVE::Tests::RepositoryRootUVE() / "Engine/Runtime/RHI/Shader/built_in";
    config.shaderSourceMountPrefixUVE = "shaders";
    return config;
}

TEST(SceneComponentAuthoringUVETest, SetSelectedSceneComponentUVE_AddsAllSupportedComponentKindsAndReplaysHistory) {
    Core::EngineCoreUVE engine(MakeSceneComponentAuthoringTestConfigUVE());
    engine.Init();
    ASSERT_TRUE(engine.Load());

    {
        EditorUVE editor(engine.GetServicesUVE(), "uve_scene_component_authoring.uvscene");
        editor.InitUVE();
        Scene::IEntityManagerUVE& entityManager = engine.GetServicesUVE().GetEntityManagerUVE();
        const Scene::EntityUVE entity = editor.CreateDocumentEntityUVE(EditorEntityKindUVE::Empty);
        ASSERT_TRUE(entityManager.IsAliveUVE(entity));

        const Scene::CameraComponentUVE camera{75.0F, 0.05F, 500.0F};
        ASSERT_TRUE(editor.SetSelectedSceneComponentUVE(EditorSceneComponentKindUVE::Camera, camera));
        EXPECT_EQ(entityManager.GetComponentUVE<Scene::CameraComponentUVE>(entity).fieldOfViewDegrees, 75.0F);

        const Scene::MeshComponentUVE mesh{Asset::AssetGuidUVE{0x1010U}, Asset::AssetGuidUVE{0x2020U}};
        ASSERT_TRUE(editor.SetSelectedSceneComponentUVE(EditorSceneComponentKindUVE::Mesh, mesh));
        EXPECT_EQ(entityManager.GetComponentUVE<Scene::MeshComponentUVE>(entity).meshGuid.value, 0x1010U);

        Scene::LightComponentUVE light{};
        light.type = Scene::LightTypeUVE::Spot;
        light.intensity = 4.0F;
        ASSERT_TRUE(editor.SetSelectedSceneComponentUVE(EditorSceneComponentKindUVE::Light, light));
        EXPECT_EQ(entityManager.GetComponentUVE<Scene::LightComponentUVE>(entity).type, Scene::LightTypeUVE::Spot);

        Scene::ColliderComponentUVE collider{};
        collider.shapeType = Scene::ColliderShapeTypeUVE::Capsule;
        collider.radius = 0.35F;
        collider.height = 1.8F;
        ASSERT_TRUE(editor.SetSelectedSceneComponentUVE(EditorSceneComponentKindUVE::Collider, collider));
        EXPECT_EQ(entityManager.GetComponentUVE<Scene::ColliderComponentUVE>(entity).shapeType,
                  Scene::ColliderShapeTypeUVE::Capsule);

        Scene::Rigid3DComponentUVE rigidBody{};
        rigidBody.isKinematic = true;
        rigidBody.mass = 2.0F;
        rigidBody.gravityScale = 0.0F;
        ASSERT_TRUE(editor.SetSelectedSceneComponentUVE(EditorSceneComponentKindUVE::Rigid3D, rigidBody));
        EXPECT_TRUE(entityManager.GetComponentUVE<Scene::Rigid3DComponentUVE>(entity).isKinematic);

        Scene::AudioSourceComponentUVE audio{};
        audio.audioAssetPath = "audio/impact.wav";
        audio.mixerGroup = "SFX";
        audio.playOnAwake = false;
        ASSERT_TRUE(editor.SetSelectedSceneComponentUVE(EditorSceneComponentKindUVE::AudioSource, audio));
        EXPECT_EQ(entityManager.GetComponentUVE<Scene::AudioSourceComponentUVE>(entity).audioAssetPath,
                  "audio/impact.wav");

        const Scene::ParticleEmitterComponentUVE particles{2048U};
        ASSERT_TRUE(editor.SetSelectedSceneComponentUVE(EditorSceneComponentKindUVE::ParticleEmitter, particles));
        EXPECT_EQ(entityManager.GetComponentUVE<Scene::ParticleEmitterComponentUVE>(entity).maxParticles, 2048U);

        const Scene::ScriptComponentUVE script{"scripts/player.uvs"};
        ASSERT_TRUE(editor.SetSelectedSceneComponentUVE(EditorSceneComponentKindUVE::Script, script));
        EXPECT_EQ(entityManager.GetComponentUVE<Scene::ScriptComponentUVE>(entity).scriptAssetPath,
                  "scripts/player.uvs");

        Scene::AnimationSequencerComponentUVE animation;
        animation.clip = Asset::AssetGuidUVE{77U};
        animation.speed = 1.25F;
        ASSERT_TRUE(editor.SetSelectedSceneComponentUVE(EditorSceneComponentKindUVE::AnimationSequencer, animation));
        EXPECT_EQ(entityManager.GetComponentUVE<Scene::AnimationSequencerComponentUVE>(entity).clip.value, 77U);

        ASSERT_TRUE(editor.RemoveSelectedSceneComponentUVE(EditorSceneComponentKindUVE::AnimationSequencer));
        EXPECT_FALSE(entityManager.HasComponentUVE<Scene::AnimationSequencerComponentUVE>(entity));
        ASSERT_TRUE(editor.UndoUVE());
        EXPECT_TRUE(entityManager.HasComponentUVE<Scene::AnimationSequencerComponentUVE>(entity));
        EXPECT_EQ(entityManager.GetComponentUVE<Scene::AnimationSequencerComponentUVE>(entity).speed, 1.25F);
        ASSERT_TRUE(editor.RedoUVE());
        EXPECT_FALSE(entityManager.HasComponentUVE<Scene::AnimationSequencerComponentUVE>(entity));

        editor.ShutdownUVE();
    }
    engine.Shutdown();
}

TEST(SceneComponentAuthoringUVETest, SetSelectedSceneComponentUVE_CharacterControllerAddEditRemoveUndoRedo) {
    Core::EngineCoreUVE engine(MakeSceneComponentAuthoringTestConfigUVE());
    engine.Init();
    ASSERT_TRUE(engine.Load());

    {
        EditorUVE editor(engine.GetServicesUVE(), "uve_scene_component_authoring_character_controller.uvscene");
        editor.InitUVE();
        Scene::IEntityManagerUVE& entityManager = engine.GetServicesUVE().GetEntityManagerUVE();
        const Scene::EntityUVE entity = editor.CreateDocumentEntityUVE(EditorEntityKindUVE::Empty);
        ASSERT_TRUE(entityManager.IsAliveUVE(entity));

        Scene::CharacterControllerComponentUVE characterController{};
        characterController.moveSpeed = 7.5F;
        characterController.jumpHeight = 2.0F;
        ASSERT_TRUE(editor.SetSelectedSceneComponentUVE(EditorSceneComponentKindUVE::CharacterController,
                                                        characterController));
        EXPECT_FLOAT_EQ(
            entityManager.GetComponentUVE<Scene::CharacterControllerComponentUVE>(entity).moveSpeed, 7.5F);

        ASSERT_TRUE(editor.RemoveSelectedSceneComponentUVE(EditorSceneComponentKindUVE::CharacterController));
        EXPECT_FALSE(entityManager.HasComponentUVE<Scene::CharacterControllerComponentUVE>(entity));
        ASSERT_TRUE(editor.UndoUVE());
        EXPECT_TRUE(entityManager.HasComponentUVE<Scene::CharacterControllerComponentUVE>(entity));
        EXPECT_FLOAT_EQ(
            entityManager.GetComponentUVE<Scene::CharacterControllerComponentUVE>(entity).jumpHeight, 2.0F);
        ASSERT_TRUE(editor.RedoUVE());
        EXPECT_FALSE(entityManager.HasComponentUVE<Scene::CharacterControllerComponentUVE>(entity));

        editor.ShutdownUVE();
    }
    engine.Shutdown();
}

TEST(SceneComponentAuthoringUVETest, SetSelectedSceneComponentUVE_UIComponentsAddEditRemoveUndoRedo) {
    Core::EngineCoreUVE engine(MakeSceneComponentAuthoringTestConfigUVE());
    engine.Init();
    ASSERT_TRUE(engine.Load());

    {
        EditorUVE editor(engine.GetServicesUVE(), "uve_scene_component_authoring_ui.uvscene");
        editor.InitUVE();
        Scene::IEntityManagerUVE& entityManager = engine.GetServicesUVE().GetEntityManagerUVE();
        const Scene::EntityUVE entity = editor.CreateDocumentEntityUVE(EditorEntityKindUVE::Empty);
        ASSERT_TRUE(entityManager.IsAliveUVE(entity));

        Scene::CanvasComponentUVE canvas{};
        canvas.sortOrder = 3;
        ASSERT_TRUE(editor.SetSelectedSceneComponentUVE(EditorSceneComponentKindUVE::Canvas, canvas));
        EXPECT_EQ(entityManager.GetComponentUVE<Scene::CanvasComponentUVE>(entity).sortOrder, 3);

        Scene::UITextComponentUVE text{};
        text.text = "Score: 0";
        text.fontSize = 24.0F;
        ASSERT_TRUE(editor.SetSelectedSceneComponentUVE(EditorSceneComponentKindUVE::UIText, text));
        EXPECT_EQ(entityManager.GetComponentUVE<Scene::UITextComponentUVE>(entity).text, "Score: 0");

        Scene::UIImageComponentUVE image{};
        image.textureAssetGuid = Asset::AssetGuidUVE{0x3030U};
        ASSERT_TRUE(editor.SetSelectedSceneComponentUVE(EditorSceneComponentKindUVE::UIImage, image));
        EXPECT_EQ(entityManager.GetComponentUVE<Scene::UIImageComponentUVE>(entity).textureAssetGuid.value, 0x3030U);

        Scene::UIButtonComponentUVE button{};
        button.rect.size = Math::Vector2UVE{200.0F, 48.0F};
        ASSERT_TRUE(editor.SetSelectedSceneComponentUVE(EditorSceneComponentKindUVE::UIButton, button));
        EXPECT_FLOAT_EQ(entityManager.GetComponentUVE<Scene::UIButtonComponentUVE>(entity).rect.size.x, 200.0F);

        ASSERT_TRUE(editor.RemoveSelectedSceneComponentUVE(EditorSceneComponentKindUVE::UIButton));
        EXPECT_FALSE(entityManager.HasComponentUVE<Scene::UIButtonComponentUVE>(entity));
        ASSERT_TRUE(editor.UndoUVE());
        EXPECT_TRUE(entityManager.HasComponentUVE<Scene::UIButtonComponentUVE>(entity));
        EXPECT_FLOAT_EQ(entityManager.GetComponentUVE<Scene::UIButtonComponentUVE>(entity).rect.size.x, 200.0F);
        ASSERT_TRUE(editor.RedoUVE());
        EXPECT_FALSE(entityManager.HasComponentUVE<Scene::UIButtonComponentUVE>(entity));

        editor.ShutdownUVE();
    }
    engine.Shutdown();
}

TEST(SceneComponentAuthoringUVETest, SetSelectedSceneComponentUVE_RejectsInvalidValuesWithoutMutation) {
    Core::EngineCoreUVE engine(MakeSceneComponentAuthoringTestConfigUVE());
    engine.Init();
    ASSERT_TRUE(engine.Load());

    {
        EditorUVE editor(engine.GetServicesUVE(), "uve_scene_component_authoring_invalid.uvscene");
        editor.InitUVE();
        Scene::IEntityManagerUVE& entityManager = engine.GetServicesUVE().GetEntityManagerUVE();
        const Scene::EntityUVE entity = editor.CreateDocumentEntityUVE(EditorEntityKindUVE::Empty);
        ASSERT_TRUE(entityManager.IsAliveUVE(entity));

        Scene::CameraComponentUVE invalidCamera{180.0F, 0.1F, 100.0F};
        EXPECT_FALSE(editor.SetSelectedSceneComponentUVE(EditorSceneComponentKindUVE::Camera, invalidCamera));
        EXPECT_FALSE(entityManager.HasComponentUVE<Scene::CameraComponentUVE>(entity));

        Scene::ColliderComponentUVE invalidCollider{};
        invalidCollider.shapeType = Scene::ColliderShapeTypeUVE::Capsule;
        invalidCollider.radius = 1.0F;
        invalidCollider.height = 1.0F;
        EXPECT_FALSE(editor.SetSelectedSceneComponentUVE(EditorSceneComponentKindUVE::Collider, invalidCollider));
        EXPECT_FALSE(entityManager.HasComponentUVE<Scene::ColliderComponentUVE>(entity));

        Scene::AudioSourceComponentUVE invalidAudio{};
        invalidAudio.spatial = true;
        invalidAudio.minDistance = 5.0F;
        invalidAudio.maxDistance = 2.0F;
        EXPECT_FALSE(editor.SetSelectedSceneComponentUVE(EditorSceneComponentKindUVE::AudioSource, invalidAudio));
        EXPECT_FALSE(entityManager.HasComponentUVE<Scene::AudioSourceComponentUVE>(entity));

        const Scene::ParticleEmitterComponentUVE invalidParticles{0U};
        EXPECT_FALSE(editor.SetSelectedSceneComponentUVE(EditorSceneComponentKindUVE::ParticleEmitter, invalidParticles));
        EXPECT_FALSE(entityManager.HasComponentUVE<Scene::ParticleEmitterComponentUVE>(entity));

        const Scene::ScriptComponentUVE invalidScript{"../player.uvs"};
        EXPECT_FALSE(editor.SetSelectedSceneComponentUVE(EditorSceneComponentKindUVE::Script, invalidScript));
        EXPECT_FALSE(entityManager.HasComponentUVE<Scene::ScriptComponentUVE>(entity));

        Scene::AnimationSequencerComponentUVE invalidAnimation;
        invalidAnimation.speed = std::numeric_limits<float>::quiet_NaN();
        EXPECT_FALSE(editor.SetSelectedSceneComponentUVE(EditorSceneComponentKindUVE::AnimationSequencer, invalidAnimation));
        EXPECT_FALSE(entityManager.HasComponentUVE<Scene::AnimationSequencerComponentUVE>(entity));

        editor.ShutdownUVE();
    }
    engine.Shutdown();
}

TEST(SceneComponentAuthoringUVETest, AnimationLibraryExportImportRoundTripsThePlayerListInOneUndoStep) {
    const std::filesystem::path root = ::UVE::Tests::MakeTestCaseDirectoryUVE("sequencer_library_roundtrip");
    Core::EngineCoreUVE engine(MakeSceneComponentAuthoringTestConfigUVE());
    engine.Init();
    ASSERT_TRUE(engine.Load());
    {
        EditorUVE editor(engine.GetServicesUVE(), "uve_sequencer_library.uvscene");
        editor.InitUVE();
        Scene::IEntityManagerUVE& entityManager = engine.GetServicesUVE().GetEntityManagerUVE();
        const Scene::EntityUVE entity = editor.CreateDocumentEntityUVE(EditorEntityKindUVE::Empty);
        ASSERT_TRUE(entityManager.IsAliveUVE(entity));

        Scene::AnimationSequencerComponentUVE animation;
        animation.clip = Asset::AssetGuidUVE{0xA11CEU};
        animation.library = {Asset::AssetGuidUVE{0xA11CEU}, Asset::AssetGuidUVE{0xB22CEU}};
        ASSERT_TRUE(editor.SetSelectedSceneComponentUVE(EditorSceneComponentKindUVE::AnimationSequencer, animation));

        const std::filesystem::path libraryPath = root / "Player Library.uvanimlib";
        EXPECT_TRUE(editor.ExportAnimationSequencerToLibraryUVE(entity, libraryPath));
        Asset::AnimationLibraryAssetUVE library;
        ASSERT_TRUE(Asset::LoadAnimationLibraryAssetUVE(libraryPath, library));
        ASSERT_EQ(library.entries.size(), 2U);
        EXPECT_EQ(library.entries[0].clip.value, 0xA11CEU);
        EXPECT_EQ(library.entries[1].clip.value, 0xB22CEU);
        // The GUIDs are made up and resolve to nothing: they keep their GUIDs under a
        // "(missing)" name, so importing back restores the exact same list.
        EXPECT_EQ(library.entries[0].name, "(missing)");

        // Clear the player through the real edit funnel, import the file back, and prove the
        // whole import lands as a single undo step.
        ASSERT_TRUE(editor.EditAnimationSequencerUVE(entity, [](Scene::AnimationSequencerComponentUVE& component) {
            component.library.clear();
            component.clip = Asset::AssetGuidUVE{};
        }));
        EXPECT_TRUE(editor.ImportAnimationLibraryIntoSequencerUVE(entity, libraryPath));
        const Scene::AnimationSequencerComponentUVE& imported =
            entityManager.GetComponentUVE<Scene::AnimationSequencerComponentUVE>(entity);
        EXPECT_EQ(imported.library.size(), 2U);
        EXPECT_EQ(imported.clip.value, 0xA11CEU);
        EXPECT_FALSE(editor.ImportAnimationLibraryIntoSequencerUVE(entity, libraryPath)); // nothing new
        ASSERT_TRUE(editor.UndoUVE());
        EXPECT_TRUE(entityManager.GetComponentUVE<Scene::AnimationSequencerComponentUVE>(entity).library.empty());

        editor.ShutdownUVE();
    }
    engine.Shutdown();
}

TEST(SceneComponentAuthoringUVETest, AnimationLibraryLinkUnlinkAreUndoStepsAndMergeDedupesInOrder) {
    const std::filesystem::path root = ::UVE::Tests::MakeTestCaseDirectoryUVE("sequencer_library_link");
    Asset::AnimationLibraryAssetUVE file;
    file.libraryId = "Link";
    file.entries.push_back({Asset::AssetGuidUVE{0xB22CEU}, "Clip B"});
    file.entries.push_back({Asset::AssetGuidUVE{0xC33CEU}, "Clip C"});
    const std::filesystem::path libraryPath = root / "Link.uvanimlib";
    ASSERT_TRUE(Asset::SaveAnimationLibraryAssetUVE(file, libraryPath));

    Core::EngineCoreUVE engine(MakeSceneComponentAuthoringTestConfigUVE());
    engine.Init();
    ASSERT_TRUE(engine.Load());
    {
        EditorUVE editor(engine.GetServicesUVE(), "uve_sequencer_library_link.uvscene");
        editor.InitUVE();
        Scene::IEntityManagerUVE& entityManager = engine.GetServicesUVE().GetEntityManagerUVE();
        const Scene::EntityUVE entity = editor.CreateDocumentEntityUVE(EditorEntityKindUVE::Empty);
        ASSERT_TRUE(entityManager.IsAliveUVE(entity));

        Scene::AnimationSequencerComponentUVE animation;
        animation.clip = Asset::AssetGuidUVE{0xA11CEU};
        animation.library = {Asset::AssetGuidUVE{0xA11CEU}};
        ASSERT_TRUE(editor.SetSelectedSceneComponentUVE(EditorSceneComponentKindUVE::AnimationSequencer, animation));

        EXPECT_TRUE(editor.LinkAnimationLibraryToSequencerUVE(entity, libraryPath));
        const Asset::AssetGuidUVE linked =
            entityManager.GetComponentUVE<Scene::AnimationSequencerComponentUVE>(entity).libraryRef;
        EXPECT_NE(linked, Asset::AssetGuidUVE{});
        EXPECT_EQ(engine.GetServicesUVE().GetAssetDatabaseUVE().ResolveUVE(linked).filename().string(), "Link.uvanimlib");

        EXPECT_TRUE(editor.UnlinkAnimationLibraryFromSequencerUVE(entity));
        EXPECT_EQ(entityManager.GetComponentUVE<Scene::AnimationSequencerComponentUVE>(entity).libraryRef,
                  Asset::AssetGuidUVE{});
        EXPECT_FALSE(editor.UnlinkAnimationLibraryFromSequencerUVE(entity)); // nothing to unlink

        ASSERT_TRUE(editor.UndoUVE()); // the unlink goes: linked again
        EXPECT_NE(entityManager.GetComponentUVE<Scene::AnimationSequencerComponentUVE>(entity).libraryRef,
                  Asset::AssetGuidUVE{});
        ASSERT_TRUE(editor.UndoUVE()); // the link goes: unlinked again
        EXPECT_EQ(entityManager.GetComponentUVE<Scene::AnimationSequencerComponentUVE>(entity).libraryRef,
                  Asset::AssetGuidUVE{});

        // The merge the picker offers: owned, the clip first when unlisted, then linked in order.
        const std::vector<Asset::AssetGuidUVE> owned{Asset::AssetGuidUVE{0xA11CEU}, Asset::AssetGuidUVE{0xB22CEU}};
        const std::vector<Asset::AssetGuidUVE> linkedClips{Asset::AssetGuidUVE{0xB22CEU}, Asset::AssetGuidUVE{0xD44DEU}};
        EXPECT_EQ(EditorUVE::MergeSequencerClipListsUVE(owned, Asset::AssetGuidUVE{0xC33CEU}, linkedClips),
                  (std::vector<Asset::AssetGuidUVE>{Asset::AssetGuidUVE{0xC33CEU}, Asset::AssetGuidUVE{0xA11CEU},
                                                    Asset::AssetGuidUVE{0xB22CEU}, Asset::AssetGuidUVE{0xD44DEU}}));
        EXPECT_TRUE(EditorUVE::MergeSequencerClipListsUVE({}, Asset::AssetGuidUVE{}, {}).empty());

        editor.ShutdownUVE();
    }
    engine.Shutdown();
}

} // namespace
} // namespace UVE::Editor::Tests
