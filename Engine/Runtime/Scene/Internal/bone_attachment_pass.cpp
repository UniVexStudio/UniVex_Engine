// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "bone_attachment_pass.h"

#include <algorithm>
#include <cstddef>
#include <optional>
#include <utility>
#include <vector>

#include "hierarchy_component.h"
#include "process_component.h"
#include "transform_component.h"
#include "world_transform_component.h"
#include "i_entity_manager.h"
#include "quaternion.h"
#include "bone_attachment_3d.h"
#include "skeleton_3d.h"
#include "i_scene_graph.h"
#include "scene_world_frame.h"

namespace UVE::Scene {

namespace {

/// The process mode the scene graph resolved for `entity` on its last update, or the hierarchy
/// default for an entity it has not seen yet. Asking the graph rather than the entity's own
/// ProcessComponent is what lets an entity without one inherit its ancestors' answer.
[[nodiscard]] TickMode ResolvedTickMode(const ISceneGraph& sceneGraph, const Entity entity) {
    const std::optional<ResolvedObjectModes> modes = sceneGraph.TryGetResolvedObjectModes(entity);
    return modes.has_value() ? modes->process : TickMode::Running;
}

/// How many parents a chain may have before it is treated as malformed. The walks below are bounded
/// by this rather than trusting the hierarchy to be acyclic: a corrupted parent link must terminate
/// the pass, not hang the frame.
inline constexpr int kMaximumHierarchyDepth = 1024;

} // namespace

BoneAttachmentPassReport SyncBoneAttachment3DObjects(IEntityManager& entityManager,
                                                           const ISceneGraph& sceneGraph) {
    BoneAttachmentPassReport report{};

    // Collected and ordered rather than iterated in place: two attachments can hang off one object,
    // and an author orders them the same way they order every other scene system - physicsPriority -
    // instead of archetype storage order deciding it. Ties keep that storage order (stable_sort).
    std::vector<std::pair<std::int32_t, Entity>> ordered;
    entityManager.ForEach<BoneAttachment3DComponent>(
        [&ordered, &sceneGraph, &entityManager](const Entity entity, BoneAttachment3DComponent&) {
            if (!IsTicking(ResolvedTickMode(sceneGraph, entity), /*simulationPaused=*/false)) {
                return;
            }
            const std::int32_t priority =
                entityManager.HasComponent<ProcessComponent>(entity)
                    ? entityManager.GetComponent<ProcessComponent>(entity).physicsPriority
                    : 0;
            ordered.emplace_back(priority, entity);
        });
    if (ordered.empty()) {
        return report;
    }
    std::stable_sort(ordered.begin(), ordered.end(),
                     [](const auto& left, const auto& right) { return left.first < right.first; });

    // The world frame of an entity as the scene graph will compose it, from the local transforms: see
    // ComposeSceneObjectWorldFrame(), which the IK pass composes by as well.
    const auto worldFrameOf = [&entityManager](Entity entity) {
        return ComposeSceneObjectWorldFrame(entityManager, entity);
    };
    const auto depthOf = [&entityManager](Entity entity) {
        int depth = 0;
        for (; depth < kMaximumHierarchyDepth && entity != kInvalidEntity && entityManager.IsAlive(entity);
             ++depth) {
            if (!entityManager.HasComponent<HierarchyComponent>(entity)) {
                break;
            }
            entity = entityManager.GetComponent<HierarchyComponent>(entity).parent;
        }
        return depth;
    };

    // Parents first: an attachment can ride another - a scope on a rifle that is itself in a hand -
    // and the inner one has to compose from the local transform the outer one wrote this pass.
    // Hierarchy depth is that order; the stable sort above keeps physicsPriority deciding between
    // attachments at the same depth.
    std::stable_sort(ordered.begin(), ordered.end(), [&depthOf](const auto& left, const auto& right) {
        return depthOf(left.second) < depthOf(right.second);
    });

    for (const auto& [priority, entity] : ordered) {
        static_cast<void>(priority);
        BoneAttachment3DComponent& attachment =
            entityManager.GetComponent<BoneAttachment3DComponent>(entity);
        ++report.considered;
        // Last frame's answer does not carry over: an attachment is on its bone this pass or it is
        // not, and `bound`/`resolvedBoneIndex` exist so an author can see which one it is.
        attachment.resolvedBoneIndex = kInvalidSkeletonBoneIndex;
        attachment.bound = false;
        if (!IsBoneAttachment3DObjectComponentResolvable(attachment) ||
            !entityManager.IsAlive(attachment.skeleton) ||
            !entityManager.HasComponent<Skeleton3DComponent>(attachment.skeleton) ||
            !entityManager.HasComponent<TransformComponent>(entity)) {
            ++report.unbound;
            continue;
        }
        const Skeleton3DComponent& skeleton =
            entityManager.GetComponent<Skeleton3DComponent>(attachment.skeleton);
        // A skeleton that is switched off is not posing anything, so nothing rides it.
        if (!skeleton.enabled) {
            ++report.unbound;
            continue;
        }
        // Index wins over name when it names a real bone - that is the pair's documented rule, and it
        // is what makes a renamed bone in a re-export observably lose a name-based attachment instead
        // of silently binding to whichever bone took the name.
        std::uint32_t boneIndex = kInvalidSkeletonBoneIndex;
        if (attachment.boneIndex != kInvalidSkeletonBoneIndex && attachment.boneIndex < skeleton.bones.size()) {
            boneIndex = attachment.boneIndex;
        } else if (!TryFindSkeletonBoneIndex(skeleton, attachment.boneName, boneIndex)) {
            ++report.unbound;
            continue;
        }

        const ObjectWorldFrame skeletonWorld = worldFrameOf(attachment.skeleton);
        ObjectWorldFrame boneWorld{};
        if (!TryResolveSkeletonBoneWorldFrame(skeleton, boneIndex, skeletonWorld, boneWorld)) {
            ++report.unbound;
            continue;
        }
        const ObjectWorldFrame attachmentWorld =
            ComposeBoneAttachmentWorldFrame(boneWorld, attachment.localPosition, attachment.localRotation,
                                               attachment.localScale);

        TransformComponent& local = entityManager.GetComponent<TransformComponent>(entity);
        const Entity parent = entityManager.HasComponent<HierarchyComponent>(entity)
                                     ? entityManager.GetComponent<HierarchyComponent>(entity).parent
                                     : kInvalidEntity;
        // A top-level attachment ignores its parent's transform - its local values ARE its world
        // values - so it is written in world units deliberately, exactly as propagation reads them.
        const bool composesFromParent =
            parent != kInvalidEntity && !local.topLevel &&
            entityManager.HasComponent<TransformComponent>(parent);
        Math::Vector3 localPosition = attachmentWorld.position;
        Math::Quaternion localRotation = attachmentWorld.rotation;
        Math::Vector3 localScale = attachmentWorld.scale;
        if (composesFromParent &&
            !TryMakeBoneAttachmentLocalTransform(attachmentWorld, worldFrameOf(parent), localPosition,
                                                    localRotation, localScale)) {
            ++report.unbound;
            continue;
        }
        Math::Quaternion normalized{};
        if (!Math::TryNormalize(localRotation, normalized)) {
            ++report.unbound;
            continue;
        }

        local.localPosition = localPosition;
        local.localRotation = normalized;
        // The stored angles described the rotation the object had before this write. Refreshing them
        // the way the animation write path does keeps an Inspector in Euler mode showing the pose the
        // bone is in instead of snapping the attachment back on the next edit.
        Math::Vector3 euler{};
        if (Math::TryToEulerOrdered(normalized, local.eulerOrder, euler)) {
            local.localEulerRadians = euler;
        }
        local.localScale = localScale;
        if (entityManager.HasComponent<WorldTransformComponent>(entity)) {
            entityManager.GetComponent<WorldTransformComponent>(entity).dirty = true;
        }
        attachment.resolvedBoneIndex = boneIndex;
        attachment.bound = true;
        ++report.bound;
    }
    return report;
}

} // namespace UVE::Scene
