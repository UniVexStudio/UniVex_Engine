// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#pragma once

#include "i_checkpoint_manager.h"

namespace UVE::Save {

/// CheckpointManager is the concrete, engine-standard implementation of
/// ICheckpointManager. Composes an ISaveGameSystem& (dependency injection, matching
/// Physics::PhysicsSystem's ICollisionSystem& precedent) and holds only the small pieces of
/// state its own doc comment promises (elapsed-since-last-save, total playtime, configured
/// interval) — no per-entity state.
class CheckpointManager final : public ICheckpointManager {
public:
    /// `saveGameSystem` must outlive this CheckpointManager. `autoSaveIntervalSeconds` must be
    /// finite and positive; invalid construction input falls back to 300.0 (the spec's "e.g.,
    /// every 5 minutes" example). `metadataTemplate` is copied and supplies caller-owned policy
    /// fields (such as the canonical engine version) to both automatic and manual checkpoints;
    /// SaveGameSystem still overwrites timestamp, slot, and payload schema.
    explicit CheckpointManager(ISaveGameSystem& saveGameSystem, double autoSaveIntervalSeconds = 300.0,
                                  GameStateMetadata metadataTemplate = {});

    void Update(double deltaTimeSeconds, Scene::IEntityManager& entityManager,
                    const std::vector<Scene::Entity>& rootEntities) override;
    [[nodiscard]] bool Checkpoint(Scene::IEntityManager& entityManager,
                                      const std::vector<Scene::Entity>& rootEntities) override;
    void SetAutoSaveIntervalSeconds(double intervalSeconds) noexcept override;
    [[nodiscard]] double GetAutoSaveIntervalSeconds() const noexcept override;
    [[nodiscard]] double GetElapsedSinceLastSaveSeconds() const noexcept override;
    [[nodiscard]] double GetTotalPlaytimeSeconds() const noexcept override;

private:
    /// Builds this checkpoint's GameStateMetadata (playtimeSeconds = m_totalPlaytimeSeconds,
    /// every other field default) and calls m_saveGameSystem->Save() for the caller-selected
    /// reserved slot, then resets m_elapsedSinceLastSaveSeconds to 0 only after success. Shared by
    /// both Update()'s interval-triggered path and Checkpoint()'s immediate path.
    [[nodiscard]] bool SaveCheckpoint(int slotIndex, Scene::IEntityManager& entityManager,
                                          const std::vector<Scene::Entity>& rootEntities);

    ISaveGameSystem* m_saveGameSystem;
    GameStateMetadata m_metadataTemplate;
    double m_autoSaveIntervalSeconds;
    double m_elapsedSinceLastSaveSeconds = 0.0;
    double m_totalPlaytimeSeconds = 0.0;
};

} // namespace UVE::Save
