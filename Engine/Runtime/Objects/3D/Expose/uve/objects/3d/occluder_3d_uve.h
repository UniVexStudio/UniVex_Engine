// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

#include "uve/component/entity_uve.h"
#include "uve/math/aabb_uve.h"
#include "uve/objects/3d/object_3d_common_uve.h"

namespace UVE::Scene {

class IEntityManagerUVE;

enum class Occluder3DObjectModeUVE : std::uint8_t {
    ConservativeBox = 0,
};

/// An Occluder3D: an authored axis-aligned box (halfExtents around the object's world position)
/// that hides whatever stands strictly BEHIND it from the current viewer. Conservative: a
/// candidate is hidden only when every corner of its world AABB is hidden. A mesh that peeks
/// around the wall still draws. No state is kept between frames.
struct Occluder3DComponentUVE final {
    Math::Vector3UVE halfExtents{2.0F, 2.0F, 2.0F};
    Occluder3DObjectModeUVE mode = Occluder3DObjectModeUVE::ConservativeBox;
    bool enabled = true;
};

[[nodiscard]] bool IsOccluder3DObjectComponentValidUVE(const Occluder3DComponentUVE& value) noexcept;

struct Occluder3DObjectDefinitionUVE final {
    static constexpr std::string_view defaultName = "Occluder3D";
    Occluder3DComponentUVE occluder{};
};

void ApplyOccluder3DObjectDefinitionUVE(IEntityManagerUVE& entityManager, EntityUVE entity,
                                        const Occluder3DObjectDefinitionUVE& value);

// One occluder against one candidate point. Invalid config, a non-finite pose, the viewer or
// the point inside the box, or a grazing touch: NOT hidden.
[[nodiscard]] bool ResolveOccluder3DFullyHiddenUVE(const Occluder3DComponentUVE& config,
                                                   const Math::Vector3UVE& occluderWorldPosition,
                                                   const Math::Vector3UVE& viewerWorldPosition,
                                                   const Math::Vector3UVE& pointWorld) noexcept;

// True only when every corner of `bounds` is hidden by this occluder. An unordered or
// non-finite AABB fails open.
[[nodiscard]] bool ResolveOccluder3DFullyHidesAabbUVE(const Occluder3DComponentUVE& config,
                                                      const Math::Vector3UVE& occluderWorldPosition,
                                                      const Math::Vector3UVE& viewerWorldPosition,
                                                      const Math::AabbUVE& bounds) noexcept;

struct Occluder3DSnapshotUVE final {
    Occluder3DComponentUVE config{};
    Math::Vector3UVE worldPosition{};
};

void CollectOccluder3DSnapshotsUVE(IEntityManagerUVE& entityManager, std::vector<Occluder3DSnapshotUVE>& out);

[[nodiscard]] bool IsOccluder3DPointDrawHiddenUVE(std::span<const Occluder3DSnapshotUVE> occluders,
                                                  const Math::Vector3UVE& viewerWorldPosition,
                                                  const Math::Vector3UVE& pointWorld) noexcept;

[[nodiscard]] bool IsOccluder3DAabbDrawHiddenUVE(std::span<const Occluder3DSnapshotUVE> occluders,
                                                 const Math::Vector3UVE& viewerWorldPosition,
                                                 const Math::AabbUVE& bounds) noexcept;

struct Occluder3DGizmoUVE final {
    Math::Vector3UVE origin{};
    Math::Vector3UVE halfExtents{2.0F, 2.0F, 2.0F};
    Math::Vector3UVE color{0.95F, 0.45F, 0.28F};
    bool enabled = true;
};

void CollectOccluder3DGizmosUVE(IEntityManagerUVE& entityManager, std::vector<Occluder3DGizmoUVE>& out);

} // namespace UVE::Scene
