// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/nodes/3d/level_streamer_3d_uve.h"

namespace UVE::Scene {

bool IsLevelStreamer3DNodeComponentValidUVE(const LevelStreamer3DNodeComponentUVE& value) noexcept {
    if (!IsBounded3DNodeStringUVE(value.levelPath) || !std::isfinite(value.loadDistance) ||
        value.loadDistance <= 0.0F || !std::isfinite(value.unloadDistance) ||
        value.unloadDistance <= value.loadDistance) {
        return false;
    }
    if (value.levelPath.empty()) {
        return !value.enabled && !value.loaded && !value.loadRequested;
    }
    return true;
}

std::optional<float> ResolveLevelStreamer3DNearestViewerDistanceSquaredUVE(
    const Math::Vector3UVE& streamerWorldPosition,
    const std::span<const Math::Vector3UVE> viewerPositions) noexcept {
    if (!std::isfinite(streamerWorldPosition.x) || !std::isfinite(streamerWorldPosition.y) ||
        !std::isfinite(streamerWorldPosition.z)) {
        return std::nullopt;
    }
    std::optional<float> nearest;
    for (const Math::Vector3UVE& viewer : viewerPositions) {
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

LevelStreamer3DActionUVE ResolveLevelStreamer3DStreamingActionUVE(
    const LevelStreamer3DStreamingFrameUVE& frame) noexcept {
    // Fail-closed: an invalid authored configuration decides nothing, ever.
    if (!std::isfinite(frame.loadDistance) || frame.loadDistance <= 0.0F ||
        !std::isfinite(frame.unloadDistance) || frame.unloadDistance <= frame.loadDistance ||
        (frame.hasViewer && !std::isfinite(frame.nearestViewerDistanceSquared))) {
        return LevelStreamer3DActionUVE::None;
    }
    if (!frame.enabled) {
        // Disabling a loaded streamer ALWAYS unloads it - matching the Unreal convention where a
        // disabled streaming volume stops holding its level - no matter where the viewer stands.
        return frame.loaded ? LevelStreamer3DActionUVE::RequestUnload : LevelStreamer3DActionUVE::None;
    }
    const float loadDistanceSquared = frame.loadDistance * frame.loadDistance;
    const float unloadDistanceSquared = frame.unloadDistance * frame.unloadDistance;
    if (!frame.loaded && !frame.loadRequested && frame.hasViewer &&
        frame.nearestViewerDistanceSquared <= loadDistanceSquared) {
        return LevelStreamer3DActionUVE::RequestLoad;
    }
    if (frame.loaded && frame.hasViewer &&
        frame.nearestViewerDistanceSquared >= unloadDistanceSquared) {
        return LevelStreamer3DActionUVE::RequestUnload;
    }
    return LevelStreamer3DActionUVE::None;
}

} // namespace UVE::Scene
