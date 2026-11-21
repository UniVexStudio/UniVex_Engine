// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "sha256.h"

#include <bit>
#include <cctype>
#include <climits>
#include <fstream>
#include <vector>

namespace UVE::Online {
namespace {

inline constexpr std::array<std::uint32_t, kSha256RoundsUVE> kRoundConstantsUVE{
    0x428a2f98U, 0x71374491U, 0xb5c0fbcfU, 0xe9b5dba5U, 0x3956c25bU, 0x59f111f1U, 0x923f82a4U, 0xab1c5ed5U,
    0xd807aa98U, 0x12835b01U, 0x243185beU, 0x550c7dc3U, 0x72be5d74U, 0x80deb1feU, 0x9bdc06a7U, 0xc19bf174U,
    0xe49b69c1U, 0xefbe4786U, 0x0fc19dc6U, 0x240ca1ccU, 0x2de92c6fU, 0x4a7484aaU, 0x5cb0a9dcU, 0x76f988daU,
    0x983e5152U, 0xa831c66dU, 0xb00327c8U, 0xbf597fc7U, 0xc6e00bf3U, 0xd5a79147U, 0x06ca6351U, 0x14292967U,
    0x27b70a85U, 0x2e1b2138U, 0x4d2c6dfcU, 0x53380d13U, 0x650a7354U, 0x766a0abbU, 0x81c2c92eU, 0x92722c85U,
    0xa2bfe8a1U, 0xa81a664bU, 0xc24b8b70U, 0xc76c51a3U, 0xd192e819U, 0xd6990624U, 0xf40e3585U, 0x106aa070U,
    0x19a4c116U, 0x1e376c08U, 0x2748774cU, 0x34b0bcb5U, 0x391c0cb3U, 0x4ed8aa4aU, 0x5b9cca4fU, 0x682e6ff3U,
    0x748f82eeU, 0x78a5636fU, 0x84c87814U, 0x8cc70208U, 0x90befffaU, 0xa4506cebU, 0xbef9a3f7U, 0xc67178f2U};

inline constexpr std::array<std::uint32_t, kSha256StateWordsUVE> kInitialStateUVE{
    0x6a09e667U, 0xbb67ae85U, 0x3c6ef372U, 0xa54ff53aU, 0x510e527fU, 0x9b05688cU, 0x1f83d9abU, 0x5be0cd19U};
inline constexpr std::size_t kBlockWordsUVE = kSha256BlockBytesUVE / kSha256WordBytesUVE;
/// Padding starts with a single set bit, and the message length in bits closes the last block.
inline constexpr std::uint8_t kPaddingMarkerUVE = 0x80U;
inline constexpr std::size_t kLengthFieldBytesUVE = sizeof(std::uint64_t);
inline constexpr std::size_t kBitsPerByteUVE = CHAR_BIT;
inline constexpr std::size_t kHexDigitsPerByteUVE = 2U;
inline constexpr unsigned kBitsPerHexDigitUVE = 4U;
inline constexpr std::uint8_t kHexDigitMaskUVE = 0x0FU;
inline constexpr std::size_t kFileChunkBytesUVE = 1U << 16U;
inline constexpr std::string_view kHexDigitsUVE = "0123456789abcdef";

/// Reads the big-endian word at `bytes`.
[[nodiscard]] std::uint32_t LoadBigEndianWordUVE(const std::uint8_t* const bytes) noexcept {
    std::uint32_t word = 0U;
    for (std::size_t index = 0U; index < kSha256WordBytesUVE; ++index) {
        word = (word << kBitsPerByteUVE) | bytes[index];
    }
    return word;
}

/// Writes the low `count` bytes of `value` to `bytes`, most significant first.
void StoreBigEndianUVE(const std::uint64_t value, const std::size_t count, std::uint8_t* const bytes) noexcept {
    for (std::size_t index = 0U; index < count; ++index) {
        bytes[index] = static_cast<std::uint8_t>(value >> ((count - 1U - index) * kBitsPerByteUVE));
    }
}

} // namespace

Sha256UVE::Sha256UVE() noexcept : m_state(kInitialStateUVE) {}

void Sha256UVE::TransformUVE(const std::uint8_t* const block) noexcept {
    // The message schedule and compression rounds below follow FIPS 180-4, section 6.2.2; the
    // rotation and shift amounts are the standard's sigma and Sigma functions (section 4.1.2).
    std::array<std::uint32_t, kSha256RoundsUVE> schedule{};
    for (std::size_t index = 0U; index < kBlockWordsUVE; ++index) {
        schedule[index] = LoadBigEndianWordUVE(block + index * kSha256WordBytesUVE);
    }
    for (std::size_t index = kBlockWordsUVE; index < kSha256RoundsUVE; ++index) {
        const std::uint32_t s0 = std::rotr(schedule[index - 15U], 7) ^ std::rotr(schedule[index - 15U], 18) ^
                                 (schedule[index - 15U] >> 3U);
        const std::uint32_t s1 = std::rotr(schedule[index - 2U], 17) ^ std::rotr(schedule[index - 2U], 19) ^
                                 (schedule[index - 2U] >> 10U);
        schedule[index] = schedule[index - kBlockWordsUVE] + s0 + schedule[index - 7U] + s1;
    }
    std::array<std::uint32_t, kSha256StateWordsUVE> work = m_state;
    for (std::size_t index = 0U; index < kSha256RoundsUVE; ++index) {
        const std::uint32_t s1 = std::rotr(work[4], 6) ^ std::rotr(work[4], 11) ^ std::rotr(work[4], 25);
        const std::uint32_t choice = (work[4] & work[5]) ^ (~work[4] & work[6]);
        const std::uint32_t temp1 = work[7] + s1 + choice + kRoundConstantsUVE[index] + schedule[index];
        const std::uint32_t s0 = std::rotr(work[0], 2) ^ std::rotr(work[0], 13) ^ std::rotr(work[0], 22);
        const std::uint32_t majority = (work[0] & work[1]) ^ (work[0] & work[2]) ^ (work[1] & work[2]);
        const std::uint32_t temp2 = s0 + majority;
        work[7] = work[6];
        work[6] = work[5];
        work[5] = work[4];
        work[4] = work[3] + temp1;
        work[3] = work[2];
        work[2] = work[1];
        work[1] = work[0];
        work[0] = temp1 + temp2;
    }
    for (std::size_t index = 0U; index < m_state.size(); ++index) {
        m_state[index] += work[index];
    }
}

void Sha256UVE::UpdateUVE(const std::span<const std::byte> data) noexcept {
    m_totalBytes += data.size();
    for (const std::byte value : data) {
        m_buffer[m_buffered++] = std::to_integer<std::uint8_t>(value);
        if (m_buffered == kSha256BlockBytesUVE) {
            TransformUVE(m_buffer.data());
            m_buffered = 0U;
        }
    }
}

Sha256DigestUVE Sha256UVE::FinishUVE() noexcept {
    const std::uint64_t totalBits = m_totalBytes * kBitsPerByteUVE;
    m_buffer[m_buffered++] = kPaddingMarkerUVE;
    if (m_buffered > kSha256BlockBytesUVE - kLengthFieldBytesUVE) {
        while (m_buffered < kSha256BlockBytesUVE) {
            m_buffer[m_buffered++] = 0U;
        }
        TransformUVE(m_buffer.data());
        m_buffered = 0U;
    }
    while (m_buffered < kSha256BlockBytesUVE - kLengthFieldBytesUVE) {
        m_buffer[m_buffered++] = 0U;
    }
    StoreBigEndianUVE(totalBits, kLengthFieldBytesUVE, m_buffer.data() + m_buffered);
    m_buffered += kLengthFieldBytesUVE;
    TransformUVE(m_buffer.data());
    Sha256DigestUVE digest{};
    for (std::size_t index = 0U; index < m_state.size(); ++index) {
        StoreBigEndianUVE(m_state[index], kSha256WordBytesUVE, digest.data() + index * kSha256WordBytesUVE);
    }
    return digest;
}

std::string ToHexUVE(const Sha256DigestUVE& digest) {
    std::string text;
    text.reserve(digest.size() * kHexDigitsPerByteUVE);
    for (const std::uint8_t value : digest) {
        text.push_back(kHexDigitsUVE[value >> kBitsPerHexDigitUVE]);
        text.push_back(kHexDigitsUVE[value & kHexDigitMaskUVE]);
    }
    return text;
}

std::optional<Sha256DigestUVE> ParseSha256HexUVE(const std::string_view text) {
    if (text.size() != kSha256DigestBytesUVE * kHexDigitsPerByteUVE) {
        return std::nullopt;
    }
    Sha256DigestUVE digest{};
    for (std::size_t index = 0U; index < kSha256DigestBytesUVE; ++index) {
        std::uint8_t value = 0U;
        for (std::size_t nibble = 0U; nibble < kHexDigitsPerByteUVE; ++nibble) {
            const char character = static_cast<char>(std::tolower(static_cast<unsigned char>(text[index * kHexDigitsPerByteUVE + nibble])));
            const std::size_t position = kHexDigitsUVE.find(character);
            if (position == std::string_view::npos) {
                return std::nullopt;
            }
            value = static_cast<std::uint8_t>((value << kBitsPerHexDigitUVE) | position);
        }
        digest[index] = value;
    }
    return digest;
}

std::optional<Sha256DigestUVE> HashFileSha256UVE(const std::filesystem::path& file) {
    std::ifstream input(file, std::ios::binary);
    if (!input.is_open()) {
        return std::nullopt;
    }
    Sha256UVE hash;
    std::vector<char> chunk(kFileChunkBytesUVE);
    while (input) {
        input.read(chunk.data(), static_cast<std::streamsize>(chunk.size()));
        const std::streamsize read = input.gcount();
        if (read > 0) {
            hash.UpdateUVE(std::as_bytes(std::span<const char>(chunk.data(), static_cast<std::size_t>(read))));
        }
    }
    if (input.bad()) {
        return std::nullopt;
    }
    return hash.FinishUVE();
}

} // namespace UVE::Online
