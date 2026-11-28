// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include <limits>
#include <span>

#include <gtest/gtest.h>

#include "audio_source_system.h"
#include "audio_system.h"
#include "null_audio_device.h"
#include "audio_source_component.h"
#include "entity.h"
#include "world_transform_component.h"
#include "blueprint_object_host.h"
#include "entity_manager.h"
#include "event_system.h"
#include "memory_manager.h"
#include "blueprint_value.h"

namespace UVE::Audio::Tests {
namespace {

class BlueprintAudioBindingsTest : public ::testing::Test {
protected:
    Memory::MemoryManager memoryManager;
    Events::EventSystem eventSystem;
    Scene::EntityManager entityManager{memoryManager.GetDefaultAllocator(), eventSystem};
    NullAudioDevice device;
    AudioSystem audioSystem{device};
    AudioSourceSystem audioSources;

    Scene::Entity MakeSource(const bool playOnAwake) {
        const Scene::Entity entity = entityManager.CreateEntity();
        entityManager.AddComponent<Scene::WorldTransformComponent>(
            entity, Scene::WorldTransformComponent{});
        Scene::AudioSourceComponent source;
        source.audioAssetPath = "sfx.wav";
        source.playOnAwake = playOnAwake;
        entityManager.AddComponent<Scene::AudioSourceComponent>(entity, source);
        return entity;
    }

