// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#include "audio_source_system.h"

#include <algorithm>
#include <limits>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include <gtest/gtest.h>

#include "audio_system.h"
#include "i_audio_clip_resolver.h"
#include "null_audio_device.h"
#include "event_system.h"
#include "memory_manager.h"
#include "audio_source_component.h"
#include "world_transform_component.h"
#include "entity_manager.h"
#include "scene_graph.h"

namespace UVE::Audio::Tests {
namespace {

class AudioSourceSystemTest : public ::testing::Test {
protected:
    Memory::MemoryManager memoryManager;
    Events::EventSystem eventSystem;
    Scene::EntityManager entityManager{memoryManager.GetDefaultAllocator(), eventSystem};
    Scene::SceneGraph sceneGraph;
    NullAudioDevice device;
    AudioSystem audioSystem{device};
    AudioSourceSystem audioSourceSystem;

    Scene::Entity MakeAudioEntity(Scene::AudioSourceComponent audioSource) {
        const Scene::Entity entity = entityManager.CreateEntity();
        sceneGraph.AttachTransform(entityManager, entity, Scene::TransformComponent{});
        sceneGraph.Update(entityManager);
        entityManager.AddComponent<Scene::AudioSourceComponent>(entity, std::move(audioSource));
        return entity;
    }

