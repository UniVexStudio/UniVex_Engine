// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "two_bone_ik_pass.h"

#include <algorithm>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "bone_modifier_component.h"
#include "hierarchy_component.h"
#include "process_component.h"
#include "transform_component.h"
#include "i_entity_manager.h"
#include "abstract_animation_objects_3d.h"
#include "skeleton_3d.h"
#include "two_bone_ik_3d.h"
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

/// The bone a reference resolved to, or the sentinel. Index first - it is the reference that cannot
/// go stale - then the exact name, which is the one an author can read in a DCC tool.
[[nodiscard]] std::uint32_t ResolveBone(const Skeleton3DComponent& skeleton, const std::uint32_t index,
                                           const std::string& name) noexcept {
    if (index < skeleton.bones.size()) {
        return index;
    }
    std::uint32_t found = kInvalidSkeletonBoneIndex;
    return TryFindSkeletonBoneIndex(skeleton, name, found) ? found : kInvalidSkeletonBoneIndex;
}

/// Whether `bone` is the child of `parent` in the skeleton's own hierarchy: a two-bone chain is three
/// successive bones, and a set of three that is not successive would make the analytic solve describe
/// a limb the rig does not have.
[[nodiscard]] bool IsChildOf(const Skeleton3DComponent& skeleton, const std::uint32_t bone,
                                const std::uint32_t parent) noexcept {
    return bone < skeleton.bones.size() && skeleton.bones[bone].parentIndex >= 0 &&
           static_cast<std::uint32_t>(skeleton.bones[bone].parentIndex) == parent;
}

/// A point authored in the skeleton's own space, placed in the world the skeleton lives in: scaled,
/// then rotated, then moved - the order every TRS compose in this engine uses.
[[nodiscard]] Math::Vector3 SkeletonSpaceToWorld(const Math::Vector3& point,
                                                       const ObjectWorldFrame& skeletonFrame) noexcept {
    const Math::Vector3 scaled{point.x * skeletonFrame.scale.x, point.y * skeletonFrame.scale.y,
                                  point.z * skeletonFrame.scale.z};
    return skeletonFrame.position + Math::RotateVector(skeletonFrame.rotation, scaled);
}

} // namespace

