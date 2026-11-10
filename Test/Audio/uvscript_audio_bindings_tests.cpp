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
#include "uvscript_object_host.h"
#include "entity_manager.h"
#include "event_system.h"
#include "memory_manager.h"
#include "uvscript_value.h"

namespace UVE::Audio::Tests {
namespace {

class UVScriptAudioBindingsUVETest : public ::testing::Test {
protected:
    Memory::MemoryManagerUVE memoryManager;
    Events::EventSystemUVE eventSystem;
    Scene::EntityManagerUVE entityManager{memoryManager.GetDefaultAllocatorUVE(), eventSystem};
    NullAudioDeviceUVE device;
    AudioSystemUVE audioSystem{device};
    AudioSourceSystemUVE audioSources;

    Scene::EntityUVE MakeSourceUVE(const bool playOnAwake) {
        const Scene::EntityUVE entity = entityManager.CreateEntityUVE();
        entityManager.AddComponentUVE<Scene::WorldTransformComponentUVE>(
            entity, Scene::WorldTransformComponentUVE{});
        Scene::AudioSourceComponentUVE source;
        source.audioAssetPath = "sfx.wav";
        source.playOnAwake = playOnAwake;
        entityManager.AddComponentUVE<Scene::AudioSourceComponentUVE>(entity, source);
        return entity;
    }

