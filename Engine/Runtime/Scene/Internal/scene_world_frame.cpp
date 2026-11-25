// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "scene_world_frame.h"

#include <vector>

#include "hierarchy_component.h"
#include "transform_component.h"
#include "i_entity_manager.h"

namespace UVE::Scene {

namespace {

/// How many parents a chain may have before it is treated as malformed. Bounded rather than trusting
/// the hierarchy to be acyclic: a corrupted parent link must end the walk, not hang the frame.
inline constexpr int kMaximumHierarchyDepth = 1024;

} // namespace

ObjectWorldFrame ComposeSceneObjectWorldFrame(IEntityManager& entityManager,
                                                    Entity entity) {
    std::vector<const TransformComponent*> chain;
    for (int depth = 0; depth < kMaximumHierarchyDepth && entityManager.IsAlive(entity); ++depth) {
        if (!entityManager.HasComponent<TransformComponent>(entity)) {
            break;
        }
        const TransformComponent& local = entityManager.GetComponent<TransformComponent>(entity);
        chain.push_back(&local);
        if (local.topLevel) {
            break;
        }
        const Entity parent = entityManager.HasComponent<HierarchyComponent>(entity)
                                     ? entityManager.GetComponent<HierarchyComponent>(entity).parent
                                     : kInvalidEntity;
        if (parent == kInvalidEntity || !entityManager.HasComponent<TransformComponent>(parent)) {
            break;
        }
        entity = parent;
    }

    ObjectWorldFrame frame{};
    for (std::size_t index = chain.size(); index > 0U; --index) {
        const TransformComponent& local = *chain[index - 1U];
        const Math::Vector3 scaled{local.localPosition.x * frame.scale.x,
                                      local.localPosition.y * frame.scale.y,
                                      local.localPosition.z * frame.scale.z};
        frame.position = frame.position + Math::RotateVector(frame.rotation, scaled);
        frame.rotation = Math::Multiply(frame.rotation, local.localRotation);
        frame.scale = Math::Vector3{frame.scale.x * local.localScale.x, frame.scale.y * local.localScale.y,
                                       frame.scale.z * local.localScale.z};
    }
    return frame;
}

} // namespace UVE::Scene
