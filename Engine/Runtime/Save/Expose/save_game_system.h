// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#pragma once

#include <filesystem>

#include "i_save_game_system.h"
#include "i_scene_serializer.h"

namespace UVE::Save {

/// SaveGameSystem is the concrete, engine-standard implementation of ISaveGameSystem.
/// Composes a Scene::ISceneSerializer& (dependency injection, matching Physics::PhysicsSystem's
/// ICollisionSystem& precedent) rather than duplicating any of its private per-component JSON
/// (de)serialization table: "world state" is bounced through a scratch `.uvscene`-shaped file on
/// disk — Save() calls sceneSerializer.Save() to a scratch path, reads the raw JSON payload
/// bytes back via Asset::ReadUveFile(), deletes the scratch file, and embeds those bytes
/// verbatim into its own two-section `.uvsave` payload (metadata JSON, then the embedded world
/// JSON); Load() does the reverse. This is the only way to reuse SceneSerializer's
/// already-tested component table without exposing it (deliberately private to
/// scene_serializer_uve.cpp) or changing ISceneSerializer's signature.
/// A `.uvsave` file is its own dedicated `.uve*` envelope payload (Asset::AssetKind::Save)
/// rather than an Asset::AssetBundle-packed bundle: AssetBundle::Pack() only reads
/// entries from real source files on disk and pays for a variable-length name-indexed entry
/// table a save file (always exactly two fixed sections) has no use for.
/// Writes are atomic: Save() writes to a temporary path in the save directory, then
/// std::filesystem::rename()s it over the real slot path only after a fully successful write, so
/// no half-written `.uvsave` file is ever visible at a slot's real path.
class SaveGameSystem final : public ISaveGameSystem {
public:
    /// `sceneSerializer` must outlive this SaveGameSystem. `saveDirectory` is created lazily
    /// on first Save() (never at construction, so a SaveGameSystem that never saves never
    /// touches disk).
    SaveGameSystem(Scene::ISceneSerializer& sceneSerializer, std::filesystem::path saveDirectory);

    [[nodiscard]] bool Save(int slotIndex, Scene::IEntityManager& entityManager,
                                const std::vector<Scene::Entity>& rootEntities,
                                const GameStateMetadata& metadata) override;
    [[nodiscard]] SaveMigrationRegistrationResult RegisterMigration(
        std::uint32_t sourceSchemaVersion, std::uint32_t targetSchemaVersion,
        SavePayloadMigrationTransform transform) override;
    [[nodiscard]] std::vector<Scene::Entity> Load(int slotIndex,
                                                         Scene::IEntityManager& entityManager) override;
    [[nodiscard]] bool DeleteSave(int slotIndex) override;
    [[nodiscard]] bool HasSave(int slotIndex) const override;
    [[nodiscard]] std::optional<GameStateMetadata> GetSaveMetadata(int slotIndex) const override;
    [[nodiscard]] std::vector<int> ListUsedSlots() const override;
    [[nodiscard]] SaveMigrationDiagnostics GetLastMigrationDiagnostics() const override;

private:
    Scene::ISceneSerializer* m_sceneSerializer;
    std::filesystem::path m_saveDirectory;
    SavePayloadMigrationRegistry m_migrationRegistry;
    mutable SaveMigrationDiagnostics m_lastMigrationDiagnostics;
};

} // namespace UVE::Save