    static bool CallBool(Core::BlueprintObjectHost& host, const std::string_view name) {
        return std::get<bool>(host.CallFunction(name, std::span<const Blueprint::Value>{}));
    }
};

TEST_F(BlueprintAudioBindingsTest, Host_DescribesAudioSurfaceOnlyOnAudioSources) {
    const Scene::Entity source = MakeSource(false);
    const Core::BlueprintObjectHost host{entityManager, nullptr, source, &audioSources, &audioSystem};
    for (const char* name : {"audio.play", "audio.stop", "audio.is_playing"}) {
        const std::optional<Blueprint::HostFunction> described = host.DescribeFunction(name);
        ASSERT_TRUE(described.has_value()) << name;
        EXPECT_TRUE(described->params.empty());
        EXPECT_EQ(described->result.kind, Blueprint::Type::Kind::Bool);
    }
    for (const char* name : {"volume", "pitch"}) {
        const std::optional<Blueprint::HostProperty> described = host.DescribeProperty(name);
        ASSERT_TRUE(described.has_value()) << name;
        EXPECT_EQ(described->type.kind, Blueprint::Type::Kind::Float);
        EXPECT_TRUE(described->writable);
    }

    const Core::BlueprintObjectHost bare{entityManager, nullptr, entityManager.CreateEntity(),
                                           &audioSources, &audioSystem};
    EXPECT_FALSE(bare.DescribeFunction("audio.play").has_value());
    EXPECT_FALSE(bare.DescribeProperty("volume").has_value());
}

TEST_F(BlueprintAudioBindingsTest, Host_VolumeAndPitch_ReadAndWriteThrough) {
    const Scene::Entity source = MakeSource(false);
    Core::BlueprintObjectHost host{entityManager, nullptr, source, &audioSources, &audioSystem};

    EXPECT_DOUBLE_EQ(std::get<double>(host.GetProperty("volume")), 1.0);
    host.SetProperty("volume", Blueprint::Value{0.5});
    host.SetProperty("pitch", Blueprint::Value{2.0});
    EXPECT_FLOAT_EQ(entityManager.GetComponent<Scene::AudioSourceComponent>(source).volume, 0.5F);
    EXPECT_FLOAT_EQ(entityManager.GetComponent<Scene::AudioSourceComponent>(source).pitch, 2.0F);

    // Invalid writes are ignored, never stored: NaN would make Sync skip the source.
    const float nan = std::numeric_limits<float>::quiet_NaN();
    host.SetProperty("volume", Blueprint::Value{static_cast<double>(nan)});
    host.SetProperty("pitch", Blueprint::Value{0.0});
    EXPECT_FLOAT_EQ(entityManager.GetComponent<Scene::AudioSourceComponent>(source).volume, 0.5F);
    EXPECT_FLOAT_EQ(entityManager.GetComponent<Scene::AudioSourceComponent>(source).pitch, 2.0F);
}

TEST_F(BlueprintAudioBindingsTest, Host_PlayCreatesVoiceOnDemand_StopAndReplay) {
    const Scene::Entity source = MakeSource(false);
    Core::BlueprintObjectHost host{entityManager, nullptr, source, &audioSources, &audioSystem};

    // No Sync ran: play births the voice mid-frame, so `ready` never depends on frame order.
    EXPECT_TRUE(CallBool(host, "audio.play"));
    EXPECT_TRUE(CallBool(host, "audio.is_playing"));
    EXPECT_TRUE(audioSources.IsEntityPlaying(source, audioSystem));

    EXPECT_TRUE(CallBool(host, "audio.stop"));
    EXPECT_FALSE(CallBool(host, "audio.is_playing"));

    EXPECT_TRUE(CallBool(host, "audio.play"));
    EXPECT_TRUE(CallBool(host, "audio.is_playing"));
}

TEST_F(BlueprintAudioBindingsTest, Host_AudioFailsClosed) {
    Core::BlueprintObjectHost bare{entityManager, nullptr, entityManager.CreateEntity(), &audioSources,
                                     &audioSystem};
    EXPECT_FALSE(CallBool(bare, "audio.play"));
    EXPECT_FALSE(CallBool(bare, "audio.stop"));
    EXPECT_FALSE(CallBool(bare, "audio.is_playing"));

    // A real source behind a check-only host: no backend, every call reads false.
    const Scene::Entity source = MakeSource(false);
    Core::BlueprintObjectHost backendless{entityManager, nullptr, source};
    EXPECT_FALSE(CallBool(backendless, "audio.play"));
    EXPECT_FALSE(CallBool(backendless, "audio.is_playing"));

    // Stopping a voice Sync never created is a no-op false, not an error.
    Core::BlueprintObjectHost host{entityManager, nullptr, source, &audioSources, &audioSystem};
    EXPECT_FALSE(CallBool(host, "audio.stop"));
    EXPECT_FALSE(CallBool(host, "audio.is_playing"));
}

TEST_F(BlueprintAudioBindingsTest, Sync_PlayOnAwakePlays_StopPersistsAcrossSync) {
    const Scene::Entity source = MakeSource(true);
    Core::BlueprintObjectHost host{entityManager, nullptr, source, &audioSources, &audioSystem};

    audioSources.Sync(entityManager, audioSystem);
    EXPECT_TRUE(CallBool(host, "audio.is_playing"));

    EXPECT_TRUE(CallBool(host, "audio.stop"));
    audioSources.Sync(entityManager, audioSystem);
    EXPECT_FALSE(CallBool(host, "audio.is_playing"));

    EXPECT_TRUE(CallBool(host, "audio.play"));
    audioSources.Sync(entityManager, audioSystem);
    EXPECT_TRUE(CallBool(host, "audio.is_playing"));
}

TEST_F(BlueprintAudioBindingsTest, Sync_CreatesStoppedVoiceWhenPlayOnAwakeIsFalse) {
    const Scene::Entity source = MakeSource(false);
    Core::BlueprintObjectHost host{entityManager, nullptr, source, &audioSources, &audioSystem};

    audioSources.Sync(entityManager, audioSystem);
    EXPECT_FALSE(CallBool(host, "audio.is_playing"));
    // ...but the voice exists: stopping it succeeds.
    EXPECT_TRUE(CallBool(host, "audio.stop"));
}

TEST_F(BlueprintAudioBindingsTest, PlayEntity_RejectsInvalidSourceThenRecovers) {
    const Scene::Entity source = MakeSource(false);
    entityManager.GetComponent<Scene::AudioSourceComponent>(source).volume =
        std::numeric_limits<float>::quiet_NaN();
    Core::BlueprintObjectHost host{entityManager, nullptr, source, &audioSources, &audioSystem};

    EXPECT_FALSE(CallBool(host, "audio.play"));
    EXPECT_FALSE(CallBool(host, "audio.is_playing"));

    entityManager.GetComponent<Scene::AudioSourceComponent>(source).volume = 0.75F;
    EXPECT_TRUE(CallBool(host, "audio.play"));
    EXPECT_TRUE(CallBool(host, "audio.is_playing"));
}

} // namespace
} // namespace UVE::Audio::Tests
