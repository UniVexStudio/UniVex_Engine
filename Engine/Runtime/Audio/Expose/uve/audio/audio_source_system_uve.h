// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#pragma once

#include <memory>

#include "uve/audio/i_audio_source_system_uve.h"

namespace UVE::Scene {
struct AudioSourceComponentUVE;
} // namespace Scene

namespace UVE::Audio {

/// AudioSourceSystemUVE is the concrete, engine-standard implementation of
/// IAudioSourceSystemUVE. Unlike Render::MeshRendererUVE/CameraSystemUVE (stateless), this system
/// must be stateful — it owns the entity->VoiceHandleUVE registry needed to detect "this entity's
/// source already exists" vs. "this entity is new" vs. "this entity's source should be destroyed"
/// across successive SyncUVE() calls — closer in shape to a pimpl'd system like
/// Physics::PhysicsSystemUVE than to MeshRendererUVE's no-members design.
class AudioSourceSystemUVE final : public IAudioSourceSystemUVE {
public:
    AudioSourceSystemUVE();
    ~AudioSourceSystemUVE() override;

    AudioSourceSystemUVE(const AudioSourceSystemUVE&) = delete;
    AudioSourceSystemUVE& operator=(const AudioSourceSystemUVE&) = delete;

    void SyncUVE(Scene::IEntityManagerUVE& entityManager, IAudioSystemUVE& audioSystem) override;
    [[nodiscard]] bool PlayEntityUVE(Scene::EntityUVE entity, Scene::IEntityManagerUVE& entityManager,
                                     IAudioSystemUVE& audioSystem) override;
    [[nodiscard]] bool StopEntityUVE(Scene::EntityUVE entity, IAudioSystemUVE& audioSystem) override;
    [[nodiscard]] bool IsEntityPlayingUVE(Scene::EntityUVE entity,
                                          const IAudioSystemUVE& audioSystem) const override;

private:
    struct EnsuredVoiceUVE final {
        VoiceHandleUVE voice;
        bool created;
    };

    /// The registry find-or-create SyncUVE() and PlayEntityUVE() share: returns the live voice, or
    /// an invalid handle when the source is invalid or creation fails. Sets no voice parameters -
    /// SyncUVE()'s tail owns those for sync-born voices, and PlayEntityUVE() sets them for the
    /// voices it births mid-frame, so no existing sync call sequence changes.
    [[nodiscard]] EnsuredVoiceUVE EnsureVoiceUVE(Scene::EntityUVE entity,
                                                 const Scene::AudioSourceComponentUVE& source,
                                                 IAudioSystemUVE& audioSystem);

    struct ImplUVE;
    std::unique_ptr<ImplUVE> m_impl;
};

} // namespace UVE::Audio
