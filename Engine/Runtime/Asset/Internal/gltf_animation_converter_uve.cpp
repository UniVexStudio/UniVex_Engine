// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/asset/gltf_animation_converter_uve.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <set>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include "uve/asset/gltf_skeleton_uve.h"
#include "uve/math/quaternion_uve.h"
#include "uve/math/vector3_uve.h"

#include "gltf_document_uve.h"

namespace UVE::Asset {
namespace {

/// One channel decoded to CPU keys: rotation fills `quatValues` (normalized on read), the other
/// paths fill `vecValues`.
struct ChannelKeysUVE final {
    std::string bone;
    GltfAnimationPathUVE path = GltfAnimationPathUVE::Translation;
    GltfAnimationInterpolationUVE interpolation = GltfAnimationInterpolationUVE::Linear;
    std::vector<float> times;
    std::vector<Math::Vector3UVE> vecValues;
    std::vector<Math::QuaternionUVE> quatValues;
};

[[nodiscard]] bool ReadChannelKeysUVE(const GltfAnimationChannelSourceUVE& channel, ChannelKeysUVE& outKeys) {
    if (channel.times.componentType != GltfComponentTypeUVE::Float ||
        !Detail::ValidateGltfAccessorViewUVE(channel.times, 4U, kMaximumAnimationAssetSamplesUVE)) {
        return false;
    }
    const std::uint64_t valueSize = channel.path == GltfAnimationPathUVE::Rotation ? 16U : 12U;
    if (channel.values.componentType != GltfComponentTypeUVE::Float ||
        !Detail::ValidateGltfAccessorViewUVE(channel.values, valueSize,
                                             kMaximumAnimationAssetSamplesUVE) ||
        channel.values.elementCount != channel.times.elementCount) {
        return false;
    }
    ChannelKeysUVE keys;
    keys.bone = channel.bone;
    keys.path = channel.path;
    keys.interpolation = channel.interpolation;
    const std::size_t count = static_cast<std::size_t>(channel.times.elementCount);
    keys.times.reserve(count);
    float previousTime = -std::numeric_limits<float>::infinity();
    for (std::size_t i = 0U; i < count; ++i) {
        const std::byte* const timeBytes = Detail::GltfAccessorElementUVE(channel.times, i, 4U);
        const std::byte* const valueBytes = Detail::GltfAccessorElementUVE(channel.values, i, valueSize);
        if (timeBytes == nullptr || valueBytes == nullptr) {
            return false;
        }
        const float time = Detail::ReadGltfFloatLEUVE(timeBytes);
        if (!std::isfinite(time) || time < 0.0F || time <= previousTime) {
            return false;
        }
        previousTime = time;
        keys.times.push_back(time);
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
    outKeys = std::move(keys);
    return true;
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

} // namespace

bool ConvertGltfAnimationUVE(const GltfAnimationSourceUVE& source, const SkeletonAssetUVE& skeleton,
                             AnimationClipAssetUVE& outClip) {
    if (!IsSkeletonAssetValidUVE(skeleton)) {
        return false;
    }
    if (source.clipId.empty() || source.clipId.size() > kMaximumAnimationAssetIdentifierBytesUVE ||
        source.channels.empty() ||
        source.channels.size() > kMaximumAnimationAssetBonesUVE * 3U) {
        return false;
    }
    // A valid input holds at most one channel per bone per path, so anything past bones*3 paths
    // is a duplicate the set below rejects — the cap only bounds the decode work up front.
    std::unordered_map<std::string_view, std::size_t> boneToRest;
    boneToRest.reserve(skeleton.joints.size() * 2U);
    for (std::size_t i = 0U; i < skeleton.joints.size(); ++i) {
        boneToRest.emplace(skeleton.joints[i].name, i);
    }
    std::set<std::pair<std::string_view, GltfAnimationPathUVE>> seenPaths;
    std::vector<ChannelKeysUVE> keys;
    keys.reserve(source.channels.size());
    float duration = 0.0F;
    for (const GltfAnimationChannelSourceUVE& channel : source.channels) {
        if (channel.bone.empty() || !boneToRest.contains(std::string_view{channel.bone}) ||
            !seenPaths.emplace(std::string_view{channel.bone}, channel.path).second) {
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
    // Channels resolve through the skin's skeleton: a channel targeting a node outside it is a
    // non-skeletal track this converter cannot carry.
    const std::optional<GltfSkeletonUVE> skeleton =
        ParseGltfSkeletonUVE(json, kMaximumSkeletonAssetJointsUVE);
    if (!skeleton.has_value()) {
        return std::nullopt;
    }
    std::unordered_map<std::size_t, std::string_view> nodeToBone;
    nodeToBone.reserve(skeleton->joints.size() * 2U);
    for (const GltfJointUVE& joint : skeleton->joints) {
        nodeToBone.emplace(joint.sourceNodeIndex, std::string_view{joint.name});
    }

    const auto channelsIt = animation.find("channels");
    const auto samplersIt = animation.find("samplers");
    if (channelsIt == animation.end() || !channelsIt->is_array() || samplersIt == animation.end() ||
        !samplersIt->is_array()) {
        return std::nullopt;
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
        const auto boneIt = nodeToBone.find(nodeIndex);
        if (boneIt == nodeToBone.end()) {
            return std::nullopt;
        }
        const auto pathIt = targetIt->find("path");
        if (pathIt == targetIt->end() || !pathIt->is_string()) {
            return std::nullopt;
        }
        const std::string& pathName = pathIt->get_ref<const std::string&>();
        GltfAnimationPathUVE path = GltfAnimationPathUVE::Translation;
        const char* expectedType = "VEC3";
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
        GltfAnimationChannelSourceUVE sourceChannel;
        sourceChannel.bone = std::string{boneIt->second};
        sourceChannel.path = path;
        sourceChannel.interpolation = *interpolation;
        sourceChannel.times = *times;
        sourceChannel.values = *values;
        source.channels.push_back(std::move(sourceChannel));
    }
    return source;
}

} // namespace UVE::Asset
