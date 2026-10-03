// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include "uve/animation/animation_clip_uve.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace UVE::Core {

enum class AnimationGraphObjectKindUVE : std::uint8_t {
    ClipPlayer = 0,
    Blend,
    Parameter,
    State,
    Transition,
    OneShot,
    TimeScale,
    Sync,
    Subtree,
    PoseCache,
    OutputPose,
};

struct AnimationGraphObjectUVE final {
    std::uint32_t id = 0U;
    AnimationGraphObjectKindUVE kind = AnimationGraphObjectKindUVE::OutputPose;
    std::string name;
    std::string clipId;
    std::string parameterId;
    std::uint32_t inputA = 0U;
    std::uint32_t inputB = 0U;
    float weight = 0.5F;
    float timeScale = 1.0F;
    bool enabled = true;
};

struct AnimationGraphUVE final {
    static constexpr std::size_t kMaximumObjectsUVE = 512U;
    static constexpr std::size_t kMaximumParametersUVE = 128U;

    std::vector<AnimationGraphObjectUVE> objects;
    std::vector<AnimationClipUVE> clips;
};

enum class AnimationGraphValidationCodeUVE : std::uint8_t {
    Valid = 0,
    EmptyTree,
    CapacityExceeded,
    InvalidObject,
    DuplicateObject,
    UnknownInput,
    InvalidClip,
    UnknownClip,
    CycleDetected,
    InvalidParameter,
    MissingOutput,
};

struct AnimationGraphValidationResultUVE final {
    AnimationGraphValidationCodeUVE code = AnimationGraphValidationCodeUVE::EmptyTree;
    std::uint32_t objectId = 0U;
    std::string message;

    [[nodiscard]] bool IsValidUVE() const noexcept {
        return code == AnimationGraphValidationCodeUVE::Valid;
    }
};

struct AnimationGraphParameterUVE final {
    std::string parameterId;
    float value = 0.0F;
};

struct AnimationGraphEvaluationResultUVE final {
    TransformPoseUVE pose;
    bool usedOutputObject = false;
    std::size_t evaluatedObjectCount = 0U;
    std::string message;

    [[nodiscard]] bool IsSuccessUVE() const noexcept {
        return usedOutputObject && evaluatedObjectCount > 0U;
    }
};

[[nodiscard]] AnimationGraphValidationResultUVE ValidateAnimationGraphUVE(
    const AnimationGraphUVE& tree) noexcept;

/// Evaluates shared objects independently for distinct local times; memoization is keyed by object ID
/// and exact local evaluation time, while active recursion remains cycle-checked by object ID.
[[nodiscard]] AnimationGraphEvaluationResultUVE EvaluateAnimationGraphUVE(
    const AnimationGraphUVE& tree, double timeSeconds, const std::vector<AnimationGraphParameterUVE>& parameters = {});

} // namespace UVE::Core
