// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/asset/gltf_animation_converter_uve.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <set>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include "uve/asset/gltf_skeleton_uve.h"
#include "uve/asset/mesh_asset_uve.h"
#include "uve/math/quaternion_uve.h"
#include "uve/math/vector3_uve.h"

#include "gltf_document_uve.h"

namespace UVE::Asset {
namespace {

/// How many nodes this parser will name. Node names exist only for weights channels, and naming
/// runs over the whole array so one node keeps one track name across every animation of a file; a
/// hostile document is bounded before that pass rather than after it.
inline constexpr std::size_t kMaximumNamedNodesUVE = 65536U;

/// One channel decoded to CPU keys: rotation fills `quatValues`, translation and scale fill
/// `vecValues`, and weights fill `weightValues` flat, key `i` at `[i * morphTargetCount, ...)`.
struct ChannelKeysUVE final {
    std::string bone;
    std::string node;
    std::size_t morphTargetCount = 0U;
    GltfAnimationPathUVE path = GltfAnimationPathUVE::Translation;
    GltfAnimationInterpolationUVE interpolation = GltfAnimationInterpolationUVE::Linear;
    std::vector<float> times;
    std::vector<Math::Vector3UVE> vecValues;
    std::vector<Math::QuaternionUVE> quatValues;
    std::vector<float> weightValues;
};

/// Key times shared by every path: float SCALAR, bounded, finite, non-negative and strictly
/// increasing. Extracted so the transform and weights decoders cannot disagree about what a valid
/// time stream is.
[[nodiscard]] bool ReadChannelTimesUVE(const GltfAnimationChannelSourceUVE& channel,
                                       std::vector<float>& outTimes) {
    if (channel.times.componentType != GltfComponentTypeUVE::Float ||
        !Detail::ValidateGltfAccessorViewUVE(channel.times, 4U, kMaximumAnimationAssetSamplesUVE)) {
        return false;
    }
    const std::size_t count = static_cast<std::size_t>(channel.times.elementCount);
    std::vector<float> times;
    times.reserve(count);
    float previousTime = -std::numeric_limits<float>::infinity();
    for (std::size_t index = 0U; index < count; ++index) {
        const std::byte* const timeBytes = Detail::GltfAccessorElementUVE(channel.times, index, 4U);
        if (timeBytes == nullptr) {
            return false;
        }
        const float time = Detail::ReadGltfFloatLEUVE(timeBytes);
        if (!std::isfinite(time) || time < 0.0F || time <= previousTime) {
            return false;
        }
        previousTime = time;
        times.push_back(time);
    }
    outTimes = std::move(times);
    return true;
}

/// A translation/rotation/scale channel: one VEC3 or VEC4 per key time.
[[nodiscard]] bool ReadTransformKeysUVE(const GltfAnimationChannelSourceUVE& channel,
                                        ChannelKeysUVE& outKeys) {
    const std::uint64_t valueSize = channel.path == GltfAnimationPathUVE::Rotation ? 16U : 12U;
    if (channel.values.componentType != GltfComponentTypeUVE::Float ||
        !Detail::ValidateGltfAccessorViewUVE(channel.values, valueSize,
                                             kMaximumAnimationAssetSamplesUVE) ||
        channel.values.elementCount != channel.times.elementCount) {
        return false;
    }
    std::vector<float> times;
    if (!ReadChannelTimesUVE(channel, times)) {
        return false;
    }
    ChannelKeysUVE keys;
    keys.bone = channel.bone;
    keys.path = channel.path;
    keys.interpolation = channel.interpolation;
    for (std::size_t index = 0U; index < times.size(); ++index) {
        const std::byte* const valueBytes = Detail::GltfAccessorElementUVE(channel.values, index, valueSize);
        if (valueBytes == nullptr) {
            return false;
        }
        if (channel.path == GltfAnimationPathUVE::Rotation) {
            const Math::QuaternionUVE value{Detail::ReadGltfFloatLEUVE(valueBytes),
                                            Detail::ReadGltfFloatLEUVE(valueBytes + 4U),
                                            Detail::ReadGltfFloatLEUVE(valueBytes + 8U),
                                            Detail::ReadGltfFloatLEUVE(valueBytes + 12U)};
            Math::QuaternionUVE normalized;
            if (!Math::IsFiniteUVE(value) || !Math::TryNormalizeUVE(value, normalized)) {
                return false;
            }
            keys.quatValues.push_back(normalized);
        } else {
            const Math::Vector3UVE value{Detail::ReadGltfFloatLEUVE(valueBytes),
                                         Detail::ReadGltfFloatLEUVE(valueBytes + 4U),
                                         Detail::ReadGltfFloatLEUVE(valueBytes + 8U)};
            if (!Math::IsFiniteUVE(value)) {
                return false;
            }
            keys.vecValues.push_back(value);
        }
    }
    keys.times = std::move(times);
    outKeys = std::move(keys);
    return true;
}

/// A weights channel: `morphTargetCount` floats per key, end to end, exactly the layout the
/// specification defines for a SCALAR weights output.
///
/// STEP is rejected rather than baked. A clip's morph track interpolates linearly by definition
/// (there is no per-track mode in the format), so honouring a step curve would mean inventing
/// duplicate keys at every boundary - and silently lerping instead would change the performance the
/// animator authored. A loud failure is the only honest answer; every DCC exporter that writes
/// facial keys writes LINEAR.
[[nodiscard]] bool ReadWeightKeysUVE(const GltfAnimationChannelSourceUVE& channel,
                                     ChannelKeysUVE& outKeys) {
    if (channel.interpolation != GltfAnimationInterpolationUVE::Linear ||
        channel.morphTargetCount == 0U || channel.morphTargetCount > kMaximumMeshMorphTargetsUVE) {
        return false;
    }
    // Bounded by the product, not by the sample cap: one key legitimately holds one float per
    // shape, so a 52-shape take uses 52x the elements a single float track would.
    const std::uint64_t maximumValues = static_cast<std::uint64_t>(kMaximumAnimationAssetSamplesUVE) *
                                        static_cast<std::uint64_t>(channel.morphTargetCount);
    if (channel.values.componentType != GltfComponentTypeUVE::Float ||
        !Detail::ValidateGltfAccessorViewUVE(channel.values, 4U, maximumValues)) {
        return false;
    }
    std::vector<float> times;
    if (!ReadChannelTimesUVE(channel, times)) {
        return false;
    }
    if (channel.values.elementCount !=
        static_cast<std::uint64_t>(times.size()) * static_cast<std::uint64_t>(channel.morphTargetCount)) {
        return false;
    }
    ChannelKeysUVE keys;
    keys.node = channel.node;
    keys.morphTargetCount = channel.morphTargetCount;
    keys.path = GltfAnimationPathUVE::Weights;
    keys.interpolation = channel.interpolation;
    keys.weightValues.reserve(times.size() * channel.morphTargetCount);
    for (std::size_t key = 0U; key < times.size(); ++key) {
        for (std::size_t target = 0U; target < channel.morphTargetCount; ++target) {
            const std::byte* const weightBytes =
                Detail::GltfAccessorElementUVE(channel.values, key * channel.morphTargetCount + target, 4U);
            if (weightBytes == nullptr) {
                return false;
            }
            const float weight = Detail::ReadGltfFloatLEUVE(weightBytes);
            if (!std::isfinite(weight)) {
                return false;
            }
            keys.weightValues.push_back(weight);
        }
    }
    keys.times = std::move(times);
    outKeys = std::move(keys);
    return true;
}

[[nodiscard]] bool ReadChannelKeysUVE(const GltfAnimationChannelSourceUVE& channel,
                                      ChannelKeysUVE& outKeys) {
    return channel.path == GltfAnimationPathUVE::Weights ? ReadWeightKeysUVE(channel, outKeys)
                                                         : ReadTransformKeysUVE(channel, outKeys);
}

/// Segment search shared by both evaluators: `times` is strictly increasing and non-empty, and
/// `time` is strictly between its ends, so the segment below the first key above `time` always
/// exists. Returns the segment end index, or 0 when `time` is exactly a key (value at `outIndex`).
[[nodiscard]] std::size_t FindKeySegmentUVE(const std::vector<float>& times, const float time,
                                            std::size_t& outIndex) {
    const auto above = std::upper_bound(times.begin(), times.end(), time);
    const std::size_t i = static_cast<std::size_t>(above - times.begin());
    if (times[i - 1U] == time) {
        outIndex = i - 1U;
        return 0U;
    }
    outIndex = i;
    return i;
}

[[nodiscard]] bool TryEvaluateVec3UVE(const ChannelKeysUVE& keys, const float time,
                                      Math::Vector3UVE& outValue) {
    if (time <= keys.times.front()) {
        outValue = keys.vecValues.front();
        return true;
    }
    if (time >= keys.times.back()) {
        outValue = keys.vecValues.back();
        return true;
    }
    std::size_t index = 0U;
    if (FindKeySegmentUVE(keys.times, time, index) == 0U) {
        outValue = keys.vecValues[index];
        return true;
    }
    if (keys.interpolation == GltfAnimationInterpolationUVE::Step) {
        outValue = keys.vecValues[index - 1U];
        return true;
    }
    const float alpha = (time - keys.times[index - 1U]) / (keys.times[index] - keys.times[index - 1U]);
    outValue = keys.vecValues[index - 1U] + (keys.vecValues[index] - keys.vecValues[index - 1U]) * alpha;
    return Math::IsFiniteUVE(outValue);
}

[[nodiscard]] bool TryEvaluateQuatUVE(const ChannelKeysUVE& keys, const float time,
                                      Math::QuaternionUVE& outValue) {
    if (time <= keys.times.front()) {
        outValue = keys.quatValues.front();
        return true;
    }
    if (time >= keys.times.back()) {
        outValue = keys.quatValues.back();
        return true;
    }
    std::size_t index = 0U;
    if (FindKeySegmentUVE(keys.times, time, index) == 0U) {
        outValue = keys.quatValues[index];
        return true;
    }
    if (keys.interpolation == GltfAnimationInterpolationUVE::Step) {
        outValue = keys.quatValues[index - 1U];
        return true;
    }
    const float alpha = (time - keys.times[index - 1U]) / (keys.times[index] - keys.times[index - 1U]);
    return Math::TrySlerpUVE(keys.quatValues[index - 1U], keys.quatValues[index], alpha, outValue);
}

[[nodiscard]] std::optional<GltfAnimationInterpolationUVE> ParseInterpolationUVE(
    const nlohmann::json& sampler) {
    const auto it = sampler.find("interpolation");
    if (it == sampler.end()) {
        return GltfAnimationInterpolationUVE::Linear;
    }
    if (!it->is_string()) {
        return std::nullopt;
    }
    const std::string& name = it->get_ref<const std::string&>();
    if (name == "LINEAR") {
        return GltfAnimationInterpolationUVE::Linear;
    }
    if (name == "STEP") {
        return GltfAnimationInterpolationUVE::Step;
    }
    return std::nullopt;
}

[[nodiscard]] bool ReadUintFieldUVE(const nlohmann::json& object, const char* key, std::size_t& outValue) {
    const auto it = object.find(key);
    if (it == object.end() || !it->is_number_unsigned()) {
        return false;
    }
    outValue = it->get<std::size_t>();
    return true;
}

/// Deterministic names for the WHOLE node array, in node order: `name`, else "Node", uniquified
/// with the same `_N` suffix the skeleton applies to bones. The whole-array pass (rather than one
/// per animation) is what makes a node resolve to ONE name across every animation of a file: the
/// runtime addresses a morph track by that name, so a name that shifted per animation would make
/// half a character's takes drive the wrong mesh.
[[nodiscard]] std::vector<std::string> BuildNodeNamesUVE(const nlohmann::json& nodes) {
    std::vector<std::string> names;
    names.reserve(nodes.size());
    std::unordered_set<std::string> used;
    for (const nlohmann::json& node : nodes) {
        std::string name = "Node";
        if (node.is_object()) {
            const auto nameIt = node.find("name");
            if (nameIt != node.end() && nameIt->is_string()) {
                name = nameIt->get<std::string>();
            }
        }
        if (name.empty()) {
            name = "Node";
        }
        std::string unique = name;
        for (std::size_t suffix = 1U; used.count(unique) != 0U; ++suffix) {
            unique = name + "_" + std::to_string(suffix);
        }
        used.insert(unique);
        names.push_back(std::move(unique));
    }
    return names;
}

/// The morph-target count of the mesh a node instances, or nullopt when the node has no mesh, the
/// mesh index is out of range, a primitive is malformed, or the primitives disagree. glTF requires
/// every primitive of a mesh to declare the same number of targets, because one weights channel
/// drives all of them; a mesh that disagrees cannot be steered by one track.
[[nodiscard]] std::optional<std::size_t> ReadMeshMorphTargetCountUVE(const nlohmann::json& document,
                                                                    const std::size_t nodeIndex) {
    const auto nodesIt = document.find("nodes");
    if (nodesIt == document.end() || !nodesIt->is_array() || nodeIndex >= nodesIt->size() ||
        !nodesIt->at(nodeIndex).is_object()) {
        return std::nullopt;
    }
    const auto meshIndex = Detail::ReadJsonUintUVE(nodesIt->at(nodeIndex), "mesh", true);
    const auto meshesIt = document.find("meshes");
    if (!meshIndex.has_value() || meshesIt == document.end() || !meshesIt->is_array() ||
        *meshIndex >= meshesIt->size() || !meshesIt->at(*meshIndex).is_object()) {
        return std::nullopt;
    }
    const nlohmann::json& mesh = meshesIt->at(*meshIndex);
    const auto primitivesIt = mesh.find("primitives");
    if (primitivesIt == mesh.end() || !primitivesIt->is_array() || primitivesIt->empty()) {
        return std::nullopt;
    }
    std::size_t targetCount = 0U;
    bool first = true;
    for (const nlohmann::json& primitive : *primitivesIt) {
        if (!primitive.is_object()) {
            return std::nullopt;
        }
        std::size_t count = 0U;
        const auto targetsIt = primitive.find("targets");
        if (targetsIt != primitive.end()) {
            if (!targetsIt->is_array()) {
                return std::nullopt;
            }
            count = targetsIt->size();
        }
        if (first) {
            targetCount = count;
            first = false;
        } else if (count != targetCount) {
            return std::nullopt;
        }
    }
    if (targetCount == 0U) {
        return std::nullopt;
    }
    return targetCount;
}

} // namespace

bool ConvertGltfAnimationUVE(const GltfAnimationSourceUVE& source, const SkeletonAssetUVE& skeleton,
                             AnimationClipAssetUVE& outClip) {
    const bool hasTransformChannels =
        std::any_of(source.channels.begin(), source.channels.end(),
                    [](const GltfAnimationChannelSourceUVE& channel) {
                        return channel.path != GltfAnimationPathUVE::Weights;
                    });
    // A lip-sync take exported from a bare head has no skin, and the clip format has carried
    // bone-free clips since weight tracks landed; only a channel that names a bone needs a skeleton
    // to resolve it against, and a caller that passes one anyway must still pass a valid one.
    if (skeleton.joints.empty() ? hasTransformChannels : !IsSkeletonAssetValidUVE(skeleton)) {
        return false;
    }
    if (source.clipId.empty() || source.clipId.size() > kMaximumAnimationAssetIdentifierBytesUVE ||
        source.channels.empty() ||
        source.channels.size() >
            kMaximumAnimationAssetBonesUVE * 3U + kMaximumAnimationAssetMorphTracksUVE) {
        return false;
    }
    // A valid input holds at most one channel per target per path, so anything past bones*3 paths
    // (plus one weights channel per morph track) is a duplicate the set below rejects - the cap only
    // bounds the decode work up front.
    std::unordered_map<std::string_view, std::size_t> boneToRest;
    boneToRest.reserve(skeleton.joints.size() * 2U);
    for (std::size_t i = 0U; i < skeleton.joints.size(); ++i) {
        boneToRest.emplace(skeleton.joints[i].name, i);
    }
    std::set<std::pair<std::string_view, GltfAnimationPathUVE>> seenPaths;
    std::size_t morphTrackCount = 0U;
    std::vector<ChannelKeysUVE> keys;
    keys.reserve(source.channels.size());
    float duration = 0.0F;
    for (const GltfAnimationChannelSourceUVE& channel : source.channels) {
        const bool isWeights = channel.path == GltfAnimationPathUVE::Weights;
        const std::string& target = isWeights ? channel.node : channel.bone;
        // Both identities are mutually exclusive by path: a weights channel that also names a bone
        // (or a transform channel that also names a node) is a caller that mixed the two up.
        const bool mixedIdentity = isWeights ? !channel.bone.empty() : channel.morphTargetCount != 0U;
        if (target.empty() || target.size() > kMaximumAnimationAssetIdentifierBytesUVE ||
            target.contains('\0') || mixedIdentity ||
            (!isWeights && !boneToRest.contains(std::string_view{target})) ||
            !seenPaths.emplace(std::string_view{target}, channel.path).second) {
            return false;
        }
        if (isWeights && ++morphTrackCount > kMaximumAnimationAssetMorphTracksUVE) {
            return false;
        }
        ChannelKeysUVE decoded;
        if (!ReadChannelKeysUVE(channel, decoded)) {
            return false;
        }
        duration = std::max(duration, decoded.times.back());
        keys.push_back(std::move(decoded));
    }
    if (!(duration > 0.0F)) {
        return false;
    }

    AnimationClipAssetUVE clip;
    clip.clipId = source.clipId;
    clip.durationSeconds = static_cast<double>(duration);
    clip.rest.reserve(skeleton.joints.size());
    for (const SkeletonJointUVE& joint : skeleton.joints) {
        AnimationAssetRestBoneUVE rest;
        rest.bone = joint.name;
        rest.parent = joint.parent;
        rest.position = joint.position;
        rest.rotation = joint.rotation;
        rest.scale = joint.scale;
        clip.rest.push_back(std::move(rest));
    }
    for (const SkeletonJointUVE& joint : skeleton.joints) {
        const ChannelKeysUVE* translation = nullptr;
        const ChannelKeysUVE* rotation = nullptr;
        const ChannelKeysUVE* scale = nullptr;
        for (const ChannelKeysUVE& decoded : keys) {
            if (decoded.bone != joint.name) {
                continue;
            }
            switch (decoded.path) {
            case GltfAnimationPathUVE::Translation:
                translation = &decoded;
                break;
            case GltfAnimationPathUVE::Rotation:
                rotation = &decoded;
                break;
            case GltfAnimationPathUVE::Scale:
                scale = &decoded;
                break;
            case GltfAnimationPathUVE::Weights:
                break; // weights never carry a bone, so this cannot be a joint's channel
            }
        }
        if (translation == nullptr && rotation == nullptr && scale == nullptr) {
            continue;
        }
        std::vector<float> unionTimes;
        if (translation != nullptr) {
            unionTimes.insert(unionTimes.end(), translation->times.begin(), translation->times.end());
        }
        if (rotation != nullptr) {
            unionTimes.insert(unionTimes.end(), rotation->times.begin(), rotation->times.end());
        }
        if (scale != nullptr) {
            unionTimes.insert(unionTimes.end(), scale->times.begin(), scale->times.end());
        }
        std::sort(unionTimes.begin(), unionTimes.end());
        unionTimes.erase(std::unique(unionTimes.begin(), unionTimes.end()), unionTimes.end());
        if (unionTimes.size() > kMaximumAnimationAssetSamplesUVE) {
            return false;
        }
        AnimationAssetBoneTrackUVE track;
        track.bone = joint.name;
        track.samples.reserve(unionTimes.size());
        for (const float time : unionTimes) {
            AnimationAssetPoseUVE pose{joint.position, joint.rotation, joint.scale};
            if (translation != nullptr && !TryEvaluateVec3UVE(*translation, time, pose.position)) {
                return false;
            }
            if (rotation != nullptr && !TryEvaluateQuatUVE(*rotation, time, pose.rotation)) {
                return false;
            }
            if (scale != nullptr && !TryEvaluateVec3UVE(*scale, time, pose.scale)) {
                return false;
            }
            track.samples.push_back(AnimationAssetSampleUVE{static_cast<double>(time), pose});
        }
        clip.bones.push_back(std::move(track));
    }
    // Weights tracks are copied, not baked: the file's keys are already what the runtime sampler
    // evaluates, so resampling them onto any other grid could only lose what the animator authored.
    for (const ChannelKeysUVE& decoded : keys) {
        if (decoded.path != GltfAnimationPathUVE::Weights) {
            continue;
        }
        AnimationAssetMorphTrackUVE track;
        track.node = decoded.node;
        track.samples.reserve(decoded.times.size());
        for (std::size_t key = 0U; key < decoded.times.size(); ++key) {
            const auto first =
                decoded.weightValues.begin() +
                static_cast<std::ptrdiff_t>(key * decoded.morphTargetCount);
            AnimationAssetMorphWeightSampleUVE sample;
            sample.timeSeconds = static_cast<double>(decoded.times[key]);
            sample.weights.assign(first, first + static_cast<std::ptrdiff_t>(decoded.morphTargetCount));
            track.samples.push_back(std::move(sample));
        }
        clip.morphWeights.push_back(std::move(track));
    }

    if (!IsAnimationClipAssetValidUVE(clip)) {
        return false;
    }
    outClip = std::move(clip);
    return true;
}

std::optional<GltfAnimationSourceUVE> ParseGltfAnimationSourceUVE(std::string_view json,
                                                                  const std::vector<std::byte>& buffer) {
    nlohmann::json document;
    try {
        document = nlohmann::json::parse(json);
    } catch (const nlohmann::json::exception&) {
        return std::nullopt;
    }
    if (!document.is_object()) {
        return std::nullopt;
    }
    const auto animationsIt = document.find("animations");
    if (animationsIt == document.end() || !animationsIt->is_array() || animationsIt->empty()) {
        return std::nullopt;
    }
    const nlohmann::json& animation = animationsIt->at(0U);
    if (!animation.is_object()) {
        return std::nullopt;
    }
    const auto channelsIt = animation.find("channels");
    const auto samplersIt = animation.find("samplers");
    if (channelsIt == animation.end() || !channelsIt->is_array() || samplersIt == animation.end() ||
        !samplersIt->is_array()) {
        return std::nullopt;
    }
    // The same bound the converter enforces, applied before the decode so an oversized channel list
    // costs nothing: one channel per target per path, plus one weights channel per morph track.
    if (channelsIt->size() >
        kMaximumAnimationAssetBonesUVE * 3U + kMaximumAnimationAssetMorphTracksUVE) {
        return std::nullopt;
    }
    // Channels resolve through the skin's skeleton: a transform channel targeting a node outside it
    // is a non-skeletal track this converter cannot carry. A document with no skin at all is where
    // a lip-sync-only export lands, so that alone is not an error - it becomes one below, when the
    // animation turns out to hold no weights channel either.
    const std::optional<GltfSkeletonUVE> skeleton =
        ParseGltfSkeletonUVE(json, kMaximumSkeletonAssetJointsUVE);
    if (!skeleton.has_value() && document.contains("skins")) {
        return std::nullopt;
    }
    std::unordered_map<std::size_t, std::string_view> nodeToBone;
    if (skeleton.has_value()) {
        nodeToBone.reserve(skeleton->joints.size() * 2U);
        for (const GltfJointUVE& joint : skeleton->joints) {
            nodeToBone.emplace(joint.sourceNodeIndex, std::string_view{joint.name});
        }
    }
    // Node names exist only to name morph tracks, so the whole-array pass runs only when the
    // animation actually carries a weights channel rather than on every skeletal file.
    bool hasWeightsChannel = false;
    for (const nlohmann::json& channel : *channelsIt) {
        if (!channel.is_object()) {
            continue;
        }
        const auto targetIt = channel.find("target");
        if (targetIt == channel.end() || !targetIt->is_object()) {
            continue;
        }
        const auto pathIt = targetIt->find("path");
        if (pathIt != targetIt->end() && pathIt->is_string() &&
            pathIt->get_ref<const std::string&>() == "weights") {
            hasWeightsChannel = true;
            break;
        }
    }
    std::vector<std::string> nodeNames;
    if (hasWeightsChannel) {
        const auto nodesIt = document.find("nodes");
        if (nodesIt == document.end() || !nodesIt->is_array() ||
            nodesIt->size() > kMaximumNamedNodesUVE) {
            return std::nullopt;
        }
        nodeNames = BuildNodeNamesUVE(*nodesIt);
    }

    GltfAnimationSourceUVE source;
    {
        std::string clipId = "clip";
        const auto nameIt = animation.find("name");
        if (nameIt != animation.end()) {
            if (!nameIt->is_string()) {
                return std::nullopt;
            }
            const std::string name = nameIt->get<std::string>();
            if (!name.empty()) {
                clipId = name;
            }
        }
        source.clipId = std::move(clipId);
    }
    bool sawWeightsChannel = false;
    source.channels.reserve(channelsIt->size());
    for (const nlohmann::json& channel : *channelsIt) {
        if (!channel.is_object()) {
            return std::nullopt;
        }
        std::size_t samplerIndex = 0U;
        if (!ReadUintFieldUVE(channel, "sampler", samplerIndex) || samplerIndex >= samplersIt->size()) {
            return std::nullopt;
        }
        const auto targetIt = channel.find("target");
        if (targetIt == channel.end() || !targetIt->is_object()) {
            return std::nullopt;
        }
        std::size_t nodeIndex = 0U;
        if (!ReadUintFieldUVE(*targetIt, "node", nodeIndex)) {
            return std::nullopt;
        }
        const auto pathIt = targetIt->find("path");
        if (pathIt == targetIt->end() || !pathIt->is_string()) {
            return std::nullopt;
        }
        const std::string& pathName = pathIt->get_ref<const std::string&>();
        GltfAnimationChannelSourceUVE sourceChannel;
        const char* expectedType = "VEC3";
        if (pathName == "weights") {
            const std::optional<std::size_t> morphTargetCount =
                ReadMeshMorphTargetCountUVE(document, nodeIndex);
            if (nodeIndex >= nodeNames.size() || !morphTargetCount.has_value() ||
                *morphTargetCount > kMaximumMeshMorphTargetsUVE ||
                nodeNames[nodeIndex].size() > kMaximumAnimationAssetIdentifierBytesUVE ||
                nodeNames[nodeIndex].contains('\0')) {
                return std::nullopt;
            }
            sourceChannel.path = GltfAnimationPathUVE::Weights;
            sourceChannel.node = nodeNames[nodeIndex];
            sourceChannel.morphTargetCount = *morphTargetCount;
            expectedType = "SCALAR";
            sawWeightsChannel = true;
        } else {
            GltfAnimationPathUVE path = GltfAnimationPathUVE::Translation;
            if (pathName == "translation") {
                path = GltfAnimationPathUVE::Translation;
            } else if (pathName == "rotation") {
                path = GltfAnimationPathUVE::Rotation;
                expectedType = "VEC4";
            } else if (pathName == "scale") {
                path = GltfAnimationPathUVE::Scale;
            } else {
                return std::nullopt;
            }
            const auto boneIt = nodeToBone.find(nodeIndex);
            if (boneIt == nodeToBone.end()) {
                return std::nullopt;
            }
            sourceChannel.path = path;
            sourceChannel.bone = std::string{boneIt->second};
        }
        const nlohmann::json& sampler = samplersIt->at(samplerIndex);
        if (!sampler.is_object()) {
            return std::nullopt;
        }
        std::size_t inputIndex = 0U;
        std::size_t outputIndex = 0U;
        if (!ReadUintFieldUVE(sampler, "input", inputIndex) ||
            !ReadUintFieldUVE(sampler, "output", outputIndex)) {
            return std::nullopt;
        }
        const std::optional<GltfAnimationInterpolationUVE> interpolation = ParseInterpolationUVE(sampler);
        if (!interpolation.has_value()) {
            return std::nullopt;
        }
        const std::optional<GltfAccessorViewUVE> times = Detail::BuildAccessorViewUVE(
            document, buffer, static_cast<std::uint64_t>(inputIndex), "SCALAR", false);
        const std::optional<GltfAccessorViewUVE> values = Detail::BuildAccessorViewUVE(
            document, buffer, static_cast<std::uint64_t>(outputIndex), expectedType, false);
        if (!times.has_value() || !values.has_value()) {
            return std::nullopt;
        }
        sourceChannel.interpolation = *interpolation;
        if (sourceChannel.path == GltfAnimationPathUVE::Weights) {
            // Both rejections happen here as well as in the decoder so a file fails at import with
            // the reason visible, not later at conversion with a nameless false.
            if (*interpolation != GltfAnimationInterpolationUVE::Linear) {
                return std::nullopt;
            }
            if (values->elementCount !=
                times->elementCount * static_cast<std::uint64_t>(sourceChannel.morphTargetCount)) {
                return std::nullopt;
            }
        }
        sourceChannel.times = *times;
        sourceChannel.values = *values;
        source.channels.push_back(std::move(sourceChannel));
    }
    if (!skeleton.has_value() && !sawWeightsChannel) {
        return std::nullopt;
    }
    return source;
}

} // namespace UVE::Asset
