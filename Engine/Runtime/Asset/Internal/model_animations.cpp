// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
#include "model_animations.h"

#include "uve_file_envelope.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <utility>

namespace UVE::Asset {
namespace {

// Layout, at the end of a mesh payload:
//   section: u32 tag 'UVAN', u32 version (2), u32 count, u64 source stamp,
//            count x { u32 nameBytes, name, f64 durationSeconds, u32 payloadBytes, clip payload }
//   footer:  u64 offset of the section, u32 tag 'UVAN'
constexpr std::uint32_t kVersionUVE = 2U;
constexpr std::size_t kFooterBytesUVE = sizeof(std::uint64_t) + sizeof(std::uint32_t);

template <class T>
void AppendUVE(std::vector<std::byte>& out, const T value) {
    std::byte bytes[sizeof(T)];
    std::memcpy(bytes, &value, sizeof(T));
    out.insert(out.end(), bytes, bytes + sizeof(T));
}

template <class T>
[[nodiscard]] bool ReadUVE(const std::vector<std::byte>& in, std::size_t& at, T& value) {
    if (at > in.size() || in.size() - at < sizeof(T)) {
        return false;
    }
    std::memcpy(&value, in.data() + at, sizeof(T));
    at += sizeof(T);
    return true;
}

/// Where the takes section starts in `payload`, or nullopt when it has none.
[[nodiscard]] std::optional<std::size_t> SectionStartUVE(const std::vector<std::byte>& payload) {
    if (payload.size() < kFooterBytesUVE) {
        return std::nullopt;
    }
    std::size_t at = payload.size() - kFooterBytesUVE;
    std::uint64_t offset = 0U;
    std::uint32_t tag = 0U;
    if (!ReadUVE(payload, at, offset) || !ReadUVE(payload, at, tag) || tag != kModelAnimationSectionTagUVE ||
        offset + sizeof(std::uint32_t) > payload.size() - kFooterBytesUVE) {
        return std::nullopt;
    }
    std::size_t check = static_cast<std::size_t>(offset);
    std::uint32_t head = 0U;
    if (!ReadUVE(payload, check, head) || head != kModelAnimationSectionTagUVE) {
        return std::nullopt;
    }
    return static_cast<std::size_t>(offset);
}

struct TakeUVE final {
    std::string name;
    double duration = 0.0;
    std::size_t payloadAt = 0U;
    std::uint32_t payloadBytes = 0U;
};

[[nodiscard]] bool ReadTakesUVE(const std::vector<std::byte>& payload, std::vector<TakeUVE>& out, std::uint64_t* stamp = nullptr) {
    out.clear();
    const std::optional<std::size_t> start = SectionStartUVE(payload);
    if (!start.has_value()) {
        return true;
    }
    std::size_t at = *start + sizeof(std::uint32_t);
    std::uint32_t version = 0U;
    std::uint32_t count = 0U;
    std::uint64_t source = 0U;
    if (!ReadUVE(payload, at, version) || version < 1U || version > kVersionUVE || !ReadUVE(payload, at, count) || count > 65536U ||
        (version >= 2U && !ReadUVE(payload, at, source))) {
        return false;
    }
    if (stamp != nullptr) {
        *stamp = source;
    }
    for (std::uint32_t i = 0U; i < count; ++i) {
        TakeUVE take;
        std::uint32_t nameBytes = 0U;
        if (!ReadUVE(payload, at, nameBytes) || nameBytes > 4096U || payload.size() - at < nameBytes) {
            return false;
        }
        take.name.assign(reinterpret_cast<const char*>(payload.data() + at), nameBytes);
        at += nameBytes;
        if (!ReadUVE(payload, at, take.duration) || !ReadUVE(payload, at, take.payloadBytes) || payload.size() - at < take.payloadBytes) {
            return false;
        }
        take.payloadAt = at;
        at += take.payloadBytes;
        out.push_back(std::move(take));
    }
    return true;
}

[[nodiscard]] std::optional<std::vector<std::byte>> ReadMeshPayloadUVE(const std::filesystem::path& model) {
    const std::optional<std::pair<UveFileHeaderUVE, std::vector<std::byte>>> file = ReadUveFileUVE(model);
    if (!file.has_value() || file->first.assetType != AssetKindUVE::Mesh) {
        return std::nullopt;
    }
    return file->second;
}

} // namespace

std::vector<ModelAnimationEntryUVE> ListModelAnimationsUVE(const std::filesystem::path& model) {
    std::vector<ModelAnimationEntryUVE> out;
    std::error_code error;
    if (!std::filesystem::is_regular_file(model, error)) {
        return out;
    }
    const std::optional<std::vector<std::byte>> payload = ReadMeshPayloadUVE(model);
    std::vector<TakeUVE> takes;
    if (!payload.has_value() || !ReadTakesUVE(*payload, takes)) {
        return out;
    }
    for (const TakeUVE& take : takes) {
        out.push_back(ModelAnimationEntryUVE{take.name, take.duration});
    }
    return out;
}

bool LoadModelAnimationUVE(const std::filesystem::path& model, const std::string& name, AnimationClipAssetUVE& outClip) {
    const std::optional<std::vector<std::byte>> payload = ReadMeshPayloadUVE(model);
    std::vector<TakeUVE> takes;
    if (!payload.has_value() || !ReadTakesUVE(*payload, takes)) {
        return false;
    }
    for (const TakeUVE& take : takes) {
        if (take.name == name) {
            const std::vector<std::byte> clip(payload->begin() + static_cast<std::ptrdiff_t>(take.payloadAt),
                                              payload->begin() + static_cast<std::ptrdiff_t>(take.payloadAt + take.payloadBytes));
            return ParseAnimationClipPayloadUVE(clip, outClip);
        }
    }
    return false;
}

std::vector<std::byte> ExtractModelAnimationSectionUVE(const std::vector<std::byte>& meshPayload) {
    const std::optional<std::size_t> start = SectionStartUVE(meshPayload);
    if (!start.has_value()) {
        return {};
    }
    return std::vector<std::byte>(meshPayload.begin() + static_cast<std::ptrdiff_t>(*start), meshPayload.end());
}

std::uint64_t ReadModelAnimationStampUVE(const std::filesystem::path& model) {
    std::error_code error;
    if (!std::filesystem::is_regular_file(model, error)) {
        return 0U;
    }
    const std::optional<std::vector<std::byte>> payload = ReadMeshPayloadUVE(model);
    std::vector<TakeUVE> takes;
    std::uint64_t stamp = 0U;
    if (!payload.has_value() || !ReadTakesUVE(*payload, takes, &stamp) || takes.empty()) {
        return 0U;
    }
    return stamp;
}

bool SaveModelAnimationsUVE(const std::filesystem::path& model, const std::vector<AnimationClipAssetUVE>& clips, const std::uint64_t sourceStamp) {
    std::optional<std::vector<std::byte>> payload = ReadMeshPayloadUVE(model);
    if (!payload.has_value()) {
        return false;
    }
    if (const std::optional<std::size_t> start = SectionStartUVE(*payload)) {
        payload->resize(*start);
    }
    if (!clips.empty()) {
        const std::size_t sectionAt = payload->size();
        AppendUVE(*payload, kModelAnimationSectionTagUVE);
        AppendUVE(*payload, kVersionUVE);
        AppendUVE(*payload, static_cast<std::uint32_t>(clips.size()));
        AppendUVE(*payload, sourceStamp);
        for (const AnimationClipAssetUVE& clip : clips) {
            std::vector<std::byte> bytes;
            if (!SerializeAnimationClipPayloadUVE(clip, bytes)) {
                return false;
            }
            AppendUVE(*payload, static_cast<std::uint32_t>(clip.clipId.size()));
            const auto* name = reinterpret_cast<const std::byte*>(clip.clipId.data());
            payload->insert(payload->end(), name, name + clip.clipId.size());
            AppendUVE(*payload, clip.durationSeconds);
            AppendUVE(*payload, static_cast<std::uint32_t>(bytes.size()));
            payload->insert(payload->end(), bytes.begin(), bytes.end());
        }
        AppendUVE(*payload, static_cast<std::uint64_t>(sectionAt));
        AppendUVE(*payload, kModelAnimationSectionTagUVE);
    }
    return WriteUveFileUVE(model, AssetKindUVE::Mesh, *payload);
}

bool SplitEmbeddedClipPathUVE(const std::filesystem::path& path, std::filesystem::path& outModel, std::string& outTake) {
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

std::string AssetDisplayNameUVE(const std::filesystem::path& path) {
    std::filesystem::path model;
    std::string take;
    if (!SplitEmbeddedClipPathUVE(path, model, take)) {
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

std::filesystem::path MakeEmbeddedClipPathUVE(const std::filesystem::path& model, const std::string& take) {
    return std::filesystem::path{model.generic_string() + "#" + take};
}

} // namespace UVE::Asset
