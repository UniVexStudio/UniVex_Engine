// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <string_view>

#include "uve/component/entity_uve.h"

namespace UVE::Scene {

class IEntityManagerUVE;

/// Abstract physics bases between Object3D and concrete 3D physics nodes. These are never created
/// directly; concrete nodes apply them so the authored component hierarchy stays consistent:
///
///   Object3D
///   +- PhysicsObject3D       takes part in collision       (PhysicsObjectComponentUVE)
///      +- SolidBody3D        is stopped by what it hits    (SolidBodyComponentUVE)
struct PhysicsObject3DNodeDefinitionUVE final {
    static constexpr std::string_view typeName = "PhysicsObject3D";
};

struct SolidBody3DNodeDefinitionUVE final {
    static constexpr std::string_view typeName = "SolidBody3D";
};

/// Applies Object3D and the PhysicsObject3D base component if they are missing.
void ApplyPhysicsObject3DBaseUVE(IEntityManagerUVE& entityManager, EntityUVE entity,
                                 std::string_view nameFallback);
/// Applies PhysicsObject3D, then the SolidBody3D component if it is missing.
void ApplySolidBody3DBaseUVE(IEntityManagerUVE& entityManager, EntityUVE entity,
                             std::string_view nameFallback);

} // namespace UVE::Scene
