// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

#include "entity.h"
#include "aabb.h"
#include "object_3d_common.h"

namespace UVE::Scene {

class IEntityManager;

enum class Occluder3DObjectMode : std::uint8_t {
    ConservativeBox = 0,
};

/// An Occluder3D: an authored axis-aligned box (halfExtents around the object's world position)
/// that hides whatever stands strictly BEHIND it from the current viewer. Conservative: a
/// candidate is hidden only when every corner of its world AABB is hidden. A mesh that peeks
/// around the wall still draws. No state is kept between frames.
struct Occluder3DComponent final {
    Math::Vector3 halfExtents{2.0F, 2.0F, 2.0F};
    Occluder3DObjectMode mode = Occluder3DObjectMode::ConservativeBox;
    bool enabled = true;
};

[[nodiscard]] bool IsOccluder3DObjectComponentValid(const Occluder3DComponent& value) noexcept;

struct Occluder3DObjectDefinition final {
    static constexpr std::string_view defaultName = "Occluder3D";
    Occluder3DComponent occluder{};
};

void ApplyOccluder3DObjectDefinition(IEntityManager& entityManager, Entity entity,
                                        const Occluder3DObjectDefinition& value);

// One occluder against one candidate point. Invalid config, a non-finite pose, the viewer or
// the point inside the box, or a grazing touch: NOT hidden.
[[nodiscard]] bool ResolveOccluder3DFullyHidden(const Occluder3DComponent& config,
                                                   const Math::Vector3& occluderWorldPosition,
                                                   const Math::Vector3& viewerWorldPosition,
                                                   const Math::Vector3& pointWorld) noexcept;

// True only when every corner of `bounds` is hidden by this occluder. An unordered or
// non-finite AABB fails open.
[[nodiscard]] bool ResolveOccluder3DFullyHidesAabb(const Occluder3DComponent& config,
                                                      const Math::Vector3& occluderWorldPosition,
                                                      const Math::Vector3& viewerWorldPosition,
                                                      const Math::Aabb& bounds) noexcept;

struct Occluder3DSnapshot final {
    Occluder3DComponent config{};
    Math::Vector3 worldPosition{};
};

void CollectOccluder3DSnapshots(IEntityManager& entityManager, std::vector<Occluder3DSnapshot>& out);

[[nodiscard]] bool IsOccluder3DPointDrawHidden(std::span<const Occluder3DSnapshot> occluders,
                                                  const Math::Vector3& viewerWorldPosition,
                                                  const Math::Vector3& pointWorld) noexcept;

[[nodiscard]] bool IsOccluder3DAabbDrawHidden(std::span<const Occluder3DSnapshot> occluders,
                                                 const Math::Vector3& viewerWorldPosition,
                                                 const Math::Aabb& bounds) noexcept;

struct Occluder3DGizmo final {
    Math::Vector3 origin{};
    Math::Vector3 halfExtents{2.0F, 2.0F, 2.0F};
    Math::Vector3 color{0.95F, 0.45F, 0.28F};
    bool enabled = true;
};

void CollectOccluder3DGizmos(IEntityManager& entityManager, std::vector<Occluder3DGizmo>& out);

} // namespace UVE::Scene
