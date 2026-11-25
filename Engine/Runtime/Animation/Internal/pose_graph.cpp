// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "pose_graph.h"

#include "hash.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <unordered_map>
#include <unordered_set>

namespace UVE::Core {
namespace {

constexpr std::size_t kMaximumIdentifierBytes = 128U;

struct PoseGraphCacheKey final {
    std::uint32_t nodeId = 0U;
    double localTime = 0.0;

    [[nodiscard]] bool operator==(const PoseGraphCacheKey&) const noexcept = default;
};

struct PoseGraphCacheKeyHash final {
    [[nodiscard]] std::size_t operator()(const PoseGraphCacheKey& key) const noexcept {
        // The shared boost-formula combiner: byte-identical to the hand-rolled mix it replaced.
        std::size_t seed = std::hash<std::uint32_t>{}(key.nodeId);
        Utilities::HashCombine(seed, std::hash<double>{}(key.localTime));
        return seed;
    }
};

[[nodiscard]] const PoseGraphNode* FindNode(
    const PoseGraph& tree, const std::uint32_t id) noexcept {
    const auto iterator = std::find_if(tree.nodes.cbegin(), tree.nodes.cend(), [id](const auto& node) {
        return node.id == id;
    });
    return iterator == tree.nodes.cend() ? nullptr : &*iterator;
}

[[nodiscard]] const AnimationClip* FindClip(
    const PoseGraph& tree, const std::string& clipId) noexcept {
    const auto iterator = std::find_if(tree.clips.cbegin(), tree.clips.cend(), [&clipId](const auto& clip) {
        return clip.clipId == clipId;
    });
    return iterator == tree.clips.cend() ? nullptr : &*iterator;
}

[[nodiscard]] float FindParameterValue(
    const std::vector<PoseGraphParameter>& parameters, const std::string& parameterId) noexcept {
    const auto iterator = std::find_if(parameters.cbegin(), parameters.cend(), [&parameterId](const auto& parameter) {
        return parameter.parameterId == parameterId;
    });
    return iterator == parameters.cend() ? 0.0F : iterator->value;
}

[[nodiscard]] TransformPose BlendPose(const TransformPose& left,
                                            const TransformPose& right,
                                            const float weight) noexcept {
    const float factor = std::clamp(weight, 0.0F, 1.0F);
    TransformPose blended;
    blended.position = left.position * (1.0F - factor) + right.position * factor;
    blended.scale = left.scale * (1.0F - factor) + right.scale * factor;
    blended.rotation = Math::Quaternion{
        left.rotation.x * (1.0F - factor) + right.rotation.x * factor,
        left.rotation.y * (1.0F - factor) + right.rotation.y * factor,
        left.rotation.z * (1.0F - factor) + right.rotation.z * factor,
        left.rotation.w * (1.0F - factor) + right.rotation.w * factor,
    };
    TransformPose normalized;
    return TryNormalizeTransformPose(blended, normalized) ? normalized : left;
}

[[nodiscard]] bool UsesInputA(const PoseGraphNodeKind kind) noexcept {
    return kind != PoseGraphNodeKind::ClipPlayer;
}

[[nodiscard]] bool UsesInputB(const PoseGraphNodeKind kind) noexcept {
    return kind == PoseGraphNodeKind::Blend || kind == PoseGraphNodeKind::Transition;
}

} // namespace

PoseGraphValidationResult ValidatePoseGraph(const PoseGraph& tree) noexcept {
    if (tree.nodes.empty()) {
        return {PoseGraphValidationCode::EmptyTree, 0U, "PoseGraph requires at least one node."};
    }
    if (tree.nodes.size() > PoseGraph::kMaximumNodes) {
        return {PoseGraphValidationCode::CapacityExceeded, 0U,
                "PoseGraph node count exceeds the bounded limit."};
    }
    std::unordered_set<std::uint32_t> nodeIds;
    nodeIds.reserve(tree.nodes.size());
    for (const PoseGraphNode& node : tree.nodes) {
        if (node.id == 0U || node.name.empty() || node.name.size() > kMaximumIdentifierBytes ||
            !std::isfinite(node.weight) || node.weight < 0.0F || node.weight > 1.0F ||
            !std::isfinite(node.timeScale) || node.timeScale < 0.0F) {
            return {PoseGraphValidationCode::InvalidNode, node.id,
                    "PoseGraph node identity or bounded numeric configuration is invalid."};
        }
        if (!nodeIds.insert(node.id).second) {
            return {PoseGraphValidationCode::DuplicateNode, node.id,
                    "PoseGraph node identifiers must be unique."};
        }
    }
    for (const AnimationClip& clip : tree.clips) {
        const AnimationClipValidationResult clipResult = ValidateAnimationClip(clip);
        if (!clipResult.IsValid()) {
            return {PoseGraphValidationCode::InvalidClip, 0U,
                    "PoseGraph contains an invalid AnimationClip resource."};
        }
    }
    std::size_t outputCount = 0U;
    for (const PoseGraphNode& node : tree.nodes) {
        if (node.kind == PoseGraphNodeKind::ClipPlayer &&
            (node.clipId.empty() || FindClip(tree, node.clipId) == nullptr)) {
            return {PoseGraphValidationCode::UnknownClip, node.id,
                    "PoseGraph ClipPlayer references an unknown clip."};
        }
        if (node.kind == PoseGraphNodeKind::Parameter &&
            (node.parameterId.empty() || node.parameterId.size() > kMaximumIdentifierBytes)) {
            return {PoseGraphValidationCode::InvalidParameter, node.id,
                    "PoseGraph Parameter requires a bounded parameter identifier."};
        }
        if (node.kind == PoseGraphNodeKind::OutputPose) {
            ++outputCount;
        }
        if (UsesInputA(node.kind) && node.inputA == 0U) {
            return {PoseGraphValidationCode::InvalidNode, node.id,
                    "PoseGraph node requires inputA."};
        }
        if (UsesInputB(node.kind) && node.inputB == 0U) {
            return {PoseGraphValidationCode::InvalidNode, node.id,
                    "PoseGraph node requires inputB."};
        }
        if (node.inputA != 0U && FindNode(tree, node.inputA) == nullptr) {
            return {PoseGraphValidationCode::UnknownInput, node.id,
                    "PoseGraph inputA references an unknown node."};
        }
        if (node.inputB != 0U && FindNode(tree, node.inputB) == nullptr) {
            return {PoseGraphValidationCode::UnknownInput, node.id,
                    "PoseGraph inputB references an unknown node."};
        }
    }
    if (outputCount == 0U) {
        return {PoseGraphValidationCode::MissingOutput, 0U,
                "PoseGraph requires an OutputPose node."};
    }

    std::unordered_map<std::uint32_t, std::uint8_t> visitState;
    visitState.reserve(tree.nodes.size());
    const std::function<bool(const PoseGraphNode&)> visit = [&](const PoseGraphNode& node) {
        const std::uint8_t state = visitState[node.id];
        if (state == 1U) {
            return false;
        }
        if (state == 2U) {
            return true;
        }
        visitState[node.id] = 1U;
        if ((node.inputA != 0U && !visit(*FindNode(tree, node.inputA))) ||
            (node.inputB != 0U && !visit(*FindNode(tree, node.inputB)))) {
            return false;
        }
        visitState[node.id] = 2U;
        return true;
    };
    for (const PoseGraphNode& node : tree.nodes) {
        if (!visit(node)) {
            return {PoseGraphValidationCode::CycleDetected, node.id,
                    "PoseGraph node inputs must be acyclic."};
        }
    }
    return {PoseGraphValidationCode::Valid, 0U, "PoseGraph is valid."};
}

PoseGraphEvaluationResult EvaluatePoseGraph(
    const PoseGraph& tree, const double timeSeconds,
    const std::vector<PoseGraphParameter>& parameters) {
    PoseGraphEvaluationResult result;
    if (!ValidatePoseGraph(tree).IsValid() || !std::isfinite(timeSeconds)) {
        result.message = "PoseGraph evaluation rejected an invalid tree or time.";
        return result;
    }
    const auto output = std::find_if(tree.nodes.cbegin(), tree.nodes.cend(), [](const auto& node) {
        return node.kind == PoseGraphNodeKind::OutputPose;
    });
    if (output == tree.nodes.cend()) {
        result.message = "PoseGraph has no output node.";
        return result;
    }
    std::unordered_map<PoseGraphCacheKey, TransformPose, PoseGraphCacheKeyHash> cache;
    std::unordered_set<std::uint32_t> evaluating;
    std::function<bool(const PoseGraphNode&, double, TransformPose&)> evaluate =
        [&](const PoseGraphNode& node, const double localTime, TransformPose& outPose) {
            const PoseGraphCacheKey cacheKey{node.id, localTime};
            if (const auto cached = cache.find(cacheKey); cached != cache.end()) {
                outPose = cached->second;
                return true;
            }
            if (!evaluating.insert(node.id).second) {
                return false;
            }
            bool success = true;
            switch (node.kind) {
                case PoseGraphNodeKind::ClipPlayer: {
                    const AnimationClip* clip = FindClip(tree, node.clipId);
                    success = clip != nullptr && TrySampleAnimationClip(*clip, localTime, true, outPose);
                    break;
                }
                case PoseGraphNodeKind::Blend: {
                    TransformPose left;
                    TransformPose right;
                    success = evaluate(*FindNode(tree, node.inputA), localTime, left) &&
                              evaluate(*FindNode(tree, node.inputB), localTime, right);
                    if (success) {
                        outPose = BlendPose(left, right, node.weight);
                    }
                    break;
                }
                case PoseGraphNodeKind::Transition: {
                    const float parameter = FindParameterValue(parameters, node.parameterId);
                    const PoseGraphNode* selected = parameter > 0.5F
                        ? FindNode(tree, node.inputB) : FindNode(tree, node.inputA);
                    success = selected != nullptr && evaluate(*selected, localTime, outPose);
                    break;
                }
                case PoseGraphNodeKind::TimeScale: {
                    const double scaledTime = localTime * static_cast<double>(node.timeScale);
                    success = std::isfinite(scaledTime) &&
                              evaluate(*FindNode(tree, node.inputA), scaledTime, outPose);
                    break;
                }
                case PoseGraphNodeKind::Parameter:
                case PoseGraphNodeKind::State:
                case PoseGraphNodeKind::OneShot:
                case PoseGraphNodeKind::Sync:
                case PoseGraphNodeKind::Subtree:
                case PoseGraphNodeKind::PoseCache:
                case PoseGraphNodeKind::OutputPose:
                    success = evaluate(*FindNode(tree, node.inputA), localTime, outPose);
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
