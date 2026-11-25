// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "time_pose_contract.h"
#include "vector3.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace UVE::Core {
namespace {

[[nodiscard]] double NonNegativeScale(const double value) noexcept {
    return std::isfinite(value) && value >= 0.0 ? value : 0.0;
}

[[nodiscard]] bool IsAnimationIdentifierValid(const std::string& identifier, const bool allowEmpty) noexcept {
    return (allowEmpty || !identifier.empty()) &&
           identifier.size() <= kMaximumAnimationIdentifierBytes &&
           identifier.find('\0') == std::string::npos;
}

[[nodiscard]] bool IsFiniteUnifiedTimeState(const UnifiedTimeState& state) noexcept {
    return std::isfinite(state.realTimeSeconds) && std::isfinite(state.gameTimeSeconds) &&
           std::isfinite(state.fixedTimeSeconds) && std::isfinite(state.animationTimeSeconds) &&
           std::isfinite(state.fixedAccumulatorSeconds) && std::isfinite(state.realDeltaSeconds) &&
           std::isfinite(state.gameDeltaSeconds) && std::isfinite(state.animationDeltaSeconds);
}

} // namespace

UnifiedTimeState UnifiedTimeContract::Advance(
    const UnifiedTimeState& previous, UnifiedTimeAdvanceInput input,
    std::size_t& outFixedSteps, bool& outInputClamped) noexcept {
    UnifiedTimeState next = previous;
    outFixedSteps = 0U;
    outInputClamped = false;

    double realDelta = input.realDeltaSeconds;
    if (!std::isfinite(realDelta) || realDelta < 0.0) {
        realDelta = 0.0;
        outInputClamped = true;
    }
    if (realDelta > kMaximumFrameDeltaSeconds) {
        realDelta = kMaximumFrameDeltaSeconds;
        outInputClamped = true;
    }
    double fixedStep = input.fixedStepSeconds;
    if (!std::isfinite(fixedStep) || fixedStep <= 0.0) {
        fixedStep = 1.0 / 60.0;
        outInputClamped = true;
    }
    const std::size_t maximumFixedSteps = std::min(
        input.maximumFixedSteps, kMaximumFixedStepsPerAdvance);
    if (input.maximumFixedSteps > kMaximumFixedStepsPerAdvance) {
        outInputClamped = true;
    }
    const double gameScale = NonNegativeScale(input.gameTimeScale);
    const double animationScale = NonNegativeScale(input.animationTimeScale);
    if (!std::isfinite(input.gameTimeScale) || input.gameTimeScale < 0.0 ||
        !std::isfinite(input.animationTimeScale) || input.animationTimeScale < 0.0) {
        outInputClamped = true;
    }

    next.frameNumber = previous.frameNumber == std::numeric_limits<std::uint64_t>::max()
        ? previous.frameNumber : previous.frameNumber + 1U;
    next.realDeltaSeconds = realDelta;
    next.gameDeltaSeconds = input.paused ? 0.0 : realDelta * gameScale;
    next.animationDeltaSeconds = input.paused ? 0.0 : realDelta * animationScale;
    next.realTimeSeconds += realDelta;
    if (!input.paused) {
        next.gameTimeSeconds += next.gameDeltaSeconds;
        next.animationTimeSeconds += next.animationDeltaSeconds;
        next.fixedAccumulatorSeconds += realDelta;
        while (outFixedSteps < maximumFixedSteps && next.fixedAccumulatorSeconds >= fixedStep) {
            next.fixedAccumulatorSeconds -= fixedStep;
            next.fixedTimeSeconds += fixedStep;
            ++outFixedSteps;
        }
    }
    next.paused = input.paused;
    if (!IsFiniteUnifiedTimeState(next)) {
        outFixedSteps = 0U;
        outInputClamped = true;
        return previous;
    }
    return next;
}

