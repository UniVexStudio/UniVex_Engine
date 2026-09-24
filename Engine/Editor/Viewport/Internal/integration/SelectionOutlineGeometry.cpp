#include "integration/SelectionOutlineGeometry.h"

#include <algorithm>
#include <unordered_map>
#include <utility>

#include "integration/EntityPicker.h"

#include "uve/component/editor_internal_entity_component_uve.h"
#include "uve/component/hierarchy_component_uve.h"
#include "uve/component/primitive_mesh_component_uve.h"
#include "uve/component/visibility_component_uve.h"
#include "uve/component/world_transform_component_uve.h"
#include "uve/math/matrix4x4_uve.h"
#include "uve/render_systems/primitive_geometry_uve.h"

namespace univex::integration {

using UVE::Scene::EntityUVE;

std::vector<univex::render::SelectionOutlineVertex> CollectSelectionOutlineTrianglesUVE(
    UVE::Scene::IEntityManagerUVE& entityManager, const std::vector<EntityUVE>& selection, const EntityUVE active) {
    std::vector<univex::render::SelectionOutlineVertex> triangles;
    if (selection.empty()) {
        return triangles;
    }

    // Parent -> children from one pass over the hierarchy, so walking below each selected node does
    // not rescan the scene per node.
    std::vector<std::pair<EntityUVE, EntityUVE>> links;
    entityManager.ForEachUVE<UVE::Scene::HierarchyComponentUVE>(
        [&links](const EntityUVE child, const UVE::Scene::HierarchyComponentUVE& hierarchy) {
            links.emplace_back(hierarchy.parent, child);
        });
    std::unordered_map<EntityUVE, std::vector<EntityUVE>> childrenOf;
    for (const auto& [parent, child] : links) {
        if (parent != UVE::Scene::kInvalidEntityUVE) {
            childrenOf[parent].push_back(child);
        }
    }

    // How strongly each node is selected: its own selection, or the strongest selected ancestor.
    std::unordered_map<EntityUVE, float> weights;
    for (const EntityUVE selected : selection) {
        if (!entityManager.IsAliveUVE(selected)) {
            continue;
        }
        const float weight = selected == active ? univex::render::kSelectionOutlineActiveWeight
                                                : univex::render::kSelectionOutlineOtherWeight;
        std::vector<EntityUVE> pending{selected};
        while (!pending.empty()) {
            const EntityUVE current = pending.back();
            pending.pop_back();
            const auto [slot, inserted] = weights.try_emplace(current, weight);
            if (!inserted) {
                if (slot->second >= weight) {
                    continue; // already reached as strongly: its subtree is done (and a loop ends here)
                }
                slot->second = weight;
            }
            if (const auto children = childrenOf.find(current); children != childrenOf.end()) {
                pending.insert(pending.end(), children->second.begin(), children->second.end());
            }
        }
    }

    for (const auto& [entity, weight] : weights) {
        if (!entityManager.HasComponentUVE<UVE::Scene::PrimitiveMeshComponentUVE>(entity) ||
            !entityManager.HasComponentUVE<UVE::Scene::WorldTransformComponentUVE>(entity) ||
            entityManager.HasComponentUVE<UVE::Scene::EditorInternalEntityComponentUVE>(entity)) {
            continue;
        }
        if (entityManager.HasComponentUVE<UVE::Scene::VisibilityComponentUVE>(entity) &&
            !entityManager.GetComponentUVE<UVE::Scene::VisibilityComponentUVE>(entity).visibleInHierarchy) {
            continue; // the renderer does not draw it, so there is no shape to outline
        }
        const auto& primitive = entityManager.GetComponentUVE<UVE::Scene::PrimitiveMeshComponentUVE>(entity);
        const auto& worldTransform = entityManager.GetComponentUVE<UVE::Scene::WorldTransformComponentUVE>(entity);
        UVE::Math::Matrix4x4UVE worldMatrix{};
        if (worldTransform.dirty || !UVE::Scene::IsPrimitiveMeshComponentValidUVE(primitive) ||
            !TryComputeWorldMatrixUVE(worldTransform, worldMatrix)) {
            continue;
        }
        const UVE::Render::PrimitiveGeometryUVE& geometry = UVE::Render::GetPrimitiveGeometryUVE(primitive.kind);
        const std::size_t indexCount = geometry.indices.size() - (geometry.indices.size() % 3U);
        triangles.reserve(triangles.size() + indexCount);
        for (std::size_t index = 0; index < indexCount; ++index) {
            const UVE::Math::Vector3UVE world =
                UVE::Math::TransformPointUVE(worldMatrix, geometry.vertices[geometry.indices[index]].position);
            triangles.push_back(univex::render::SelectionOutlineVertex{world.x, world.y, world.z, weight});
        }
    }
    return triangles;
}

} // namespace univex::integration
