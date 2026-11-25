// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "full_body_ik_pass.h"

#include <algorithm>
#include <cmath>
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
#include "full_body_ik_3d.h"
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

/// A point authored in the skeleton's own space, placed in the world the skeleton lives in: scaled,
/// then rotated, then moved - the order every TRS compose in this engine uses.
[[nodiscard]] Math::Vector3 SkeletonSpaceToWorld(const Math::Vector3& point,
                                                       const ObjectWorldFrame& skeletonFrame) noexcept {
    const Math::Vector3 scaled{point.x * skeletonFrame.scale.x, point.y * skeletonFrame.scale.y,
                                  point.z * skeletonFrame.scale.z};
    return skeletonFrame.position + Math::RotateVector(skeletonFrame.rotation, scaled);
}

/// One bone's world frame as the pose currently puts it - the bone's own parent bone's frame
/// composed through the skeleton, which is what the solver's world rotations are expressed in.
[[nodiscard]] bool TryResolveBoneWorldFrame(const Skeleton3DComponent& skeleton,
                                               const std::uint32_t bone,
                                               const ObjectWorldFrame& skeletonFrame,
                                               ObjectWorldFrame& outFrame) noexcept {
    return TryResolveSkeletonBoneWorldFrame(skeleton, bone, skeletonFrame, outFrame);
}

} // namespace

