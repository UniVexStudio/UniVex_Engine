// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <optional>
#include <string>
#include <string_view>

#include "entity.h"
#include "object_3d_common.h"
#include "object_3d.h"

namespace UVE::Scene {

class IEntityManager;

struct Marker3DComponent final {
    std::string markerName = "Marker";
    Math::Vector3 localPosition{};
    Math::Quaternion localRotation{};
    bool enabled = true;

    [[nodiscard]] bool operator==(const Marker3DComponent&) const = default;
};

[[nodiscard]] bool IsMarker3DObjectComponentValid(const Marker3DComponent& value) noexcept;

/// Marker3D's creation recipe — the Object3D baseline plus a default `Marker3DComponent`.
struct Marker3DObjectDefinition final {
    static constexpr std::string_view typeName = "Marker3D";
    static constexpr std::string_view defaultName = "Marker3D";

    Marker3DComponent marker{};

    [[nodiscard]] bool operator==(const Marker3DObjectDefinition&) const = default;
};

[[nodiscard]] bool IsMarker3DObjectDefinitionValid(const Marker3DObjectDefinition& value) noexcept;

void ApplyMarker3DObjectDefinition(IEntityManager& entityManager, Entity entity,
                                      const Marker3DObjectDefinition& value);

/// A marker's composed WORLD viewpoint: where an observer stands (position) and which way they
/// face (rotation).
struct Marker3DPose final {
    Math::Vector3 position{};
    Math::Quaternion rotation{};

    [[nodiscard]] bool operator==(const Marker3DPose&) const = default;
};

/// Composes the marker's authored offset under its object's world transform, the identical
/// contract SpawnPoint3D's ComposeSpawnPointPose owns (and whose round-trip through the
/// player-local inverse is measured below 1e-4 in Test/Objects/3D): position =
/// objectPosition + objectRotation * localPosition, rotation = objectRotation * localRotation; object
/// scale is deliberately NOT applied (annotation points are placed, not resized - see the spawn
/// point module for the prefab-scale rationale). Degenerate or non-finite input yields no value
/// so every caller fails closed. The editor's fly-to-marker focus and QueryMarkers3D both
/// compose through this; a named marker is a scene-persistent viewpoint, not an inert tag.
[[nodiscard]] std::optional<Marker3DPose> ComposeMarker3DPose(
    Math::Vector3 objectPosition, Math::Quaternion objectRotation,
    Math::Vector3 localPosition, Math::Quaternion localRotation) noexcept;

/// Node policy for a `Marker3D` scene object.
class Marker3D final {
public:
    Marker3D() = delete;

    static constexpr Math::Vector3 kLocalForwardAxis{0.0F, 0.0F, -1.0F};
    static constexpr Math::Vector3 kLocalUpAxis{0.0F, 1.0F, 0.0F};

    /// True when `marker` is enabled and passes `IsMarker3DObjectComponentValid`.
    [[nodiscard]] static bool IsQueryable(const Marker3DComponent& marker) noexcept;

    /// True when `filterName` is empty (match-all) or equals `marker.markerName`.
    [[nodiscard]] static bool MatchesNameFilter(
        const Marker3DComponent& marker, std::string_view filterName) noexcept;

    /// Composes `marker`'s world pose under `(objectPosition, objectRotation)` when `marker` is
    /// queryable; returns `std::nullopt` otherwise.
    [[nodiscard]] static std::optional<Marker3DPose> ResolveWorldPose(
        const Marker3DComponent& marker, const Math::Vector3& objectPosition,
        const Math::Quaternion& objectRotation) noexcept;

    /// Returns the world-space forward direction (`-Z` rotated by `pose.rotation`).
    [[nodiscard]] static Math::Vector3 ForwardDirection(const Marker3DPose& pose) noexcept;

    /// Returns the world-space up direction (`+Y` rotated by `pose.rotation`).
    [[nodiscard]] static Math::Vector3 UpDirection(const Marker3DPose& pose) noexcept;
};

} // namespace UVE::Scene
