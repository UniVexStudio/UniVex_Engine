// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#pragma once

#include <memory>

#include "i_audio_source_system.h"

namespace UVE::Scene {
struct AudioSourceComponent;
} // namespace Scene

namespace UVE::Audio {

/// AudioSourceSystem is the concrete, engine-standard implementation of
/// IAudioSourceSystem. Unlike Render::MeshRenderer/CameraSystem (stateless), this system
/// must be stateful — it owns the entity->VoiceHandle registry needed to detect "this entity's
/// source already exists" vs. "this entity is new" vs. "this entity's source should be destroyed"
/// across successive Sync() calls — closer in shape to a pimpl'd system like
/// Physics::PhysicsSystem than to MeshRenderer's no-members design.
class AudioSourceSystem final : public IAudioSourceSystem {
public:
    AudioSourceSystem();
    ~AudioSourceSystem() override;

    AudioSourceSystem(const AudioSourceSystem&) = delete;
    AudioSourceSystem& operator=(const AudioSourceSystem&) = delete;

    void Sync(Scene::IEntityManager& entityManager, IAudioSystem& audioSystem) override;
    [[nodiscard]] bool PlayEntity(Scene::Entity entity, Scene::IEntityManager& entityManager,
                                     IAudioSystem& audioSystem) override;
    [[nodiscard]] bool StopEntity(Scene::Entity entity, IAudioSystem& audioSystem) override;
    [[nodiscard]] bool IsEntityPlaying(Scene::Entity entity,
                                          const IAudioSystem& audioSystem) const override;

private:
    struct EnsuredVoice final {
        VoiceHandle voice;
        bool created;
    };

    /// The registry find-or-create Sync() and PlayEntity() share: returns the live voice, or
    /// an invalid handle when the source is invalid or creation fails. Sets no voice parameters -
    /// Sync()'s tail owns those for sync-born voices, and PlayEntity() sets them for the
    /// voices it births mid-frame, so no existing sync call sequence changes.
    [[nodiscard]] EnsuredVoice EnsureVoice(Scene::Entity entity,
                                                 const Scene::AudioSourceComponent& source,
                                                 IAudioSystem& audioSystem);

    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace UVE::Audio
