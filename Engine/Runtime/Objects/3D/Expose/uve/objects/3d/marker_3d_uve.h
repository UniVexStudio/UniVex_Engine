// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <optional>
#include <string>
#include <string_view>

#include "uve/component/entity_uve.h"
#include "uve/objects/3d/object_3d_common_uve.h"
#include "uve/objects/3d/object_3d_uve.h"

namespace UVE::Scene {

class IEntityManagerUVE;

struct Marker3DComponentUVE final {
    std::string markerName = "Marker";
    Math::Vector3UVE localPosition{};
    Math::QuaternionUVE localRotation{};
    bool enabled = true;

    [[nodiscard]] bool operator==(const Marker3DComponentUVE&) const = default;
};

[[nodiscard]] bool IsMarker3DObjectComponentValidUVE(const Marker3DComponentUVE& value) noexcept;

/// Marker3D's creation recipe — the Object3D baseline plus a default `Marker3DComponentUVE`.
struct Marker3DObjectDefinitionUVE final {
    static constexpr std::string_view typeName = "Marker3D";
    static constexpr std::string_view defaultName = "Marker3D";

    Marker3DComponentUVE marker{};

    [[nodiscard]] bool operator==(const Marker3DObjectDefinitionUVE&) const = default;
};

[[nodiscard]] bool IsMarker3DObjectDefinitionValidUVE(const Marker3DObjectDefinitionUVE& value) noexcept;

void ApplyMarker3DObjectDefinitionUVE(IEntityManagerUVE& entityManager, EntityUVE entity,
                                      const Marker3DObjectDefinitionUVE& value);

/// A marker's composed WORLD viewpoint: where an observer stands (position) and which way they
/// face (rotation).
struct Marker3DPoseUVE final {
    Math::Vector3UVE position{};
    Math::QuaternionUVE rotation{};

    [[nodiscard]] bool operator==(const Marker3DPoseUVE&) const = default;
};

/// Composes the marker's authored offset under its object's world transform, the identical
/// contract SpawnPoint3D's ComposeSpawnPointPoseUVE owns (and whose round-trip through the
/// player-local inverse is measured below 1e-4 in Test/Objects/3D): position =
/// objectPosition + objectRotation * localPosition, rotation = objectRotation * localRotation; object
/// scale is deliberately NOT applied (annotation points are placed, not resized - see the spawn
/// point module for the prefab-scale rationale). Degenerate or non-finite input yields no value
/// so every caller fails closed. The editor's fly-to-marker focus and QueryMarkers3DUVE both
/// compose through this; a named marker is a scene-persistent viewpoint, not an inert tag.
[[nodiscard]] std::optional<Marker3DPoseUVE> ComposeMarker3DPoseUVE(
    Math::Vector3UVE objectPosition, Math::QuaternionUVE objectRotation,
    Math::Vector3UVE localPosition, Math::QuaternionUVE localRotation) noexcept;

/// Node policy for a `Marker3D` scene object.
class Marker3DUVE final {
public:
    Marker3DUVE() = delete;

    static constexpr Math::Vector3UVE kLocalForwardAxisUVE{0.0F, 0.0F, -1.0F};
    static constexpr Math::Vector3UVE kLocalUpAxisUVE{0.0F, 1.0F, 0.0F};

    /// True when `marker` is enabled and passes `IsMarker3DObjectComponentValidUVE`.
    [[nodiscard]] static bool IsQueryableUVE(const Marker3DComponentUVE& marker) noexcept;

    /// True when `filterName` is empty (match-all) or equals `marker.markerName`.
    [[nodiscard]] static bool MatchesNameFilterUVE(
        const Marker3DComponentUVE& marker, std::string_view filterName) noexcept;

    /// Composes `marker`'s world pose under `(objectPosition, objectRotation)` when `marker` is
    /// queryable; returns `std::nullopt` otherwise.
    [[nodiscard]] static std::optional<Marker3DPoseUVE> ResolveWorldPoseUVE(
        const Marker3DComponentUVE& marker, const Math::Vector3UVE& objectPosition,
        const Math::QuaternionUVE& objectRotation) noexcept;

    /// Returns the world-space forward direction (`-Z` rotated by `pose.rotation`).
    [[nodiscard]] static Math::Vector3UVE ForwardDirectionUVE(const Marker3DPoseUVE& pose) noexcept;

    /// Returns the world-space up direction (`+Y` rotated by `pose.rotation`).
    [[nodiscard]] static Math::Vector3UVE UpDirectionUVE(const Marker3DPoseUVE& pose) noexcept;
};

} // namespace UVE::Scene
