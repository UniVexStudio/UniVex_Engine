// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <optional>
#include <string>

#include "uve/nodes/3d/node_3d_common_uve.h"

namespace UVE::Scene {

struct Marker3DNodeComponentUVE final {
    std::string markerName = "Marker";
    Math::Vector3UVE localPosition{};
    Math::QuaternionUVE localRotation{};
    bool enabled = true;
};

[[nodiscard]] bool IsMarker3DNodeComponentValidUVE(const Marker3DNodeComponentUVE& value) noexcept;

/// A marker's composed WORLD viewpoint: where an observer stands (position) and which way they
/// face (rotation).
struct Marker3DPoseUVE final {
    Math::Vector3UVE position{};
    Math::QuaternionUVE rotation{};
};

/// Composes the marker's authored offset under its node's world transform, the identical
/// contract SpawnPoint3D's ComposeSpawnPointPoseUVE owns (and whose round-trip through the
/// player-local inverse is measured below 1e-4 in Test/Nodes/3D): position =
/// nodePosition + nodeRotation * localPosition, rotation = nodeRotation * localRotation; node
/// scale is deliberately NOT applied (annotation points are placed, not resized - see the spawn
/// point module for the prefab-scale rationale). Degenerate or non-finite input yields no value
/// so every caller fails closed. This is the pure half of Marker3D's live consumer - the editor
/// viewport's fly-to-marker focus - which turns an otherwise inert Godot-style annotation into a
/// scene-persistent named viewpoint.
[[nodiscard]] std::optional<Marker3DPoseUVE> ComposeMarker3DPoseUVE(
    const Math::Vector3UVE nodePosition, const Math::QuaternionUVE nodeRotation,
    const Math::Vector3UVE localPosition, const Math::QuaternionUVE localRotation) noexcept;

} // namespace UVE::Scene
