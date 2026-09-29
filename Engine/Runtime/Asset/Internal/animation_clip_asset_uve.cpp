// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/asset/animation_clip_asset_uve.h"

#include <cmath>
#include <exception>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include "uve/asset/uve_file_envelope_uve.h"
#include "uve/logging/logging_macros_uve.h"
#include "uve/math/vector3_uve.h"

namespace UVE::Asset {
namespace {

using JsonUVE = nlohmann::json;
constexpr std::string_view kAnimationSchemaUVE = "uve-animation-v2";
constexpr std::string_view kAnimationSchemaV3UVE = "uve-animation-v3";
constexpr std::string_view kAnimationSchemaV1UVE = "uve-animation-v1";

[[nodiscard]] bool IsFinitePoseUVE(const AnimationAssetPoseUVE& pose) noexcept {
    return Math::IsFiniteUVE(pose.position) && Math::IsFiniteUVE(pose.rotation) &&
           Math::IsFiniteUVE(pose.scale);
}

[[nodiscard]] JsonUVE ToVectorJsonUVE(const Math::Vector3UVE& value) {
    return JsonUVE::array({value.x, value.y, value.z});
}

[[nodiscard]] JsonUVE ToQuaternionJsonUVE(const Math::QuaternionUVE& value) {
    return JsonUVE::array({value.x, value.y, value.z, value.w});
}

[[nodiscard]] bool ReadVectorJsonUVE(const JsonUVE& value, Math::Vector3UVE& outVector) {
    if (!value.is_array() || value.size() != 3U) {
        return false;
    }
    const Math::Vector3UVE candidate{value.at(0).get<float>(), value.at(1).get<float>(), value.at(2).get<float>()};
    if (!Math::IsFiniteUVE(candidate)) {
        return false;
    }
    outVector = candidate;
    return true;
}

[[nodiscard]] bool ReadQuaternionJsonUVE(const JsonUVE& value, Math::QuaternionUVE& outRotation) {
    if (!value.is_array() || value.size() != 4U) {
        return false;
    }
    const Math::QuaternionUVE candidate{value.at(0).get<float>(), value.at(1).get<float>(),
                                       value.at(2).get<float>(), value.at(3).get<float>()};
    Math::QuaternionUVE normalized;
    if (!Math::TryNormalizeUVE(candidate, normalized)) {
        return false;
    }
    outRotation = normalized;
    return true;
}

[[nodiscard]] bool ReadPoseJsonUVE(const JsonUVE& value, AnimationAssetPoseUVE& outPose) {
    if (!value.is_object() || !value.contains("position") || !value.contains("rotation") ||
        !value.contains("scale")) {
        return false;
    }
    AnimationAssetPoseUVE candidate;
    if (!ReadVectorJsonUVE(value.at("position"), candidate.position) ||
        !ReadQuaternionJsonUVE(value.at("rotation"), candidate.rotation) ||
        !ReadVectorJsonUVE(value.at("scale"), candidate.scale)) {
        return false;
    }
    outPose = candidate;
    return true;
}

/// Times sorted, inside [0, duration], poses finite with a usable rotation.
[[nodiscard]] bool AreSamplesValidUVE(const std::vector<AnimationAssetSampleUVE>& samples,
                                      const double durationSeconds) noexcept {
    if (samples.size() > kMaximumAnimationAssetSamplesUVE) {
        return false;
    }
    double previousTime = -std::numeric_limits<double>::infinity();
    for (const AnimationAssetSampleUVE& sample : samples) {
        if (!std::isfinite(sample.timeSeconds) || sample.timeSeconds < 0.0 || sample.timeSeconds > durationSeconds ||
            sample.timeSeconds < previousTime || !IsFinitePoseUVE(sample.pose)) {
            return false;
        }
        Math::QuaternionUVE normalized;
        if (!Math::TryNormalizeUVE(sample.pose.rotation, normalized)) {
            return false;
        }
        previousTime = sample.timeSeconds;
    }
    return true;
}

[[nodiscard]] JsonUVE ToSamplesJsonUVE(const std::vector<AnimationAssetSampleUVE>& samples) {
    JsonUVE result = JsonUVE::array();
    for (const AnimationAssetSampleUVE& sample : samples) {
        result.push_back({{"timeSeconds", sample.timeSeconds},
                          {"pose", {{"position", ToVectorJsonUVE(sample.pose.position)},
                                    {"rotation", ToQuaternionJsonUVE(sample.pose.rotation)},
                                    {"scale", ToVectorJsonUVE(sample.pose.scale)}}}});
    }
    return result;
}

[[nodiscard]] bool ReadSamplesJsonUVE(const JsonUVE& samples, std::vector<AnimationAssetSampleUVE>& outSamples) {
    if (!samples.is_array() || samples.size() > kMaximumAnimationAssetSamplesUVE) {
        return false;
    }
    outSamples.reserve(samples.size());
    for (const JsonUVE& value : samples) {
        if (!value.is_object() || !value.contains("timeSeconds") || !value.contains("pose")) {
            return false;
        }
        AnimationAssetSampleUVE sample;
        sample.timeSeconds = value.at("timeSeconds").get<double>();
        if (!ReadPoseJsonUVE(value.at("pose"), sample.pose)) {
            return false;
        }
        outSamples.push_back(std::move(sample));
    }
    return true;
}

[[nodiscard]] bool IsRestValidUVE(const std::vector<AnimationAssetRestBoneUVE>& rest) noexcept {
    if (rest.size() > kMaximumAnimationAssetBonesUVE) {
        return false;
    }
    for (std::size_t index = 0U; index < rest.size(); ++index) {
        const AnimationAssetRestBoneUVE& bone = rest[index];
        Math::QuaternionUVE normalized;
        if (bone.bone.empty() || bone.bone.size() > kMaximumAnimationAssetIdentifierBytesUVE || bone.bone.contains('\0') ||
            bone.parent < -1 || bone.parent >= static_cast<std::int32_t>(index) || !Math::IsFiniteUVE(bone.position) ||
            !Math::IsFiniteUVE(bone.scale) || !Math::TryNormalizeUVE(bone.rotation, normalized)) {
            return false;
        }
        for (std::size_t other = 0U; other < index; ++other) {
            if (rest[other].bone == bone.bone) {
                return false;
            }
        }
    }
    return true;
}

} // namespace

bool IsAnimationClipAssetValidUVE(const AnimationClipAssetUVE& clip) noexcept {
    if (clip.clipId.empty() || clip.clipId.size() > kMaximumAnimationAssetIdentifierBytesUVE ||
        !std::isfinite(clip.durationSeconds) || clip.durationSeconds <= 0.0 ||
        clip.events.size() > kMaximumAnimationAssetEventsUVE || clip.bones.size() > kMaximumAnimationAssetBonesUVE ||
        !AreSamplesValidUVE(clip.samples, clip.durationSeconds) || !IsRestValidUVE(clip.rest)) {
        return false;
    }
    bool anyBoneSamples = false;
    for (std::size_t index = 0U; index < clip.bones.size(); ++index) {
        const AnimationAssetBoneTrackUVE& track = clip.bones[index];
        if (track.bone.empty() || track.bone.size() > kMaximumAnimationAssetIdentifierBytesUVE ||
            track.bone.contains('\0') || !AreSamplesValidUVE(track.samples, clip.durationSeconds)) {
            return false;
        }
        for (std::size_t other = 0U; other < index; ++other) {
            if (clip.bones[other].bone == track.bone) {
                return false; // one track per bone
            }
        }
        anyBoneSamples = anyBoneSamples || !track.samples.empty();
    }
    if (clip.samples.empty() && !anyBoneSamples) {
        return false; // nothing moves
    }
    double previousTime = -std::numeric_limits<double>::infinity();
    for (const AnimationAssetEventUVE& event : clip.events) {
        if (!std::isfinite(event.timeSeconds) || event.timeSeconds < 0.0 ||
            event.timeSeconds > clip.durationSeconds || event.timeSeconds < previousTime || event.eventId.empty() ||
            event.eventId.size() > kMaximumAnimationAssetIdentifierBytesUVE ||
            event.eventId.contains('\0')) {
            return false;
        }
        previousTime = event.timeSeconds;
    }
    return true;
}

bool SaveAnimationClipAssetUVE(const AnimationClipAssetUVE& clip, const std::filesystem::path& path) {
    if (!IsAnimationClipAssetValidUVE(clip)) {
        UVE_ERROR("AnimationClipAssetUVE: refusing to save invalid clip to {}", path.string());
        return false;
    }
    const bool v3 = !clip.rest.empty() || clip.conformed;
    JsonUVE document{{"schema", v3 ? kAnimationSchemaV3UVE : kAnimationSchemaUVE}, {"clipId", clip.clipId},
                     {"durationSeconds", clip.durationSeconds}, {"samples", JsonUVE::array()},
                     {"events", JsonUVE::array()}, {"bones", JsonUVE::array()}};
    document["samples"] = ToSamplesJsonUVE(clip.samples);
    for (const AnimationAssetBoneTrackUVE& track : clip.bones) {
        document["bones"].push_back({{"bone", track.bone}, {"samples", ToSamplesJsonUVE(track.samples)}});
    }
    if (v3) {
        document["conformed"] = clip.conformed;
        document["rest"] = JsonUVE::array();
        for (const AnimationAssetRestBoneUVE& bone : clip.rest) {
            document["rest"].push_back({{"bone", bone.bone}, {"parent", bone.parent},
                                        {"position", ToVectorJsonUVE(bone.position)},
                                        {"rotation", ToQuaternionJsonUVE(bone.rotation)},
                                        {"scale", ToVectorJsonUVE(bone.scale)}});
        }
    }
    for (const AnimationAssetEventUVE& event : clip.events) {
        document["events"].push_back({{"timeSeconds", event.timeSeconds}, {"eventId", event.eventId}});
    }
    const std::string serialized = document.dump();
    if (serialized.empty() || serialized.size() > kMaximumAnimationAssetPayloadBytesUVE) {
        UVE_ERROR("AnimationClipAssetUVE: serialized payload is empty or oversized for {}", path.string());
        return false;
    }
    const auto* const bytes = reinterpret_cast<const std::byte*>(serialized.data());
    return WriteUveFileUVE(path, AssetKindUVE::Animation, std::vector<std::byte>(bytes, bytes + serialized.size()));
}

bool LoadAnimationClipAssetUVE(const std::filesystem::path& path, AnimationClipAssetUVE& outClip) {
    const std::optional<std::pair<UveFileHeaderUVE, std::vector<std::byte>>> file = ReadUveFileUVE(path);
    if (!file.has_value() || file->first.assetType != AssetKindUVE::Animation || file->second.empty() ||
        file->second.size() > kMaximumAnimationAssetPayloadBytesUVE) {
        UVE_ERROR("AnimationClipAssetUVE: invalid animation envelope {}", path.string());
        return false;
    }
    try {
        const std::string serialized(reinterpret_cast<const char*>(file->second.data()), file->second.size());
        const JsonUVE document = JsonUVE::parse(serialized);
        const std::string schema = document.is_object() ? document.value("schema", std::string{}) : std::string{};
        if (!document.is_object() || (schema != kAnimationSchemaV3UVE && schema != kAnimationSchemaUVE && schema != kAnimationSchemaV1UVE) ||
            !document.contains("clipId") || !document.contains("durationSeconds") ||
            !document.contains("samples") || !document.contains("events")) {
            return false;
        }
        AnimationClipAssetUVE candidate;
        candidate.clipId = document.at("clipId").get<std::string>();
        candidate.durationSeconds = document.at("durationSeconds").get<double>();
        const JsonUVE& events = document.at("events");
        if (!events.is_array() || events.size() > kMaximumAnimationAssetEventsUVE ||
            !ReadSamplesJsonUVE(document.at("samples"), candidate.samples)) {
            return false;
        }
        if (document.contains("bones")) {
            const JsonUVE& bones = document.at("bones");
            if (!bones.is_array() || bones.size() > kMaximumAnimationAssetBonesUVE) {
                return false;
            }
            candidate.bones.reserve(bones.size());
            for (const JsonUVE& value : bones) {
                if (!value.is_object() || !value.contains("bone") || !value.contains("samples")) {
                    return false;
                }
                AnimationAssetBoneTrackUVE track;
                track.bone = value.at("bone").get<std::string>();
                if (!ReadSamplesJsonUVE(value.at("samples"), track.samples)) {
                    return false;
                }
                candidate.bones.push_back(std::move(track));
            }
        }
        if (schema == kAnimationSchemaV3UVE) {
            candidate.conformed = document.value("conformed", false);
            const JsonUVE rest = document.value("rest", JsonUVE::array());
            if (!rest.is_array() || rest.size() > kMaximumAnimationAssetBonesUVE) {
                return false;
            }
            for (const JsonUVE& value : rest) {
                if (!value.is_object() || !value.contains("bone") || !value.contains("position") ||
                    !value.contains("rotation")) {
                    return false;
                }
                AnimationAssetRestBoneUVE bone;
                bone.bone = value.at("bone").get<std::string>();
                bone.parent = value.value("parent", -1);
                if (!ReadVectorJsonUVE(value.at("position"), bone.position) ||
                    !ReadQuaternionJsonUVE(value.at("rotation"), bone.rotation) ||
                    (value.contains("scale") && !ReadVectorJsonUVE(value.at("scale"), bone.scale))) {
                    return false;
                }
                candidate.rest.push_back(std::move(bone));
            }
        }
        candidate.events.reserve(events.size());
        for (const JsonUVE& value : events) {
            if (!value.is_object() || !value.contains("timeSeconds") || !value.contains("eventId")) {
                return false;
            }
            candidate.events.push_back(
                AnimationAssetEventUVE{value.at("timeSeconds").get<double>(), value.at("eventId").get<std::string>()});
        }
        if (!IsAnimationClipAssetValidUVE(candidate)) {
            return false;
        }
        outClip = std::move(candidate);
        return true;
    } catch (const std::exception& exception) {
        UVE_ERROR("AnimationClipAssetUVE: failed to parse {}: {}", path.string(), exception.what());
        return false;
    } catch (...) {
        UVE_ERROR("AnimationClipAssetUVE: failed to parse {} with an unknown exception", path.string());
        return false;
    }
}

} // namespace UVE::Asset