AnimationContractValidationResult ValidateSkeletonDefinition(
    const SkeletonDefinition& skeleton) noexcept {
    if (skeleton.skeletonId.empty() && skeleton.joints.empty()) {
        return {AnimationContractValidationCode::Valid, 0U, "empty skeleton contract"};
    }
    if (!IsAnimationIdentifierValid(skeleton.skeletonId, false)) {
        return {AnimationContractValidationCode::InvalidIdentifier, 0U,
                "skeleton identifier is empty or exceeds its bound"};
    }
    if (skeleton.joints.empty() || skeleton.joints.size() > kMaximumSkeletonJoints) {
        return {AnimationContractValidationCode::CapacityExceeded, 0U,
                "skeleton joint count is empty or exceeds its bound"};
    }
    for (std::size_t index = 0U; index < skeleton.joints.size(); ++index) {
        const SkeletonJoint& joint = skeleton.joints[index];
        if (!IsAnimationIdentifierValid(joint.jointId, false) ||
            !IsAnimationIdentifierValid(joint.parentJointId, true)) {
            return {AnimationContractValidationCode::InvalidIdentifier, index,
                    "skeleton joint identifier exceeds its bound"};
        }
        if (std::find_if(skeleton.joints.cbegin(), skeleton.joints.cbegin() + static_cast<std::ptrdiff_t>(index),
                         [&joint](const SkeletonJoint& previous) {
                             return previous.jointId == joint.jointId;
                         }) != skeleton.joints.cbegin() + static_cast<std::ptrdiff_t>(index)) {
            return {AnimationContractValidationCode::DuplicateJoint, index,
                    "skeleton joint identifiers must be unique"};
        }
    }
    for (std::size_t index = 0U; index < skeleton.joints.size(); ++index) {
        const std::string& parentId = skeleton.joints[index].parentJointId;
        if (!parentId.empty() && std::find_if(
                skeleton.joints.cbegin(), skeleton.joints.cend(), [&parentId](const SkeletonJoint& joint) {
                    return joint.jointId == parentId;
                }) == skeleton.joints.cend()) {
            return {AnimationContractValidationCode::UnknownParent, index,
                    "skeleton joint parent references an unknown joint"};
        }
    }
    return {AnimationContractValidationCode::Valid, 0U, "valid skeleton contract"};
}

AnimationContractValidationResult ValidateSkeletonRetargetMap(
    const SkeletonDefinition& sourceSkeleton, const SkeletonDefinition& targetSkeleton,
    const std::vector<SkeletonRetargetMapEntry>& map) noexcept {
    const AnimationContractValidationResult sourceValidation =
        ValidateSkeletonDefinition(sourceSkeleton);
    if (!sourceValidation.IsValid()) {
        return sourceValidation;
    }
    const AnimationContractValidationResult targetValidation =
        ValidateSkeletonDefinition(targetSkeleton);
    if (!targetValidation.IsValid()) {
        return targetValidation;
    }
    if (sourceSkeleton.skeletonId.empty() || targetSkeleton.skeletonId.empty() ||
        map.size() > kMaximumSkeletonJoints) {
        return {AnimationContractValidationCode::CapacityExceeded, 0U,
                "retarget map requires bounded non-empty source and target skeletons"};
    }
    for (std::size_t index = 0U; index < map.size(); ++index) {
        const SkeletonRetargetMapEntry& entry = map[index];
        if (!IsAnimationIdentifierValid(entry.sourceJointId, false) ||
            !IsAnimationIdentifierValid(entry.targetJointId, false)) {
            return {AnimationContractValidationCode::InvalidIdentifier, index,
                    "retarget map joint identifiers are invalid"};
        }
        const auto sourceIt = std::find_if(sourceSkeleton.joints.cbegin(), sourceSkeleton.joints.cend(),
                                           [&entry](const SkeletonJoint& joint) {
                                               return joint.jointId == entry.sourceJointId;
                                           });
        const auto targetIt = std::find_if(targetSkeleton.joints.cbegin(), targetSkeleton.joints.cend(),
                                           [&entry](const SkeletonJoint& joint) {
                                               return joint.jointId == entry.targetJointId;
                                           });
        if (sourceIt == sourceSkeleton.joints.cend() || targetIt == targetSkeleton.joints.cend()) {
            return {AnimationContractValidationCode::SkeletonMismatch, index,
                    "retarget map references an unknown source or target joint"};
        }
        if (std::find_if(map.cbegin(), map.cbegin() + static_cast<std::ptrdiff_t>(index),
                         [&entry](const SkeletonRetargetMapEntry& previous) {
                             return previous.sourceJointId == entry.sourceJointId ||
                                    previous.targetJointId == entry.targetJointId;
                         }) != map.cbegin() + static_cast<std::ptrdiff_t>(index)) {
            return {AnimationContractValidationCode::DuplicateJoint, index,
                    "retarget map source and target joints must be unique"};
        }
    }
    return {AnimationContractValidationCode::Valid, 0U, "valid skeleton retarget map"};
}

