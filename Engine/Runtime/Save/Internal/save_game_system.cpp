// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#include "save_game_system.h"
#include "save_payload_compression.h"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <exception>
#include <system_error>
#include <utility>

#include <nlohmann/json.hpp>

#include "logging_macros.h"
#include "binary_buffer.h"

namespace UVE::Save {

namespace {

[[nodiscard]] bool IsValidSlotIndex(int slotIndex) noexcept {
    return slotIndex == kAutoSaveSlotIndex || slotIndex == kManualCheckpointSlotIndex ||
           (slotIndex >= 0 && slotIndex < kSaveSlotCount);
}

[[nodiscard]] bool IsValidSaveMetadata(const GameStateMetadata& metadata) noexcept {
    return metadata.savedAtUnixSeconds >= 0 && std::isfinite(metadata.playtimeSeconds) &&
           metadata.playtimeSeconds >= 0.0 && metadata.saveName.size() <= kMaximumSaveNameBytes &&
           metadata.saveName.find('\0') == std::string::npos;
}

[[nodiscard]] bool IsSaveMetadataForSlot(const GameStateMetadata& metadata, const int slotIndex) noexcept {
    return metadata.slotIndex == slotIndex;
}

/// The filename stem (no extension) for `slotIndex` — `"autosave"` and
/// `"manual_checkpoint"` for reserved slots, or `"slot_07"`-style for numbered slots.
[[nodiscard]] std::string SlotFileStem(int slotIndex) {
    if (slotIndex == kAutoSaveSlotIndex) {
        return "autosave";
    }
    if (slotIndex == kManualCheckpointSlotIndex) {
        return "manual_checkpoint";
    }
    char buffer[16];
    std::snprintf(buffer, sizeof(buffer), "slot_%02d", slotIndex);
    return std::string(buffer);
}

[[nodiscard]] std::filesystem::path SlotFilePath(const std::filesystem::path& saveDirectory, int slotIndex) {
    return saveDirectory / (SlotFileStem(slotIndex) + ".uvsave");
}

/// The atomic-write staging path Save() writes to before renaming over SlotFilePath().
[[nodiscard]] std::filesystem::path TempSaveFilePath(const std::filesystem::path& saveDirectory, int slotIndex) {
    return saveDirectory / (SlotFileStem(slotIndex) + ".uvsave.tmp");
}

/// The scratch `.uvscene`-shaped file Save()/Load() bounce world-state JSON through, to
/// reuse Scene::ISceneSerializer without exposing its private per-component JSON table.
[[nodiscard]] std::filesystem::path ScratchScenePath(const std::filesystem::path& saveDirectory, int slotIndex) {
    return saveDirectory / ("." + SlotFileStem(slotIndex) + "_scratch.uvscene");
}

class ScratchFileCleanup final {
public:
    explicit ScratchFileCleanup(std::filesystem::path path) : m_path(std::move(path)) {}
    ~ScratchFileCleanup() noexcept {
        std::error_code errorCode;
        std::filesystem::remove(m_path, errorCode);
    }

