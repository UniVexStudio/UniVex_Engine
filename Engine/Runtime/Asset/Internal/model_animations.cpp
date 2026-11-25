// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
#include "model_animations.h"

#include "uve_file_envelope.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <span>
#include <utility>

namespace UVE::Asset {
namespace {

// Layout, at the end of a mesh payload:
//   section: u32 tag 'UVAN', u32 version (2), u32 count, u64 source stamp,
//            count x { u32 nameBytes, name, f64 durationSeconds, u32 payloadBytes, clip payload }
//   footer:  u64 offset of the section, u32 tag 'UVAN'
constexpr std::uint32_t kVersion = 2U;
constexpr std::size_t kFooterBytes = sizeof(std::uint64_t) + sizeof(std::uint32_t);

template <class T>
void Append(std::vector<std::byte>& out, const T value) {
    std::byte bytes[sizeof(T)];
    std::memcpy(bytes, &value, sizeof(T));
    out.insert(out.end(), bytes, bytes + sizeof(T));
}

template <class T>
[[nodiscard]] bool Read(const std::vector<std::byte>& in, std::size_t& at, T& value) {
    if (at > in.size() || in.size() - at < sizeof(T)) {
        return false;
    }
    std::memcpy(&value, in.data() + at, sizeof(T));
    at += sizeof(T);
    return true;
}

/// Where the takes section starts in `payload`, or nullopt when it has none.
[[nodiscard]] std::optional<std::size_t> SectionStart(const std::vector<std::byte>& payload) {
    if (payload.size() < kFooterBytes) {
        return std::nullopt;
    }
    std::size_t at = payload.size() - kFooterBytes;
    std::uint64_t offset = 0U;
    std::uint32_t tag = 0U;
    if (!Read(payload, at, offset) || !Read(payload, at, tag) || tag != kModelAnimationSectionTag ||
        offset + sizeof(std::uint32_t) > payload.size() - kFooterBytes) {
        return std::nullopt;
    }
    std::size_t check = static_cast<std::size_t>(offset);
    std::uint32_t head = 0U;
    if (!Read(payload, check, head) || head != kModelAnimationSectionTag) {
        return std::nullopt;
    }
    return static_cast<std::size_t>(offset);
}

struct Take final {
    std::string name;
    double duration = 0.0;
    std::size_t payloadAt = 0U;
    std::uint32_t payloadBytes = 0U;
};

[[nodiscard]] bool ReadTakes(const std::vector<std::byte>& payload, std::vector<Take>& out, std::uint64_t* stamp = nullptr) {
    out.clear();
    const std::optional<std::size_t> start = SectionStart(payload);
    if (!start.has_value()) {
        return true;
    }
    std::size_t at = *start + sizeof(std::uint32_t);
    std::uint32_t version = 0U;
    std::uint32_t count = 0U;
    std::uint64_t source = 0U;
    if (!Read(payload, at, version) || version < 1U || version > kVersion || !Read(payload, at, count) || count > 65536U ||
        (version >= 2U && !Read(payload, at, source))) {
        return false;
    }
    if (stamp != nullptr) {
        *stamp = source;
    }
    for (std::uint32_t i = 0U; i < count; ++i) {
        Take take;
        std::uint32_t nameBytes = 0U;
        if (!Read(payload, at, nameBytes) || nameBytes > 4096U || payload.size() - at < nameBytes) {
            return false;
        }
        take.name.assign(reinterpret_cast<const char*>(payload.data() + at), nameBytes);
        at += nameBytes;
        if (!Read(payload, at, take.duration) || !Read(payload, at, take.payloadBytes) || payload.size() - at < take.payloadBytes) {
            return false;
        }
        take.payloadAt = at;
        at += take.payloadBytes;
        out.push_back(std::move(take));
    }
    return true;
}

[[nodiscard]] std::optional<std::vector<std::byte>> ReadMeshPayload(const std::filesystem::path& model) {
    const std::optional<std::pair<UveFileHeader, std::vector<std::byte>>> file = ReadUveFile(model);
    if (!file.has_value() || file->first.assetType != AssetKind::Mesh) {
        return std::nullopt;
    }
    return file->second;
}

} // namespace

std::vector<ModelAnimationEntry> ListModelAnimations(const std::filesystem::path& model) {
    std::vector<ModelAnimationEntry> out;
    std::error_code error;
    if (!std::filesystem::is_regular_file(model, error)) {
        return out;
    }
    const std::optional<std::vector<std::byte>> payload = ReadMeshPayload(model);
    std::vector<Take> takes;
    if (!payload.has_value() || !ReadTakes(*payload, takes)) {
        return out;
    }
    for (const Take& take : takes) {
        out.push_back(ModelAnimationEntry{take.name, take.duration});
    }
    return out;
}

bool LoadModelAnimation(const std::filesystem::path& model, const std::string& name, AnimationClipAsset& outClip) {
    const std::optional<std::vector<std::byte>> payload = ReadMeshPayload(model);
    std::vector<Take> takes;
    if (!payload.has_value() || !ReadTakes(*payload, takes)) {
        return false;
    }
    for (const Take& take : takes) {
        if (take.name == name) {
            const std::vector<std::byte> clip(payload->begin() + static_cast<std::ptrdiff_t>(take.payloadAt),
                                              payload->begin() + static_cast<std::ptrdiff_t>(take.payloadAt + take.payloadBytes));
            return ParseAnimationClipPayload(clip, outClip);
        }
    }
    return false;
}

bool LoadAllModelAnimations(const std::filesystem::path& model,
                               std::vector<AnimationClipAsset>& outClips,
                               std::uint64_t* outStamp) {
    outClips.clear();
    if (outStamp != nullptr) {
        *outStamp = 0U;
    }
    std::error_code error;
    if (!std::filesystem::is_regular_file(model, error)) {
        return false;
    }
    const std::optional<std::vector<std::byte>> payload = ReadMeshPayload(model);
    std::vector<Take> takes;
    std::uint64_t stamp = 0U;
    if (!payload.has_value() || !ReadTakes(*payload, takes, &stamp)) {
        return false;
    }
    if (outStamp != nullptr) {
        *outStamp = stamp;
    }
    outClips.reserve(takes.size());
    for (const Take& take : takes) {
        const std::vector<std::byte> bytes(payload->begin() + static_cast<std::ptrdiff_t>(take.payloadAt),
                                           payload->begin() + static_cast<std::ptrdiff_t>(take.payloadAt + take.payloadBytes));
        AnimationClipAsset clip;
        if (ParseAnimationClipPayload(bytes, clip)) {
            clip.clipId = take.name;
            outClips.push_back(std::move(clip));
        }
    }
    return true;
}

bool InspectModelAnimationsSummary(
    const std::filesystem::path& model,
    std::vector<std::pair<std::string, AnimationClipSummary>>& outSummaries) {
    outSummaries.clear();
    std::error_code error;
    if (!std::filesystem::is_regular_file(model, error)) {
        return false;
    }
    const std::optional<std::vector<std::byte>> payload = ReadMeshPayload(model);
    std::vector<Take> takes;
    if (!payload.has_value() || !ReadTakes(*payload, takes)) {
        return false;
    }
    outSummaries.reserve(takes.size());
    for (const Take& take : takes) {
        const std::span<const std::byte> bytes(payload->data() + take.payloadAt, take.payloadBytes);
        outSummaries.emplace_back(take.name, InspectAnimationClipSummary(bytes));
    }
    return true;
}

std::vector<std::byte> ExtractModelAnimationSection(const std::vector<std::byte>& meshPayload) {
    const std::optional<std::size_t> start = SectionStart(meshPayload);
    if (!start.has_value()) {
        return {};
    }
    return std::vector<std::byte>(meshPayload.begin() + static_cast<std::ptrdiff_t>(*start), meshPayload.end());
}

std::uint64_t ReadModelAnimationStamp(const std::filesystem::path& model) {
    std::error_code error;
    if (!std::filesystem::is_regular_file(model, error)) {
        return 0U;
    }
    const std::optional<std::vector<std::byte>> payload = ReadMeshPayload(model);
    std::vector<Take> takes;
    std::uint64_t stamp = 0U;
    if (!payload.has_value() || !ReadTakes(*payload, takes, &stamp) || takes.empty()) {
        return 0U;
    }
    return stamp;
}

bool SaveModelAnimations(const std::filesystem::path& model, const std::vector<AnimationClipAsset>& clips, const std::uint64_t sourceStamp) {
    std::optional<std::vector<std::byte>> payload = ReadMeshPayload(model);
    if (!payload.has_value()) {
        return false;
    }
    if (const std::optional<std::size_t> start = SectionStart(*payload)) {
        payload->resize(*start);
    }
    if (!clips.empty()) {
        const std::size_t sectionAt = payload->size();
        Append(*payload, kModelAnimationSectionTag);
        Append(*payload, kVersion);
        Append(*payload, static_cast<std::uint32_t>(clips.size()));
        Append(*payload, sourceStamp);
        for (const AnimationClipAsset& clip : clips) {
            std::vector<std::byte> bytes;
            if (!SerializeAnimationClipPayload(clip, bytes)) {
                return false;
            }
            Append(*payload, static_cast<std::uint32_t>(clip.clipId.size()));
            const auto* name = reinterpret_cast<const std::byte*>(clip.clipId.data());
            payload->insert(payload->end(), name, name + clip.clipId.size());
            Append(*payload, clip.durationSeconds);
            Append(*payload, static_cast<std::uint32_t>(bytes.size()));
            payload->insert(payload->end(), bytes.begin(), bytes.end());
        }
        Append(*payload, static_cast<std::uint64_t>(sectionAt));
        Append(*payload, kModelAnimationSectionTag);
    }
    return WriteUveFile(model, AssetKind::Mesh, *payload);
}

bool SplitEmbeddedClipPath(const std::filesystem::path& path, std::filesystem::path& outModel, std::string& outTake) {
    const std::string text = path.generic_string();
    const std::size_t hash = text.rfind('#');
    if (hash == std::string::npos || hash + 1U >= text.size()) {
        return false;
    }
    const std::string file = text.substr(0U, hash);
    if (file.size() < 8U || file.compare(file.size() - 8U, 8U, ".uvmodel") != 0) {
        return false;
    }
    outModel = std::filesystem::path{file};
    outTake = text.substr(hash + 1U);
    return true;
}

std::string AssetDisplayName(const std::filesystem::path& path) {
    std::filesystem::path model;
    std::string take;
    if (!SplitEmbeddedClipPath(path, model, take)) {
        return path.stem().string();
    }
    // "Hero.fbx.uvmodel" reads as "Hero".
    std::filesystem::path owner = model.stem();
    while (owner.has_extension()) {
        owner = owner.stem();
    }
    const std::string name = owner.string();
    return take.empty() || take == name ? name : name + " / " + take;
}

std::filesystem::path MakeEmbeddedClipPath(const std::filesystem::path& model, const std::string& take) {
    return std::filesystem::path{model.generic_string() + "#" + take};
}

} // namespace UVE::Asset
