// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/scene/bone_attachment_pass_uve.h"

#include <algorithm>
#include <cstddef>
#include <optional>
#include <utility>
#include <vector>

#include "uve/component/hierarchy_component_uve.h"
#include "uve/component/process_component_uve.h"
#include "uve/component/transform_component_uve.h"
#include "uve/component/world_transform_component_uve.h"
#include "uve/entity/i_entity_manager_uve.h"
#include "uve/math/quaternion_uve.h"
#include "uve/objects/3d/bone_attachment_3d_uve.h"
#include "uve/objects/3d/skeleton_3d_uve.h"
#include "uve/scene/i_scene_graph_uve.h"

namespace UVE::Scene {

namespace {

/// The process mode the scene graph resolved for `entity` on its last update, or the hierarchy
/// default for an entity it has not seen yet. Asking the graph rather than the entity's own
/// ProcessComponentUVE is what lets an entity without one inherit its ancestors' answer.
[[nodiscard]] TickModeUVE ResolvedTickModeUVE(const ISceneGraphUVE& sceneGraph, const EntityUVE entity) {
    const std::optional<ResolvedObjectModesUVE> modes = sceneGraph.TryGetResolvedObjectModesUVE(entity);
    return modes.has_value() ? modes->process : TickModeUVE::Running;
}

/// How many parents a chain may have before it is treated as malformed. The walks below are bounded
/// by this rather than trusting the hierarchy to be acyclic: a corrupted parent link must terminate
/// the pass, not hang the frame.
inline constexpr int kMaximumHierarchyDepthUVE = 1024;

} // namespace

BoneAttachmentPassReportUVE SyncBoneAttachment3DObjectsUVE(IEntityManagerUVE& entityManager,
                                                           const ISceneGraphUVE& sceneGraph) {
    BoneAttachmentPassReportUVE report{};

    // Collected and ordered rather than iterated in place: two attachments can hang off one object,
    // and an author orders them the same way they order every other scene system - physicsPriority -
    // instead of archetype storage order deciding it. Ties keep that storage order (stable_sort).
    std::vector<std::pair<std::int32_t, EntityUVE>> ordered;
    entityManager.ForEachUVE<BoneAttachment3DComponentUVE>(
        [&ordered, &sceneGraph, &entityManager](const EntityUVE entity, BoneAttachment3DComponentUVE&) {
            if (!IsTickingUVE(ResolvedTickModeUVE(sceneGraph, entity), /*simulationPaused=*/false)) {
                return;
            }
            const std::int32_t priority =
                entityManager.HasComponentUVE<ProcessComponentUVE>(entity)
                    ? entityManager.GetComponentUVE<ProcessComponentUVE>(entity).physicsPriority
                    : 0;
            ordered.emplace_back(priority, entity);
        });
    if (ordered.empty()) {
        return report;
    }
    std::stable_sort(ordered.begin(), ordered.end(),
                     [](const auto& left, const auto& right) { return left.first < right.first; });

    // The world frame of an entity as the scene graph will compose it: every local transform up the
    // chain, outermost first, with `topLevel` cutting the chain and a parent carrying no transform of
    // its own cutting it too - the two rules SceneGraphUVE::UpdateUVE() composes by. Read from the
    // LOCAL components rather than the cached world transform because this pass is meant to run
    // before propagation: a parent that moved this frame has not published a world transform yet,
    // and the attachment has to land on the bone this frame, not next.
    const auto worldFrameOf = [&entityManager](EntityUVE entity) {
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
    };
    const auto depthOf = [&entityManager](EntityUVE entity) {
        int depth = 0;
        for (; depth < kMaximumHierarchyDepthUVE && entity != kInvalidEntityUVE && entityManager.IsAliveUVE(entity);
             ++depth) {
            if (!entityManager.HasComponentUVE<HierarchyComponentUVE>(entity)) {
                break;
            }
            entity = entityManager.GetComponentUVE<HierarchyComponentUVE>(entity).parent;
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
        BoneAttachment3DComponentUVE& attachment =
            entityManager.GetComponentUVE<BoneAttachment3DComponentUVE>(entity);
        ++report.considered;
        // Last frame's answer does not carry over: an attachment is on its bone this pass or it is
        // not, and `bound`/`resolvedBoneIndex` exist so an author can see which one it is.
        attachment.resolvedBoneIndex = kInvalidSkeletonBoneIndexUVE;
        attachment.bound = false;
        if (!IsBoneAttachment3DObjectComponentResolvableUVE(attachment) ||
            !entityManager.IsAliveUVE(attachment.skeleton) ||
            !entityManager.HasComponentUVE<Skeleton3DComponentUVE>(attachment.skeleton) ||
            !entityManager.HasComponentUVE<TransformComponentUVE>(entity)) {
            ++report.unbound;
            continue;
        }
        const Skeleton3DComponentUVE& skeleton =
            entityManager.GetComponentUVE<Skeleton3DComponentUVE>(attachment.skeleton);
        // A skeleton that is switched off is not posing anything, so nothing rides it.
        if (!skeleton.enabled) {
            ++report.unbound;
            continue;
        }
        // Index wins over name when it names a real bone - that is the pair's documented rule, and it
        // is what makes a renamed bone in a re-export observably lose a name-based attachment instead
        // of silently binding to whichever bone took the name.
        std::uint32_t boneIndex = kInvalidSkeletonBoneIndexUVE;
        if (attachment.boneIndex != kInvalidSkeletonBoneIndexUVE && attachment.boneIndex < skeleton.bones.size()) {
            boneIndex = attachment.boneIndex;
        } else if (!TryFindSkeletonBoneIndexUVE(skeleton, attachment.boneName, boneIndex)) {
            ++report.unbound;
            continue;
        }

        const ObjectWorldFrameUVE skeletonWorld = worldFrameOf(attachment.skeleton);
        ObjectWorldFrameUVE boneWorld{};
        if (!TryResolveSkeletonBoneWorldFrameUVE(skeleton, boneIndex, skeletonWorld, boneWorld)) {
            ++report.unbound;
            continue;
        }
        const ObjectWorldFrameUVE attachmentWorld =
            ComposeBoneAttachmentWorldFrameUVE(boneWorld, attachment.localPosition, attachment.localRotation,
                                               attachment.localScale);

        TransformComponentUVE& local = entityManager.GetComponentUVE<TransformComponentUVE>(entity);
        const EntityUVE parent = entityManager.HasComponentUVE<HierarchyComponentUVE>(entity)
                                     ? entityManager.GetComponentUVE<HierarchyComponentUVE>(entity).parent
                                     : kInvalidEntityUVE;
        // A top-level attachment ignores its parent's transform - its local values ARE its world
        // values - so it is written in world units deliberately, exactly as propagation reads them.
        const bool composesFromParent =
            parent != kInvalidEntityUVE && !local.topLevel &&
            entityManager.HasComponentUVE<TransformComponentUVE>(parent);
        Math::Vector3UVE localPosition = attachmentWorld.position;
        Math::QuaternionUVE localRotation = attachmentWorld.rotation;
        Math::Vector3UVE localScale = attachmentWorld.scale;
        if (composesFromParent &&
            !TryMakeBoneAttachmentLocalTransformUVE(attachmentWorld, worldFrameOf(parent), localPosition,
                                                    localRotation, localScale)) {
            ++report.unbound;
            continue;
        }
        Math::QuaternionUVE normalized{};
        if (!Math::TryNormalizeUVE(localRotation, normalized)) {
            ++report.unbound;
            continue;
        }

        local.localPosition = localPosition;
        local.localRotation = normalized;
        // The stored angles described the rotation the object had before this write. Refreshing them
        // the way the animation write path does keeps an Inspector in Euler mode showing the pose the
        // bone is in instead of snapping the attachment back on the next edit.
        Math::Vector3UVE euler{};
        if (Math::TryToEulerOrderedUVE(normalized, local.eulerOrder, euler)) {
            local.localEulerRadians = euler;
        }
        local.localScale = localScale;
        if (entityManager.HasComponentUVE<WorldTransformComponentUVE>(entity)) {
            entityManager.GetComponentUVE<WorldTransformComponentUVE>(entity).dirty = true;
        }
        attachment.resolvedBoneIndex = boneIndex;
        attachment.bound = true;
        ++report.bound;
    }
    return report;
}

} // namespace UVE::Scene
