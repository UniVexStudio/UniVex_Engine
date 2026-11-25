// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "level_streamer_3d.h"

#include "i_entity_manager.h"

namespace UVE::Scene {

bool IsLevelStreamer3DObjectComponentValid(const LevelStreamer3DComponent& value) noexcept {
    if (!IsBounded3DObjectString(value.levelPath) || !std::isfinite(value.loadDistance) ||
        value.loadDistance <= 0.0F || !std::isfinite(value.unloadDistance) ||
        value.unloadDistance <= value.loadDistance) {
        return false;
    }
    if (value.levelPath.empty()) {
        return !value.enabled && !value.loaded && !value.loadRequested;
    }
    return true;
}

bool IsLevelStreamer3DObjectDefinitionValid(
    const LevelStreamer3DObjectDefinition& value) noexcept {
    return IsLevelStreamer3DObjectComponentValid(value.streamer);
}

void ApplyLevelStreamer3DObjectDefinition(IEntityManager& entityManager,
                                             const Entity entity,
                                             const LevelStreamer3DObjectDefinition& value) {
    if (!entityManager.IsAlive(entity)) {
        return;
    }
    EnsureObject3DBaseline(entityManager, entity,
                              LevelStreamer3DObjectDefinition::defaultName);
    LevelStreamer3DComponent seeded = value.streamer;
    seeded.loaded = false;
    seeded.loadRequested = false;
    if (entityManager.HasComponent<LevelStreamer3DComponent>(entity)) {
        entityManager.GetComponent<LevelStreamer3DComponent>(entity) = seeded;
    } else {
        entityManager.AddComponent<LevelStreamer3DComponent>(entity, seeded);
    }
}

std::optional<float> ResolveLevelStreamer3DNearestViewerDistanceSquared(
    const Math::Vector3& streamerWorldPosition,
    const std::span<const Math::Vector3> viewerPositions) noexcept {
    if (!std::isfinite(streamerWorldPosition.x) || !std::isfinite(streamerWorldPosition.y) ||
        !std::isfinite(streamerWorldPosition.z)) {
        return std::nullopt;
    }
    std::optional<float> nearest;
    for (const Math::Vector3& viewer : viewerPositions) {
        if (!std::isfinite(viewer.x) || !std::isfinite(viewer.y) || !std::isfinite(viewer.z)) {
            continue;
        }
        const float dx = viewer.x - streamerWorldPosition.x;
        const float dy = viewer.y - streamerWorldPosition.y;
        const float dz = viewer.z - streamerWorldPosition.z;
        const float distanceSquared = dx * dx + dy * dy + dz * dz;
        if (!nearest.has_value() || distanceSquared < *nearest) {
            nearest = distanceSquared;
        }
    }
    return nearest;
}

LevelStreamer3DAction ResolveLevelStreamer3DStreamingAction(
    const LevelStreamer3DStreamingFrame& frame) noexcept {
    // Fail-closed: an invalid authored configuration decides nothing, ever.
    if (!std::isfinite(frame.loadDistance) || frame.loadDistance <= 0.0F ||
        !std::isfinite(frame.unloadDistance) || frame.unloadDistance <= frame.loadDistance ||
        (frame.hasViewer && !std::isfinite(frame.nearestViewerDistanceSquared))) {
        return LevelStreamer3DAction::None;
    }
    if (!frame.enabled) {
        // Disabling a loaded streamer ALWAYS unloads it - matching the common convention where a
        // disabled streaming volume stops holding its level - no matter where the viewer stands.
        return frame.loaded ? LevelStreamer3DAction::RequestUnload : LevelStreamer3DAction::None;
    }
    const float loadDistanceSquared = frame.loadDistance * frame.loadDistance;
    const float unloadDistanceSquared = frame.unloadDistance * frame.unloadDistance;
    if (!frame.loaded && !frame.loadRequested && frame.hasViewer &&
        frame.nearestViewerDistanceSquared <= loadDistanceSquared) {
        return LevelStreamer3DAction::RequestLoad;
    }
    if (frame.loaded && frame.hasViewer &&
        frame.nearestViewerDistanceSquared >= unloadDistanceSquared) {
        return LevelStreamer3DAction::RequestUnload;
    }
    return LevelStreamer3DAction::None;
}

LevelStreamer3DStreamingFrame LevelStreamer3D::BuildStreamingFrame(
    const LevelStreamer3DComponent& streamer, const Math::Vector3& streamerWorldPosition,
    const std::span<const Math::Vector3> viewerPositions) noexcept {
    LevelStreamer3DStreamingFrame frame{};
    frame.enabled = streamer.enabled;
    frame.loaded = streamer.loaded;
    frame.loadRequested = streamer.loadRequested;
    frame.loadDistance = streamer.loadDistance;
    frame.unloadDistance = streamer.unloadDistance;
    if (const std::optional<float> nearest = ResolveLevelStreamer3DNearestViewerDistanceSquared(
            streamerWorldPosition, viewerPositions);
        nearest.has_value()) {
        frame.hasViewer = true;
        frame.nearestViewerDistanceSquared = *nearest;
    }
    return frame;
}

LevelStreamer3DAction LevelStreamer3D::EvaluateAction(
    const LevelStreamer3DComponent& streamer, const Math::Vector3& streamerWorldPosition,
    const std::span<const Math::Vector3> viewerPositions) noexcept {
    if (!IsLevelStreamer3DObjectComponentValid(streamer)) {
        return LevelStreamer3DAction::None;
    }
    return ResolveLevelStreamer3DStreamingAction(
        BuildStreamingFrame(streamer, streamerWorldPosition, viewerPositions));
}

float LevelStreamer3D::HysteresisBandWidth(
    const LevelStreamer3DComponent& streamer) noexcept {
    if (!IsLevelStreamer3DObjectComponentValid(streamer)) {
        return 0.0F;
    }
    return streamer.unloadDistance - streamer.loadDistance;
}

} // namespace UVE::Scene
