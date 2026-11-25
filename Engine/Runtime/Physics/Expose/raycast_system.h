// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#pragma once

#include "i_raycast_system.h"

namespace UVE::Physics {

/// RaycastSystem is the concrete, engine-standard implementation of IRaycastSystem.
/// Deliberately stateless (no members) — matches CollisionSystem's precedent exactly.
class RaycastSystem final : public IRaycastSystem {
public:
    [[nodiscard]] std::optional<RaycastHit> Raycast(Scene::IEntityManager& entityManager,
                                                             const RaycastQuery& query) const override;
};

} // namespace UVE::Physics
