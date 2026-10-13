// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/animation/pose_graph_uve.h"

#include "uve/utilities/hash_uve.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <unordered_map>
#include <unordered_set>

namespace UVE::Core {
namespace {

constexpr std::size_t kMaximumIdentifierBytesUVE = 128U;

struct PoseGraphCacheKeyUVE final {
    std::uint32_t nodeId = 0U;
    double localTime = 0.0;

    [[nodiscard]] bool operator==(const PoseGraphCacheKeyUVE&) const noexcept = default;
};

struct PoseGraphCacheKeyHashUVE final {
    [[nodiscard]] std::size_t operator()(const PoseGraphCacheKeyUVE& key) const noexcept {
        // The shared boost-formula combiner: byte-identical to the hand-rolled mix it replaced.
        std::size_t seed = std::hash<std::uint32_t>{}(key.nodeId);
        Utilities::HashCombineUVE(seed, std::hash<double>{}(key.localTime));
        return seed;
    }
};

[[nodiscard]] const PoseGraphNodeUVE* FindNodeUVE(
    const PoseGraphUVE& tree, const std::uint32_t id) noexcept {
    const auto iterator = std::find_if(tree.nodes.cbegin(), tree.nodes.cend(), [id](const auto& node) {
        return node.id == id;
    });
    return iterator == tree.nodes.cend() ? nullptr : &*iterator;
}

[[nodiscard]] const AnimationClipUVE* FindClipUVE(
    const PoseGraphUVE& tree, const std::string& clipId) noexcept {
    const auto iterator = std::find_if(tree.clips.cbegin(), tree.clips.cend(), [&clipId](const auto& clip) {
        return clip.clipId == clipId;
    });
    return iterator == tree.clips.cend() ? nullptr : &*iterator;
}

[[nodiscard]] float FindParameterValueUVE(
    const std::vector<PoseGraphParameterUVE>& parameters, const std::string& parameterId) noexcept {
    const auto iterator = std::find_if(parameters.cbegin(), parameters.cend(), [&parameterId](const auto& parameter) {
        return parameter.parameterId == parameterId;
    });
    return iterator == parameters.cend() ? 0.0F : iterator->value;
}

[[nodiscard]] TransformPoseUVE BlendPoseUVE(const TransformPoseUVE& left,
                                            const TransformPoseUVE& right,
                                            const float weight) noexcept {
    const float factor = std::clamp(weight, 0.0F, 1.0F);
    TransformPoseUVE blended;
    blended.position = left.position * (1.0F - factor) + right.position * factor;
    blended.scale = left.scale * (1.0F - factor) + right.scale * factor;
    blended.rotation = Math::QuaternionUVE{
        left.rotation.x * (1.0F - factor) + right.rotation.x * factor,
        left.rotation.y * (1.0F - factor) + right.rotation.y * factor,
        left.rotation.z * (1.0F - factor) + right.rotation.z * factor,
        left.rotation.w * (1.0F - factor) + right.rotation.w * factor,
    };
    TransformPoseUVE normalized;
    return TryNormalizeTransformPoseUVE(blended, normalized) ? normalized : left;
}

[[nodiscard]] bool UsesInputAUVE(const PoseGraphNodeKindUVE kind) noexcept {
    return kind != PoseGraphNodeKindUVE::ClipPlayer;
}

[[nodiscard]] bool UsesInputBUVE(const PoseGraphNodeKindUVE kind) noexcept {
    return kind == PoseGraphNodeKindUVE::Blend || kind == PoseGraphNodeKindUVE::Transition;
}

} // namespace

PoseGraphValidationResultUVE ValidatePoseGraphUVE(const PoseGraphUVE& tree) noexcept {
    if (tree.nodes.empty()) {
        return {PoseGraphValidationCodeUVE::EmptyTree, 0U, "PoseGraph requires at least one node."};
    }
    if (tree.nodes.size() > PoseGraphUVE::kMaximumNodesUVE) {
        return {PoseGraphValidationCodeUVE::CapacityExceeded, 0U,
                "PoseGraph node count exceeds the bounded limit."};
    }
    std::unordered_set<std::uint32_t> nodeIds;
    nodeIds.reserve(tree.nodes.size());
    for (const PoseGraphNodeUVE& node : tree.nodes) {
        if (node.id == 0U || node.name.empty() || node.name.size() > kMaximumIdentifierBytesUVE ||
            !std::isfinite(node.weight) || node.weight < 0.0F || node.weight > 1.0F ||
            !std::isfinite(node.timeScale) || node.timeScale < 0.0F) {
            return {PoseGraphValidationCodeUVE::InvalidNode, node.id,
                    "PoseGraph node identity or bounded numeric configuration is invalid."};
        }
        if (!nodeIds.insert(node.id).second) {
            return {PoseGraphValidationCodeUVE::DuplicateNode, node.id,
                    "PoseGraph node identifiers must be unique."};
        }
    }
    for (const AnimationClipUVE& clip : tree.clips) {
        const AnimationClipValidationResultUVE clipResult = ValidateAnimationClipUVE(clip);
        if (!clipResult.IsValidUVE()) {
            return {PoseGraphValidationCodeUVE::InvalidClip, 0U,
                    "PoseGraph contains an invalid AnimationClip resource."};
        }
    }
    std::size_t outputCount = 0U;
    for (const PoseGraphNodeUVE& node : tree.nodes) {
        if (node.kind == PoseGraphNodeKindUVE::ClipPlayer &&
            (node.clipId.empty() || FindClipUVE(tree, node.clipId) == nullptr)) {
            return {PoseGraphValidationCodeUVE::UnknownClip, node.id,
                    "PoseGraph ClipPlayer references an unknown clip."};
        }
        if (node.kind == PoseGraphNodeKindUVE::Parameter &&
            (node.parameterId.empty() || node.parameterId.size() > kMaximumIdentifierBytesUVE)) {
            return {PoseGraphValidationCodeUVE::InvalidParameter, node.id,
                    "PoseGraph Parameter requires a bounded parameter identifier."};
        }
        if (node.kind == PoseGraphNodeKindUVE::OutputPose) {
            ++outputCount;
        }
        if (UsesInputAUVE(node.kind) && node.inputA == 0U) {
            return {PoseGraphValidationCodeUVE::InvalidNode, node.id,
                    "PoseGraph node requires inputA."};
        }
        if (UsesInputBUVE(node.kind) && node.inputB == 0U) {
            return {PoseGraphValidationCodeUVE::InvalidNode, node.id,
                    "PoseGraph node requires inputB."};
        }
        if (node.inputA != 0U && FindNodeUVE(tree, node.inputA) == nullptr) {
            return {PoseGraphValidationCodeUVE::UnknownInput, node.id,
                    "PoseGraph inputA references an unknown node."};
        }
        if (node.inputB != 0U && FindNodeUVE(tree, node.inputB) == nullptr) {
            return {PoseGraphValidationCodeUVE::UnknownInput, node.id,
                    "PoseGraph inputB references an unknown node."};
        }
    }
    if (outputCount == 0U) {
        return {PoseGraphValidationCodeUVE::MissingOutput, 0U,
                "PoseGraph requires an OutputPose node."};
    }

    std::unordered_map<std::uint32_t, std::uint8_t> visitState;
    visitState.reserve(tree.nodes.size());
    const std::function<bool(const PoseGraphNodeUVE&)> visit = [&](const PoseGraphNodeUVE& node) {
        const std::uint8_t state = visitState[node.id];
        if (state == 1U) {
            return false;
        }
        if (state == 2U) {
            return true;
        }
        visitState[node.id] = 1U;
        if ((node.inputA != 0U && !visit(*FindNodeUVE(tree, node.inputA))) ||
            (node.inputB != 0U && !visit(*FindNodeUVE(tree, node.inputB)))) {
            return false;
        }
        visitState[node.id] = 2U;
        return true;
    };
    for (const PoseGraphNodeUVE& node : tree.nodes) {
        if (!visit(node)) {
            return {PoseGraphValidationCodeUVE::CycleDetected, node.id,
                    "PoseGraph node inputs must be acyclic."};
        }
    }
    return {PoseGraphValidationCodeUVE::Valid, 0U, "PoseGraph is valid."};
}

PoseGraphEvaluationResultUVE EvaluatePoseGraphUVE(
    const PoseGraphUVE& tree, const double timeSeconds,
    const std::vector<PoseGraphParameterUVE>& parameters) {
    PoseGraphEvaluationResultUVE result;
    if (!ValidatePoseGraphUVE(tree).IsValidUVE() || !std::isfinite(timeSeconds)) {
        result.message = "PoseGraph evaluation rejected an invalid tree or time.";
        return result;
    }
    const auto output = std::find_if(tree.nodes.cbegin(), tree.nodes.cend(), [](const auto& node) {
        return node.kind == PoseGraphNodeKindUVE::OutputPose;
    });
    if (output == tree.nodes.cend()) {
        result.message = "PoseGraph has no output node.";
        return result;
    }
    std::unordered_map<PoseGraphCacheKeyUVE, TransformPoseUVE, PoseGraphCacheKeyHashUVE> cache;
    std::unordered_set<std::uint32_t> evaluating;
    std::function<bool(const PoseGraphNodeUVE&, double, TransformPoseUVE&)> evaluate =
        [&](const PoseGraphNodeUVE& node, const double localTime, TransformPoseUVE& outPose) {
            const PoseGraphCacheKeyUVE cacheKey{node.id, localTime};
            if (const auto cached = cache.find(cacheKey); cached != cache.end()) {
                outPose = cached->second;
                return true;
            }
            if (!evaluating.insert(node.id).second) {
                return false;
            }
            bool success = true;
            switch (node.kind) {
                case PoseGraphNodeKindUVE::ClipPlayer: {
                    const AnimationClipUVE* clip = FindClipUVE(tree, node.clipId);
                    success = clip != nullptr && TrySampleAnimationClipUVE(*clip, localTime, true, outPose);
                    break;
                }
                case PoseGraphNodeKindUVE::Blend: {
                    TransformPoseUVE left;
                    TransformPoseUVE right;
                    success = evaluate(*FindNodeUVE(tree, node.inputA), localTime, left) &&
                              evaluate(*FindNodeUVE(tree, node.inputB), localTime, right);
                    if (success) {
                        outPose = BlendPoseUVE(left, right, node.weight);
                    }
                    break;
                }
                case PoseGraphNodeKindUVE::Transition: {
                    const float parameter = FindParameterValueUVE(parameters, node.parameterId);
                    const PoseGraphNodeUVE* selected = parameter > 0.5F
                        ? FindNodeUVE(tree, node.inputB) : FindNodeUVE(tree, node.inputA);
                    success = selected != nullptr && evaluate(*selected, localTime, outPose);
                    break;
                }
                case PoseGraphNodeKindUVE::TimeScale: {
                    const double scaledTime = localTime * static_cast<double>(node.timeScale);
                    success = std::isfinite(scaledTime) &&
                              evaluate(*FindNodeUVE(tree, node.inputA), scaledTime, outPose);
                    break;
                }
                case PoseGraphNodeKindUVE::Parameter:
                case PoseGraphNodeKindUVE::State:
                case PoseGraphNodeKindUVE::OneShot:
                case PoseGraphNodeKindUVE::Sync:
                case PoseGraphNodeKindUVE::Subtree:
                case PoseGraphNodeKindUVE::PoseCache:
                case PoseGraphNodeKindUVE::OutputPose:
                    success = evaluate(*FindNodeUVE(tree, node.inputA), localTime, outPose);
                    break;
            }
            evaluating.erase(node.id);
            if (success) {
                cache.emplace(cacheKey, outPose);
                ++result.evaluatedNodeCount;
            }
            return success;
        };
    result.usedOutputNode = evaluate(*output, timeSeconds, result.pose);
    result.message = result.usedOutputNode ? "PoseGraph evaluated successfully." :
                                             "PoseGraph evaluation failed.";
    return result;
}

} // namespace UVE::Core
