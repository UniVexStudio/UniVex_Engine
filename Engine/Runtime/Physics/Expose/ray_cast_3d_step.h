// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cstdint>

#include "entity.h"
#include "i_entity_manager.h"
#include "vector3.h"
#include "ray_cast_3d.h"
#include "i_raycast_system.h"

namespace UVE::Physics {

enum class RayCast3DStepCode : std::uint8_t {
    Hit = 0,
    Miss,
    Disabled,
    UnknownEntity,
    NotARayCast,
    MissingTransform,
    InvalidComponent,
};

struct RayCast3DStepResult final {
    RayCast3DStepCode code = RayCast3DStepCode::UnknownEntity;
    bool hasHit = false;
    Scene::Entity hitEntity = Scene::kInvalidEntity;
    Math::Vector3 hitPosition{};
    Math::Vector3 hitNormal{};

    [[nodiscard]] bool Hit() const noexcept { return code == RayCast3DStepCode::Hit; }
    [[nodiscard]] bool Missed() const noexcept { return code == RayCast3DStepCode::Miss; }
    [[nodiscard]] bool IsDisabled() const noexcept { return code == RayCast3DStepCode::Disabled; }
    [[nodiscard]] bool IsRefused() const noexcept {
        return code == RayCast3DStepCode::UnknownEntity || code == RayCast3DStepCode::NotARayCast;
    }
};

/// One evaluation of a RayCast3D: node policy (enabled, local-axis direction, exclusion prefix,
/// result writeback) then IRaycastSystem for the actual cast. Every fail-closed gate clears
/// the whole result. The ray's own entity is always ignored.
[[nodiscard]] RayCast3DStepResult StepRayCast3D(Scene::IEntityManager& entityManager,
                                                       const IRaycastSystem& raycastSystem,
                                                       Scene::Entity entity);

} // namespace UVE::Physics
