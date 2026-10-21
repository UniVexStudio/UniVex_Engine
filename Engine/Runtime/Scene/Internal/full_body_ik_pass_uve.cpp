// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/scene/full_body_ik_pass_uve.h"

#include <algorithm>
#include <cmath>
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
#include "uve/objects/3d/abstract_animation_objects_3d_uve.h"
#include "uve/objects/3d/full_body_ik_3d_uve.h"
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

/// A point authored in the skeleton's own space, placed in the world the skeleton lives in: scaled,
/// then rotated, then moved - the order every TRS compose in this engine uses.
[[nodiscard]] Math::Vector3UVE SkeletonSpaceToWorldUVE(const Math::Vector3UVE& point,
                                                       const ObjectWorldFrameUVE& skeletonFrame) noexcept {
    const Math::Vector3UVE scaled{point.x * skeletonFrame.scale.x, point.y * skeletonFrame.scale.y,
                                  point.z * skeletonFrame.scale.z};
    return skeletonFrame.position + Math::RotateVectorUVE(skeletonFrame.rotation, scaled);
}

/// One bone's world frame as the pose currently puts it - the bone's own parent bone's frame
/// composed through the skeleton, which is what the solver's world rotations are expressed in.
[[nodiscard]] bool TryResolveBoneWorldFrameUVE(const Skeleton3DComponentUVE& skeleton,
                                               const std::uint32_t bone,
                                               const ObjectWorldFrameUVE& skeletonFrame,
                                               ObjectWorldFrameUVE& outFrame) noexcept {
    return TryResolveSkeletonBoneWorldFrameUVE(skeleton, bone, skeletonFrame, outFrame);
}

} // namespace

