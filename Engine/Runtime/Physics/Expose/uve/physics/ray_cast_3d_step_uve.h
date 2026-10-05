// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cstdint>

#include "uve/component/entity_uve.h"
#include "uve/entity/i_entity_manager_uve.h"
#include "uve/math/vector3_uve.h"
#include "uve/objects/3d/ray_cast_3d_uve.h"
#include "uve/physics/i_raycast_system_uve.h"

namespace UVE::Physics {

enum class RayCast3DStepCodeUVE : std::uint8_t {
    Hit = 0,
    Miss,
    Disabled,
    UnknownEntity,
    NotARayCast,
    MissingTransform,
    InvalidComponent,
};

struct RayCast3DStepResultUVE final {
    RayCast3DStepCodeUVE code = RayCast3DStepCodeUVE::UnknownEntity;
    bool hasHit = false;
    Scene::EntityUVE hitEntity = Scene::kInvalidEntityUVE;
    Math::Vector3UVE hitPosition{};
    Math::Vector3UVE hitNormal{};

    [[nodiscard]] bool HitUVE() const noexcept { return code == RayCast3DStepCodeUVE::Hit; }
    [[nodiscard]] bool MissedUVE() const noexcept { return code == RayCast3DStepCodeUVE::Miss; }
    [[nodiscard]] bool IsDisabledUVE() const noexcept { return code == RayCast3DStepCodeUVE::Disabled; }
    [[nodiscard]] bool IsRefusedUVE() const noexcept {
        return code == RayCast3DStepCodeUVE::UnknownEntity || code == RayCast3DStepCodeUVE::NotARayCast;
    }
};

/// One evaluation of a RayCast3D: node policy (enabled, local-axis direction, exclusion prefix,
/// result writeback) then IRaycastSystemUVE for the actual cast. Every fail-closed gate clears
/// the whole result. The ray's own entity is always ignored.
[[nodiscard]] RayCast3DStepResultUVE StepRayCast3DUVE(Scene::IEntityManagerUVE& entityManager,
                                                       const IRaycastSystemUVE& raycastSystem,
                                                       Scene::EntityUVE entity);

} // namespace UVE::Physics
