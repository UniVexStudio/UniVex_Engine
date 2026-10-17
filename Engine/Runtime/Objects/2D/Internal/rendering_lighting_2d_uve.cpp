// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/objects/2d/rendering_lighting_2d_uve.h"

#include <algorithm>
#include <cmath>

#include "uve/component/disabled_component_uve.h"
#include "uve/component/transform_component_uve.h"
#include "uve/entity/i_entity_manager_uve.h"

namespace UVE::Scene {

void ApplyMeshInstance2DObjectDefinitionUVE(IEntityManagerUVE& entityManager,
                                            const EntityUVE entity,
                                            const MeshInstance2DObjectDefinitionUVE& value) {
    MeshInstance2DComponentUVE out = value.mesh;
    out.instanced = false;
    entityManager.AddComponentUVE<MeshInstance2DComponentUVE>(entity, std::move(out));
}

void ApplyMultiMeshInstance2DObjectDefinitionUVE(
    IEntityManagerUVE& entityManager, const EntityUVE entity,
    const MultiMeshInstance2DObjectDefinitionUVE& value) {
    MeshInstance2DComponentUVE out = value.mesh;
    out.instanced = true;
    entityManager.AddComponentUVE<MeshInstance2DComponentUVE>(entity, std::move(out));
}

void ApplyCanvasGroupObjectDefinitionUVE(IEntityManagerUVE& entityManager, const EntityUVE entity,
                                         const CanvasGroupObjectDefinitionUVE& value) {
    CanvasGroupComponentUVE out = value.group;
    out.groupOpacity = std::isfinite(out.groupOpacity) ? std::clamp(out.groupOpacity, 0.0F, 1.0F)
                                                       : 1.0F;
    entityManager.AddComponentUVE<CanvasGroupComponentUVE>(entity, out);
}

void ApplyBackBufferCopyObjectDefinitionUVE(IEntityManagerUVE& entityManager,
                                            const EntityUVE entity,
                                            const BackBufferCopyObjectDefinitionUVE& value) {
    entityManager.AddComponentUVE<BackBufferCopyComponentUVE>(entity, value.backBufferCopy);
}

void ApplyLight2DObjectDefinitionUVE(IEntityManagerUVE& entityManager, const EntityUVE entity,
                                     const Light2DObjectDefinitionUVE& value) {
    entityManager.AddComponentUVE<Light2DComponentUVE>(entity, value.light);
}

void ApplyPointLight2DObjectDefinitionUVE(IEntityManagerUVE& entityManager, const EntityUVE entity,
                                          const PointLight2DObjectDefinitionUVE& value) {
    Light2DComponentUVE out = value.light;
    out.kind = Light2DKindUVE::Point;
    entityManager.AddComponentUVE<Light2DComponentUVE>(entity, out);
}

void ApplyDirectionalLight2DObjectDefinitionUVE(
    IEntityManagerUVE& entityManager, const EntityUVE entity,
    const DirectionalLight2DObjectDefinitionUVE& value) {
    Light2DComponentUVE out = value.light;
    out.kind = Light2DKindUVE::Directional;
    entityManager.AddComponentUVE<Light2DComponentUVE>(entity, out);
}

void ApplyLightOccluder2DObjectDefinitionUVE(IEntityManagerUVE& entityManager,
                                             const EntityUVE entity,
                                             const LightOccluder2DObjectDefinitionUVE& value) {
    entityManager.AddComponentUVE<LightOccluder2DComponentUVE>(entity, value.occluder);
}

Math::ColorUVE EvaluateLight2DIlluminationAtPointUVE(IEntityManagerUVE& entityManager,
                                                     const Math::Vector2UVE& point2D) {
    if (!Math::IsFiniteUVE(point2D)) {
        return Math::ColorUVE{0.0F, 0.0F, 0.0F};
    }
    std::vector<Math::RectUVE> occluderRects;
    entityManager.ForEachUVE<LightOccluder2DComponentUVE>(
        [&](const EntityUVE entity, const LightOccluder2DComponentUVE& occ) {
            if (!occ.enabled || entityManager.HasComponentUVE<DisabledComponentUVE>(entity)) {
                return;
            }
            Math::Vector2UVE pos{0.0F, 0.0F};
            if (entityManager.HasComponentUVE<TransformComponentUVE>(entity)) {
                const auto& lp =
                    entityManager.GetComponentUVE<TransformComponentUVE>(entity).localPosition;
                pos = Math::Vector2UVE{lp.x, lp.y};
            }
            occluderRects.push_back(
                Math::RectUVE{occ.boundingRect.position + pos, occ.boundingRect.size});
        });

    Math::ColorUVE accum{0.0F, 0.0F, 0.0F};
    entityManager.ForEachUVE<Light2DComponentUVE>(
        [&](const EntityUVE entity, const Light2DComponentUVE& light) {
            if (!light.enabled || entityManager.HasComponentUVE<DisabledComponentUVE>(entity) ||
                !std::isfinite(light.energy) || light.energy <= 0.0F) {
                return;
            }
            float attenuation = 1.0F;
            Math::Vector2UVE lightPos{0.0F, 0.0F};
            if (entityManager.HasComponentUVE<TransformComponentUVE>(entity)) {
                const auto& lp =
                    entityManager.GetComponentUVE<TransformComponentUVE>(entity).localPosition;
                lightPos = Math::Vector2UVE{lp.x, lp.y};
            }
            if (light.kind == Light2DKindUVE::Point) {
                if (!std::isfinite(light.rangePixels) || light.rangePixels <= 0.0F) {
                    return;
                }
                const Math::Vector2UVE d = point2D - lightPos;
                const float dist = std::sqrt(d.x * d.x + d.y * d.y);
                if (dist >= light.rangePixels) {
                    return;
                }
                attenuation = 1.0F - (dist / light.rangePixels);
            }
            if (light.shadowEnabled) {
                const Math::Vector2UVE midpoint = (lightPos + point2D) * 0.5F;
                for (const Math::RectUVE& rect : occluderRects) {
                    if (Math::ContainsUVE(rect, midpoint) || Math::ContainsUVE(rect, point2D)) {
                        return;
                    }
                }
            }
            accum.r += light.color.r * light.energy * attenuation;
            accum.g += light.color.g * light.energy * attenuation;
            accum.b += light.color.b * light.energy * attenuation;
        });
    return accum;
}

} // namespace UVE::Scene
