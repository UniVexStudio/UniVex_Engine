// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#pragma once

#include <vector>

#include "i_save_game_system.h"
#include "entity.h"
#include "i_entity_manager.h"

namespace UVE::Save {

/// ICheckpointManager is the auto-save/manual-checkpoint system (Part 17's
/// CheckpointManager): accumulates elapsed time via Update(), writing to
/// ISaveGameSystem's reserved kAutoSaveSlotIndex whenever the configured interval elapses;
/// Checkpoint() writes to the distinct kManualCheckpointSlotIndex immediately, on demand.
///
/// A successfully written checkpoint — automatic or manual — resets the elapsed-time accumulator,
/// so a successful manual Checkpoint() call defers the next automatic one rather than being
/// immediately followed by a redundant one. A failed save preserves the accumulated time for a
/// later retry.
/// Thread-safety: not thread-safe, matching every other per-frame-driven system in this engine
/// (PhysicsSystem, AudioSourceSystem) — Update() is intended to be called once per frame
/// from the main engine thread.
class ICheckpointManager {
public:
    virtual ~ICheckpointManager() = default;

    /// Accumulates a finite nonnegative `deltaTimeSeconds` into both the total-playtime counter
    /// and the since-last-save counter; invalid negative or non-finite deltas are ignored without
    /// counter or save-state mutation. Once the latter reaches GetAutoSaveIntervalSeconds(), saves
    /// `rootEntities` (from `entityManager`) to kAutoSaveSlotIndex via the composed
    /// ISaveGameSystem and resets the since-last-save counter to 0 only when the save succeeds;
    /// a failed save preserves accumulated time so it can be retried on a later update rather than
    /// losing the elapsed interval.
    virtual void Update(double deltaTimeSeconds, Scene::IEntityManager& entityManager,
                            const std::vector<Scene::Entity>& rootEntities) = 0;

    /// Immediately saves `rootEntities` to kManualCheckpointSlotIndex (the spec's "manual save points
    /// - e.g. before boss fight"), regardless of how much time has elapsed since the last auto-save,
    /// then resets the since-last-save counter to 0 only when the save succeeds. Returns whatever the
    /// underlying ISaveGameSystem::Save() returned.
    [[nodiscard]] virtual bool Checkpoint(Scene::IEntityManager& entityManager,
                                              const std::vector<Scene::Entity>& rootEntities) = 0;

    /// Changes the auto-save interval to a finite positive number, in seconds, without resetting
    /// the current elapsed counter. Invalid zero, negative, or non-finite values are ignored and
    /// retain the last valid interval; shortening a valid interval below the already-elapsed time
    /// triggers an auto-save on the very next Update() call.
    virtual void SetAutoSaveIntervalSeconds(double intervalSeconds) noexcept = 0;
    [[nodiscard]] virtual double GetAutoSaveIntervalSeconds() const noexcept = 0;

    /// Seconds accumulated since the last successful auto-save. Successful manual checkpoints
    /// reset this accumulator because they defer the next automatic save; failed saves preserve it
    /// for retry. Exposed
    /// so tests can assert "just under the interval, no save fired" / "at/over the interval, one
    /// fired" without a real wall-clock wait.
    [[nodiscard]] virtual double GetElapsedSinceLastSaveSeconds() const noexcept = 0;

    /// Total gameplay seconds accumulated across every Update() call since construction —
    /// copied into GameStateMetadata::playtimeSeconds for every auto-save/checkpoint this
    /// CheckpointManager writes.
    [[nodiscard]] virtual double GetTotalPlaytimeSeconds() const noexcept = 0;
};

} // namespace UVE::Save