    [[nodiscard]] bool AnyRecordedPlayCall() const {
        const std::vector<RecordedAudioCall>& recorded = device.GetRecordedCalls();
        return std::any_of(recorded.begin(), recorded.end(),
                            [](const RecordedAudioCall& call) { return std::holds_alternative<PlayVoiceCall>(call); });
    }
};

class RejectingClipResolver final : public IAudioClipResolver {
public:
    [[nodiscard]] AudioClipResolution ResolveAudioClip(const std::string_view authoredPath) const override {
        if (authoredPath == "rejected.wav") {
            return AudioClipResolution{false, {}, "test rejection"};
        }
        return AudioClipResolution{true, std::string(authoredPath), {}};
    }
};

TEST(AudioSourceComponentTest, IsAudioSourceComponentValid_RejectsUnsafeParameters) {
    EXPECT_TRUE(Scene::IsAudioSourceComponentValid(Scene::AudioSourceComponent{}));

    Scene::AudioSourceComponent invalid = {};
    invalid.volume = -0.1F;
    EXPECT_FALSE(Scene::IsAudioSourceComponentValid(invalid));
    invalid = {};
    invalid.pitch = 0.0F;
    EXPECT_FALSE(Scene::IsAudioSourceComponentValid(invalid));
    invalid = {};
    invalid.pitch = std::numeric_limits<float>::quiet_NaN();
    EXPECT_FALSE(Scene::IsAudioSourceComponentValid(invalid));
    invalid = {};
    invalid.minDistance = 0.0F;
    EXPECT_FALSE(Scene::IsAudioSourceComponentValid(invalid));
    invalid = {};
    invalid.maxDistance = invalid.minDistance;
    EXPECT_FALSE(Scene::IsAudioSourceComponentValid(invalid));
    invalid = {};
    invalid.spatial = false;
    invalid.minDistance = 0.0F;
    invalid.maxDistance = 0.0F;
    EXPECT_TRUE(Scene::IsAudioSourceComponentValid(invalid));
    invalid = {};
    invalid.attenuationCurve = static_cast<Scene::AudioAttenuationCurve>(99U);
    EXPECT_FALSE(Scene::IsAudioSourceComponentValid(invalid));
    invalid = {};
    invalid.mixerGroup.assign(65U, 'X');
    EXPECT_FALSE(Scene::IsAudioSourceComponentValid(invalid));
    invalid = {};
    invalid.mixerGroup = std::string("SFX\0invalid", 11U);
    EXPECT_FALSE(Scene::IsAudioSourceComponentValid(invalid));
}

TEST_F(AudioSourceSystemTest, PlayOnAwakeEntity_CreatesAndPlaysSourceOnFirstSync) {
    Scene::AudioSourceComponent audioSource;
    audioSource.playOnAwake = true;
    MakeAudioEntity(audioSource);

    audioSourceSystem.Sync(entityManager, audioSystem);

    EXPECT_EQ(device.GetLiveVoiceCount(), 1U);
    EXPECT_TRUE(AnyRecordedPlayCall());
}

TEST_F(AudioSourceSystemTest, AuthoredMixerGroup_RoutesAndReroutesWithoutReplacingVoice) {
    ASSERT_TRUE(audioSystem.RegisterMixerGroup("SFX"));
    ASSERT_TRUE(audioSystem.RegisterMixerGroup("Music"));
    Scene::AudioSourceComponent audioSource;
    audioSource.mixerGroup = "SFX";
    const Scene::Entity entity = MakeAudioEntity(audioSource);

    audioSourceSystem.Sync(entityManager, audioSystem);
    ASSERT_EQ(device.GetLiveVoiceCount(), 1U);
    auto diagnostics = audioSystem.GetMixerDiagnostics();
    ASSERT_EQ(diagnostics.groups.size(), 3U);
    const auto sourceCountFor = [](const AudioMixerDiagnostics& snapshot, const std::string_view name) {
        const auto iterator = std::find_if(snapshot.groups.begin(), snapshot.groups.end(),
                                           [name](const AudioMixerGroupSnapshot& group) { return group.name == name; });
        return iterator == snapshot.groups.end() ? 0U : iterator->sourceCount;
    };
    EXPECT_EQ(sourceCountFor(diagnostics, "SFX"), 1U);

    device.ClearRecordedCalls();
    entityManager.GetComponent<Scene::AudioSourceComponent>(entity).mixerGroup = "Music";
    audioSourceSystem.Sync(entityManager, audioSystem);
    EXPECT_EQ(device.GetLiveVoiceCount(), 1U);
    diagnostics = audioSystem.GetMixerDiagnostics();
    EXPECT_EQ(sourceCountFor(diagnostics, "SFX"), 0U);
    EXPECT_EQ(sourceCountFor(diagnostics, "Music"), 1U);
}

TEST_F(AudioSourceSystemTest, PlayOnAwakeFalse_CreatesButDoesNotAutoPlay) {
    Scene::AudioSourceComponent audioSource;
    audioSource.playOnAwake = false;
    MakeAudioEntity(audioSource);

    audioSourceSystem.Sync(entityManager, audioSystem);

    EXPECT_EQ(device.GetLiveVoiceCount(), 1U);
    EXPECT_FALSE(AnyRecordedPlayCall());
}

TEST_F(AudioSourceSystemTest, MovingEntity_UpdatesPushedPositionAndGain) {
    Scene::AudioSourceComponent audioSource;
    audioSource.spatial = true;
    audioSource.minDistance = 1.0F;
    audioSource.maxDistance = 9.0F;
    const Scene::Entity entity = MakeAudioEntity(audioSource);

    audioSourceSystem.Sync(entityManager, audioSystem);
    audioSystem.Update(0.0F);

    auto local = entityManager.GetComponent<Scene::TransformComponent>(entity);
    local.localPosition = Math::Vector3{5.0F, 0.0F, 0.0F};
    sceneGraph.SetLocalTransform(entityManager, entity, local);
    sceneGraph.Update(entityManager);

    device.ClearRecordedCalls();
    audioSourceSystem.Sync(entityManager, audioSystem);
    audioSystem.Update(0.0F);

    const std::vector<RecordedAudioCall>& recorded = device.GetRecordedCalls();
    const auto iterator = std::find_if(recorded.begin(), recorded.end(), [](const RecordedAudioCall& call) {
        return std::holds_alternative<SetVoiceParamsCall>(call);
    });
    ASSERT_NE(iterator, recorded.end());
    const auto& params = std::get<SetVoiceParamsCall>(*iterator).params;
    EXPECT_EQ(params.position, (Math::Vector3{5.0F, 0.0F, 0.0F}));
    // distance=5, minDistance=1, maxDistance=9 -> exact midpoint -> gain 0.5.
    EXPECT_FLOAT_EQ(params.gain, 0.5F);
}

TEST_F(AudioSourceSystemTest, InvalidAudioSource_SkipsCreationAndPreservesExistingVoice) {
    Scene::AudioSourceComponent invalid = {};
    invalid.spatial = true;
    invalid.minDistance = 2.0F;
    invalid.maxDistance = 2.0F;
    const Scene::Entity entity = MakeAudioEntity(invalid);

    audioSourceSystem.Sync(entityManager, audioSystem);
    EXPECT_EQ(device.GetLiveVoiceCount(), 0U);
    EXPECT_TRUE(device.GetRecordedCalls().empty());

    entityManager.GetComponent<Scene::AudioSourceComponent>(entity).maxDistance = 4.0F;
    audioSourceSystem.Sync(entityManager, audioSystem);
    ASSERT_EQ(device.GetLiveVoiceCount(), 1U);
    device.ClearRecordedCalls();

    entityManager.GetComponent<Scene::AudioSourceComponent>(entity).maxDistance = 2.0F;
    audioSourceSystem.Sync(entityManager, audioSystem);

    EXPECT_EQ(device.GetLiveVoiceCount(), 1U);
    EXPECT_TRUE(device.GetRecordedCalls().empty());
}

TEST_F(AudioSourceSystemTest, RemovingAudioSourceComponent_DestroysVoice) {
    Scene::AudioSourceComponent audioSource;
    const Scene::Entity entity = MakeAudioEntity(audioSource);

    audioSourceSystem.Sync(entityManager, audioSystem);
    ASSERT_EQ(device.GetLiveVoiceCount(), 1U);

    entityManager.RemoveComponent<Scene::AudioSourceComponent>(entity);
    audioSourceSystem.Sync(entityManager, audioSystem);

    EXPECT_EQ(device.GetLiveVoiceCount(), 0U);
}

TEST_F(AudioSourceSystemTest, DestroyingEntity_DestroysVoice) {
    Scene::AudioSourceComponent audioSource;
    const Scene::Entity entity = MakeAudioEntity(audioSource);

    audioSourceSystem.Sync(entityManager, audioSystem);
    ASSERT_EQ(device.GetLiveVoiceCount(), 1U);

    entityManager.DestroyEntity(entity);
    audioSourceSystem.Sync(entityManager, audioSystem);

    EXPECT_EQ(device.GetLiveVoiceCount(), 0U);
}

TEST_F(AudioSourceSystemTest, RepeatedSync_UnchangedScene_DoesNotCreateASecondVoice) {
    Scene::AudioSourceComponent audioSource;
    MakeAudioEntity(audioSource);

    audioSourceSystem.Sync(entityManager, audioSystem);
    ASSERT_EQ(device.GetLiveVoiceCount(), 1U);

    audioSourceSystem.Sync(entityManager, audioSystem);
    audioSourceSystem.Sync(entityManager, audioSystem);

    EXPECT_EQ(device.GetLiveVoiceCount(), 1U);
}

TEST_F(AudioSourceSystemTest, ChangingAudioAssetPath_ReplacesVoiceAndPreservesCount) {
    Scene::AudioSourceComponent audioSource;
    audioSource.audioAssetPath = "a.wav";
    audioSource.playOnAwake = true;
    const Scene::Entity entity = MakeAudioEntity(audioSource);

    audioSourceSystem.Sync(entityManager, audioSystem);
    ASSERT_EQ(device.GetLiveVoiceCount(), 1U);
    device.ClearRecordedCalls();

    entityManager.GetComponent<Scene::AudioSourceComponent>(entity).audioAssetPath = "b.wav";
    audioSourceSystem.Sync(entityManager, audioSystem);

    EXPECT_EQ(device.GetLiveVoiceCount(), 1U);
    EXPECT_TRUE(AnyRecordedPlayCall());
}

TEST_F(AudioSourceSystemTest, ChangingLooping_ReplacesVoiceAtomically) {
    Scene::AudioSourceComponent audioSource;
    audioSource.looping = false;
    audioSource.playOnAwake = true;
    const Scene::Entity entity = MakeAudioEntity(audioSource);

    audioSourceSystem.Sync(entityManager, audioSystem);
    ASSERT_EQ(device.GetLiveVoiceCount(), 1U);
    device.ClearRecordedCalls();

    entityManager.GetComponent<Scene::AudioSourceComponent>(entity).looping = true;
    audioSourceSystem.Sync(entityManager, audioSystem);

    EXPECT_EQ(device.GetLiveVoiceCount(), 1U);
    EXPECT_TRUE(AnyRecordedPlayCall());
}

TEST_F(AudioSourceSystemTest, UnchangedDescriptor_DoesNotReplaceVoice) {
    Scene::AudioSourceComponent audioSource;
    audioSource.playOnAwake = false;
    MakeAudioEntity(audioSource);

    audioSourceSystem.Sync(entityManager, audioSystem);
    ASSERT_EQ(device.GetLiveVoiceCount(), 1U);
    device.ClearRecordedCalls();

    audioSourceSystem.Sync(entityManager, audioSystem);

    EXPECT_EQ(device.GetLiveVoiceCount(), 1U);
    EXPECT_TRUE(device.GetRecordedCalls().empty());
}

TEST_F(AudioSourceSystemTest, ReplacementPreservesPlayingState) {
    Scene::AudioSourceComponent audioSource;
    audioSource.audioAssetPath = "playing-a.wav";
    audioSource.playOnAwake = true;
    const Scene::Entity entity = MakeAudioEntity(audioSource);

    audioSourceSystem.Sync(entityManager, audioSystem);
    ASSERT_EQ(device.GetLiveVoiceCount(), 1U);
    device.ClearRecordedCalls();

    entityManager.GetComponent<Scene::AudioSourceComponent>(entity).audioAssetPath = "playing-b.wav";
    audioSourceSystem.Sync(entityManager, audioSystem);

    EXPECT_EQ(device.GetLiveVoiceCount(), 1U);
    EXPECT_TRUE(AnyRecordedPlayCall());
}

TEST_F(AudioSourceSystemTest, FailedReplacement_PreservesOldVoice) {
    RejectingClipResolver resolver;
    AudioSystem resolvingAudioSystem{device, &resolver};
    AudioSourceSystem resolvingAudioSourceSystem;
    Scene::AudioSourceComponent audioSource;
    audioSource.audioAssetPath = "accepted.wav";
    audioSource.playOnAwake = false;
    const Scene::Entity entity = MakeAudioEntity(audioSource);

    resolvingAudioSourceSystem.Sync(entityManager, resolvingAudioSystem);
    ASSERT_EQ(device.GetLiveVoiceCount(), 1U);
    device.ClearRecordedCalls();

    entityManager.GetComponent<Scene::AudioSourceComponent>(entity).audioAssetPath = "rejected.wav";
    resolvingAudioSourceSystem.Sync(entityManager, resolvingAudioSystem);

    EXPECT_EQ(device.GetLiveVoiceCount(), 1U);
    EXPECT_TRUE(device.GetRecordedCalls().empty());
}


} // namespace
} // namespace UVE::Audio::Tests