AnimationContractValidationResult ValidatePoseBuffer(
    const PoseBuffer& pose, const SkeletonDefinition& skeleton) noexcept {
    const AnimationContractValidationResult skeletonValidation = ValidateSkeletonDefinition(skeleton);
    if (!skeletonValidation.IsValid()) {
        return skeletonValidation;
    }
    if (!IsAnimationIdentifierValid(pose.skeletonId, true)) {
        return {AnimationContractValidationCode::InvalidIdentifier, 0U,
                "pose buffer skeleton identifier is empty, oversized, or contains NUL"};
    }
    if (skeleton.skeletonId.empty() && pose.skeletonId.empty() && pose.localJoints.empty()) {
        return {AnimationContractValidationCode::Valid, 0U, "empty pose contract"};
    }
    if (pose.skeletonId != skeleton.skeletonId || pose.localJoints.size() != skeleton.joints.size()) {
        return {AnimationContractValidationCode::SkeletonMismatch, 0U,
                "pose buffer skeleton identity or joint count does not match"};
    }
    for (std::size_t index = 0U; index < pose.localJoints.size(); ++index) {
        TransformPose normalized;
        if (!TryNormalizeTransformPose(pose.localJoints[index], normalized)) {
            return {AnimationContractValidationCode::InvalidPose, index,
                    "pose buffer contains a non-finite or non-normalizable joint transform"};
        }
    }
    return {AnimationContractValidationCode::Valid, 0U, "valid pose contract"};
}

AnimationContractValidationResult ValidateAnimationEvaluationContext(
    const AnimationEvaluationContext& context) noexcept {
    const UnifiedTimeState& time = context.time;
    const auto finiteNonNegative = [](const double value) noexcept {
        return std::isfinite(value) && value >= 0.0;
    };
    if (!finiteNonNegative(time.realTimeSeconds) || !finiteNonNegative(time.gameTimeSeconds) ||
        !finiteNonNegative(time.fixedTimeSeconds) || !finiteNonNegative(time.animationTimeSeconds) ||
        !finiteNonNegative(time.fixedAccumulatorSeconds) || !finiteNonNegative(time.realDeltaSeconds) ||
        !finiteNonNegative(time.gameDeltaSeconds) || !finiteNonNegative(time.animationDeltaSeconds) ||
        !finiteNonNegative(context.sampleTimeSeconds)) {
        return {AnimationContractValidationCode::InvalidTime, 0U,
                "animation evaluation time contains a non-finite or negative value"};
    }
    return {AnimationContractValidationCode::Valid, 0U, "valid evaluation context"};
}

bool IsFiniteTransformPose(const TransformPose& pose) noexcept {
    return Math::IsFinite(pose.position) && Math::IsFinite(pose.scale) &&
           Math::IsFinite(pose.rotation);
}

bool TryNormalizeTransformPose(const TransformPose& pose,
                                  TransformPose& outNormalized) noexcept {
    if (!IsFiniteTransformPose(pose)) {
        return false;
    }
    Math::Quaternion normalizedRotation;
    if (!Math::TryNormalize(pose.rotation, normalizedRotation)) {
        return false;
    }
    outNormalized = pose;
    outNormalized.rotation = normalizedRotation;
    return true;
}

} // namespace UVE::Core
