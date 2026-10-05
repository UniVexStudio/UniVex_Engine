// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <string_view>

#include "uve/component/entity_uve.h"
#include "uve/component/surface_instance_component_uve.h"
#include "uve/math/aabb_uve.h"

namespace UVE::Scene {

class IEntityManagerUVE;

// The non-physics, non-animation render bases between Object3D and concrete 3D objects. None of these
// abstract kinds is created directly; they let child object recipes share the components they need:
//
//   Object3D
//   +- RenderInstance3D      is drawn                      (RenderInstanceComponentUVE)
//      +- SurfaceInstance3D  draws geometry                (SurfaceInstanceComponentUVE)
//      +- LightEmitter3D     gives off light               (LightEmitterComponentUVE)
//
// Physics bases live under Objects/3D/Physics; animation bases live under Objects/Animation.

struct RenderInstance3DObjectDefinitionUVE final {
    static constexpr std::string_view typeName = "RenderInstance3D";
};

struct SurfaceInstance3DObjectDefinitionUVE final {
    static constexpr std::string_view typeName = "SurfaceInstance3D";
};

struct LightEmitter3DObjectDefinitionUVE final {
    static constexpr std::string_view typeName = "LightEmitter3D";
};

/// The Object3D recipe - transform baseline, Visibility and the common Object section - under `name`.
/// Every Object3D child applies this first, directly or through its abstract base.
void ApplyObject3DRecipeUVE(IEntityManagerUVE& entityManager, EntityUVE entity, std::string_view name);

/// Each applies the Object3D recipe and attaches its base component where missing. Existing
/// components and their authored values are left alone, and a destroyed entity is refused quietly.
void ApplyRenderInstance3DBaseUVE(IEntityManagerUVE& entityManager, EntityUVE entity,
                                 std::string_view nameFallback);
/// The RenderInstance3D recipe, then the base's own component.
void ApplySurfaceInstance3DBaseUVE(IEntityManagerUVE& entityManager, EntityUVE entity,
                                   std::string_view nameFallback);
void ApplyLightEmitter3DBaseUVE(IEntityManagerUVE& entityManager, EntityUVE entity,
                                std::string_view nameFallback);

// =================================================================================================
// What SurfaceInstance3D means to the draw list.
//
// The component is authored on every mesh, primitive and particle. These functions are the one
// reading of it that the mesh walk (and later the primitive and particle walks) must share, so a
// visibility range or extra cull margin cannot mean two things in two places.
// =================================================================================================

/// True when `cameraDistance` is outside the surface's authored visibility range, so the object
/// must not be drawn. An end of 0 means no far limit. A non-finite distance or an invalid surface
/// is outside - fail closed, never draw a range that cannot be honoured.
[[nodiscard]] bool IsSurfaceInstance3DOutsideVisibilityRangeUVE(const SurfaceInstanceComponentUVE& surface,
                                                                float cameraDistance) noexcept;

/// Grows `bounds` by `extraCullMargin` on every side, so frustum and occlusion tests cover
/// vertices a shader may move outside the mesh AABB. Zero, negative or non-finite margin, or
/// non-finite bounds: left unchanged.
void ExpandSurfaceInstance3DCullBoundsUVE(const SurfaceInstanceComponentUVE& surface,
                                          Math::AabbUVE& bounds) noexcept;

} // namespace UVE::Scene