    ScratchFileCleanup(const ScratchFileCleanup&) = delete;
    ScratchFileCleanup& operator=(const ScratchFileCleanup&) = delete;

private:
    std::filesystem::path m_path;
};

[[nodiscard]] std::vector<std::byte> EncodeMetadataJson(const GameStateMetadata& metadata) {
    nlohmann::json json;
    json["savedAtUnixSeconds"] = metadata.savedAtUnixSeconds;
    json["engineVersionMajor"] = metadata.engineVersionMajor;
    json["engineVersionMinor"] = metadata.engineVersionMinor;
    json["engineVersionPatch"] = metadata.engineVersionPatch;
    json["engineVersionBuild"] = metadata.engineVersionBuild;
    json["payloadSchemaVersion"] = metadata.payloadSchemaVersion;
    json["playtimeSeconds"] = metadata.playtimeSeconds;
    json["slotIndex"] = metadata.slotIndex;
    json["saveName"] = metadata.saveName;

    const std::string text = json.dump();
    const auto* const bytes = reinterpret_cast<const std::byte*>(text.data());
    return std::vector<std::byte>(bytes, bytes + text.size());
}

[[nodiscard]] std::optional<GameStateMetadata> DecodeMetadataJson(const std::vector<std::byte>& bytes) {
    const std::string text(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    try {
        const nlohmann::json json = nlohmann::json::parse(text);
        GameStateMetadata metadata;
        metadata.savedAtUnixSeconds = json.at("savedAtUnixSeconds").get<std::int64_t>();
        metadata.engineVersionMajor = json.at("engineVersionMajor").get<std::uint32_t>();
        metadata.engineVersionMinor = json.at("engineVersionMinor").get<std::uint32_t>();
        metadata.engineVersionPatch = json.at("engineVersionPatch").get<std::uint32_t>();
        metadata.engineVersionBuild = json.at("engineVersionBuild").get<std::uint32_t>();
        metadata.payloadSchemaVersion = json.value("payloadSchemaVersion", std::uint32_t{1});
        metadata.playtimeSeconds = json.at("playtimeSeconds").get<double>();
        metadata.slotIndex = json.at("slotIndex").get<int>();
        metadata.saveName = json.value("saveName", std::string{});
        return IsValidSaveMetadata(metadata) ? std::optional<GameStateMetadata>{metadata} : std::nullopt;
    } catch (const nlohmann::json::exception& jsonError) {
        UVE_ERROR("SaveGameSystem: failed to parse save metadata: {}", jsonError.what());
        return std::nullopt;
    }
}

/// Reads only the schema discriminator needed to select a migration. Legacy metadata may not yet
/// satisfy DecodeMetadataJson(), so this intentionally performs no current-schema field access.
[[nodiscard]] std::optional<std::uint32_t> DecodeMetadataSchemaVersion(
    const std::vector<std::byte>& bytes) {
    const std::string text(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    try {
        const nlohmann::json json = nlohmann::json::parse(text);
        return json.value("payloadSchemaVersion", std::uint32_t{1});
    } catch (const nlohmann::json::exception& jsonError) {
        UVE_ERROR("SaveGameSystem: failed to parse save schema discriminator: {}", jsonError.what());
        return std::nullopt;
    }
}

/// Builds a `.uvsave` payload: `metadataJsonLength uint32`, `metadataJsonBytes`,
/// `worldJsonLength uint64`, `worldJsonBytes` — the *only* function that touches this fixed
/// layout, so a future increment can wrap this function's output (and SplitSavePayload's
/// input) through a compressor/encryptor without touching anything else in this file.
[[nodiscard]] std::vector<std::byte> BuildSavePayload(const std::vector<std::byte>& metadataJsonBytes,
                                                          const std::vector<std::byte>& worldJsonBytes) {
    std::vector<std::byte> payload;
    Utilities::AppendUint32Le(payload, static_cast<std::uint32_t>(metadataJsonBytes.size()));
    Utilities::AppendBytes(payload, metadataJsonBytes.data(), metadataJsonBytes.size());
    Utilities::AppendUint64Le(payload, worldJsonBytes.size());
    Utilities::AppendBytes(payload, worldJsonBytes.data(), worldJsonBytes.size());
    return CompressSavePayload(payload);
}

/// Splits a `.uvsave` payload back into its metadata and world JSON byte sections. Returns
/// false (no logging — callers attach path/slot context) on any truncation, bounds, or trailing-byte failure.
[[nodiscard]] bool SplitSavePayload(const std::vector<std::byte>& payload,
                                        std::vector<std::byte>& outMetadataJsonBytes,
                                        std::vector<std::byte>& outWorldJsonBytes) {
    std::size_t offset = 0;
    std::uint32_t metadataLength = 0;
    if (!Utilities::ReadUint32LeFromBuffer(payload, offset, metadataLength) ||
        !Utilities::ReadBytesFromBuffer(payload, offset, metadataLength, outMetadataJsonBytes)) {
        return false;
    }
    std::uint64_t worldLength = 0;
    if (!Utilities::ReadUint64LeFromBuffer(payload, offset, worldLength) ||
        !Utilities::ReadBytesFromBuffer(payload, offset, worldLength, outWorldJsonBytes)) {
        return false;
    }
    return offset == payload.size();
}

/// Reads only the metadata section of a `.uvsave` payload — never copies the (potentially much
/// larger) world JSON bytes, keeping GetSaveMetadata() cheap relative to a full Load().
[[nodiscard]] bool SplitSaveMetadataOnly(const std::vector<std::byte>& payload,
                                             std::vector<std::byte>& outMetadataJsonBytes) {
    std::size_t offset = 0;
    std::uint32_t metadataLength = 0;
    if (!Utilities::ReadUint32LeFromBuffer(payload, offset, metadataLength) ||
        !Utilities::ReadBytesFromBuffer(payload, offset, metadataLength, outMetadataJsonBytes)) {
        return false;
    }
    std::uint64_t worldLength = 0U;
    if (!Utilities::ReadUint64LeFromBuffer(payload, offset, worldLength)) {
        return false;
    }
    const std::size_t remainingBytes = payload.size() - offset;
    return worldLength == static_cast<std::uint64_t>(remainingBytes);
}

} // namespace

SaveGameSystem::SaveGameSystem(Scene::ISceneSerializer& sceneSerializer, std::filesystem::path saveDirectory)
    : m_sceneSerializer(&sceneSerializer), m_saveDirectory(std::move(saveDirectory)) {}

SaveMigrationRegistrationResult SaveGameSystem::RegisterMigration(
    const std::uint32_t sourceSchemaVersion, const std::uint32_t targetSchemaVersion,
    SavePayloadMigrationTransform transform) {
    return m_migrationRegistry.Register(sourceSchemaVersion, targetSchemaVersion, std::move(transform));
}

bool SaveGameSystem::Save(int slotIndex, Scene::IEntityManager& entityManager,
                                 const std::vector<Scene::Entity>& rootEntities,
                                 const GameStateMetadata& metadata) {
    if (!IsValidSlotIndex(slotIndex)) {
        UVE_ERROR("SaveGameSystem: Save called with an out-of-range slot index ({})", slotIndex);
        return false;
    }
    if (!IsValidSaveMetadata(metadata)) {
        UVE_ERROR("SaveGameSystem: Save rejected invalid playtime or save-name metadata for slot {}", slotIndex);
        return false;
    }

    std::error_code errorCode;
    std::filesystem::create_directories(m_saveDirectory, errorCode);
    if (errorCode) {
        UVE_ERROR("SaveGameSystem: Save could not prepare save directory \"{}\": {}",
                  m_saveDirectory.string(), errorCode.message());
        return false;
    }

    const std::filesystem::path scratchPath = ScratchScenePath(m_saveDirectory, slotIndex);
    const ScratchFileCleanup scratchCleanup(scratchPath);
    bool serialized = false;
    try {
        serialized = m_sceneSerializer->Save(entityManager, rootEntities, scratchPath,
                                                 Scene::SceneAssetType::Scene);
    } catch (const std::exception& exception) {
        UVE_ERROR("SaveGameSystem: Save scene serializer threw for slot {}: {}", slotIndex,
                  exception.what());
        return false;
    } catch (...) {
        UVE_ERROR("SaveGameSystem: Save scene serializer threw an unknown exception for slot {}", slotIndex);
        return false;
    }
    if (!serialized) {
        UVE_ERROR("SaveGameSystem: Save failed to serialize world state for slot {}", slotIndex);
        return false;
    }

    std::optional<std::pair<Asset::UveFileHeader, std::vector<std::byte>>> scratchFile =
        Asset::ReadUveFile(scratchPath);
    std::filesystem::remove(scratchPath, errorCode);
    if (!scratchFile.has_value()) {
        UVE_ERROR("SaveGameSystem: Save failed to re-read scratch world file for slot {}", slotIndex);
        return false;
    }
    if (scratchFile->first.assetType != Asset::AssetKind::Scene) {
        UVE_ERROR("SaveGameSystem: Save scratch world file has unexpected asset type {} for slot {}",
                  static_cast<std::uint32_t>(scratchFile->first.assetType), slotIndex);
        return false;
    }

    GameStateMetadata finalMetadata = metadata;
    finalMetadata.savedAtUnixSeconds =
        static_cast<std::int64_t>(std::chrono::system_clock::to_time_t(std::chrono::system_clock::now()));
    finalMetadata.slotIndex = slotIndex;
    finalMetadata.payloadSchemaVersion = kCurrentSavePayloadSchemaVersion;

    const std::vector<std::byte> metadataJsonBytes = EncodeMetadataJson(finalMetadata);
    const std::vector<std::byte> payload = BuildSavePayload(metadataJsonBytes, scratchFile->second);
    if (payload.empty()) {
        UVE_ERROR("SaveGameSystem: Save rejected an oversized or unrepresentable payload for slot {}", slotIndex);
        return false;
    }

    const std::filesystem::path tempPath = TempSaveFilePath(m_saveDirectory, slotIndex);
    if (!Asset::WriteUveFile(tempPath, Asset::AssetKind::Save, payload)) {
        UVE_ERROR("SaveGameSystem: Save failed to write temporary save file for slot {}", slotIndex);
        std::filesystem::remove(tempPath, errorCode);
        return false;
    }

    const std::filesystem::path finalPath = SlotFilePath(m_saveDirectory, slotIndex);
    std::filesystem::rename(tempPath, finalPath, errorCode);
    if (errorCode) {
        UVE_ERROR("SaveGameSystem: Save failed to finalize save file for slot {}: {}", slotIndex,
                   errorCode.message());
        std::filesystem::remove(tempPath, errorCode);
        return false;
    }

    return true;
}

std::vector<Scene::Entity> SaveGameSystem::Load(int slotIndex, Scene::IEntityManager& entityManager) {
    if (!IsValidSlotIndex(slotIndex)) {
        UVE_ERROR("SaveGameSystem: Load called with an out-of-range slot index ({})", slotIndex);
        return {};
    }

    const std::filesystem::path finalPath = SlotFilePath(m_saveDirectory, slotIndex);
    std::optional<std::pair<Asset::UveFileHeader, std::vector<std::byte>>> file = Asset::ReadUveFile(finalPath);
    if (!file.has_value()) {
        return {}; // ReadUveFile already logged the specific reason.
    }
    auto& [header, payload] = file.value();
    if (header.assetType != Asset::AssetKind::Save) {
        UVE_ERROR("SaveGameSystem: \"{}\" has unexpected asset type {}", finalPath.string(),
                   static_cast<std::uint32_t>(header.assetType));
        return {};
    }
    std::vector<std::byte> expandedPayload;
    if (!DecompressSavePayload(payload, expandedPayload)) {
        UVE_ERROR("SaveGameSystem: \"{}\" has a corrupt or oversized compressed payload", finalPath.string());
        return {};
    }
    payload = std::move(expandedPayload);

    std::vector<std::byte> metadataJsonBytes;
    std::vector<std::byte> worldJsonBytes;
    if (!SplitSavePayload(payload, metadataJsonBytes, worldJsonBytes)) {
        std::vector<std::byte> emptyPayload;
        m_lastMigrationDiagnostics = m_migrationRegistry.Migrate(kCurrentSavePayloadSchemaVersion,
                                                            kCurrentSavePayloadSchemaVersion, emptyPayload);
        UVE_ERROR("SaveGameSystem: \"{}\" has a corrupt or truncated save payload", finalPath.string());
        return {};
    }

    const std::optional<std::uint32_t> sourceSchemaVersion =
        DecodeMetadataSchemaVersion(metadataJsonBytes);
    if (!sourceSchemaVersion.has_value()) {
        std::vector<std::byte> emptyPayload;
        m_lastMigrationDiagnostics = m_migrationRegistry.Migrate(kCurrentSavePayloadSchemaVersion,
                                                            kCurrentSavePayloadSchemaVersion, emptyPayload);
        return {};
    }
    m_lastMigrationDiagnostics = m_migrationRegistry.Migrate(sourceSchemaVersion.value(),
                                                        kCurrentSavePayloadSchemaVersion, payload);
    if (!m_lastMigrationDiagnostics.Succeeded()) {
        UVE_ERROR("SaveGameSystem: \"{}\" cannot load schema v{}: {}", finalPath.string(),
                   sourceSchemaVersion.value(), m_lastMigrationDiagnostics.reason);
        return {};
    }
    if (m_lastMigrationDiagnostics.status == SaveMigrationStatus::Migrated &&
        !SplitSavePayload(payload, metadataJsonBytes, worldJsonBytes)) {
        UVE_ERROR("SaveGameSystem: \"{}\" migration produced an invalid payload", finalPath.string());
        return {};
    }
    const std::optional<GameStateMetadata> metadata = DecodeMetadataJson(metadataJsonBytes);
    if (!metadata.has_value()) {
        std::vector<std::byte> emptyPayload;
        m_lastMigrationDiagnostics = m_migrationRegistry.Migrate(kCurrentSavePayloadSchemaVersion,
                                                            kCurrentSavePayloadSchemaVersion, emptyPayload);
        return {};
    }
    if (!IsValidSaveMetadata(metadata.value())) {
        UVE_ERROR("SaveGameSystem: slot {} contains invalid playtime metadata", slotIndex);
        return {};
    }
    if (!IsSaveMetadataForSlot(metadata.value(), slotIndex)) {
        UVE_ERROR("SaveGameSystem: slot {} contains metadata for slot {}", slotIndex, metadata->slotIndex);
        return {};
    }

    const std::filesystem::path scratchPath = ScratchScenePath(m_saveDirectory, slotIndex);
    const ScratchFileCleanup scratchCleanup(scratchPath);
    if (!Asset::WriteUveFile(scratchPath, Asset::AssetKind::Scene, worldJsonBytes)) {
        UVE_ERROR("SaveGameSystem: Load failed to write scratch world file for slot {}", slotIndex);
        return {};
    }

    try {
        return m_sceneSerializer->Load(entityManager, scratchPath);
    } catch (const std::exception& exception) {
        UVE_ERROR("SaveGameSystem: Load scene serializer threw for slot {}: {}", slotIndex, exception.what());
    } catch (...) {
        UVE_ERROR("SaveGameSystem: Load scene serializer threw an unknown exception for slot {}", slotIndex);
    }
    return {};
}

bool SaveGameSystem::DeleteSave(int slotIndex) {
    if (!IsValidSlotIndex(slotIndex)) {
        UVE_ERROR("SaveGameSystem: DeleteSave called with an out-of-range slot index ({})", slotIndex);
        return false;
    }

    std::error_code errorCode;
    const bool removed = std::filesystem::remove(SlotFilePath(m_saveDirectory, slotIndex), errorCode);
    if (errorCode) {
        UVE_ERROR("SaveGameSystem: DeleteSave failed for slot {}: {}", slotIndex, errorCode.message());
        return false;
    }
    return removed;
}

bool SaveGameSystem::HasSave(int slotIndex) const {
    if (!IsValidSlotIndex(slotIndex)) {
        return false;
    }
    return GetSaveMetadata(slotIndex).has_value();
}

std::optional<GameStateMetadata> SaveGameSystem::GetSaveMetadata(int slotIndex) const {
    if (!IsValidSlotIndex(slotIndex)) {
        UVE_ERROR("SaveGameSystem: GetSaveMetadata called with an out-of-range slot index ({})", slotIndex);
        return std::nullopt;
    }

    const std::filesystem::path finalPath = SlotFilePath(m_saveDirectory, slotIndex);
    std::optional<std::pair<Asset::UveFileHeader, std::vector<std::byte>>> file = Asset::ReadUveFile(finalPath);
    if (!file.has_value()) {
        return std::nullopt;
    }
    const auto& [header, payload] = file.value();
    if (header.assetType != Asset::AssetKind::Save) {
        UVE_ERROR("SaveGameSystem: \"{}\" has unexpected asset type {}", finalPath.string(),
                   static_cast<std::uint32_t>(header.assetType));
        return std::nullopt;
    }
    std::vector<std::byte> expandedPayload;
    if (!DecompressSavePayload(payload, expandedPayload)) {
        UVE_ERROR("SaveGameSystem: \"{}\" has a corrupt or oversized compressed payload", finalPath.string());
        return std::nullopt;
    }

    std::vector<std::byte> metadataJsonBytes;
    if (!SplitSaveMetadataOnly(expandedPayload, metadataJsonBytes)) {
        UVE_ERROR("SaveGameSystem: \"{}\" has a corrupt or truncated save payload", finalPath.string());
        return std::nullopt;
    }

    const std::optional<std::uint32_t> sourceSchemaVersion =
        DecodeMetadataSchemaVersion(metadataJsonBytes);
    if (!sourceSchemaVersion.has_value()) {
        return std::nullopt;
    }
    m_lastMigrationDiagnostics = m_migrationRegistry.Migrate(sourceSchemaVersion.value(),
                                                        kCurrentSavePayloadSchemaVersion, expandedPayload);
    if (!m_lastMigrationDiagnostics.Succeeded()) {
        UVE_ERROR("SaveGameSystem: \"{}\" metadata schema v{} is unsupported: {}", finalPath.string(),
                   sourceSchemaVersion.value(), m_lastMigrationDiagnostics.reason);
        return std::nullopt;
    }
    if (m_lastMigrationDiagnostics.status == SaveMigrationStatus::Migrated &&
        !SplitSaveMetadataOnly(expandedPayload, metadataJsonBytes)) {
        UVE_ERROR("SaveGameSystem: \"{}\" migration produced an invalid payload", finalPath.string());
        return std::nullopt;
    }
    const std::optional<GameStateMetadata> metadata = DecodeMetadataJson(metadataJsonBytes);
    if (!metadata.has_value() || !IsValidSaveMetadata(metadata.value())) {
        if (metadata.has_value()) {
            UVE_ERROR("SaveGameSystem: slot {} contains invalid playtime metadata", slotIndex);
        }
        return std::nullopt;
    }
    if (!IsSaveMetadataForSlot(metadata.value(), slotIndex)) {
        UVE_ERROR("SaveGameSystem: slot {} contains metadata for slot {}", slotIndex, metadata->slotIndex);
        return std::nullopt;
    }
    return metadata;
}

std::vector<int> SaveGameSystem::ListUsedSlots() const {
    std::vector<int> slots;
    std::error_code errorCode;
    if (!std::filesystem::exists(m_saveDirectory, errorCode) || errorCode) {
        return slots;
    }

    for (int slotIndex = 0; slotIndex < kSaveSlotCount; ++slotIndex) {
        if (HasSave(slotIndex)) {
            slots.push_back(slotIndex);
        }
    }
    return slots;
}

SaveMigrationDiagnostics SaveGameSystem::GetLastMigrationDiagnostics() const {
    return m_lastMigrationDiagnostics;
}

} // namespace UVE::Save
