// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/objects/3d/abstract_objects_3d_uve.h"

#include <cmath>

#include "uve/component/light_emitter_component_uve.h"
#include "uve/component/render_instance_component_uve.h"
#include "uve/component/surface_instance_component_uve.h"
#include "uve/component/visibility_component_uve.h"
#include "uve/entity/i_entity_manager_uve.h"
#include "uve/objects/3d/object_3d_uve.h"

namespace UVE::Scene {
void ApplyObject3DRecipeUVE(IEntityManagerUVE& entityManager, const EntityUVE entity, const std::string_view name) {
    if (!entityManager.IsAliveUVE(entity)) {
        return;
    }
    EnsureObject3DBaselineUVE(entityManager, entity, name);
    if (!entityManager.HasComponentUVE<VisibilityComponentUVE>(entity)) {
        entityManager.AddComponentUVE<VisibilityComponentUVE>(entity, VisibilityComponentUVE{});
    }
    EnsureCommonObjectSectionUVE(entityManager, entity);
}

namespace {

/// The Object3D recipe under the child's name, then the base component.
template <typename BaseComponentT>
void ApplyBaseUVE(IEntityManagerUVE& entityManager, const EntityUVE entity, const std::string_view nameFallback) {
    if (!entityManager.IsAliveUVE(entity)) {
        return;
    }
    ApplyObject3DRecipeUVE(entityManager, entity, nameFallback);
    if (!entityManager.HasComponentUVE<BaseComponentT>(entity)) {
        entityManager.AddComponentUVE<BaseComponentT>(entity, BaseComponentT{});
    }
}

template <typename ComponentT>
void EnsureUVE(IEntityManagerUVE& entityManager, const EntityUVE entity) {
    if (entityManager.IsAliveUVE(entity) && !entityManager.HasComponentUVE<ComponentT>(entity)) {
        entityManager.AddComponentUVE<ComponentT>(entity, ComponentT{});
    }
}

} // namespace

void ApplyRenderInstance3DBaseUVE(IEntityManagerUVE& entityManager, const EntityUVE entity,
                                  const std::string_view nameFallback) {
    ApplyBaseUVE<RenderInstanceComponentUVE>(entityManager, entity, nameFallback);
}

void ApplySurfaceInstance3DBaseUVE(IEntityManagerUVE& entityManager, const EntityUVE entity,
                                   const std::string_view nameFallback) {
    ApplyRenderInstance3DBaseUVE(entityManager, entity, nameFallback);
    EnsureUVE<SurfaceInstanceComponentUVE>(entityManager, entity);
}

void ApplyLightEmitter3DBaseUVE(IEntityManagerUVE& entityManager, const EntityUVE entity,
                                const std::string_view nameFallback) {
    ApplyRenderInstance3DBaseUVE(entityManager, entity, nameFallback);
    EnsureUVE<LightEmitterComponentUVE>(entityManager, entity);
}

bool IsSurfaceInstance3DOutsideVisibilityRangeUVE(const SurfaceInstanceComponentUVE& surface,
                                                  const float cameraDistance) noexcept {
    if (!IsSurfaceInstanceComponentValidUVE(surface) || !std::isfinite(cameraDistance) || cameraDistance < 0.0F) {
        return true;
    }
    if (cameraDistance < surface.visibilityRangeBegin) {
        return true;
    }
    // End 0 is "no far limit". Inclusive on both ends: a camera sitting exactly on Begin or End
    // still sees the object, so the boundary is not a flicker seam.
    return surface.visibilityRangeEnd > 0.0F && cameraDistance > surface.visibilityRangeEnd;
}

void ExpandSurfaceInstance3DCullBoundsUVE(const SurfaceInstanceComponentUVE& surface, Math::AabbUVE& bounds) noexcept {
    const float margin = surface.extraCullMargin;
    if (!std::isfinite(margin) || margin <= 0.0F || !std::isfinite(bounds.min.x) || !std::isfinite(bounds.min.y) ||
        !std::isfinite(bounds.min.z) || !std::isfinite(bounds.max.x) || !std::isfinite(bounds.max.y) ||
        !std::isfinite(bounds.max.z)) {
        return;
    }
    bounds.min.x -= margin;
    bounds.min.y -= margin;
    bounds.min.z -= margin;
    bounds.max.x += margin;
    bounds.max.y += margin;
    bounds.max.z += margin;
}

} // namespace UVE::Scene