FullBodyIKPassReportUVE SyncFullBodyIK3DObjectsUVE(IEntityManagerUVE& entityManager,
                                                   const ISceneGraphUVE& sceneGraph,
                                                   const std::span<const EntityUVE> posedSkeletons) {
    FullBodyIKPassReportUVE report{};
    if (posedSkeletons.empty()) {
        return report;
    }

    // Collected and ordered rather than iterated in place: a solve can share a skeleton with a
    // two-bone chain or another solve, and when they do the second has to answer against the pose
    // the first left, so the author's own ordering - physicsPriority - decides which, not archetype
    // storage order. Ties keep that order.
    std::vector<std::pair<std::int32_t, EntityUVE>> ordered;
    entityManager.ForEachUVE<FullBodyIK3DComponentUVE>(
        [&ordered, &sceneGraph, &entityManager](const EntityUVE entity, FullBodyIK3DComponentUVE&) {
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
        FullBodyIK3DComponentUVE& solve = entityManager.GetComponentUVE<FullBodyIK3DComponentUVE>(entity);
        // Every attempt starts from "nothing was solved": the state a refused solve leaves behind is
        // the same it has when it was never solvable, so nothing downstream can read a stale answer.
        solve.solved = false;
        solve.converged = false;
        solve.iterationsUsed = 0U;
        for (FullBodyIKEffectorUVE& effector : solve.effectors) {
            effector.resolvedBoneIndex = kInvalidSkeletonBoneIndexUVE;
            effector.reached = false;
            effector.distanceMetres = 0.0F;
        }

        const float influence = ResolveBoneModifierInfluenceUVE(entityManager, entity);
        if (!IsBoneModifierActiveUVE(entityManager, entity) ||
            !IsFullBodyIK3DObjectComponentResolvableUVE(solve) ||
            !IsFullBodyIK3DObjectComponentValidUVE(solve)) {
            ++report.refused;
            continue;
        }
        const EntityUVE skeletonEntity = solve.skeleton;
        if (!entityManager.IsAliveUVE(skeletonEntity) ||
            !entityManager.HasComponentUVE<Skeleton3DComponentUVE>(skeletonEntity)) {
            ++report.refused;
            continue;
        }
        // The pose this solve would blend over is the one the drivers wrote; a skeleton no driver
        // wrote this pass carries a pose that already has its modifiers in it.
        if (std::find(posedSkeletons.begin(), posedSkeletons.end(), skeletonEntity) == posedSkeletons.end()) {
            ++report.refused;
            continue;
        }
        Skeleton3DComponentUVE& skeleton = entityManager.GetComponentUVE<Skeleton3DComponentUVE>(skeletonEntity);
        const ObjectWorldFrameUVE skeletonFrame = ComposeSceneObjectWorldFrameUVE(entityManager, skeletonEntity);

        const std::uint32_t rootBone = ResolveBoneUVE(skeleton, solve.rootBoneIndex, solve.rootBoneName);
        if (rootBone == kInvalidSkeletonBoneIndexUVE) {
            ++report.refused;
            continue;
        }

        // Effector bones resolve first and whole: one that names nothing refuses the solve rather
        // than dropping that limb, because a solve that silently lost an effector would pose the
        // body against a problem the author did not author.
        std::vector<std::uint32_t> effectorBones;
        std::vector<Math::Vector3UVE> effectorTargets;
        std::vector<float> effectorWeights;
        bool effectorsResolved = !solve.effectors.empty();
        for (const FullBodyIKEffectorUVE& effector : solve.effectors) {
            const std::uint32_t bone = ResolveBoneUVE(skeleton, effector.boneIndex, effector.boneName);
            if (bone == kInvalidSkeletonBoneIndexUVE) {
                effectorsResolved = false;
                break;
            }
            Math::Vector3UVE target{};
            if (effector.target != kInvalidEntityUVE) {
                // A reference that names something which is gone refuses rather than falling back -
                // the author pointed at an object, and silently solving toward another place would
                // hide that it went.
                if (!entityManager.IsAliveUVE(effector.target) ||
                    !entityManager.HasComponentUVE<TransformComponentUVE>(effector.target)) {
                    effectorsResolved = false;
                    break;
                }
                target = ComposeSceneObjectWorldFrameUVE(entityManager, effector.target).position;
            } else {
                target = SkeletonSpaceToWorldUVE(effector.targetPosition, skeletonFrame);
            }
            effectorBones.push_back(bone);
            effectorTargets.push_back(target);
            effectorWeights.push_back(effector.positionWeight);
        }
        if (!effectorsResolved) {
            ++report.refused;
            continue;
        }

        // The whole rig as the pose puts it, in world space: the solver drives the root bone's
        // subtree and leaves everything else - and every bone length - exactly as it found them.
        const std::size_t boneCount = skeleton.bones.size();
        std::vector<SkeletonBonePoseUVE> pose = GetSkeletonCurrentPoseUVE(skeleton);
        if (pose.size() != boneCount) {
            ++report.refused;
            continue;
        }
        FullBodyIKProblemUVE problem;
        problem.joints.reserve(boneCount);
        for (std::size_t bone = 0U; bone < boneCount; ++bone) {
            ObjectWorldFrameUVE frame{};
            if (!TryResolveBoneWorldFrameUVE(skeleton, static_cast<std::uint32_t>(bone), skeletonFrame, frame)) {
                problem.joints.clear();
                break;
            }
            problem.joints.push_back(FullBodyIKJointUVE{
                skeleton.bones[bone].parentIndex, frame.position, frame.rotation, pose[bone].position});
        }
        if (problem.joints.size() != boneCount) {
            ++report.refused;
            continue;
        }
        problem.effectorJoints.reserve(effectorBones.size());
        for (const std::uint32_t bone : effectorBones) {
            problem.effectorJoints.push_back(static_cast<std::size_t>(bone));
        }
        problem.effectorTargets = std::move(effectorTargets);
        problem.effectorWeights = std::move(effectorWeights);

        const FullBodyIKSolverSettingsUVE settings{
            static_cast<std::size_t>(rootBone), solve.maxIterations, solve.toleranceMetres,
            solve.rootTranslationWeight, solve.maxJointStepRadians};
        const std::optional<FullBodyIKSolverReportUVE> solution = SolveFullBodyIKUVE(problem, settings);
        if (!solution.has_value()) {
            ++report.refused;
            continue;
        }

        // Which joints the solve actually rotated: every strict ancestor of every effector bone from
        // the root down to its parent - the effector bones themselves are left alone, because a
        // hand's own rotation is the point the chain aims, not a joint the solver drives, exactly
        // as in the two-bone object. A bone that is one effector's tip but another chain's joint is
        // still driven: the solver rotated it for the chain that passes through.
        std::vector<bool> driven(boneCount, false);
        for (const std::uint32_t bone : effectorBones) {
            std::size_t cursor = bone;
            while (cursor != static_cast<std::size_t>(rootBone)) {
                const std::int32_t parent = skeleton.bones[cursor].parentIndex;
                if (parent < 0) {
                    break; // unreachable after a successful solve; refuse to walk off the hierarchy
                }
                cursor = static_cast<std::size_t>(parent);
                driven[cursor] = true;
            }
        }

        // The root bone's world rotation right before the solve wrote anything: its parent is
        // outside the driven subtree, so this frame does not move.
        ObjectWorldFrameUVE rootParentFrame = skeletonFrame;
        if (skeleton.bones[rootBone].parentIndex >= 0) {
            const auto parentBone = static_cast<std::uint32_t>(skeleton.bones[rootBone].parentIndex);
            if (!TryResolveBoneWorldFrameUVE(skeleton, parentBone, skeletonFrame, rootParentFrame)) {
                ++report.refused;
                continue;
            }
        }
        if (std::abs(rootParentFrame.scale.x) < 1.0e-6F || std::abs(rootParentFrame.scale.y) < 1.0e-6F ||
            std::abs(rootParentFrame.scale.z) < 1.0e-6F) {
            ++report.refused; // a collapsed axis cannot carry a translation back into local space
            continue;
        }

        // Written whole and in hierarchy order: each driven bone's solved WORLD rotation is turned
        // into a local against its solved parent's world rotation, then blended over the posed
        // local through the modifier's influence. The root's translated position makes the same
        // trip through its parent's frame - rotation, then inverse scale - to become a local
        // position once, on the bone that carries it.
        bool writeOk = true;
        for (std::size_t bone = 0U; bone < boneCount && writeOk; ++bone) {
            if (driven[bone]) {
                Math::QuaternionUVE parentWorldRotation = rootParentFrame.rotation;
                if (skeleton.bones[bone].parentIndex >= 0) {
                    const auto parentBone = static_cast<std::size_t>(skeleton.bones[bone].parentIndex);
                    if (driven[parentBone]) {
                        parentWorldRotation = solution->solvedRotations[parentBone];
                    } else {
                        ObjectWorldFrameUVE parentFrame{};
                        if (!TryResolveBoneWorldFrameUVE(skeleton, static_cast<std::uint32_t>(parentBone),
                                                         skeletonFrame, parentFrame)) {
                            writeOk = false;
                            break;
                        }
                        parentWorldRotation = parentFrame.rotation;
                    }
                } else {
                    parentWorldRotation = skeletonFrame.rotation;
                }
                Math::QuaternionUVE inverseParent{};
                if (!Math::TryInverseUVE(parentWorldRotation, inverseParent)) {
                    writeOk = false;
                    break;
                }
                const Math::QuaternionUVE solvedLocal =
                    Math::MultiplyUVE(inverseParent, solution->solvedRotations[bone]);
                Math::QuaternionUVE blended{};
                if (!TryBlendTwoBoneIKRotationUVE(pose[bone].rotation, solvedLocal, influence, blended)) {
                    writeOk = false;
                    break;
                }
                pose[bone].rotation = blended;
            }
        }
        if (writeOk) {
            Math::Vector3UVE worldDelta = solution->solvedPositions[static_cast<std::size_t>(rootBone)] -
                                          rootParentFrame.position;
            Math::QuaternionUVE inverseParent{};
            if (!Math::TryInverseUVE(rootParentFrame.rotation, inverseParent)) {
                writeOk = false;
            } else {
                const Math::Vector3UVE unrotated = Math::RotateVectorUVE(inverseParent, worldDelta);
                const Math::Vector3UVE localDelta{unrotated.x / rootParentFrame.scale.x,
                                                  unrotated.y / rootParentFrame.scale.y,
                                                  unrotated.z / rootParentFrame.scale.z};
                pose[static_cast<std::size_t>(rootBone)].position = BlendBoneModifierPositionUVE(
                    BoneModifierComponentUVE{true, influence}, pose[static_cast<std::size_t>(rootBone)].position,
                    localDelta);
            }
        }
        if (!writeOk) {
            ++report.refused;
            continue;
        }
        // Materialized whole before one bone is touched: the pose entries this solve is not writing
        // are the animation's, and a pose vector missing them would drop every track but this body.
        skeleton.pose = std::move(pose);

        for (std::size_t index = 0U; index < solve.effectors.size(); ++index) {
            FullBodyIKEffectorUVE& effector = solve.effectors[index];
            effector.resolvedBoneIndex = effectorBones[index];
            effector.reached = solution->reached[index];
            effector.distanceMetres = solution->distances[index];
        }
        solve.solved = true;
        solve.converged = solution->converged;
        solve.iterationsUsed = static_cast<std::uint32_t>(solution->iterationsUsed);
        ++report.solved;
    }
    return report;
}

} // namespace UVE::Scene
