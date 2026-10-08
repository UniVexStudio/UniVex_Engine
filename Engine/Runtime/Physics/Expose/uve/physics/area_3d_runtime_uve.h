// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cstddef>
#include <span>
#include <vector>

#include "uve/component/entity_uve.h"
#include "uve/entity/i_entity_manager_uve.h"
#include "uve/math/vector3_uve.h"
#include "uve/physics/area_overlap_system_uve.h"

namespace UVE::Physics {

struct Area3DOccupancySyncResultUVE final {
    std::size_t areaCount = 0U;
    std::size_t refreshedAreaCount = 0U;
    std::size_t truncatedAreaCount = 0U;
    std::size_t listedBodyCount = 0U;
    std::size_t listedAreaCount = 0U;
};

struct Area3DBodySpaceUVE final {
    Scene::EntityUVE body{};
    Math::Vector3UVE gravity{};
    float linearDamp = 0.0F;
    float angularDamp = 0.0F;

    [[nodiscard]] bool operator==(const Area3DBodySpaceUVE&) const noexcept = default;
};

[[nodiscard]] bool HasAnyArea3DSpaceOverrideUVE(Scene::IEntityManagerUVE& entityManager);

[[nodiscard]] Area3DOccupancySyncResultUVE ApplyArea3DOccupancyUVE(
    Scene::IEntityManagerUVE& entityManager, const AreaOverlapQueryResultUVE& snapshot);

[[nodiscard]] Area3DOccupancySyncResultUVE SyncArea3DOccupancyUVE(Scene::IEntityManagerUVE& entityManager);

[[nodiscard]] std::vector<Area3DBodySpaceUVE> CollectArea3DBodySpacesUVE(
    Scene::IEntityManagerUVE& entityManager, const Math::Vector3UVE& worldGravity);

[[nodiscard]] const Area3DBodySpaceUVE* FindArea3DBodySpaceUVE(
    std::span<const Area3DBodySpaceUVE> spaces, Scene::EntityUVE body) noexcept;

} // namespace UVE::Physics
