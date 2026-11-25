// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cstddef>
#include <span>
#include <vector>

#include "entity.h"
#include "i_entity_manager.h"
#include "vector3.h"
#include "area_overlap_system.h"

namespace UVE::Physics {

struct Area3DOccupancySyncResult final {
    std::size_t areaCount = 0U;
    std::size_t refreshedAreaCount = 0U;
    std::size_t truncatedAreaCount = 0U;
    std::size_t listedBodyCount = 0U;
    std::size_t listedAreaCount = 0U;
};

struct Area3DBodySpace final {
    Scene::Entity body{};
    Math::Vector3 gravity{};
    float linearDamp = 0.0F;
    float angularDamp = 0.0F;

    [[nodiscard]] bool operator==(const Area3DBodySpace&) const noexcept = default;
};

[[nodiscard]] bool HasAnyArea3DSpaceOverride(Scene::IEntityManager& entityManager);

[[nodiscard]] Area3DOccupancySyncResult ApplyArea3DOccupancy(
    Scene::IEntityManager& entityManager, const AreaOverlapQueryResult& snapshot);

[[nodiscard]] Area3DOccupancySyncResult SyncArea3DOccupancy(Scene::IEntityManager& entityManager);

[[nodiscard]] std::vector<Area3DBodySpace> CollectArea3DBodySpaces(
    Scene::IEntityManager& entityManager, const Math::Vector3& worldGravity);

[[nodiscard]] const Area3DBodySpace* FindArea3DBodySpace(
    std::span<const Area3DBodySpace> spaces, Scene::Entity body) noexcept;

} // namespace UVE::Physics
