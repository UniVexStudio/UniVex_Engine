// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#pragma once

#include "i_audio_system.h"
#include "i_entity_manager.h"

namespace UVE::Audio {

/// IAudioSourceSystem is the ECS-to-AudioSystem sync step (mirrors Render::IMeshRenderer's
/// "ECS -> render queue extraction" shape, adapted for a resource with a create/destroy lifecycle
/// rather than a per-frame-rebuilt queue): walks every entity with WorldTransformComponent +
/// AudioSourceComponent, creating/updating/destroying the corresponding IAudioSystem source
/// so per-entity spatial audio needs no hand-written boilerplate at every call site.
/// Thread-safety: not thread-safe; Sync() is intended to be called once per frame from the main
/// engine thread, after SceneGraph::Update() has produced this frame's world transforms —
/// the same ordering contract Render::IMeshRenderer::ExtractRenderQueue() documents.
class IAudioSourceSystem {
public:
    virtual ~IAudioSourceSystem() = default;

    /// For every entity with both WorldTransformComponent and AudioSourceComponent: creates
    /// a source (via `audioSystem.CreateSource()`) the first time this entity is seen —
    /// auto-playing it if `AudioSourceComponent::playOnAwake` is true — caching entity->voice in
    /// this system's own internal registry; updates the source's position/volume/pitch every call
    /// to track the entity's current WorldTransformComponent/AudioSourceComponent values;
    /// and destroys the source for any previously-seen entity no longer matching (removed
    /// entirely, or its AudioSourceComponent was removed) since the last call.
    /// Descriptor-affecting edits to audioAssetPath, looping, spatial, minDistance, maxDistance, or
    /// attenuationCurve are applied by creating a replacement voice first. The replacement inherits
    /// the previous voice's Stopped/Playing state; if creation or playback restart fails, the old
    /// voice and its cached descriptor remain live. Volume, pitch, and position continue updating in
    /// place, while playOnAwake is only honored for the first source creation.
    virtual void Sync(Scene::IEntityManager& entityManager, IAudioSystem& audioSystem) = 0;

    /// Starts this entity's voice, creating it first when Sync() has not seen the entity yet -
    /// scripts tick before the audio sync each frame, so an `audio.play()` in `ready` must not
    /// depend on frame order. Creation follows Sync()'s qualification (alive, WorldTransform +
    /// AudioSource, valid) and descriptor path, including the initial position/volume/pitch a
    /// Sync-born voice would get from the sync tail. Returns false, creating nothing, when the
    /// entity does not qualify or playback fails. Thread-safety: same main-thread contract as Sync().
    [[nodiscard]] virtual bool PlayEntity(Scene::Entity entity, Scene::IEntityManager& entityManager,
                                             IAudioSystem& audioSystem) = 0;

    /// Stops this entity's voice. Returns false when no voice is registered - stopping a voice
    /// Sync() never created is a no-op, never an error. Each method takes only the systems it
    /// needs: stopping needs no entity manager.
    [[nodiscard]] virtual bool StopEntity(Scene::Entity entity, IAudioSystem& audioSystem) = 0;

    /// True exactly when a voice is registered for the entity and the audio system reports it
    /// Playing. Unknown entities read as not playing.
    [[nodiscard]] virtual bool IsEntityPlaying(Scene::Entity entity,
                                                  const IAudioSystem& audioSystem) const = 0;
};

} // namespace UVE::Audio
