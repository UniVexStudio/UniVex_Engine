// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cstdint>
#include <string_view>

#include "uve/component/entity_uve.h"
#include "uve/component/render_instance_component_uve.h"
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

/// True when this surface writes the shadow maps. Off does not. On, DoubleSided and ShadowsOnly
/// do. DoubleSided is the same caster as On: the depth pass already rasterizes both faces.
[[nodiscard]] bool SurfaceInstance3DCastsShadowUVE(const SurfaceInstanceComponentUVE& surface) noexcept;

/// True when this surface is drawn in the colour view. ShadowsOnly is a caster only, so it is not.
[[nodiscard]] bool SurfaceInstance3DDrawsInViewUVE(const SurfaceInstanceComponentUVE& surface) noexcept;

/// Opacity in [0, 1]: 1 fully solid, 0 invisible. The authored field is the inverse (0 opaque,
/// 1 invisible). Non-finite transparency is treated as fully solid so a broken value cannot
/// blank the object.
[[nodiscard]] float SurfaceInstance3DOpacityUVE(const SurfaceInstanceComponentUVE& surface) noexcept;

/// Weight in [0, 1] for a Self fade across the begin/end margins. Disabled and Dependencies are
/// 1 inside the hard range: Dependencies fades stand-ins, which this draw does not own. Non-finite
/// or out of range is 0.
[[nodiscard]] float SurfaceInstance3DVisibilityFadeWeightUVE(const SurfaceInstanceComponentUVE& surface,
                                                            float cameraDistance) noexcept;

/// True when the surface names an overlay material. Empty is none.
[[nodiscard]] bool SurfaceInstance3DHasOverlayUVE(const SurfaceInstanceComponentUVE& surface) noexcept;

/// Pulls an overlay pass slightly closer than its host so back-to-front order paints it on top.
/// Non-finite depth is left unchanged.
[[nodiscard]] float ApplySurfaceInstance3DOverlaySortBiasUVE(float baseDepth) noexcept;

/// Distance LodGroup3D should resolve against after `lodBias`. Above 1 keeps detailed levels
/// longer (the camera looks closer); below 1 drops them sooner. Bias 1, non-finite, or not
/// positive leaves `cameraDistance` unchanged. Non-finite distance is returned as-is.
[[nodiscard]] float SurfaceInstance3DLodDistanceUVE(const SurfaceInstanceComponentUVE& surface,
                                                    float cameraDistance) noexcept;

// =================================================================================================
// What RenderInstance3D means to the draw list.
// =================================================================================================

/// True when this instance's layers overlap `viewLayerMask`. A view mask of 0, or an instance on
/// no layers, is off the view. An entity with no RenderInstance component is treated as layer 1.
[[nodiscard]] bool IsRenderInstance3DOnViewLayersUVE(std::uint32_t renderLayers,
                                                     std::uint32_t viewLayerMask) noexcept;

/// Adds `sortingOffset` to the frustum-derived depth. Non-finite offset leaves `baseDepth`
/// unchanged so a broken offset cannot poison the queue.
[[nodiscard]] float ApplyRenderInstance3DSortingOffsetUVE(float baseDepth, float sortingOffset) noexcept;

// =================================================================================================
// What LightEmitter3D means to the light list.
// =================================================================================================

/// True when this light's cull mask overlaps the receiver's render layers. A light with no bits
/// set, or a receiver on no layers, is unlit by it.
[[nodiscard]] bool IsLightEmitter3DLightingLayersUVE(std::uint32_t cullMask, std::uint32_t renderLayers) noexcept;

/// Weight in [0, 1] for a distance fade that begins at `begin` and reaches zero over `length`.
/// A zero length is a hard cut at `begin`. Non-finite inputs fail closed (weight 0).
[[nodiscard]] float LightEmitter3DDistanceFadeWeightUVE(float distance, float begin, float length) noexcept;

} // namespace UVE::Scene
