// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/animation/pose_graph_uve.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <unordered_map>
#include <unordered_set>

namespace UVE::Core {
namespace {

constexpr std::size_t kMaximumIdentifierBytesUVE = 128U;

struct PoseGraphCacheKeyUVE final {
    std::uint32_t objectId = 0U;
    double localTime = 0.0;

    [[nodiscard]] bool operator==(const PoseGraphCacheKeyUVE&) const noexcept = default;
};

struct PoseGraphCacheKeyHashUVE final {
    [[nodiscard]] std::size_t operator()(const PoseGraphCacheKeyUVE& key) const noexcept {
        const std::size_t objectHash = std::hash<std::uint32_t>{}(key.objectId);
        const std::size_t timeHash = std::hash<double>{}(key.localTime);
        return objectHash ^ (timeHash + static_cast<std::size_t>(0x9e3779b9U) +
                           (objectHash << 6U) + (objectHash >> 2U));
    }
};

[[nodiscard]] const PoseGraphObjectUVE* FindObjectUVE(
    const PoseGraphUVE& tree, const std::uint32_t id) noexcept {
    const auto iterator = std::find_if(tree.objects.cbegin(), tree.objects.cend(), [id](const auto& object) {
        return object.id == id;
    });
    return iterator == tree.objects.cend() ? nullptr : &*iterator;
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

[[nodiscard]] bool UsesInputAUVE(const PoseGraphObjectKindUVE kind) noexcept {
    return kind != PoseGraphObjectKindUVE::ClipPlayer;
}

[[nodiscard]] bool UsesInputBUVE(const PoseGraphObjectKindUVE kind) noexcept {
    return kind == PoseGraphObjectKindUVE::Blend || kind == PoseGraphObjectKindUVE::Transition;
}

} // namespace

PoseGraphValidationResultUVE ValidatePoseGraphUVE(const PoseGraphUVE& tree) noexcept {
    if (tree.objects.empty()) {
        return {PoseGraphValidationCodeUVE::EmptyTree, 0U, "PoseGraph requires at least one object."};
    }
    if (tree.objects.size() > PoseGraphUVE::kMaximumObjectsUVE) {
        return {PoseGraphValidationCodeUVE::CapacityExceeded, 0U,
                "PoseGraph object count exceeds the bounded limit."};
    }
    std::unordered_set<std::uint32_t> objectIds;
    objectIds.reserve(tree.objects.size());
    for (const PoseGraphObjectUVE& object : tree.objects) {
        if (object.id == 0U || object.name.empty() || object.name.size() > kMaximumIdentifierBytesUVE ||
            !std::isfinite(object.weight) || object.weight < 0.0F || object.weight > 1.0F ||
            !std::isfinite(object.timeScale) || object.timeScale < 0.0F) {
            return {PoseGraphValidationCodeUVE::InvalidObject, object.id,
                    "PoseGraph object identity or bounded numeric configuration is invalid."};
        }
        if (!objectIds.insert(object.id).second) {
            return {PoseGraphValidationCodeUVE::DuplicateObject, object.id,
                    "PoseGraph object identifiers must be unique."};
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
    for (const PoseGraphObjectUVE& object : tree.objects) {
        if (object.kind == PoseGraphObjectKindUVE::ClipPlayer &&
            (object.clipId.empty() || FindClipUVE(tree, object.clipId) == nullptr)) {
            return {PoseGraphValidationCodeUVE::UnknownClip, object.id,
                    "PoseGraph ClipPlayer references an unknown clip."};
        }
        if (object.kind == PoseGraphObjectKindUVE::Parameter &&
            (object.parameterId.empty() || object.parameterId.size() > kMaximumIdentifierBytesUVE)) {
            return {PoseGraphValidationCodeUVE::InvalidParameter, object.id,
                    "PoseGraph Parameter requires a bounded parameter identifier."};
        }
        if (object.kind == PoseGraphObjectKindUVE::OutputPose) {
            ++outputCount;
        }
        if (UsesInputAUVE(object.kind) && object.inputA == 0U) {
            return {PoseGraphValidationCodeUVE::InvalidObject, object.id,
                    "PoseGraph object requires inputA."};
        }
        if (UsesInputBUVE(object.kind) && object.inputB == 0U) {
            return {PoseGraphValidationCodeUVE::InvalidObject, object.id,
                    "PoseGraph object requires inputB."};
        }
        if (object.inputA != 0U && FindObjectUVE(tree, object.inputA) == nullptr) {
            return {PoseGraphValidationCodeUVE::UnknownInput, object.id,
                    "PoseGraph inputA references an unknown object."};
        }
        if (object.inputB != 0U && FindObjectUVE(tree, object.inputB) == nullptr) {
            return {PoseGraphValidationCodeUVE::UnknownInput, object.id,
                    "PoseGraph inputB references an unknown object."};
        }
    }
    if (outputCount == 0U) {
        return {PoseGraphValidationCodeUVE::MissingOutput, 0U,
                "PoseGraph requires an OutputPose object."};
    }

    std::unordered_map<std::uint32_t, std::uint8_t> visitState;
    visitState.reserve(tree.objects.size());
    const std::function<bool(const PoseGraphObjectUVE&)> visit = [&](const PoseGraphObjectUVE& object) {
        const std::uint8_t state = visitState[object.id];
        if (state == 1U) {
            return false;
        }
        if (state == 2U) {
            return true;
        }
        visitState[object.id] = 1U;
        if ((object.inputA != 0U && !visit(*FindObjectUVE(tree, object.inputA))) ||
            (object.inputB != 0U && !visit(*FindObjectUVE(tree, object.inputB)))) {
            return false;
        }
        visitState[object.id] = 2U;
        return true;
    };
    for (const PoseGraphObjectUVE& object : tree.objects) {
        if (!visit(object)) {
            return {PoseGraphValidationCodeUVE::CycleDetected, object.id,
                    "PoseGraph object inputs must be acyclic."};
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
    const auto output = std::find_if(tree.objects.cbegin(), tree.objects.cend(), [](const auto& object) {
        return object.kind == PoseGraphObjectKindUVE::OutputPose;
    });
    if (output == tree.objects.cend()) {
        result.message = "PoseGraph has no output object.";
        return result;
    }
    std::unordered_map<PoseGraphCacheKeyUVE, TransformPoseUVE, PoseGraphCacheKeyHashUVE> cache;
    std::unordered_set<std::uint32_t> evaluating;
    std::function<bool(const PoseGraphObjectUVE&, double, TransformPoseUVE&)> evaluate =
        [&](const PoseGraphObjectUVE& object, const double localTime, TransformPoseUVE& outPose) {
            const PoseGraphCacheKeyUVE cacheKey{object.id, localTime};
            if (const auto cached = cache.find(cacheKey); cached != cache.end()) {
                outPose = cached->second;
                return true;
            }
            if (!evaluating.insert(object.id).second) {
                return false;
            }
            bool success = true;
            switch (object.kind) {
                case PoseGraphObjectKindUVE::ClipPlayer: {
                    const AnimationClipUVE* clip = FindClipUVE(tree, object.clipId);
                    success = clip != nullptr && TrySampleAnimationClipUVE(*clip, localTime, true, outPose);
                    break;
                }
                case PoseGraphObjectKindUVE::Blend: {
                    TransformPoseUVE left;
                    TransformPoseUVE right;
                    success = evaluate(*FindObjectUVE(tree, object.inputA), localTime, left) &&
                              evaluate(*FindObjectUVE(tree, object.inputB), localTime, right);
                    if (success) {
                        outPose = BlendPoseUVE(left, right, object.weight);
                    }
                    break;
                }
                case PoseGraphObjectKindUVE::Transition: {
                    const float parameter = FindParameterValueUVE(parameters, object.parameterId);
                    const PoseGraphObjectUVE* selected = parameter > 0.5F
                        ? FindObjectUVE(tree, object.inputB) : FindObjectUVE(tree, object.inputA);
                    success = selected != nullptr && evaluate(*selected, localTime, outPose);
                    break;
                }
                case PoseGraphObjectKindUVE::TimeScale: {
                    const double scaledTime = localTime * static_cast<double>(object.timeScale);
                    success = std::isfinite(scaledTime) &&
                              evaluate(*FindObjectUVE(tree, object.inputA), scaledTime, outPose);
                    break;
                }
                case PoseGraphObjectKindUVE::Parameter:
                case PoseGraphObjectKindUVE::State:
                case PoseGraphObjectKindUVE::OneShot:
                case PoseGraphObjectKindUVE::Sync:
                case PoseGraphObjectKindUVE::Subtree:
                case PoseGraphObjectKindUVE::PoseCache:
                case PoseGraphObjectKindUVE::OutputPose:
                    success = evaluate(*FindObjectUVE(tree, object.inputA), localTime, outPose);
                    break;
            }
            evaluating.erase(object.id);
            if (success) {
                cache.emplace(cacheKey, outPose);
                ++result.evaluatedObjectCount;
            }
            return success;
        };
    result.usedOutputObject = evaluate(*output, timeSeconds, result.pose);
    result.message = result.usedOutputObject ? "PoseGraph evaluated successfully." :
                                             "PoseGraph evaluation failed.";
    return result;
}

} // namespace UVE::Core
