// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#pragma once

#include <optional>
#include <vector>

#include "game_state_metadata.h"
#include "save_payload_migration.h"
#include "entity.h"
#include "i_entity_manager.h"

namespace UVE::Save {

/// Number of numbered player save slots (spec: "Up to 99 save slots per user"). Valid numbered
/// slotIndex values for every ISaveGameSystem method below are [0, kSaveSlotCount).
inline constexpr int kSaveSlotCount = 99;

/// The reserved slot used by automatic checkpoints. It is deliberately outside
/// [0, kSaveSlotCount) so it can never collide with, or be enumerated alongside, a player's
/// numbered saves. Every slot-taking method accepts this value.
inline constexpr int kAutoSaveSlotIndex = -1;

/// The reserved slot used by explicit/manual checkpoints. It is deliberately distinct from the
/// automatic slot and from numbered saves; ListUsedSlots() never returns either reserved slot.
inline constexpr int kManualCheckpointSlotIndex = -2;

/// ISaveGameSystem is the core save/load manager (Part 17's SaveGameSystem): slot-based
/// persistence of "world state" — every entity reachable from an explicit root-entity list,
/// exactly like Scene::ISceneSerializer::Save()'s own contract (a caller-supplied list, not
/// an implicit "the whole world") — plus a small metadata section (timestamp, engine version,
/// playtime). Deliberately takes the same "explicit roots" shape as ISceneSerializer rather
/// than inventing a second, competing "capture everything" mechanism; a caller that wants "the
/// whole scene" passes every entity ISceneGraph::GetChildren() reports for
/// Scene::kInvalidEntity (i.e. every scene-graph root), exactly as EngineCore does when
/// driving CheckpointManager.
/// Thread-safety: not thread-safe, matching ISceneSerializer's own contract — every method
/// does direct file I/O and/or mutates the entity manager passed to it.
class ISaveGameSystem {
public:
    virtual ~ISaveGameSystem() = default;

    /// Serializes every entity reachable from `rootEntities` (via SceneSerializer, exactly
    /// like ISceneSerializer::Save()'s own reachability rule) plus `metadata` to the file
    /// backing `slotIndex`, overwriting any existing save in that slot. `slotIndex` must be
    /// kAutoSaveSlotIndex, kManualCheckpointSlotIndex, or in [0, kSaveSlotCount) — any other value logs a UVE_ERROR and
    /// returns false without touching disk. `metadata.savedAtUnixSeconds`/`slotIndex`/
    /// `payloadSchemaVersion` are overwritten internally; every other field is taken verbatim
    /// from the caller. Returns false (logging the reason) on an out-of-range slot, an
    /// unregistered component type, or any file-system write error — never leaves a corrupt or
    /// partially-written file visible at the slot's path (see SaveGameSystem's atomic
    /// temp-file-then-rename write).
    [[nodiscard]] virtual bool Save(int slotIndex, Scene::IEntityManager& entityManager,
                                        const std::vector<Scene::Entity>& rootEntities,
                                        const GameStateMetadata& metadata) = 0;

    /// Registers one exact source-to-target payload transform. The callable receives a private
    /// working copy; failure or an exception leaves the caller's payload untouched. Registration
    /// is bounded and duplicate source/target pairs are rejected. This seam owns migration only,
    /// not compression, encryption, cloud transport, or gameplay-domain state.
    [[nodiscard]] virtual SaveMigrationRegistrationResult RegisterMigration(
        std::uint32_t sourceSchemaVersion, std::uint32_t targetSchemaVersion,
        SavePayloadMigrationTransform transform) = 0;

    /// Deserializes `slotIndex`'s save into fresh live entities via `entityManager` (exactly
    /// like ISceneSerializer::Load()), returning the newly-created root entities in file
    /// order. Returns an empty vector (logging the reason) on an out-of-range slot, a missing
    /// save, a corrupt/truncated file, or an unsupported envelope/compression/asset-type.
    [[nodiscard]] virtual std::vector<Scene::Entity> Load(int slotIndex,
                                                                 Scene::IEntityManager& entityManager) = 0;

    /// Deletes `slotIndex`'s save file. Returns true iff a file was actually removed; returns
    /// false without logging if the slot was simply already empty (not an error), or false while
    /// logging if `slotIndex` itself is out of range.
    [[nodiscard]] virtual bool DeleteSave(int slotIndex) = 0;

    /// True iff `slotIndex` is in range and currently has a save file. Never logs — a false
    /// result for an empty slot is the expected common case, not an error.
    [[nodiscard]] virtual bool HasSave(int slotIndex) const = 0;

    /// Reads and returns just `slotIndex`'s metadata section, without deserializing any entity —
    /// cheap enough to call for every slot when populating a save/load picker UI, unlike
    /// Load(), which pays full ECS entity-creation cost. Returns std::nullopt (logging the
    /// reason) if `slotIndex` is out of range, empty, or the file is corrupt/truncated.
    [[nodiscard]] virtual std::optional<GameStateMetadata> GetSaveMetadata(int slotIndex) const = 0;

    /// Returns every numbered slot (never kAutoSaveSlotIndex) currently holding a save file,
    /// ascending. Returns an empty vector (not an error) if the save directory doesn't exist yet
    /// (a first-run engine has no saves).
    [[nodiscard]] virtual std::vector<int> ListUsedSlots() const = 0;

    /// Returns the copied result of the most recent load/metadata schema dispatch. The result is
    /// deterministic and does not expose the save payload; NotRequired means the file already
    /// matched the current schema.
    [[nodiscard]] virtual SaveMigrationDiagnostics GetLastMigrationDiagnostics() const = 0;
};

} // namespace UVE::Save
