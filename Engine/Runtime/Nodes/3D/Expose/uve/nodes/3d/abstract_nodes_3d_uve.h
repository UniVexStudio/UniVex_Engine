// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <string_view>

#include "uve/component/entity_uve.h"

namespace UVE::Scene {

class IEntityManagerUVE;

// The non-physics, non-animation render bases between Node3D and concrete 3D nodes. None of these
// abstract kinds is created directly; they let child node recipes share the components they need:
//
//   Node3D
//   +- RenderInstance3D      is drawn                      (RenderInstanceComponentUVE)
//      +- SurfaceInstance3D  draws geometry                (SurfaceInstanceComponentUVE)
//      +- LightEmitter3D     gives off light               (LightEmitterComponentUVE)
//
// Physics bases live under Nodes/3D/Physics; animation bases live under Nodes/Animation.

struct RenderInstance3DNodeDefinitionUVE final {
    static constexpr std::string_view typeName = "RenderInstance3D";
};

struct SurfaceInstance3DNodeDefinitionUVE final {
    static constexpr std::string_view typeName = "SurfaceInstance3D";
};

struct LightEmitter3DNodeDefinitionUVE final {
    static constexpr std::string_view typeName = "LightEmitter3D";
};

/// The Node3D recipe - transform baseline, Visibility and the common Node section - under `name`.
/// Every Node3D child applies this first, directly or through its abstract base.
void ApplyNode3DRecipeUVE(IEntityManagerUVE& entityManager, EntityUVE entity, std::string_view name);

/// Each applies the Node3D recipe and attaches its base component where missing. Existing
/// components and their authored values are left alone, and a destroyed entity is refused quietly.
void ApplyRenderInstance3DBaseUVE(IEntityManagerUVE& entityManager, EntityUVE entity,
                                 std::string_view nameFallback);
/// The RenderInstance3D recipe, then the base's own component.
void ApplySurfaceInstance3DBaseUVE(IEntityManagerUVE& entityManager, EntityUVE entity,
                                   std::string_view nameFallback);
void ApplyLightEmitter3DBaseUVE(IEntityManagerUVE& entityManager, EntityUVE entity,
                                std::string_view nameFallback);

} // namespace UVE::Scene
