// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/scene/scene_world_frame_uve.h"

#include <vector>

#include "uve/component/hierarchy_component_uve.h"
#include "uve/component/transform_component_uve.h"
#include "uve/entity/i_entity_manager_uve.h"

namespace UVE::Scene {

namespace {

/// How many parents a chain may have before it is treated as malformed. Bounded rather than trusting
/// the hierarchy to be acyclic: a corrupted parent link must end the walk, not hang the frame.
inline constexpr int kMaximumHierarchyDepthUVE = 1024;

} // namespace

ObjectWorldFrameUVE ComposeSceneObjectWorldFrameUVE(IEntityManagerUVE& entityManager,
                                                    EntityUVE entity) {
    std::vector<const TransformComponentUVE*> chain;
    for (int depth = 0; depth < kMaximumHierarchyDepthUVE && entityManager.IsAliveUVE(entity); ++depth) {
        if (!entityManager.HasComponentUVE<TransformComponentUVE>(entity)) {
            break;
        }
        const TransformComponentUVE& local = entityManager.GetComponentUVE<TransformComponentUVE>(entity);
        chain.push_back(&local);
        if (local.topLevel) {
            break;
        }
        const EntityUVE parent = entityManager.HasComponentUVE<HierarchyComponentUVE>(entity)
                                     ? entityManager.GetComponentUVE<HierarchyComponentUVE>(entity).parent
                                     : kInvalidEntityUVE;
        if (parent == kInvalidEntityUVE || !entityManager.HasComponentUVE<TransformComponentUVE>(parent)) {
            break;
        }
        entity = parent;
    }

    ObjectWorldFrameUVE frame{};
    for (std::size_t index = chain.size(); index > 0U; --index) {
        const TransformComponentUVE& local = *chain[index - 1U];
        const Math::Vector3UVE scaled{local.localPosition.x * frame.scale.x,
                                      local.localPosition.y * frame.scale.y,
                                      local.localPosition.z * frame.scale.z};
        frame.position = frame.position + Math::RotateVectorUVE(frame.rotation, scaled);
        frame.rotation = Math::MultiplyUVE(frame.rotation, local.localRotation);
        frame.scale = Math::Vector3UVE{frame.scale.x * local.localScale.x, frame.scale.y * local.localScale.y,
                                       frame.scale.z * local.localScale.z};
    }
    return frame;
}

} // namespace UVE::Scene
