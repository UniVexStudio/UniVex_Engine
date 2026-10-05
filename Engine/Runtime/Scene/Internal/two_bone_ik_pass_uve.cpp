// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/scene/two_bone_ik_pass_uve.h"

#include <algorithm>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "uve/component/bone_modifier_component_uve.h"
#include "uve/component/hierarchy_component_uve.h"
#include "uve/component/process_component_uve.h"
#include "uve/component/transform_component_uve.h"
#include "uve/entity/i_entity_manager_uve.h"
#include "uve/objects/3d/skeleton_3d_uve.h"
#include "uve/objects/3d/two_bone_ik_3d_uve.h"
#include "uve/scene/i_scene_graph_uve.h"
#include "uve/scene/scene_world_frame_uve.h"

namespace UVE::Scene {

namespace {

/// The process mode the scene graph resolved for `entity` on its last update, or the hierarchy
/// default for an entity it has not seen yet. Asking the graph rather than the entity's own
/// ProcessComponentUVE is what lets an entity without one inherit its ancestors' answer.
[[nodiscard]] TickModeUVE ResolvedTickModeUVE(const ISceneGraphUVE& sceneGraph, const EntityUVE entity) {
    const std::optional<ResolvedObjectModesUVE> modes = sceneGraph.TryGetResolvedObjectModesUVE(entity);
    return modes.has_value() ? modes->process : TickModeUVE::Running;
}

/// The bone a reference resolved to, or the sentinel. Index first - it is the reference that cannot
/// go stale - then the exact name, which is the one an author can read in a DCC tool.
[[nodiscard]] std::uint32_t ResolveBoneUVE(const Skeleton3DComponentUVE& skeleton, const std::uint32_t index,
                                           const std::string& name) noexcept {
    if (index < skeleton.bones.size()) {
        return index;
    }
    std::uint32_t found = kInvalidSkeletonBoneIndexUVE;
    return TryFindSkeletonBoneIndexUVE(skeleton, name, found) ? found : kInvalidSkeletonBoneIndexUVE;
}

/// Whether `bone` is the child of `parent` in the skeleton's own hierarchy: a two-bone chain is three
/// successive bones, and a set of three that is not successive would make the analytic solve describe
/// a limb the rig does not have.
[[nodiscard]] bool IsChildOfUVE(const Skeleton3DComponentUVE& skeleton, const std::uint32_t bone,
                                const std::uint32_t parent) noexcept {
    return bone < skeleton.bones.size() && skeleton.bones[bone].parentIndex >= 0 &&
           static_cast<std::uint32_t>(skeleton.bones[bone].parentIndex) == parent;
}

/// A point authored in the skeleton's own space, placed in the world the skeleton lives in: scaled,
/// then rotated, then moved - the order every TRS compose in this engine uses.
[[nodiscard]] Math::Vector3UVE SkeletonSpaceToWorldUVE(const Math::Vector3UVE& point,
                                                       const ObjectWorldFrameUVE& skeletonFrame) noexcept {
    const Math::Vector3UVE scaled{point.x * skeletonFrame.scale.x, point.y * skeletonFrame.scale.y,
                                  point.z * skeletonFrame.scale.z};
    return skeletonFrame.position + Math::RotateVectorUVE(skeletonFrame.rotation, scaled);
}

} // namespace

TwoBoneIKPassReportUVE SyncTwoBoneIK3DObjectsUVE(IEntityManagerUVE& entityManager,
                                                 const ISceneGraphUVE& sceneGraph,
                                                 const std::span<const EntityUVE> posedSkeletons) {
    TwoBoneIKPassReportUVE report{};
    if (posedSkeletons.empty()) {
        return report;
    }

    // Collected and ordered rather than iterated in place: two chains can share a skeleton, and when
    // they do the second one has to solve against the pose the first one left, so the author's own
    // ordering - physicsPriority - decides which, not archetype storage order. Ties keep that order.
    std::vector<std::pair<std::int32_t, EntityUVE>> ordered;
    entityManager.ForEachUVE<TwoBoneIK3DComponentUVE>(
        [&ordered, &sceneGraph, &entityManager](const EntityUVE entity, TwoBoneIK3DComponentUVE&) {
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

    for (const auto& [priority, entity] : ordered) {
        static_cast<void>(priority);
        ++report.considered;
        TwoBoneIK3DComponentUVE& chain = entityManager.GetComponentUVE<TwoBoneIK3DComponentUVE>(entity);
        // Every attempt starts from "nothing was solved": the state a refused chain leaves behind is
        // the same it has when it was never solvable, so nothing downstream can read a stale answer.
        chain.resolvedRootBoneIndex = kInvalidSkeletonBoneIndexUVE;
        chain.resolvedMiddleBoneIndex = kInvalidSkeletonBoneIndexUVE;
        chain.resolvedEndBoneIndex = kInvalidSkeletonBoneIndexUVE;
        chain.solved = false;
        chain.reached = false;
        chain.endToTargetDistanceMetres = 0.0F;

        const bool active = !entityManager.HasComponentUVE<BoneModifierComponentUVE>(entity) ||
                            entityManager.GetComponentUVE<BoneModifierComponentUVE>(entity).active;
        const float influence =
            entityManager.HasComponentUVE<BoneModifierComponentUVE>(entity)
                ? entityManager.GetComponentUVE<BoneModifierComponentUVE>(entity).influence
                : 1.0F;
        if (!active || !(influence > 0.0F) || !IsTwoBoneIK3DObjectComponentResolvableUVE(chain)) {
            ++report.refused;
            continue;
        }
        const EntityUVE skeletonEntity = chain.skeleton;
        if (!entityManager.IsAliveUVE(skeletonEntity) ||
            !entityManager.HasComponentUVE<Skeleton3DComponentUVE>(skeletonEntity)) {
            ++report.refused;
            continue;
        }
        // The pose this chain would blend over is the one the drivers wrote; a skeleton no driver
        // wrote this pass carries a pose that already has its modifiers in it.
        if (std::find(posedSkeletons.begin(), posedSkeletons.end(), skeletonEntity) == posedSkeletons.end()) {
            ++report.refused;
            continue;
        }
        Skeleton3DComponentUVE& skeleton = entityManager.GetComponentUVE<Skeleton3DComponentUVE>(skeletonEntity);

        const std::uint32_t rootBone = ResolveBoneUVE(skeleton, chain.rootBoneIndex, chain.rootBoneName);
        const std::uint32_t middleBone = ResolveBoneUVE(skeleton, chain.middleBoneIndex, chain.middleBoneName);
        const std::uint32_t endBone = ResolveBoneUVE(skeleton, chain.endBoneIndex, chain.endBoneName);
        if (rootBone == kInvalidSkeletonBoneIndexUVE || middleBone == kInvalidSkeletonBoneIndexUVE ||
            endBone == kInvalidSkeletonBoneIndexUVE || !IsChildOfUVE(skeleton, middleBone, rootBone) ||
            !IsChildOfUVE(skeleton, endBone, middleBone)) {
            ++report.refused;
            continue;
        }

        const ObjectWorldFrameUVE skeletonFrame = ComposeSceneObjectWorldFrameUVE(entityManager, skeletonEntity);
        ObjectWorldFrameUVE rootFrame{};
        ObjectWorldFrameUVE middleFrame{};
        ObjectWorldFrameUVE endFrame{};
        if (!TryResolveSkeletonBoneWorldFrameUVE(skeleton, rootBone, skeletonFrame, rootFrame) ||
            !TryResolveSkeletonBoneWorldFrameUVE(skeleton, middleBone, skeletonFrame, middleFrame) ||
            !TryResolveSkeletonBoneWorldFrameUVE(skeleton, endBone, skeletonFrame, endFrame)) {
            ++report.refused;
            continue;
        }
        // The root's local rotation is written against its parent BONE's world rotation; a root bone
        // with no parent bone answers to the skeleton itself.
        Math::QuaternionUVE parentRotation = skeletonFrame.rotation;
        if (skeleton.bones[rootBone].parentIndex >= 0) {
            ObjectWorldFrameUVE parentFrame{};
            const auto parentBone = static_cast<std::uint32_t>(skeleton.bones[rootBone].parentIndex);
            if (!TryResolveSkeletonBoneWorldFrameUVE(skeleton, parentBone, skeletonFrame, parentFrame)) {
                ++report.refused;
                continue;
            }
            parentRotation = parentFrame.rotation;
        }

        // The target: a live object's own world position, else a point in the skeleton's space. A
        // reference that names something which is gone refuses rather than falling back - the author
        // pointed at an object, and silently solving toward another place would hide that it went.
        Math::Vector3UVE target{};
        if (chain.target != kInvalidEntityUVE) {
            if (!entityManager.IsAliveUVE(chain.target) ||
                !entityManager.HasComponentUVE<TransformComponentUVE>(chain.target)) {
                ++report.refused;
                continue;
            }
            target = ComposeSceneObjectWorldFrameUVE(entityManager, chain.target).position;
        } else {
            target = SkeletonSpaceToWorldUVE(chain.targetPosition, skeletonFrame);
        }

        // The pole: a live object's world position seen from the chain's root, else the authored
        // direction turned into the world. Zero means neither was given, and the solver reads that as
        // "keep the plane the pose is already in".
        Math::Vector3UVE pole{};
        if (chain.poleTarget != kInvalidEntityUVE) {
            if (!entityManager.IsAliveUVE(chain.poleTarget) ||
                !entityManager.HasComponentUVE<TransformComponentUVE>(chain.poleTarget)) {
                ++report.refused;
                continue;
            }
            pole = ComposeSceneObjectWorldFrameUVE(entityManager, chain.poleTarget).position - rootFrame.position;
        } else {
            pole = Math::RotateVectorUVE(skeletonFrame.rotation, chain.poleDirection);
        }

        const TwoBoneIKChainUVE joints{rootFrame.position, middleFrame.position, endFrame.position, parentRotation,
                                       rootFrame.rotation, middleFrame.rotation};
        const std::optional<TwoBoneIKSolutionUVE> solution = SolveTwoBoneIKUVE(joints, target, pole);
        if (!solution.has_value()) {
            ++report.refused;
            continue;
        }

        // Materialized whole before one bone is touched: the pose entries this chain is not writing
        // are the animation's, and a pose vector missing them would drop every track but this limb.
        std::vector<SkeletonBonePoseUVE> pose = GetSkeletonCurrentPoseUVE(skeleton);
        Math::QuaternionUVE rootRotation{};
        Math::QuaternionUVE middleRotation{};
        if (!TryBlendTwoBoneIKRotationUVE(pose[rootBone].rotation, solution->rootLocalRotation, influence,
                                          rootRotation) ||
            !TryBlendTwoBoneIKRotationUVE(pose[middleBone].rotation, solution->middleLocalRotation, influence,
                                          middleRotation)) {
            ++report.refused;
            continue;
        }
        pose[rootBone].rotation = rootRotation;
        pose[middleBone].rotation = middleRotation;
        skeleton.pose = std::move(pose);

        chain.resolvedRootBoneIndex = rootBone;
        chain.resolvedMiddleBoneIndex = middleBone;
        chain.resolvedEndBoneIndex = endBone;
        chain.solved = true;
        chain.reached = solution->reached;
        chain.endToTargetDistanceMetres = solution->endToTargetDistanceMetres;
        ++report.solved;
    }
    return report;
}

} // namespace UVE::Scene
