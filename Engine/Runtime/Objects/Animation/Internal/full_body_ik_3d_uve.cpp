// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/objects/3d/full_body_ik_3d_uve.h"

#include <algorithm>
#include <cmath>
#include <optional>
#include <vector>

#include "uve/entity/i_entity_manager_uve.h"
#include "uve/math/scalar_uve.h"
#include "uve/objects/3d/abstract_animation_objects_3d_uve.h"
#include "uve/objects/3d/two_bone_ik_3d_uve.h"

namespace UVE::Scene {

namespace {

/// Lengths closer than this are treated as absent. A joint sitting on its own child and a target on
/// top of the joint it aims from are both degenerate pulls: the directions the solve rotates
/// between stop being directions, and the branch that divides by them would hand the pose an
/// infinity.
inline constexpr float kDegenerateLengthUVE = 1.0e-6F;

/// One effector's sweep may repeat inside an iteration until its own pull error stops improving.
/// Without this, a single pass per effector cannot land even its own chain - and two effectors
/// sharing a spine then trade places in an exact two-cycle, each undoing the other's half-done
/// sweep forever. The cap and the improvement floor keep the inner loop bounded and deterministic.
inline constexpr std::uint32_t kMaximumFullBodyIKInnerSweepsUVE = 16U;
inline constexpr float kMinimumFullBodyIKImprovementUVE = 1.0e-7F;

/// How much of an exact two-bone finish is applied per step when another effector's chain claims
/// the same joints. Two effectors answering on one spine turn alternating exact solves into a
/// two-cycle - each finish lands its own limb by over-rotating the joints the other one needs - and
/// a half-strength update is the averaged projection that makes the alternation converge on the
/// pose both of them can have. A chain nothing else shares takes the finish in FULL strength: the
/// damping exists only for the shared case, and a damped solo finish would let the sweep phase's
/// own plateau pull against the exact answer every iteration.
inline constexpr float kSharedJointRelaxationUVE = 0.5F;

/// The rotation that turns `from` into `to` along the shortest arc, leaving the twist about that arc
/// alone. Built from the half-way quaternion rather than from an axis and an angle, because
/// acos() loses most of its precision exactly where a limb usually is (a nearly straight one).
/// The inputs need only be non-degenerate directions - they are normalized here first, since the
/// half-way formula is only valid for unit vectors and every caller naturally has raw offsets.
/// Identical parallel directions are the identity rotation; antiparallel ones pick their half-turn
/// axis deterministically (the smallest component of `from` crossed with that world axis), because
/// an arbitrary choice there would make a straightened limb flip between frames.
[[nodiscard]] bool TryFromToRotationUVE(const Math::Vector3UVE& from, const Math::Vector3UVE& to,
                                        Math::QuaternionUVE& out) noexcept {
    const float fromLength = Math::LengthUVE(from);
    const float toLength = Math::LengthUVE(to);
    if (!(fromLength > kDegenerateLengthUVE) || !(toLength > kDegenerateLengthUVE)) {
        return false;
    }
    const Math::Vector3UVE unitFrom = from * (1.0F / fromLength);
    const Math::Vector3UVE unitTo = to * (1.0F / toLength);
    const float dot = std::clamp(Math::DotUVE(unitFrom, unitTo), -1.0F, 1.0F);
    // Parallel means PARALLEL - the early-out exists only to skip the half-way formula when the
    // cross product is numerically empty, and 1 - 1e-7 is where that starts, not 1 - 1e-6: a dot
    // floor of 1e-6 swallows every correction under 0.08 degrees, which is exactly the size of the
    // residual an iterative solve is still trying to remove. The formula below is correct for every
    // non-antiparallel input, including the tiny ones.
    if (dot >= 1.0F - 1.0e-7F) {
        out = Math::QuaternionUVE{};
        return true;
    }
    if (dot <= -1.0F + kDegenerateLengthUVE) {
        const Math::Vector3UVE axis =
            std::abs(unitFrom.x) <= std::abs(unitFrom.y) && std::abs(unitFrom.x) <= std::abs(unitFrom.z)
                ? Math::CrossUVE(unitFrom, Math::Vector3UVE{1.0F, 0.0F, 0.0F})
            : std::abs(unitFrom.y) <= std::abs(unitFrom.z)
                ? Math::CrossUVE(unitFrom, Math::Vector3UVE{0.0F, 1.0F, 0.0F})
                : Math::CrossUVE(unitFrom, Math::Vector3UVE{0.0F, 0.0F, 1.0F});
        const float length = Math::LengthUVE(axis);
        if (!(length > kDegenerateLengthUVE)) {
            return false;
        }
        return Math::TryMakeAxisAngleUVE(axis * (1.0F / length), Math::kPiUVE, out);
    }
    const Math::Vector3UVE cross = Math::CrossUVE(unitFrom, unitTo);
    return Math::TryNormalizeUVE(Math::QuaternionUVE{cross.x, cross.y, cross.z, 1.0F + dot}, out);
}

/// Caps a rotation to `maxStepRadians` about its own axis. One sweep may only move a joint this
/// far, which is what turns a violent pull into a pose that changes at a rate an animator would
/// key instead of snapping through a fold in a single frame.
[[nodiscard]] Math::QuaternionUVE ClampStepUVE(const Math::QuaternionUVE& rotation,
                                               const float maxStepRadians) noexcept {
    const float angle = 2.0F * std::acos(std::clamp(std::abs(rotation.w), 0.0F, 1.0F));
    if (angle <= maxStepRadians || !(angle > 0.0F)) {
        return rotation;
    }
    const Math::Vector3UVE vector{rotation.x, rotation.y, rotation.z};
    const float length = Math::LengthUVE(vector);
    if (!(length > kDegenerateLengthUVE)) {
        return rotation;
    }
    const Math::Vector3UVE axis = vector * (1.0F / length);
    Math::QuaternionUVE clamped{};
    return Math::TryMakeAxisAngleUVE(axis, maxStepRadians, clamped) ? clamped : rotation;
}

[[nodiscard]] bool IsFiniteJointUVE(const FullBodyIKJointUVE& joint) noexcept {
    return Math::IsFiniteUVE(joint.worldPosition) && Math::IsFiniteUVE(joint.worldRotation) &&
           Math::IsFiniteUVE(joint.localOffset);
}

} // namespace

bool IsFullBodyIK3DObjectComponentValidUVE(const FullBodyIK3DComponentUVE& value) noexcept {
    if (!IsBounded3DObjectStringUVE(value.rootBoneName) ||
        value.effectors.size() > kMaximumFullBodyIKEffectorsUVE ||
        value.maxIterations < kMinimumFullBodyIKIterationsUVE ||
        value.maxIterations > kMaximumFullBodyIKIterationsUVE ||
        !(value.toleranceMetres >= kMinimumFullBodyIKToleranceMetresUVE) ||
        !(value.toleranceMetres <= kMaximumFullBodyIKToleranceMetresUVE) ||
        !std::isfinite(value.rootTranslationWeight) || value.rootTranslationWeight < 0.0F ||
        value.rootTranslationWeight > 1.0F ||
        !(value.maxJointStepRadians >= kMinimumFullBodyIKStepRadiansUVE) ||
        !(value.maxJointStepRadians <= kMaximumFullBodyIKStepRadiansUVE)) {
        return false;
    }
    for (const FullBodyIKEffectorUVE& effector : value.effectors) {
        if (!IsBounded3DObjectStringUVE(effector.boneName) ||
            !IsFinite3DObjectVectorUVE(effector.targetPosition) ||
            !std::isfinite(effector.positionWeight) || effector.positionWeight < 0.0F ||
            effector.positionWeight > 1.0F) {
            return false;
        }
    }
    return true;
}

std::optional<FullBodyIKSolverReportUVE> SolveFullBodyIKUVE(
    const FullBodyIKProblemUVE& problem, const FullBodyIKSolverSettingsUVE& settings) noexcept {
    // ---- Fail-closed validation ---------------------------------------------------------------
    const std::size_t jointCount = problem.joints.size();
    const std::size_t effectorCount = problem.effectorJoints.size();
    if (jointCount == 0U || jointCount > kMaximumFullBodyIKJointsUVE ||
        effectorCount == 0U || effectorCount > kMaximumFullBodyIKEffectorsUVE ||
        problem.effectorTargets.size() != effectorCount ||
        problem.effectorWeights.size() != effectorCount ||
        settings.rootJoint >= jointCount ||
        settings.maxIterations < kMinimumFullBodyIKIterationsUVE ||
        settings.maxIterations > kMaximumFullBodyIKIterationsUVE ||
        !(settings.toleranceMetres >= kMinimumFullBodyIKToleranceMetresUVE) ||
        !(settings.toleranceMetres <= kMaximumFullBodyIKToleranceMetresUVE) ||
        !std::isfinite(settings.rootTranslationWeight) || settings.rootTranslationWeight < 0.0F ||
        settings.rootTranslationWeight > 1.0F ||
        !(settings.maxJointStepRadians >= kMinimumFullBodyIKStepRadiansUVE) ||
        !(settings.maxJointStepRadians <= kMaximumFullBodyIKStepRadiansUVE)) {
        return std::nullopt;
    }
    for (std::size_t index = 0U; index < jointCount; ++index) {
        const FullBodyIKJointUVE& joint = problem.joints[index];
        if (!IsFiniteJointUVE(joint)) {
            return std::nullopt;
        }
        if (joint.parent >= 0) {
            const auto parent = static_cast<std::size_t>(joint.parent);
            if (parent >= index) {
                return std::nullopt; // not a hierarchy in order: a parent must come first
            }
        }
    }

    // Which joints the solve may drive: the root's subtree. An effector outside it would make the
    // sweeps describe a body the rig does not have, so that is a refused problem, not a pose.
    std::vector<bool> inSubtree(jointCount, false);
    inSubtree[settings.rootJoint] = true;
    for (std::size_t index = 0U; index < jointCount; ++index) {
        const FullBodyIKJointUVE& joint = problem.joints[index];
        if (joint.parent >= 0 &&
            inSubtree[static_cast<std::size_t>(joint.parent)]) {
            inSubtree[index] = true;
        }
    }
    for (std::size_t effector = 0U; effector < effectorCount; ++effector) {
        const std::size_t joint = problem.effectorJoints[effector];
        const float weight = problem.effectorWeights[effector];
        if (joint >= jointCount || !inSubtree[joint] ||
            !Math::IsFiniteUVE(problem.effectorTargets[effector]) || !std::isfinite(weight) ||
            weight < 0.0F || weight > 1.0F) {
            return std::nullopt;
        }
    }

    // ---- Solve ---------------------------------------------------------------------------------
    std::vector<Math::Vector3UVE> positions(jointCount);
    std::vector<Math::QuaternionUVE> rotations(jointCount);
    for (std::size_t index = 0U; index < jointCount; ++index) {
        positions[index] = problem.joints[index].worldPosition;
        rotations[index] = problem.joints[index].worldRotation;
    }

    // Forward kinematics for everything the solve owns. The root joint is the anchor its
    // translation moves; joints outside the subtree are the posed rig the solve must not touch.
    // Hierarchy order (parents before children) is validated above, so one ordered pass is the
    // whole recompute; after any joint rotation it is idempotent for untouched branches.
    const auto recomputeSubtree = [&]() noexcept {
        for (std::size_t index = 0U; index < jointCount; ++index) {
            if (index == settings.rootJoint || !inSubtree[index]) {
                continue;
            }
            const FullBodyIKJointUVE& joint = problem.joints[index];
            positions[index] =
                positions[static_cast<std::size_t>(joint.parent)] +
                Math::RotateVectorUVE(rotations[static_cast<std::size_t>(joint.parent)],
                                      joint.localOffset);
        }
    };

    // Pull targets: where each effector's weight says it should stop - a fixed point part-way from
    // where the pose PUT the effector toward its target. Fixed at the posed position on purpose: a
    // pull target that re-derived from the moving effector would turn `positionWeight` into a
    // convergence rate (every sweep stepping half-way again) instead of the authored "this foot
    // leans this much" it has to be.
    std::vector<Math::Vector3UVE> pullTargets(effectorCount);
    for (std::size_t effector = 0U; effector < effectorCount; ++effector) {
        const float weight = problem.effectorWeights[effector];
        pullTargets[effector] =
            problem.joints[problem.effectorJoints[effector]].worldPosition +
            (problem.effectorTargets[effector] -
             problem.joints[problem.effectorJoints[effector]].worldPosition) *
                weight;
    }

    FullBodyIKSolverReportUVE report;
    report.solvedPositions = positions;
    report.solvedRotations = rotations;
    report.distances.assign(effectorCount, 0.0F);
    report.reached.assign(effectorCount, false);

    // Chain paths from the root out to each effector, precomputed once: the hierarchy cannot change
    // mid-solve, and rebuilding them per sweep would be cost without information.
    std::vector<std::vector<std::size_t>> chains(effectorCount);
    for (std::size_t effector = 0U; effector < effectorCount; ++effector) {
        std::size_t cursor = problem.effectorJoints[effector];
        chains[effector].push_back(cursor);
        while (cursor != settings.rootJoint) {
            cursor = static_cast<std::size_t>(problem.joints[cursor].parent);
            chains[effector].push_back(cursor);
        }
        std::reverse(chains[effector].begin(), chains[effector].end()); // root ... effector
    }

    const auto measurePullError = [&](const std::size_t effector) noexcept {
        return Math::LengthUVE(positions[problem.effectorJoints[effector]] - pullTargets[effector]);
    };
    const auto allPullingEffectorsReached = [&]() noexcept {
        for (std::size_t effector = 0U; effector < effectorCount; ++effector) {
            if (problem.effectorWeights[effector] > 0.0F &&
                measurePullError(effector) > settings.toleranceMetres) {
                return false;
            }
        }
        return true;
    };

    // Which chains must negotiate: two effectors whose paths share any joint cannot both finish
    // exactly in the same pass, and those are the ones the shared relaxation below is for.
    std::vector<bool> sharesJoints(effectorCount, false);
    for (std::size_t effector = 0U; effector < effectorCount; ++effector) {
        for (std::size_t other = 0U; other < effectorCount; ++other) {
            if (other == effector) {
                continue;
            }
            for (const std::size_t joint : chains[effector]) {
                if (std::find(chains[other].begin(), chains[other].end(), joint) !=
                    chains[other].end()) {
                    sharesJoints[effector] = true;
                    break;
                }
            }
        }
    }

    for (std::uint32_t iteration = 0U; iteration < settings.maxIterations; ++iteration) {
        report.iterationsUsed = static_cast<std::size_t>(iteration) + 1U;

        // Root translation: the weighted average of the pulls, applied as one rigid shift. This is
        // what turns "the foot is planted behind the character" from an impossible knee bend into a
        // step back - and with the weight at zero it is exactly nothing, so a rooted solve is the
        // same problem with this line off.
        if (settings.rootTranslationWeight > 0.0F) {
            Math::Vector3UVE weightedError{};
            float totalWeight = 0.0F;
            for (std::size_t effector = 0U; effector < effectorCount; ++effector) {
                const float weight = problem.effectorWeights[effector];
                if (weight <= 0.0F) {
                    continue;
                }
                weightedError = weightedError + (pullTargets[effector] -
                                                 positions[problem.effectorJoints[effector]]) *
                                                    weight;
                totalWeight += weight;
            }
            if (totalWeight > kDegenerateLengthUVE) {
                const Math::Vector3UVE delta =
                    weightedError * (settings.rootTranslationWeight / totalWeight);
                positions[settings.rootJoint] = positions[settings.rootJoint] + delta;
                report.rootTranslation = report.rootTranslation + delta;
                recomputeSubtree();
            }
        }

        // One CCD sweep per effector, in the author's own order: later effectors solve against what
        // earlier ones left, which is the only arbitration two pulls on one spine can have, and it
        // is the same answer every run because the order is the file's.
        for (std::size_t effector = 0U; effector < effectorCount; ++effector) {
            const float weight = problem.effectorWeights[effector];
            if (weight <= 0.0F) {
                continue;
            }
            const std::size_t effectorJoint = problem.effectorJoints[effector];
            const Math::Vector3UVE target = pullTargets[effector];
            const std::vector<std::size_t>& chain = chains[effector];
            // Land this effector before the next one pulls: one pass is rarely enough for a chain
            // with a shared spine above it, and a half-done sweep is exactly what the next effector
            // would undo.
            float previousError = measurePullError(effector);
            for (std::uint32_t inner = 0U; inner < kMaximumFullBodyIKInnerSweepsUVE; ++inner) {
                if (previousError <= settings.toleranceMetres) {
                    break;
                }
                // Tip inward: the joint nearest the effector bends first, so the pose folds where an
                // animator would fold it - the wrist before the elbow before the shoulder.
                for (std::size_t step = chain.size(); step >= 2U; --step) {
                    const std::size_t joint = chain[step - 2U];
                    const Math::Vector3UVE current = positions[effectorJoint] - positions[joint];
                    const Math::Vector3UVE desired = target - positions[joint];
                    Math::QuaternionUVE turn{};
                    if (!TryFromToRotationUVE(current, desired, turn)) {
                        continue;
                    }
                    rotations[joint] =
                        Math::MultiplyUVE(ClampStepUVE(turn, settings.maxJointStepRadians),
                                          rotations[joint]);
                    recomputeSubtree();
                }
                const float error = measurePullError(effector);
                if (error >= previousError - kMinimumFullBodyIKImprovementUVE) {
                    break; // this chain is as close as its own sweep gets
                }
                previousError = error;
            }
            // Exact limb finish: whatever the sweeps did to the body above, the last two joints of
            // this chain are an analytic two-bone problem - two circles, no iteration - and solving
            // them exactly is what removes CCD's plateau on long chains and its dilemma on straight
            // ones. The pose the sweeps left gives the solve its bend plane; a degenerate chain it
            // refuses simply keeps the swept answer.
            if (chain.size() >= 3U) {
                const std::size_t lowerJoint = chain[chain.size() - 3U];
                const std::size_t middleJoint = chain[chain.size() - 2U];
                Math::QuaternionUVE parentRotation{};
                if (problem.joints[lowerJoint].parent >= 0) {
                    parentRotation =
                        rotations[static_cast<std::size_t>(problem.joints[lowerJoint].parent)];
                }
                const TwoBoneIKChainUVE analytic{positions[lowerJoint], positions[middleJoint],
                                                 positions[effectorJoint], parentRotation,
                                                 rotations[lowerJoint], rotations[middleJoint]};
                const std::optional<TwoBoneIKSolutionUVE> solution =
                    SolveTwoBoneIKUVE(analytic, pullTargets[effector], Math::Vector3UVE{});
                if (solution.has_value()) {
                    const Math::QuaternionUVE exactLower =
                        Math::MultiplyUVE(parentRotation, solution->rootLocalRotation);
                    const Math::QuaternionUVE exactMiddle =
                        Math::MultiplyUVE(exactLower, solution->middleLocalRotation);
                    const float strength =
                        sharesJoints[effector] ? kSharedJointRelaxationUVE : 1.0F;
                    Math::QuaternionUVE relaxedLower{};
                    Math::QuaternionUVE relaxedMiddle{};
                    if (!TryBlendTwoBoneIKRotationUVE(rotations[lowerJoint], exactLower, strength,
                                                      relaxedLower) ||
                        !TryBlendTwoBoneIKRotationUVE(rotations[middleJoint], exactMiddle, strength,
                                                      relaxedMiddle)) {
                        relaxedLower = exactLower;
                        relaxedMiddle = exactMiddle;
                    }
                    rotations[lowerJoint] = relaxedLower;
                    rotations[middleJoint] = relaxedMiddle;
                    recomputeSubtree();
                }
            }
        }

        if (allPullingEffectorsReached()) {
            report.converged = true;
            break;
        }
    }
    // The last effector step may have landed everything the budget allowed; re-measure before
    // reporting so `converged` reflects the pose actually returned.
    report.converged = allPullingEffectorsReached();

    for (std::size_t effector = 0U; effector < effectorCount; ++effector) {
        // The report answers the authoring question - did the limb land where it was aimed? - so
        // the distance is to the TRUE target even when the weight only leaned part-way there.
        const float distance = Math::LengthUVE(positions[problem.effectorJoints[effector]] -
                                               problem.effectorTargets[effector]);
        report.distances[effector] = distance;
        report.reached[effector] = distance <= settings.toleranceMetres;
    }
    report.solvedPositions = positions;
    report.solvedRotations = rotations;
    return report;
}

void ApplyFullBodyIK3DObjectDefinitionUVE(IEntityManagerUVE& entityManager, const EntityUVE entity,
                                          const FullBodyIK3DObjectDefinitionUVE& value) {
    static_cast<void>(value);
    ApplyBoneModifier3DBaseUVE(entityManager, entity, FullBodyIK3DObjectDefinitionUVE::defaultName);
    if (entityManager.IsAliveUVE(entity) && !entityManager.HasComponentUVE<FullBodyIK3DComponentUVE>(entity)) {
        entityManager.AddComponentUVE<FullBodyIK3DComponentUVE>(entity, FullBodyIK3DComponentUVE{});
    }
}

} // namespace UVE::Scene