TwoBoneIKPassReport SyncTwoBoneIK3DObjects(IEntityManager& entityManager,
                                                 const ISceneGraph& sceneGraph,
                                                 const std::span<const Entity> posedSkeletons) {
    TwoBoneIKPassReport report{};
    if (posedSkeletons.empty()) {
        return report;
    }

    // Collected and ordered rather than iterated in place: two chains can share a skeleton, and when
    // they do the second one has to solve against the pose the first one left, so the author's own
    // ordering - physicsPriority - decides which, not archetype storage order. Ties keep that order.
    std::vector<std::pair<std::int32_t, Entity>> ordered;
    entityManager.ForEach<TwoBoneIK3DComponent>(
        [&ordered, &sceneGraph, &entityManager](const Entity entity, TwoBoneIK3DComponent&) {
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

    for (const auto& [priority, entity] : ordered) {
        static_cast<void>(priority);
        ++report.considered;
        TwoBoneIK3DComponent& chain = entityManager.GetComponent<TwoBoneIK3DComponent>(entity);
        // Every attempt starts from "nothing was solved": the state a refused chain leaves behind is
        // the same it has when it was never solvable, so nothing downstream can read a stale answer.
        chain.resolvedRootBoneIndex = kInvalidSkeletonBoneIndex;
        chain.resolvedMiddleBoneIndex = kInvalidSkeletonBoneIndex;
        chain.resolvedEndBoneIndex = kInvalidSkeletonBoneIndex;
        chain.solved = false;
        chain.reached = false;
        chain.endToTargetDistanceMetres = 0.0F;

        const float influence = ResolveBoneModifierInfluence(entityManager, entity);
        if (!IsBoneModifierActive(entityManager, entity) ||
            !IsTwoBoneIK3DObjectComponentResolvable(chain)) {
            ++report.refused;
            continue;
        }
        const Entity skeletonEntity = chain.skeleton;
        if (!entityManager.IsAlive(skeletonEntity) ||
            !entityManager.HasComponent<Skeleton3DComponent>(skeletonEntity)) {
            ++report.refused;
            continue;
        }
        // The pose this chain would blend over is the one the drivers wrote; a skeleton no driver
        // wrote this pass carries a pose that already has its modifiers in it.
        if (std::find(posedSkeletons.begin(), posedSkeletons.end(), skeletonEntity) == posedSkeletons.end()) {
            ++report.refused;
            continue;
        }
        Skeleton3DComponent& skeleton = entityManager.GetComponent<Skeleton3DComponent>(skeletonEntity);

        const std::uint32_t rootBone = ResolveBone(skeleton, chain.rootBoneIndex, chain.rootBoneName);
        const std::uint32_t middleBone = ResolveBone(skeleton, chain.middleBoneIndex, chain.middleBoneName);
        const std::uint32_t endBone = ResolveBone(skeleton, chain.endBoneIndex, chain.endBoneName);
        if (rootBone == kInvalidSkeletonBoneIndex || middleBone == kInvalidSkeletonBoneIndex ||
            endBone == kInvalidSkeletonBoneIndex || !IsChildOf(skeleton, middleBone, rootBone) ||
            !IsChildOf(skeleton, endBone, middleBone)) {
            ++report.refused;
            continue;
        }

        const ObjectWorldFrame skeletonFrame = ComposeSceneObjectWorldFrame(entityManager, skeletonEntity);
        ObjectWorldFrame rootFrame{};
        ObjectWorldFrame middleFrame{};
        ObjectWorldFrame endFrame{};
        if (!TryResolveSkeletonBoneWorldFrame(skeleton, rootBone, skeletonFrame, rootFrame) ||
            !TryResolveSkeletonBoneWorldFrame(skeleton, middleBone, skeletonFrame, middleFrame) ||
            !TryResolveSkeletonBoneWorldFrame(skeleton, endBone, skeletonFrame, endFrame)) {
            ++report.refused;
            continue;
        }
        // The root's local rotation is written against its parent BONE's world rotation; a root bone
        // with no parent bone answers to the skeleton itself.
        Math::Quaternion parentRotation = skeletonFrame.rotation;
        if (skeleton.bones[rootBone].parentIndex >= 0) {
            ObjectWorldFrame parentFrame{};
            const auto parentBone = static_cast<std::uint32_t>(skeleton.bones[rootBone].parentIndex);
            if (!TryResolveSkeletonBoneWorldFrame(skeleton, parentBone, skeletonFrame, parentFrame)) {
                ++report.refused;
                continue;
            }
            parentRotation = parentFrame.rotation;
        }

        // The target: a live object's own world position, else a point in the skeleton's space. A
        // reference that names something which is gone refuses rather than falling back - the author
        // pointed at an object, and silently solving toward another place would hide that it went.
        Math::Vector3 target{};
        if (chain.target != kInvalidEntity) {
            if (!entityManager.IsAlive(chain.target) ||
                !entityManager.HasComponent<TransformComponent>(chain.target)) {
                ++report.refused;
                continue;
            }
            target = ComposeSceneObjectWorldFrame(entityManager, chain.target).position;
        } else {
            target = SkeletonSpaceToWorld(chain.targetPosition, skeletonFrame);
        }

        // The pole: a live object's world position seen from the chain's root, else the authored
        // direction turned into the world. Zero means neither was given, and the solver reads that as
        // "keep the plane the pose is already in".
        Math::Vector3 pole{};
        if (chain.poleTarget != kInvalidEntity) {
            if (!entityManager.IsAlive(chain.poleTarget) ||
                !entityManager.HasComponent<TransformComponent>(chain.poleTarget)) {
                ++report.refused;
                continue;
            }
            pole = ComposeSceneObjectWorldFrame(entityManager, chain.poleTarget).position - rootFrame.position;
        } else {
            pole = Math::RotateVector(skeletonFrame.rotation, chain.poleDirection);
        }

        const TwoBoneIKChain joints{rootFrame.position, middleFrame.position, endFrame.position, parentRotation,
                                       rootFrame.rotation, middleFrame.rotation};
        const std::optional<TwoBoneIKSolution> solution = SolveTwoBoneIK(joints, target, pole);
        if (!solution.has_value()) {
            ++report.refused;
            continue;
        }

        // Materialized whole before one bone is touched: the pose entries this chain is not writing
        // are the animation's, and a pose vector missing them would drop every track but this limb.
        std::vector<SkeletonBonePose> pose = GetSkeletonCurrentPose(skeleton);
        Math::Quaternion rootRotation{};
        Math::Quaternion middleRotation{};
        if (!TryBlendTwoBoneIKRotation(pose[rootBone].rotation, solution->rootLocalRotation, influence,
                                          rootRotation) ||
            !TryBlendTwoBoneIKRotation(pose[middleBone].rotation, solution->middleLocalRotation, influence,
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
