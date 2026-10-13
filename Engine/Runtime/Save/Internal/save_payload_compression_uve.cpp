// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
#include "uve/save/save_payload_compression_uve.h"
#include "uve/utilities/binary_buffer_uve.h"
#include "uve/utilities/hash_uve.h"
#include <cstring>
#include <iterator>
#include <utility>
namespace UVE::Save {
namespace {
constexpr std::byte kLegacyMagic[] = {std::byte{'U'}, std::byte{'V'}, std::byte{'S'}, std::byte{'C'}};
constexpr std::byte kChecksummedMagic[] = {std::byte{'U'}, std::byte{'V'}, std::byte{'S'}, std::byte{'2'}};
constexpr std::size_t kLegacyHeaderBytes = sizeof(kLegacyMagic) + sizeof(std::uint64_t);
constexpr std::size_t kChecksummedHeaderBytes = kLegacyHeaderBytes + sizeof(std::uint64_t);
[[nodiscard]] bool HasMagicUVE(const std::vector<std::byte>& payload, const std::byte (&magic)[4]) noexcept {
    return payload.size() >= sizeof(magic) && std::memcmp(payload.data(), magic, sizeof(magic)) == 0;
}
[[nodiscard]] std::uint64_t ComputeSavePayloadFingerprintUVE(const std::vector<std::byte>& payload) noexcept {
    // The checksum is embedded in every compressed save, so its values are a format: the legacy
    // transposed seed is preserved deliberately (see kFnv1a64LegacyOffsetBasisUVE). Old saves
    // keep verifying bit-for-bit.
    // (An empty payload hashes from a null data pointer with a zero size, which the hasher
    // defines as a no-op — matching the old loop, which simply never iterated.)
    return Utilities::HashBytesUVE(payload.data(), payload.size(),
                                   Utilities::kFnv1a64LegacyOffsetBasisUVE);
}
} // namespace
std::vector<std::byte> CompressSavePayloadUVE(const std::vector<std::byte>& payload) {
    if (payload.size() > kMaximumCompressedSavePayloadBytesUVE) {
        return {};
    }
    std::vector<std::byte> compressed;
    compressed.reserve(kChecksummedHeaderBytes + payload.size());
    compressed.insert(compressed.end(), std::begin(kChecksummedMagic), std::end(kChecksummedMagic));
    // Little-endian by format: byte-identical to the old host-order writes on every
    // little-endian target (all of them), so old saves keep loading unchanged.
    Utilities::AppendUint64LeUVE(compressed, payload.size());
    Utilities::AppendUint64LeUVE(compressed, ComputeSavePayloadFingerprintUVE(payload));
    for (std::size_t offset = 0U; offset < payload.size();) {
        const std::byte value = payload[offset];
        std::size_t runLength = 1U;
        while (offset + runLength < payload.size() && payload[offset + runLength] == value && runLength < 255U) {
            ++runLength;
        }
        compressed.push_back(static_cast<std::byte>(runLength));
        compressed.push_back(value);
        offset += runLength;
    }
    if (compressed.size() >= payload.size()) {
        return payload;
    }
    return compressed;
}
bool DecompressSavePayloadUVE(const std::vector<std::byte>& payload,
                              std::vector<std::byte>& outPayload) {
    const bool checksummed = HasMagicUVE(payload, kChecksummedMagic);
    const bool legacyCompressed = HasMagicUVE(payload, kLegacyMagic);
    if (!checksummed && !legacyCompressed) {
        if (payload.size() > kMaximumCompressedSavePayloadBytesUVE) {
            return false;
        }
        outPayload = payload;
        return true;
    }
    const std::size_t headerBytes = checksummed ? kChecksummedHeaderBytes : kLegacyHeaderBytes;
    std::uint64_t expectedSize = 0U;
    // The old local reader took its offset by value; the shared reader advances it, so each
    // fixed-offset read gets a throwaway cursor. Same bytes, same values.
    std::size_t sizeOffset = sizeof(kChecksummedMagic);
    if (!Utilities::ReadUint64LeFromBufferUVE(payload, sizeOffset, expectedSize) ||
        expectedSize > kMaximumCompressedSavePayloadBytesUVE || payload.size() < headerBytes) {
        return false;
    }
    std::uint64_t expectedFingerprint = 0U;
    std::size_t fingerprintOffset = kLegacyHeaderBytes;
    if (checksummed && !Utilities::ReadUint64LeFromBufferUVE(payload, fingerprintOffset, expectedFingerprint)) {
        return false;
    }
    std::vector<std::byte> expanded;
    expanded.reserve(static_cast<std::size_t>(expectedSize));
    for (std::size_t offset = headerBytes; offset < payload.size();) {
        if (offset + 2U > payload.size()) {
            return false;
        }
        const std::size_t runLength = std::to_integer<std::uint8_t>(payload[offset]);
        if (runLength == 0U || expanded.size() > static_cast<std::size_t>(expectedSize) - runLength) {
            return false;
        }
        expanded.insert(expanded.end(), runLength, payload[offset + 1U]);
        offset += 2U;
    }
    if (expanded.size() != static_cast<std::size_t>(expectedSize) ||
        (checksummed && ComputeSavePayloadFingerprintUVE(expanded) != expectedFingerprint)) {
        return false;
    }
    outPayload = std::move(expanded);
    return true;
}
} // namespace UVE::Save
