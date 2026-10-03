// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

// The pose graph EVALUATION model. Not the authored animation graph.
//
// Two graph vocabularies live in this engine and they are easy to mistake for each other:
//
//   UVE::Scene::AnimationGraphComponentUVE  the authored graph - what the editor draws, what the
//                                           serializer writes, 12 node kinds, asset GUIDs, N
//                                           inputs, editor positions, blend spaces.
//   UVE::Core::PoseGraphUVE (this file)     the evaluation model - 11 node kinds, string clipId,
//                                           fixed inputA/inputB, no editor state.
//
// Each has kinds the other lacks: this one has Sync, Subtree and PoseCache; the authored graph has
// BlendSpace1D/2D, Additive, Select, LayeredBlend and TimeSeek. Nothing converts between them yet.
// Graph elements are called NODES in both - see the retired-name guard in
// Engine/Tools/check_engine_vocabulary.py.

#pragma once

#include "uve/animation/animation_clip_uve.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace UVE::Core {

enum class PoseGraphNodeKindUVE : std::uint8_t {
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

struct PoseGraphNodeUVE final {
    std::uint32_t id = 0U;
    PoseGraphNodeKindUVE kind = PoseGraphNodeKindUVE::OutputPose;
    std::string name;
    std::string clipId;
    std::string parameterId;
    std::uint32_t inputA = 0U;
    std::uint32_t inputB = 0U;
    float weight = 0.5F;
    float timeScale = 1.0F;
    bool enabled = true;
};

struct PoseGraphUVE final {
    static constexpr std::size_t kMaximumNodesUVE = 512U;
    static constexpr std::size_t kMaximumParametersUVE = 128U;

    std::vector<PoseGraphNodeUVE> nodes;
    std::vector<AnimationClipUVE> clips;
};

enum class PoseGraphValidationCodeUVE : std::uint8_t {
    Valid = 0,
    EmptyTree,
    CapacityExceeded,
    InvalidNode,
    DuplicateNode,
    UnknownInput,
    InvalidClip,
    UnknownClip,
    CycleDetected,
    InvalidParameter,
    MissingOutput,
};

struct PoseGraphValidationResultUVE final {
    PoseGraphValidationCodeUVE code = PoseGraphValidationCodeUVE::EmptyTree;
    std::uint32_t nodeId = 0U;
    std::string message;

    [[nodiscard]] bool IsValidUVE() const noexcept {
        return code == PoseGraphValidationCodeUVE::Valid;
    }
};

struct PoseGraphParameterUVE final {
    std::string parameterId;
    float value = 0.0F;
};

struct PoseGraphEvaluationResultUVE final {
    TransformPoseUVE pose;
    bool usedOutputNode = false;
    std::size_t evaluatedNodeCount = 0U;
    std::string message;

    [[nodiscard]] bool IsSuccessUVE() const noexcept {
        return usedOutputNode && evaluatedNodeCount > 0U;
    }
};

[[nodiscard]] PoseGraphValidationResultUVE ValidatePoseGraphUVE(
    const PoseGraphUVE& tree) noexcept;

/// Evaluates shared nodes independently for distinct local times; memoization is keyed by node ID
/// and exact local evaluation time, while active recursion remains cycle-checked by node ID.
[[nodiscard]] PoseGraphEvaluationResultUVE EvaluatePoseGraphUVE(
    const PoseGraphUVE& tree, double timeSeconds, const std::vector<PoseGraphParameterUVE>& parameters = {});

} // namespace UVE::Core
