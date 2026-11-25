#include "integration/selection_outline_geometry.h"

#include <algorithm>
#include <unordered_map>
#include <utility>

#include "integration/entity_picker.h"

#include "editor_internal_entity_component.h"
#include "hierarchy_component.h"
#include "primitive_mesh_component.h"
#include "visibility_component.h"
#include "world_transform_component.h"
#include "matrix4x4.h"
#include "primitive_geometry.h"

namespace univex::integration {

using UVE::Scene::Entity;

std::vector<univex::render::SelectionOutlineVertex> CollectSelectionOutlineTriangles(
    UVE::Scene::IEntityManager& entityManager, const std::vector<Entity>& selection, const Entity active) {
    std::vector<univex::render::SelectionOutlineVertex> triangles;
    if (selection.empty()) {
        return triangles;
    }

    // Parent -> children from one pass over the hierarchy, so walking below each selected object does
    // not rescan the scene per object.
    std::vector<std::pair<Entity, Entity>> links;
    entityManager.ForEach<UVE::Scene::HierarchyComponent>(
        [&links](const Entity child, const UVE::Scene::HierarchyComponent& hierarchy) {
            links.emplace_back(hierarchy.parent, child);
        });
    std::unordered_map<Entity, std::vector<Entity>> childrenOf;
    for (const auto& [parent, child] : links) {
        if (parent != UVE::Scene::kInvalidEntity) {
            childrenOf[parent].push_back(child);
        }
    }

    // How strongly each object is selected: its own selection, or the strongest selected ancestor.
    std::unordered_map<Entity, float> weights;
    for (const Entity selected : selection) {
        if (!entityManager.IsAlive(selected)) {
            continue;
        }
        const float weight = selected == active ? univex::render::kSelectionOutlineActiveWeight
                                                : univex::render::kSelectionOutlineOtherWeight;
        std::vector<Entity> pending{selected};
        while (!pending.empty()) {
            const Entity current = pending.back();
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
        if (!entityManager.HasComponent<UVE::Scene::PrimitiveMeshComponent>(entity) ||
            !entityManager.HasComponent<UVE::Scene::WorldTransformComponent>(entity) ||
            entityManager.HasComponent<UVE::Scene::EditorInternalEntityComponent>(entity)) {
            continue;
        }
        if (entityManager.HasComponent<UVE::Scene::VisibilityComponent>(entity) &&
            !entityManager.GetComponent<UVE::Scene::VisibilityComponent>(entity).visibleInHierarchy) {
            continue; // the renderer does not draw it, so there is no shape to outline
        }
        const auto& primitive = entityManager.GetComponent<UVE::Scene::PrimitiveMeshComponent>(entity);
        const auto& worldTransform = entityManager.GetComponent<UVE::Scene::WorldTransformComponent>(entity);
        UVE::Math::Matrix4x4 worldMatrix{};
        if (worldTransform.dirty || !UVE::Scene::IsPrimitiveMeshComponentValid(primitive) ||
            !TryComputeWorldMatrix(worldTransform, worldMatrix)) {
            continue;
        }
        const UVE::Render::PrimitiveGeometry& geometry = UVE::Render::GetPrimitiveGeometry(primitive.kind);
        const std::size_t indexCount = geometry.indices.size() - (geometry.indices.size() % 3U);
        triangles.reserve(triangles.size() + indexCount);
        for (std::size_t index = 0; index < indexCount; ++index) {
            const UVE::Math::Vector3 world =
                UVE::Math::TransformPoint(worldMatrix, geometry.vertices[geometry.indices[index]].position);
            triangles.push_back(univex::render::SelectionOutlineVertex{world.x, world.y, world.z, weight});
        }
    }
    return triangles;
}

} // namespace univex::integration
