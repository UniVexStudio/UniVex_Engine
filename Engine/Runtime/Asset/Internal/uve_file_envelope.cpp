

#include "uve_file_envelope.h"

#include <array>
#include <cstring>
#include <fstream>
#include <limits>
#include <string>

#include "logging_macros.h"
#include "binary_buffer.h"

namespace UVE::Asset {

namespace {

constexpr std::array<char, 4> kUveMagic{'U', 'V', 'E', '\0'};
constexpr std::uint32_t kEnvelopeVersion = 1;
constexpr std::uint32_t kCompressionMethodNone = 0;
constexpr std::uint64_t kMaximumPayloadBytes = kMaximumUveFilePayloadBytes;
constexpr std::size_t kEnvelopeHeaderBytes = kUveFileEnvelopeHeaderBytes;

[[nodiscard]] bool IsAssetKindValid(const std::uint32_t assetType) noexcept {
    return assetType >= static_cast<std::uint32_t>(AssetKind::Scene) &&
           assetType <= static_cast<std::uint32_t>(AssetKind::ParticleSystem);
}

[[nodiscard]] std::string SourceDescription(const std::string_view sourceDescription) {
    return sourceDescription.empty() ? std::string{"<memory>"} : std::string{sourceDescription};
}

} // namespace

bool IsUveFilePayloadSizeValid(const std::size_t payloadBytes) noexcept {
    return payloadBytes <= kMaximumUveFilePayloadBytes;
}

std::vector<std::byte> EncodeUveFileEnvelope(const AssetKind assetType,
                                                const std::vector<std::byte>& payload) {
    if (!IsUveFilePayloadSizeValid(payload.size())) {
        return {};
    }
    std::vector<std::byte> envelope;
    envelope.reserve(kEnvelopeHeaderBytes + payload.size());
    const auto* const magicBytes = reinterpret_cast<const std::byte*>(kUveMagic.data());
    envelope.insert(envelope.end(), magicBytes, magicBytes + kUveMagic.size());
    // All header integers are little-endian by format: byte-identical to the old host-order
    // writes on every little-endian target (all of them), so existing .uve files decode unchanged.
    Utilities::AppendUint32Le(envelope, kEnvelopeVersion);
    Utilities::AppendUint32Le(envelope, static_cast<std::uint32_t>(assetType));
    Utilities::AppendUint32Le(envelope, kCompressionMethodNone);
    Utilities::AppendUint64Le(envelope, static_cast<std::uint64_t>(payload.size()));
    envelope.insert(envelope.end(), payload.begin(), payload.end());
    return envelope;
}

std::optional<std::pair<UveFileHeader, std::vector<std::byte>>>
DecodeUveFileEnvelope(const std::vector<std::byte>& envelope, const std::string_view sourceDescription) {
    const std::string source = SourceDescription(sourceDescription);
    if (envelope.size() < kUveMagic.size() ||
        std::memcmp(envelope.data(), kUveMagic.data(), kUveMagic.size()) != 0) {
        UVE_ERROR("UveFileEnvelope: \"{}\" is not a valid .uve file (bad magic)", source);
        return std::nullopt;
    }

    std::size_t offset = kUveMagic.size();
    UveFileHeader header;
    std::uint32_t assetType = 0;
    std::uint64_t payloadLength = 0;
    if (!Utilities::ReadUint32LeFromBuffer(envelope, offset, header.version) ||
        !Utilities::ReadUint32LeFromBuffer(envelope, offset, assetType) ||
        !Utilities::ReadUint32LeFromBuffer(envelope, offset, header.compressionMethod) ||
        !Utilities::ReadUint64LeFromBuffer(envelope, offset, payloadLength)) {
        UVE_ERROR("UveFileEnvelope: \"{}\" has a truncated header", source);
        return std::nullopt;
    }
    header.assetType = static_cast<AssetKind>(assetType);

    if (header.version != kEnvelopeVersion) {
        UVE_ERROR("UveFileEnvelope: \"{}\" has unsupported envelope version {}", source, header.version);
        return std::nullopt;
    }
    if (!IsAssetKindValid(assetType)) {
        UVE_ERROR("UveFileEnvelope: \"{}\" has an invalid asset kind {}", source, assetType);
        return std::nullopt;
    }
    if (header.compressionMethod != kCompressionMethodNone) {
        UVE_ERROR("UveFileEnvelope: \"{}\" uses unsupported compression method {}", source,
                  header.compressionMethod);
        return std::nullopt;
    }
    if (payloadLength > kMaximumPayloadBytes || payloadLength > std::numeric_limits<std::size_t>::max() ||
        payloadLength > envelope.size() - offset) {
        UVE_ERROR("UveFileEnvelope: \"{}\" has an invalid or oversized payload length", source);
        return std::nullopt;
    }

    const std::size_t payloadSize = static_cast<std::size_t>(payloadLength);
    return std::make_pair(header,
                          std::vector<std::byte>{envelope.begin() + static_cast<std::ptrdiff_t>(offset),
                                                 envelope.begin() + static_cast<std::ptrdiff_t>(offset + payloadSize)});
}

bool WriteUveFile(const std::filesystem::path& path, const AssetKind assetType,
                     const std::vector<std::byte>& payload) {
    if (!IsUveFilePayloadSizeValid(payload.size())) {
        UVE_ERROR("UveFileEnvelope: refused oversized payload for {}", path.string());
        return false;
    }
    if (!IsAssetKindValid(static_cast<std::uint32_t>(assetType))) {
        UVE_ERROR("UveFileEnvelope: refused to write \"{}\" with invalid asset kind {}", path.string(),
                  static_cast<std::uint32_t>(assetType));
        return false;
    }
    std::ofstream file(path, std::ios::binary);
    if (!file.is_open()) {
        UVE_ERROR("UveFileEnvelope: failed to open \"{}\" for writing", path.string());
        return false;
    }

    const std::vector<std::byte> envelope = EncodeUveFileEnvelope(assetType, payload);
    file.write(reinterpret_cast<const char*>(envelope.data()), static_cast<std::streamsize>(envelope.size()));
    if (!file.good()) {
        UVE_ERROR("UveFileEnvelope: failed to write \"{}\": stream error after write", path.string());
        return false;
    }
    return true;
}

std::optional<std::pair<UveFileHeader, std::vector<std::byte>>>
ReadUveFile(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        UVE_ERROR("UveFileEnvelope: failed to open \"{}\" for reading", path.string());
        return std::nullopt;
    }

    const std::streamoff fileSize = file.tellg();
    if (fileSize < 0 || static_cast<std::uint64_t>(fileSize) >
                            kEnvelopeHeaderBytes + kMaximumPayloadBytes) {
        UVE_ERROR("UveFileEnvelope: \"{}\" has an invalid or oversized payload length", path.string());
        return std::nullopt;
    }

    std::vector<std::byte> envelope(static_cast<std::size_t>(fileSize));
    file.seekg(0, std::ios::beg);
    if (!envelope.empty()) {
        file.read(reinterpret_cast<char*>(envelope.data()), static_cast<std::streamsize>(envelope.size()));
        if (!file) {
            UVE_ERROR("UveFileEnvelope: \"{}\" could not be read completely", path.string());
            return std::nullopt;
        }
    }

    return DecodeUveFileEnvelope(envelope, path.string());
}

} // namespace UVE::Asset
