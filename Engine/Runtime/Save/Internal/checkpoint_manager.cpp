// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#include "checkpoint_manager.h"

#include <cmath>
#include <utility>

namespace UVE::Save {
namespace {

constexpr double kDefaultAutoSaveIntervalSeconds = 300.0;

[[nodiscard]] bool IsValidAutoSaveInterval(const double intervalSeconds) noexcept {
    return std::isfinite(intervalSeconds) && intervalSeconds > 0.0;
}

} // namespace

CheckpointManager::CheckpointManager(ISaveGameSystem& saveGameSystem,
                                             double autoSaveIntervalSeconds,
                                             GameStateMetadata metadataTemplate)
    : m_saveGameSystem(&saveGameSystem),
      m_metadataTemplate(std::move(metadataTemplate)),
      m_autoSaveIntervalSeconds(IsValidAutoSaveInterval(autoSaveIntervalSeconds)
                                    ? autoSaveIntervalSeconds
                                    : kDefaultAutoSaveIntervalSeconds) {}

void CheckpointManager::Update(double deltaTimeSeconds, Scene::IEntityManager& entityManager,
                                      const std::vector<Scene::Entity>& rootEntities) {
    if (!std::isfinite(deltaTimeSeconds) || deltaTimeSeconds < 0.0) {
        return;
    }
    m_totalPlaytimeSeconds += deltaTimeSeconds;
    m_elapsedSinceLastSaveSeconds += deltaTimeSeconds;

    if (m_elapsedSinceLastSaveSeconds >= m_autoSaveIntervalSeconds) {
        static_cast<void>(SaveCheckpoint(kAutoSaveSlotIndex, entityManager, rootEntities));
    }
}

bool CheckpointManager::Checkpoint(Scene::IEntityManager& entityManager,
                                          const std::vector<Scene::Entity>& rootEntities) {
    return SaveCheckpoint(kManualCheckpointSlotIndex, entityManager, rootEntities);
}

void CheckpointManager::SetAutoSaveIntervalSeconds(const double intervalSeconds) noexcept {
    if (IsValidAutoSaveInterval(intervalSeconds)) {
        m_autoSaveIntervalSeconds = intervalSeconds;
    }
}

double CheckpointManager::GetAutoSaveIntervalSeconds() const noexcept {
    return m_autoSaveIntervalSeconds;
}

double CheckpointManager::GetElapsedSinceLastSaveSeconds() const noexcept {
    return m_elapsedSinceLastSaveSeconds;
}

double CheckpointManager::GetTotalPlaytimeSeconds() const noexcept {
    return m_totalPlaytimeSeconds;
}

bool CheckpointManager::SaveCheckpoint(const int slotIndex, Scene::IEntityManager& entityManager,
                                              const std::vector<Scene::Entity>& rootEntities) {
    GameStateMetadata metadata = m_metadataTemplate;
    metadata.playtimeSeconds = m_totalPlaytimeSeconds;

    const bool saved = m_saveGameSystem->Save(slotIndex, entityManager, rootEntities, metadata);
    if (saved) {
        m_elapsedSinceLastSaveSeconds = 0.0;
    }
    return saved;
}

} // namespace UVE::Save
