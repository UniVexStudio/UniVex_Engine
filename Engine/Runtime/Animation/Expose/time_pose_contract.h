// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include "quaternion.h"
#include "vector3.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace UVE::Core {

struct UnifiedTimeState final {
    std::uint64_t frameNumber = 0U;
    double realTimeSeconds = 0.0;
    double gameTimeSeconds = 0.0;
    double fixedTimeSeconds = 0.0;
    double animationTimeSeconds = 0.0;
    double fixedAccumulatorSeconds = 0.0;
    double realDeltaSeconds = 0.0;
    double gameDeltaSeconds = 0.0;
    double animationDeltaSeconds = 0.0;
    bool paused = false;

    [[nodiscard]] bool operator==(const UnifiedTimeState&) const = default;
};

struct UnifiedTimeAdvanceInput final {
    double realDeltaSeconds = 0.0;
    double gameTimeScale = 1.0;
    double animationTimeScale = 1.0;
    double fixedStepSeconds = 1.0 / 60.0;
    std::size_t maximumFixedSteps = 8U;
    bool paused = false;
};

struct UnifiedTimeAdvanceResult final {
    UnifiedTimeState state;
    std::size_t fixedSteps = 0U;
    bool inputClamped = false;

    [[nodiscard]] bool IsAccepted() const noexcept {
        return state.realDeltaSeconds >= 0.0 && state.gameDeltaSeconds >= 0.0 &&
               state.animationDeltaSeconds >= 0.0;
    }
};

// Shared time-domain state is a pure transition contract. The engine timer remains responsible
// for wall-clock sampling; this contract only advances copied values for game/fixed/animation users.
class UnifiedTimeContract final {
public:
    static constexpr double kMaximumFrameDeltaSeconds = 0.25;
    static constexpr std::size_t kMaximumFixedStepsPerAdvance = 8U;

    [[nodiscard]] static UnifiedTimeState Advance(
        const UnifiedTimeState& previous, UnifiedTimeAdvanceInput input,
        std::size_t& outFixedSteps, bool& outInputClamped) noexcept;
};

struct TransformPose final {
    Math::Vector3 position;
    Math::Quaternion rotation;
    Math::Vector3 scale{1.0F, 1.0F, 1.0F};

    [[nodiscard]] bool operator==(const TransformPose&) const noexcept = default;
};

inline constexpr std::size_t kMaximumSkeletonJoints = 256U;
/// Maximum byte length for skeleton/joint identifiers; accepted identifiers are non-empty where
/// required and never contain embedded NUL bytes.
inline constexpr std::size_t kMaximumAnimationIdentifierBytes = 128U;

struct SkeletonJoint final {
    std::string jointId;
    std::string parentJointId;

    [[nodiscard]] bool operator==(const SkeletonJoint&) const noexcept = default;
};

struct SkeletonDefinition final {
    std::string skeletonId;
    std::vector<SkeletonJoint> joints;

    [[nodiscard]] bool operator==(const SkeletonDefinition&) const noexcept = default;
};

struct SkeletonRetargetMapEntry final {
    std::string sourceJointId;
    std::string targetJointId;

    [[nodiscard]] bool operator==(const SkeletonRetargetMapEntry&) const noexcept = default;
};

struct PoseBuffer final {
    std::string skeletonId;
    std::vector<TransformPose> localJoints;

    [[nodiscard]] bool operator==(const PoseBuffer&) const noexcept = default;
};

struct AnimationEvaluationContext final {
    UnifiedTimeState time;
    double sampleTimeSeconds = 0.0;

    [[nodiscard]] bool operator==(const AnimationEvaluationContext&) const noexcept = default;
};

enum class AnimationContractValidationCode : std::uint8_t {
    Valid = 0,
    InvalidIdentifier,
    CapacityExceeded,
    DuplicateJoint,
    UnknownParent,
    InvalidPose,
    SkeletonMismatch,
    InvalidTime,
};

struct AnimationContractValidationResult final {
    AnimationContractValidationCode code = AnimationContractValidationCode::Valid;
    std::size_t index = 0U;
    std::string message;

    [[nodiscard]] bool IsValid() const noexcept {
        return code == AnimationContractValidationCode::Valid;
    }
};

struct PoseSample final {
    double timeSeconds = 0.0;
    TransformPose pose;

    [[nodiscard]] bool operator==(const PoseSample&) const noexcept = default;
};

[[nodiscard]] AnimationContractValidationResult ValidateSkeletonDefinition(
    const SkeletonDefinition& skeleton) noexcept;
[[nodiscard]] AnimationContractValidationResult ValidatePoseBuffer(
    const PoseBuffer& pose, const SkeletonDefinition& skeleton) noexcept;
[[nodiscard]] AnimationContractValidationResult ValidateSkeletonRetargetMap(
    const SkeletonDefinition& sourceSkeleton, const SkeletonDefinition& targetSkeleton,
    const std::vector<SkeletonRetargetMapEntry>& map) noexcept;
[[nodiscard]] AnimationContractValidationResult ValidateAnimationEvaluationContext(
    const AnimationEvaluationContext& context) noexcept;

[[nodiscard]] bool IsFiniteTransformPose(const TransformPose& pose) noexcept;
[[nodiscard]] bool TryNormalizeTransformPose(const TransformPose& pose,
                                                 TransformPose& outNormalized) noexcept;

} // namespace UVE::Core