FullBodyIKPassReport SyncFullBodyIK3DObjects(IEntityManager& entityManager,
                                                   const ISceneGraph& sceneGraph,
                                                   const std::span<const Entity> posedSkeletons) {
    FullBodyIKPassReport report{};
    if (posedSkeletons.empty()) {
        return report;
    }

    // Collected and ordered rather than iterated in place: a solve can share a skeleton with a
    // two-bone chain or another solve, and when they do the second has to answer against the pose
    // the first left, so the author's own ordering - physicsPriority - decides which, not archetype
    // storage order. Ties keep that order.
    std::vector<std::pair<std::int32_t, Entity>> ordered;
    entityManager.ForEach<FullBodyIK3DComponent>(
        [&ordered, &sceneGraph, &entityManager](const Entity entity, FullBodyIK3DComponent&) {
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
        FullBodyIK3DComponent& solve = entityManager.GetComponent<FullBodyIK3DComponent>(entity);
        // Every attempt starts from "nothing was solved": the state a refused solve leaves behind is
        // the same it has when it was never solvable, so nothing downstream can read a stale answer.
        solve.solved = false;
        solve.converged = false;
        solve.iterationsUsed = 0U;
        for (FullBodyIKEffector& effector : solve.effectors) {
            effector.resolvedBoneIndex = kInvalidSkeletonBoneIndex;
            effector.reached = false;
            effector.distanceMetres = 0.0F;
        }

        const float influence = ResolveBoneModifierInfluence(entityManager, entity);
        if (!IsBoneModifierActive(entityManager, entity) ||
            !IsFullBodyIK3DObjectComponentResolvable(solve) ||
            !IsFullBodyIK3DObjectComponentValid(solve)) {
            ++report.refused;
            continue;
        }
        const Entity skeletonEntity = solve.skeleton;
        if (!entityManager.IsAlive(skeletonEntity) ||
            !entityManager.HasComponent<Skeleton3DComponent>(skeletonEntity)) {
            ++report.refused;
            continue;
        }
        // The pose this solve would blend over is the one the drivers wrote; a skeleton no driver
        // wrote this pass carries a pose that already has its modifiers in it.
        if (std::find(posedSkeletons.begin(), posedSkeletons.end(), skeletonEntity) == posedSkeletons.end()) {
            ++report.refused;
            continue;
        }
        Skeleton3DComponent& skeleton = entityManager.GetComponent<Skeleton3DComponent>(skeletonEntity);
        const ObjectWorldFrame skeletonFrame = ComposeSceneObjectWorldFrame(entityManager, skeletonEntity);

        const std::uint32_t rootBone = ResolveBone(skeleton, solve.rootBoneIndex, solve.rootBoneName);
        if (rootBone == kInvalidSkeletonBoneIndex) {
            ++report.refused;
            continue;
        }

        // Effector bones resolve first and whole: one that names nothing refuses the solve rather
        // than dropping that limb, because a solve that silently lost an effector would pose the
        // body against a problem the author did not author.
        std::vector<std::uint32_t> effectorBones;
        std::vector<Math::Vector3> effectorTargets;
        std::vector<float> effectorWeights;
        bool effectorsResolved = !solve.effectors.empty();
        for (const FullBodyIKEffector& effector : solve.effectors) {
            const std::uint32_t bone = ResolveBone(skeleton, effector.boneIndex, effector.boneName);
            if (bone == kInvalidSkeletonBoneIndex) {
                effectorsResolved = false;
                break;
            }
            Math::Vector3 target{};
            if (effector.target != kInvalidEntity) {
                // A reference that names something which is gone refuses rather than falling back -
                // the author pointed at an object, and silently solving toward another place would
                // hide that it went.
                if (!entityManager.IsAlive(effector.target) ||
                    !entityManager.HasComponent<TransformComponent>(effector.target)) {
                    effectorsResolved = false;
                    break;
                }
                target = ComposeSceneObjectWorldFrame(entityManager, effector.target).position;
            } else {
                target = SkeletonSpaceToWorld(effector.targetPosition, skeletonFrame);
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
        std::vector<SkeletonBonePose> pose = GetSkeletonCurrentPose(skeleton);
        if (pose.size() != boneCount) {
            ++report.refused;
            continue;
        }
        FullBodyIKProblem problem;
        problem.joints.reserve(boneCount);
        for (std::size_t bone = 0U; bone < boneCount; ++bone) {
            ObjectWorldFrame frame{};
            if (!TryResolveBoneWorldFrame(skeleton, static_cast<std::uint32_t>(bone), skeletonFrame, frame)) {
                problem.joints.clear();
                break;
            }
            problem.joints.push_back(FullBodyIKJoint{
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

        const FullBodyIKSolverSettings settings{
            static_cast<std::size_t>(rootBone), solve.maxIterations, solve.toleranceMetres,
            solve.rootTranslationWeight, solve.maxJointStepRadians};
        const std::optional<FullBodyIKSolverReport> solution = SolveFullBodyIK(problem, settings);
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
        ObjectWorldFrame rootParentFrame = skeletonFrame;
        if (skeleton.bones[rootBone].parentIndex >= 0) {
            const auto parentBone = static_cast<std::uint32_t>(skeleton.bones[rootBone].parentIndex);
            if (!TryResolveBoneWorldFrame(skeleton, parentBone, skeletonFrame, rootParentFrame)) {
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
                Math::Quaternion parentWorldRotation = rootParentFrame.rotation;
                if (skeleton.bones[bone].parentIndex >= 0) {
                    const auto parentBone = static_cast<std::size_t>(skeleton.bones[bone].parentIndex);
                    if (driven[parentBone]) {
                        parentWorldRotation = solution->solvedRotations[parentBone];
                    } else {
                        ObjectWorldFrame parentFrame{};
                        if (!TryResolveBoneWorldFrame(skeleton, static_cast<std::uint32_t>(parentBone),
                                                         skeletonFrame, parentFrame)) {
                            writeOk = false;
                            break;
                        }
                        parentWorldRotation = parentFrame.rotation;
                    }
                } else {
                    parentWorldRotation = skeletonFrame.rotation;
                }
                Math::Quaternion inverseParent{};
                if (!Math::TryInverse(parentWorldRotation, inverseParent)) {
                    writeOk = false;
                    break;
                }
                const Math::Quaternion solvedLocal =
                    Math::Multiply(inverseParent, solution->solvedRotations[bone]);
                Math::Quaternion blended{};
                if (!TryBlendTwoBoneIKRotation(pose[bone].rotation, solvedLocal, influence, blended)) {
                    writeOk = false;
                    break;
                }
                pose[bone].rotation = blended;
            }
        }
        if (writeOk) {
            Math::Vector3 worldDelta = solution->solvedPositions[static_cast<std::size_t>(rootBone)] -
                                          rootParentFrame.position;
            Math::Quaternion inverseParent{};
            if (!Math::TryInverse(rootParentFrame.rotation, inverseParent)) {
                writeOk = false;
            } else {
                const Math::Vector3 unrotated = Math::RotateVector(inverseParent, worldDelta);
                const Math::Vector3 localDelta{unrotated.x / rootParentFrame.scale.x,
                                                  unrotated.y / rootParentFrame.scale.y,
                                                  unrotated.z / rootParentFrame.scale.z};
                pose[static_cast<std::size_t>(rootBone)].position = BlendBoneModifierPosition(
                    BoneModifierComponent{true, influence}, pose[static_cast<std::size_t>(rootBone)].position,
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
            FullBodyIKEffector& effector = solve.effectors[index];
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
