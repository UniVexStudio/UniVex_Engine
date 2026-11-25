// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "animation_clip_asset.h"
#include "model_animations.h"

// The morph-track weight cap IS the mesh shape cap: a clip may not carry more weights per key than
// any mesh can have shapes, or the two formats could disagree about what a track means.
#include "mesh_asset.h"

#include <cmath>
#include <exception>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include "uve_file_envelope.h"
#include "logging_macros.h"
#include "vector3.h"

namespace UVE::Asset {
namespace {

using Json = nlohmann::json;
constexpr std::string_view kAnimationSchema = "uve-animation-v2";
constexpr std::string_view kAnimationSchemaV3 = "uve-animation-v3";
/// v4 adds blend-shape weight tracks. Written only when a clip has them, so every clip that does
/// not - which is all of them until facial animation is authored - stays byte-identical to v3.
constexpr std::string_view kAnimationSchemaV4 = "uve-animation-v4";
constexpr std::string_view kAnimationSchemaV1 = "uve-animation-v1";

[[nodiscard]] bool IsFinitePose(const AnimationAssetPose& pose) noexcept {
    return Math::IsFinite(pose.position) && Math::IsFinite(pose.rotation) &&
           Math::IsFinite(pose.scale);
}

[[nodiscard]] Json ToVectorJson(const Math::Vector3& value) {
    return Json::array({value.x, value.y, value.z});
}

[[nodiscard]] Json ToQuaternionJson(const Math::Quaternion& value) {
    return Json::array({value.x, value.y, value.z, value.w});
}

[[nodiscard]] bool ReadVectorJson(const Json& value, Math::Vector3& outVector) {
    if (!value.is_array() || value.size() != 3U) {
        return false;
    }
    const Math::Vector3 candidate{value.at(0).get<float>(), value.at(1).get<float>(), value.at(2).get<float>()};
    if (!Math::IsFinite(candidate)) {
        return false;
    }
    outVector = candidate;
    return true;
}

[[nodiscard]] bool ReadQuaternionJson(const Json& value, Math::Quaternion& outRotation) {
    if (!value.is_array() || value.size() != 4U) {
        return false;
    }
    const Math::Quaternion candidate{value.at(0).get<float>(), value.at(1).get<float>(),
                                       value.at(2).get<float>(), value.at(3).get<float>()};
    Math::Quaternion normalized;
    if (!Math::TryNormalize(candidate, normalized)) {
        return false;
    }
    outRotation = normalized;
    return true;
}

[[nodiscard]] bool ReadPoseJson(const Json& value, AnimationAssetPose& outPose) {
    if (!value.is_object() || !value.contains("position") || !value.contains("rotation") ||
        !value.contains("scale")) {
        return false;
    }
    AnimationAssetPose candidate;
    if (!ReadVectorJson(value.at("position"), candidate.position) ||
        !ReadQuaternionJson(value.at("rotation"), candidate.rotation) ||
        !ReadVectorJson(value.at("scale"), candidate.scale)) {
        return false;
    }
    outPose = candidate;
    return true;
}

/// Times sorted, inside [0, duration], poses finite with a usable rotation.
[[nodiscard]] bool AreSamplesValid(const std::vector<AnimationAssetSample>& samples,
                                      const double durationSeconds) noexcept {
    if (samples.size() > kMaximumAnimationAssetSamples) {
        return false;
    }
    double previousTime = -std::numeric_limits<double>::infinity();
    for (const AnimationAssetSample& sample : samples) {
        if (!std::isfinite(sample.timeSeconds) || sample.timeSeconds < 0.0 || sample.timeSeconds > durationSeconds ||
            sample.timeSeconds < previousTime || !IsFinitePose(sample.pose)) {
            return false;
        }
        Math::Quaternion normalized;
        if (!Math::TryNormalize(sample.pose.rotation, normalized)) {
            return false;
        }
        previousTime = sample.timeSeconds;
    }
    return true;
}

[[nodiscard]] Json ToSamplesJson(const std::vector<AnimationAssetSample>& samples) {
    Json result = Json::array();
    for (const AnimationAssetSample& sample : samples) {
        result.push_back({{"timeSeconds", sample.timeSeconds},
                          {"pose", {{"position", ToVectorJson(sample.pose.position)},
                                    {"rotation", ToQuaternionJson(sample.pose.rotation)},
                                    {"scale", ToVectorJson(sample.pose.scale)}}}});
    }
    return result;
}

[[nodiscard]] bool ReadSamplesJson(const Json& samples, std::vector<AnimationAssetSample>& outSamples) {
    if (!samples.is_array() || samples.size() > kMaximumAnimationAssetSamples) {
        return false;
    }
    outSamples.reserve(samples.size());
    for (const Json& value : samples) {
        if (!value.is_object() || !value.contains("timeSeconds") || !value.contains("pose")) {
            return false;
        }
        AnimationAssetSample sample;
        sample.timeSeconds = value.at("timeSeconds").get<double>();
        if (!ReadPoseJson(value.at("pose"), sample.pose)) {
            return false;
        }
        outSamples.push_back(std::move(sample));
    }
    return true;
}

[[nodiscard]] bool IsRestValid(const std::vector<AnimationAssetRestBone>& rest) noexcept {
    if (rest.size() > kMaximumAnimationAssetBones) {
        return false;
    }
    for (std::size_t index = 0U; index < rest.size(); ++index) {
        const AnimationAssetRestBone& bone = rest[index];
        Math::Quaternion normalized;
        if (bone.bone.empty() || bone.bone.size() > kMaximumAnimationAssetIdentifierBytes || bone.bone.contains('\0') ||
            bone.parent < -1 || bone.parent >= static_cast<std::int32_t>(index) || !Math::IsFinite(bone.position) ||
            !Math::IsFinite(bone.scale) || !Math::TryNormalize(bone.rotation, normalized)) {
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

/// Blend-shape tracks: each names a mesh, carries at least one key, keeps its keys ordered and in
/// range, and uses ONE weight count throughout - a track whose keys disagree about how many shapes
/// it drives cannot be interpolated at all.
[[nodiscard]] bool AreMorphTracksValid(const std::vector<AnimationAssetMorphTrack>& tracks,
                                          const double durationSeconds) noexcept {
    if (tracks.size() > kMaximumAnimationAssetMorphTracks) {
        return false;
    }
    for (std::size_t index = 0U; index < tracks.size(); ++index) {
        const AnimationAssetMorphTrack& track = tracks[index];
        if (track.node.empty() || track.node.size() > kMaximumAnimationAssetIdentifierBytes ||
            track.node.contains('\0') || track.samples.empty()) {
            return false;
        }
        const std::size_t targetCount = track.samples.front().weights.size();
        if (targetCount > kMaximumMeshMorphTargets) {
            return false;
        }
        double previousTime = -std::numeric_limits<double>::infinity();
        for (const AnimationAssetMorphWeightSample& sample : track.samples) {
            if (!std::isfinite(sample.timeSeconds) || sample.timeSeconds < 0.0 ||
                sample.timeSeconds > durationSeconds || sample.timeSeconds < previousTime ||
                sample.weights.size() != targetCount) {
                return false;
            }
            for (const float weight : sample.weights) {
                if (!std::isfinite(weight)) {
                    return false;
                }
            }
            previousTime = sample.timeSeconds;
        }
        for (std::size_t other = 0U; other < index; ++other) {
            if (tracks[other].node == track.node) {
                return false; // one track per mesh
            }
        }
    }
    return true;
}

} // namespace

bool IsAnimationClipAssetValid(const AnimationClipAsset& clip) noexcept {
    if (clip.clipId.empty() || clip.clipId.size() > kMaximumAnimationAssetIdentifierBytes ||
        !std::isfinite(clip.durationSeconds) || clip.durationSeconds <= 0.0 ||
        clip.events.size() > kMaximumAnimationAssetEvents || clip.bones.size() > kMaximumAnimationAssetBones ||
        !AreSamplesValid(clip.samples, clip.durationSeconds) || !IsRestValid(clip.rest) ||
        !AreMorphTracksValid(clip.morphWeights, clip.durationSeconds)) {
        return false;
    }
    bool anyBoneSamples = false;
    for (std::size_t index = 0U; index < clip.bones.size(); ++index) {
        const AnimationAssetBoneTrack& track = clip.bones[index];
        if (track.bone.empty() || track.bone.size() > kMaximumAnimationAssetIdentifierBytes ||
            track.bone.contains('\0') || !AreSamplesValid(track.samples, clip.durationSeconds)) {
            return false;
        }
        for (std::size_t other = 0U; other < index; ++other) {
            if (clip.bones[other].bone == track.bone) {
                return false; // one track per bone
            }
        }
        anyBoneSamples = anyBoneSamples || !track.samples.empty();
    }
    if (clip.objects.size() > kMaximumAnimationAssetBones) {
        return false;
    }
    for (std::size_t index = 0U; index < clip.objects.size(); ++index) {
        const AnimationAssetBoneTrack& track = clip.objects[index];
        if (track.bone.empty() || track.bone.size() > kMaximumAnimationAssetIdentifierBytes ||
            track.bone.contains('\0') || !AreSamplesValid(track.samples, clip.durationSeconds)) {
            return false;
        }
        for (std::size_t other = 0U; other < index; ++other) {
            if (clip.objects[other].bone == track.bone) {
                return false; // one track per object
            }
        }
        anyBoneSamples = anyBoneSamples || !track.samples.empty();
    }
    // A facial clip on a static head legitimately has no object and no bone samples - the shapes
    // are the entire animation - so morph tracks count as movement too.
    if (clip.samples.empty() && !anyBoneSamples && clip.morphWeights.empty()) {
        return false; // nothing moves
    }
    double previousTime = -std::numeric_limits<double>::infinity();
    for (const AnimationAssetEvent& event : clip.events) {
        if (!std::isfinite(event.timeSeconds) || event.timeSeconds < 0.0 ||
            event.timeSeconds > clip.durationSeconds || event.timeSeconds < previousTime || event.eventId.empty() ||
            event.eventId.size() > kMaximumAnimationAssetIdentifierBytes ||
            event.eventId.contains('\0')) {
            return false;
        }
        previousTime = event.timeSeconds;
    }
    return true;
}

bool SerializeAnimationClipPayload(const AnimationClipAsset& clip, std::vector<std::byte>& outPayload) {
    if (!IsAnimationClipAssetValid(clip)) {
        return false;
    }
    // The schema is the union of what the clip needs: v4 only when it carries blend shapes, v3 only
    // when it carries a rest skeleton or has been conformed, v2 otherwise. An older engine reads
    // exactly what it always read.
    const bool v4 = !clip.morphWeights.empty();
    const bool v3 = !clip.rest.empty() || clip.conformed;
    std::string_view schema = kAnimationSchema;
    if (v4) {
        schema = kAnimationSchemaV4;
    } else if (v3) {
        schema = kAnimationSchemaV3;
    }
    Json document{{"schema", std::string{schema}}, {"clipId", clip.clipId},
                     {"durationSeconds", clip.durationSeconds}, {"samples", Json::array()},
                     {"events", Json::array()}, {"bones", Json::array()}};
    document["samples"] = ToSamplesJson(clip.samples);
    for (const AnimationAssetBoneTrack& track : clip.bones) {
        document["bones"].push_back({{"bone", track.bone}, {"samples", ToSamplesJson(track.samples)}});
    }
    if (!clip.objects.empty()) {
        // Only when present: a clip without them stays byte-for-byte what it was.
        document["objects"] = Json::array();
        for (const AnimationAssetBoneTrack& track : clip.objects) {
            document["objects"].push_back({{"object", track.bone}, {"samples", ToSamplesJson(track.samples)}});
        }
    }
    if (v4 || v3) {
        document["conformed"] = clip.conformed;
        document["rest"] = Json::array();
        for (const AnimationAssetRestBone& bone : clip.rest) {
            document["rest"].push_back({{"bone", bone.bone}, {"parent", bone.parent},
                                        {"position", ToVectorJson(bone.position)},
                                        {"rotation", ToQuaternionJson(bone.rotation)},
                                        {"scale", ToVectorJson(bone.scale)}});
        }
    }
    for (const AnimationAssetEvent& event : clip.events) {
        document["events"].push_back({{"timeSeconds", event.timeSeconds}, {"eventId", event.eventId}});
    }
    if (v4) {
        document["morphWeights"] = Json::array();
        for (const AnimationAssetMorphTrack& track : clip.morphWeights) {
            Json trackJson{{"node", track.node}, {"samples", Json::array()}};
            for (const AnimationAssetMorphWeightSample& sample : track.samples) {
                trackJson["samples"].push_back(
                    {{"timeSeconds", sample.timeSeconds}, {"weights", sample.weights}});
            }
            document["morphWeights"].push_back(std::move(trackJson));
        }
    }
    const std::string serialized = document.dump();
    if (serialized.empty() || serialized.size() > kMaximumAnimationAssetPayloadBytes) {
        return false;
    }
    const auto* const bytes = reinterpret_cast<const std::byte*>(serialized.data());
    outPayload.assign(bytes, bytes + serialized.size());
    return true;
}

bool SaveAnimationClipAsset(const AnimationClipAsset& clip, const std::filesystem::path& path) {
    if (!IsAnimationClipAssetValid(clip)) {
        UVE_ERROR("AnimationClipAsset: refusing to save invalid clip to {}", path.string());
        return false;
    }
    // A take kept inside a model is written back into the model, beside its other takes.
    std::filesystem::path model;
    std::string take;
    if (SplitEmbeddedClipPath(path, model, take)) {
        std::vector<AnimationClipAsset> takes;
        std::uint64_t stamp = 0U;
        if (!LoadAllModelAnimations(model, takes, &stamp)) {
            return false;
        }
        bool replaced = false;
        for (AnimationClipAsset& existing : takes) {
            if (existing.clipId == take) {
                existing = clip;
                existing.clipId = take;
                replaced = true;
                break;
            }
        }
        if (!replaced) {
            AnimationClipAsset added = clip;
            added.clipId = take;
            takes.push_back(std::move(added));
        }
        return SaveModelAnimations(model, takes, stamp);
    }
    std::vector<std::byte> payload;
    if (!SerializeAnimationClipPayload(clip, payload)) {
        UVE_ERROR("AnimationClipAsset: serialized payload is empty or oversized for {}", path.string());
        return false;
    }
    return WriteUveFile(path, AssetKind::Animation, payload);
}

namespace {

struct AnimationClipSummarySax final : nlohmann::json_sax<Json> {
    int depth = 0;
    bool rootIsObject = false;
    std::string topKey;
    bool inRestArray = false;
    bool inBonesArray = false;
    bool hasSchema = false;
    bool validSchema = false;
    bool v3OrHigher = false;
    bool hasClipId = false;
    bool hasDuration = false;
    bool hasSamples = false;
    bool hasEvents = false;
    bool conformed = false;
    bool hasRest = false;
    bool hasBones = false;

    bool null() override {
        if (depth == 1) {
            topKey.clear();
        }
        return true;
    }
    bool boolean(const bool val) override {
        if (depth == 1 && topKey == "conformed") {
            conformed = val;
        }
        if (depth == 1) {
            topKey.clear();
        }
        return true;
    }
    bool number_integer(const number_integer_t val) override {
        if (depth == 1 && topKey == "durationSeconds") {
            hasDuration = val >= 0;
        }
        if (depth == 1) {
            topKey.clear();
        }
        return true;
    }
    bool number_unsigned(const number_unsigned_t) override {
        if (depth == 1 && topKey == "durationSeconds") {
            hasDuration = true;
        }
        if (depth == 1) {
            topKey.clear();
        }
        return true;
    }
    bool number_float(const number_float_t val, const string_t&) override {
        if (depth == 1 && topKey == "durationSeconds") {
            hasDuration = std::isfinite(val) && val >= 0.0;
        }
        if (depth == 1) {
            topKey.clear();
        }
        return true;
    }
    bool string(string_t& val) override {
        if (depth == 1) {
            if (topKey == "schema") {
                hasSchema = true;
                validSchema = (val == kAnimationSchemaV4 || val == kAnimationSchemaV3 ||
                               val == kAnimationSchema || val == kAnimationSchemaV1);
                v3OrHigher = (val == kAnimationSchemaV4 || val == kAnimationSchemaV3);
            } else if (topKey == "clipId") {
                hasClipId = !val.empty();
            }
            topKey.clear();
        }
        return true;
    }
    bool binary(binary_t&) override {
        if (depth == 1) {
            topKey.clear();
        }
        return true;
    }
    bool start_object(std::size_t) override {
        if (depth == 0) {
            rootIsObject = true;
        } else if (depth == 2) {
            if (inRestArray) {
                hasRest = true;
            }
            if (inBonesArray) {
                hasBones = true;
            }
        }
        ++depth;
        return true;
    }
    bool end_object() override {
        --depth;
        if (depth == 1) {
            topKey.clear();
        }
        return true;
    }
    bool start_array(std::size_t) override {
        if (depth == 1) {
            if (topKey == "samples") {
                hasSamples = true;
            } else if (topKey == "events") {
                hasEvents = true;
            } else if (topKey == "rest") {
                inRestArray = true;
            } else if (topKey == "bones") {
                inBonesArray = true;
            }
        }
        ++depth;
        return true;
    }
    bool end_array() override {
        --depth;
        if (depth == 1) {
            inRestArray = false;
            inBonesArray = false;
            topKey.clear();
        }
        return true;
    }
    bool key(string_t& val) override {
        if (depth == 1) {
            topKey = val;
        }
        return true;
    }
    bool parse_error(std::size_t, const std::string&, const Json::exception&) override {
        return false;
    }
};

} // namespace

AnimationClipSummary InspectAnimationClipSummary(const std::span<const std::byte> payload) {
    AnimationClipSummary summary;
    if (payload.empty() || payload.size() > kMaximumAnimationAssetPayloadBytes) {
        return summary;
    }
    AnimationClipSummarySax sax;
    const auto* const begin = reinterpret_cast<const char*>(payload.data());
    const auto* const end = begin + payload.size();
    const bool parsed = Json::sax_parse(begin, end, &sax);
    if (!parsed || !sax.rootIsObject || !sax.hasSchema || !sax.validSchema || !sax.hasClipId || !sax.hasDuration ||
        !sax.hasSamples || !sax.hasEvents) {
        return summary;
    }
    summary.valid = true;
    summary.conformed = sax.v3OrHigher && sax.conformed;
    summary.hasRest = sax.v3OrHigher && sax.hasRest;
    summary.hasBones = sax.hasBones;
    return summary;
}

AnimationClipSummary InspectAnimationClipFileSummary(const std::filesystem::path& path) {
    std::filesystem::path model;
    std::string take;
    if (SplitEmbeddedClipPath(path, model, take)) {
        std::vector<std::pair<std::string, AnimationClipSummary>> summaries;
        if (!InspectModelAnimationsSummary(model, summaries)) {
            return {};
        }
        for (const auto& [name, summary] : summaries) {
            if (name == take) {
                return summary;
            }
        }
        return {};
    }
    const std::optional<std::pair<UveFileHeader, std::vector<std::byte>>> file = ReadUveFile(path);
    if (!file.has_value() || file->first.assetType != AssetKind::Animation || file->second.empty() ||
        file->second.size() > kMaximumAnimationAssetPayloadBytes) {
        return {};
    }
    return InspectAnimationClipSummary(file->second);
}

bool LoadAnimationClipAsset(const std::filesystem::path& path, AnimationClipAsset& outClip) {
    // A take kept inside a model ("Hero.uvmodel#Walk") is read from the model.
    std::filesystem::path model;
    std::string take;
    if (SplitEmbeddedClipPath(path, model, take)) {
        return LoadModelAnimation(model, take, outClip);
    }
    const std::optional<std::pair<UveFileHeader, std::vector<std::byte>>> file = ReadUveFile(path);
    if (!file.has_value() || file->first.assetType != AssetKind::Animation || file->second.empty() ||
        file->second.size() > kMaximumAnimationAssetPayloadBytes) {
        UVE_ERROR("AnimationClipAsset: invalid animation envelope {}", path.string());
        return false;
    }
    return ParseAnimationClipPayload(file->second, outClip);
}

bool ParseAnimationClipPayload(const std::vector<std::byte>& payload, AnimationClipAsset& outClip) {
    if (payload.empty() || payload.size() > kMaximumAnimationAssetPayloadBytes) {
        return false;
    }
    try {
        const std::string serialized(reinterpret_cast<const char*>(payload.data()), payload.size());
        const Json document = Json::parse(serialized);
        const std::string schema = document.is_object() ? document.value("schema", std::string{}) : std::string{};
        if (!document.is_object() ||
            (schema != kAnimationSchemaV4 && schema != kAnimationSchemaV3 &&
             schema != kAnimationSchema && schema != kAnimationSchemaV1) ||
            !document.contains("clipId") || !document.contains("durationSeconds") ||
            !document.contains("samples") || !document.contains("events")) {
            return false;
        }
        AnimationClipAsset candidate;
        candidate.clipId = document.at("clipId").get<std::string>();
        candidate.durationSeconds = document.at("durationSeconds").get<double>();
        const Json& events = document.at("events");
        if (!events.is_array() || events.size() > kMaximumAnimationAssetEvents ||
            !ReadSamplesJson(document.at("samples"), candidate.samples)) {
            return false;
        }
        if (document.contains("bones")) {
            const Json& bones = document.at("bones");
            if (!bones.is_array() || bones.size() > kMaximumAnimationAssetBones) {
                return false;
            }
            candidate.bones.reserve(bones.size());
            for (const Json& value : bones) {
                if (!value.is_object() || !value.contains("bone") || !value.contains("samples")) {
                    return false;
                }
                AnimationAssetBoneTrack track;
                track.bone = value.at("bone").get<std::string>();
                if (!ReadSamplesJson(value.at("samples"), track.samples)) {
                    return false;
                }
                candidate.bones.push_back(std::move(track));
            }
        }
        if (document.contains("objects")) {
            const Json& objects = document.at("objects");
            if (!objects.is_array() || objects.size() > kMaximumAnimationAssetBones) {
                return false;
            }
            candidate.objects.reserve(objects.size());
            for (const Json& value : objects) {
                if (!value.is_object() || !value.contains("object") || !value.at("object").is_string() ||
                    !value.contains("samples")) {
                    return false;
                }
                AnimationAssetBoneTrack track;
                track.bone = value.at("object").get<std::string>();
                if (!ReadSamplesJson(value.at("samples"), track.samples)) {
                    return false;
                }
                candidate.objects.push_back(std::move(track));
            }
        }
        if (schema == kAnimationSchemaV3 || schema == kAnimationSchemaV4) {
            candidate.conformed = document.value("conformed", false);
            const Json rest = document.value("rest", Json::array());
            if (!rest.is_array() || rest.size() > kMaximumAnimationAssetBones) {
                return false;
            }
            for (const Json& value : rest) {
                if (!value.is_object() || !value.contains("bone") || !value.contains("position") ||
                    !value.contains("rotation")) {
                    return false;
                }
                AnimationAssetRestBone bone;
                bone.bone = value.at("bone").get<std::string>();
                bone.parent = value.value("parent", -1);
                if (!ReadVectorJson(value.at("position"), bone.position) ||
                    !ReadQuaternionJson(value.at("rotation"), bone.rotation) ||
                    (value.contains("scale") && !ReadVectorJson(value.at("scale"), bone.scale))) {
                    return false;
                }
                candidate.rest.push_back(std::move(bone));
            }
        }
        if (document.contains("morphWeights")) {
            const Json& tracks = document.at("morphWeights");
            if (!tracks.is_array() || tracks.size() > kMaximumAnimationAssetMorphTracks) {
                return false;
            }
            candidate.morphWeights.reserve(tracks.size());
            for (const Json& value : tracks) {
                if (!value.is_object() || !value.contains("node") || !value.at("node").is_string() ||
                    !value.contains("samples") || !value.at("samples").is_array()) {
                    return false;
                }
                AnimationAssetMorphTrack track;
                track.node = value.at("node").get<std::string>();
                const Json& samples = value.at("samples");
                if (samples.size() > kMaximumAnimationAssetSamples) {
                    return false;
                }
                track.samples.reserve(samples.size());
                for (const Json& sampleJson : samples) {
                    if (!sampleJson.is_object() || !sampleJson.contains("timeSeconds") ||
                        !sampleJson.contains("weights") || !sampleJson.at("weights").is_array()) {
                        return false;
                    }
                    AnimationAssetMorphWeightSample sample;
                    sample.timeSeconds = sampleJson.at("timeSeconds").get<double>();
                    const Json& weights = sampleJson.at("weights");
                    if (weights.size() > kMaximumMeshMorphTargets) {
                        return false;
                    }
                    sample.weights.reserve(weights.size());
                    for (const Json& weight : weights) {
                        if (!weight.is_number()) {
                            return false;
                        }
                        sample.weights.push_back(weight.get<float>());
                    }
                    track.samples.push_back(std::move(sample));
                }
                candidate.morphWeights.push_back(std::move(track));
            }
        }
        candidate.events.reserve(events.size());
        for (const Json& value : events) {
            if (!value.is_object() || !value.contains("timeSeconds") || !value.contains("eventId")) {
                return false;
            }
            candidate.events.push_back(
                AnimationAssetEvent{value.at("timeSeconds").get<double>(), value.at("eventId").get<std::string>()});
        }
        if (!IsAnimationClipAssetValid(candidate)) {
            return false;
        }
        outClip = std::move(candidate);
        return true;
    } catch (const std::exception& exception) {
        UVE_ERROR("AnimationClipAsset: failed to parse a clip: {}", exception.what());
        return false;
    } catch (...) {
        UVE_ERROR("AnimationClipAsset: failed to parse a clip with an unknown exception");
        return false;
    }
}

} // namespace UVE::Asset