    static bool CallBoolUVE(Core::UVScriptObjectHostUVE& host, const std::string_view name) {
        return std::get<bool>(host.CallFunctionUVE(name, std::span<const UVScript::ValueUVE>{}));
    }
};

TEST_F(UVScriptAudioBindingsUVETest, Host_DescribesAudioSurfaceOnlyOnAudioSources) {
    const Scene::EntityUVE source = MakeSourceUVE(false);
    const Core::UVScriptObjectHostUVE host{entityManager, nullptr, source, &audioSources, &audioSystem};
    for (const char* name : {"audio.play", "audio.stop", "audio.is_playing"}) {
        const std::optional<UVScript::HostFunctionUVE> described = host.DescribeFunctionUVE(name);
        ASSERT_TRUE(described.has_value()) << name;
        EXPECT_TRUE(described->params.empty());
        EXPECT_EQ(described->result.kind, UVScript::TypeUVE::KindUVE::Bool);
    }
    for (const char* name : {"volume", "pitch"}) {
        const std::optional<UVScript::HostPropertyUVE> described = host.DescribePropertyUVE(name);
        ASSERT_TRUE(described.has_value()) << name;
        EXPECT_EQ(described->type.kind, UVScript::TypeUVE::KindUVE::Float);
        EXPECT_TRUE(described->writable);
    }

    const Core::UVScriptObjectHostUVE bare{entityManager, nullptr, entityManager.CreateEntityUVE(),
                                           &audioSources, &audioSystem};
    EXPECT_FALSE(bare.DescribeFunctionUVE("audio.play").has_value());
    EXPECT_FALSE(bare.DescribePropertyUVE("volume").has_value());
}

TEST_F(UVScriptAudioBindingsUVETest, Host_VolumeAndPitch_ReadAndWriteThrough) {
    const Scene::EntityUVE source = MakeSourceUVE(false);
    Core::UVScriptObjectHostUVE host{entityManager, nullptr, source, &audioSources, &audioSystem};

    EXPECT_DOUBLE_EQ(std::get<double>(host.GetPropertyUVE("volume")), 1.0);
    host.SetPropertyUVE("volume", UVScript::ValueUVE{0.5});
    host.SetPropertyUVE("pitch", UVScript::ValueUVE{2.0});
    EXPECT_FLOAT_EQ(entityManager.GetComponentUVE<Scene::AudioSourceComponentUVE>(source).volume, 0.5F);
    EXPECT_FLOAT_EQ(entityManager.GetComponentUVE<Scene::AudioSourceComponentUVE>(source).pitch, 2.0F);

    // Invalid writes are ignored, never stored: NaN would make Sync skip the source.
    const float nan = std::numeric_limits<float>::quiet_NaN();
    host.SetPropertyUVE("volume", UVScript::ValueUVE{static_cast<double>(nan)});
    host.SetPropertyUVE("pitch", UVScript::ValueUVE{0.0});
    EXPECT_FLOAT_EQ(entityManager.GetComponentUVE<Scene::AudioSourceComponentUVE>(source).volume, 0.5F);
    EXPECT_FLOAT_EQ(entityManager.GetComponentUVE<Scene::AudioSourceComponentUVE>(source).pitch, 2.0F);
}

TEST_F(UVScriptAudioBindingsUVETest, Host_PlayCreatesVoiceOnDemand_StopAndReplay) {
    const Scene::EntityUVE source = MakeSourceUVE(false);
    Core::UVScriptObjectHostUVE host{entityManager, nullptr, source, &audioSources, &audioSystem};

    // No Sync ran: play births the voice mid-frame, so `ready` never depends on frame order.
    EXPECT_TRUE(CallBoolUVE(host, "audio.play"));
    EXPECT_TRUE(CallBoolUVE(host, "audio.is_playing"));
    EXPECT_TRUE(audioSources.IsEntityPlayingUVE(source, audioSystem));

    EXPECT_TRUE(CallBoolUVE(host, "audio.stop"));
    EXPECT_FALSE(CallBoolUVE(host, "audio.is_playing"));

    EXPECT_TRUE(CallBoolUVE(host, "audio.play"));
    EXPECT_TRUE(CallBoolUVE(host, "audio.is_playing"));
}

TEST_F(UVScriptAudioBindingsUVETest, Host_AudioFailsClosed) {
    Core::UVScriptObjectHostUVE bare{entityManager, nullptr, entityManager.CreateEntityUVE(), &audioSources,
                                     &audioSystem};
    EXPECT_FALSE(CallBoolUVE(bare, "audio.play"));
    EXPECT_FALSE(CallBoolUVE(bare, "audio.stop"));
    EXPECT_FALSE(CallBoolUVE(bare, "audio.is_playing"));

    // A real source behind a check-only host: no backend, every call reads false.
    const Scene::EntityUVE source = MakeSourceUVE(false);
    Core::UVScriptObjectHostUVE backendless{entityManager, nullptr, source};
    EXPECT_FALSE(CallBoolUVE(backendless, "audio.play"));
    EXPECT_FALSE(CallBoolUVE(backendless, "audio.is_playing"));

    // Stopping a voice Sync never created is a no-op false, not an error.
    Core::UVScriptObjectHostUVE host{entityManager, nullptr, source, &audioSources, &audioSystem};
    EXPECT_FALSE(CallBoolUVE(host, "audio.stop"));
    EXPECT_FALSE(CallBoolUVE(host, "audio.is_playing"));
}

TEST_F(UVScriptAudioBindingsUVETest, Sync_PlayOnAwakePlays_StopPersistsAcrossSync) {
    const Scene::EntityUVE source = MakeSourceUVE(true);
    Core::UVScriptObjectHostUVE host{entityManager, nullptr, source, &audioSources, &audioSystem};

    audioSources.SyncUVE(entityManager, audioSystem);
    EXPECT_TRUE(CallBoolUVE(host, "audio.is_playing"));

    EXPECT_TRUE(CallBoolUVE(host, "audio.stop"));
    audioSources.SyncUVE(entityManager, audioSystem);
    EXPECT_FALSE(CallBoolUVE(host, "audio.is_playing"));

    EXPECT_TRUE(CallBoolUVE(host, "audio.play"));
    audioSources.SyncUVE(entityManager, audioSystem);
    EXPECT_TRUE(CallBoolUVE(host, "audio.is_playing"));
}

TEST_F(UVScriptAudioBindingsUVETest, Sync_CreatesStoppedVoiceWhenPlayOnAwakeIsFalse) {
    const Scene::EntityUVE source = MakeSourceUVE(false);
    Core::UVScriptObjectHostUVE host{entityManager, nullptr, source, &audioSources, &audioSystem};

    audioSources.SyncUVE(entityManager, audioSystem);
    EXPECT_FALSE(CallBoolUVE(host, "audio.is_playing"));
    // ...but the voice exists: stopping it succeeds.
    EXPECT_TRUE(CallBoolUVE(host, "audio.stop"));
}

TEST_F(UVScriptAudioBindingsUVETest, PlayEntity_RejectsInvalidSourceThenRecovers) {
    const Scene::EntityUVE source = MakeSourceUVE(false);
    entityManager.GetComponentUVE<Scene::AudioSourceComponentUVE>(source).volume =
        std::numeric_limits<float>::quiet_NaN();
    Core::UVScriptObjectHostUVE host{entityManager, nullptr, source, &audioSources, &audioSystem};

    EXPECT_FALSE(CallBoolUVE(host, "audio.play"));
    EXPECT_FALSE(CallBoolUVE(host, "audio.is_playing"));

    entityManager.GetComponentUVE<Scene::AudioSourceComponentUVE>(source).volume = 0.75F;
    EXPECT_TRUE(CallBoolUVE(host, "audio.play"));
    EXPECT_TRUE(CallBoolUVE(host, "audio.is_playing"));
}

} // namespace
} // namespace UVE::Audio::Tests
