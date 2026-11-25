// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "sha256.h"

#include <bit>
#include <cctype>
#include <climits>
#include <fstream>
#include <vector>

namespace UVE::Online {
namespace {

inline constexpr std::array<std::uint32_t, kSha256Rounds> kRoundConstants{
    0x428a2f98U, 0x71374491U, 0xb5c0fbcfU, 0xe9b5dba5U, 0x3956c25bU, 0x59f111f1U, 0x923f82a4U, 0xab1c5ed5U,
    0xd807aa98U, 0x12835b01U, 0x243185beU, 0x550c7dc3U, 0x72be5d74U, 0x80deb1feU, 0x9bdc06a7U, 0xc19bf174U,
    0xe49b69c1U, 0xefbe4786U, 0x0fc19dc6U, 0x240ca1ccU, 0x2de92c6fU, 0x4a7484aaU, 0x5cb0a9dcU, 0x76f988daU,
    0x983e5152U, 0xa831c66dU, 0xb00327c8U, 0xbf597fc7U, 0xc6e00bf3U, 0xd5a79147U, 0x06ca6351U, 0x14292967U,
    0x27b70a85U, 0x2e1b2138U, 0x4d2c6dfcU, 0x53380d13U, 0x650a7354U, 0x766a0abbU, 0x81c2c92eU, 0x92722c85U,
    0xa2bfe8a1U, 0xa81a664bU, 0xc24b8b70U, 0xc76c51a3U, 0xd192e819U, 0xd6990624U, 0xf40e3585U, 0x106aa070U,
    0x19a4c116U, 0x1e376c08U, 0x2748774cU, 0x34b0bcb5U, 0x391c0cb3U, 0x4ed8aa4aU, 0x5b9cca4fU, 0x682e6ff3U,
    0x748f82eeU, 0x78a5636fU, 0x84c87814U, 0x8cc70208U, 0x90befffaU, 0xa4506cebU, 0xbef9a3f7U, 0xc67178f2U};

inline constexpr std::array<std::uint32_t, kSha256StateWords> kInitialState{
    0x6a09e667U, 0xbb67ae85U, 0x3c6ef372U, 0xa54ff53aU, 0x510e527fU, 0x9b05688cU, 0x1f83d9abU, 0x5be0cd19U};
inline constexpr std::size_t kBlockWords = kSha256BlockBytes / kSha256WordBytes;
/// Padding starts with a single set bit, and the message length in bits closes the last block.
inline constexpr std::uint8_t kPaddingMarker = 0x80U;
inline constexpr std::size_t kLengthFieldBytes = sizeof(std::uint64_t);
inline constexpr std::size_t kBitsPerByte = CHAR_BIT;
inline constexpr std::size_t kHexDigitsPerByte = 2U;
inline constexpr unsigned kBitsPerHexDigit = 4U;
inline constexpr std::uint8_t kHexDigitMask = 0x0FU;
inline constexpr std::size_t kFileChunkBytes = 1U << 16U;
inline constexpr std::string_view kHexDigits = "0123456789abcdef";

/// Reads the big-endian word at `bytes`.
[[nodiscard]] std::uint32_t LoadBigEndianWord(const std::uint8_t* const bytes) noexcept {
    std::uint32_t word = 0U;
    for (std::size_t index = 0U; index < kSha256WordBytes; ++index) {
        word = (word << kBitsPerByte) | bytes[index];
    }
    return word;
}

/// Writes the low `count` bytes of `value` to `bytes`, most significant first.
void StoreBigEndian(const std::uint64_t value, const std::size_t count, std::uint8_t* const bytes) noexcept {
    for (std::size_t index = 0U; index < count; ++index) {
        bytes[index] = static_cast<std::uint8_t>(value >> ((count - 1U - index) * kBitsPerByte));
    }
}

} // namespace

Sha256::Sha256() noexcept : m_state(kInitialState) {}

void Sha256::Transform(const std::uint8_t* const block) noexcept {
    // The message schedule and compression rounds below follow FIPS 180-4, section 6.2.2; the
    // rotation and shift amounts are the standard's sigma and Sigma functions (section 4.1.2).
    std::array<std::uint32_t, kSha256Rounds> schedule{};
    for (std::size_t index = 0U; index < kBlockWords; ++index) {
        schedule[index] = LoadBigEndianWord(block + index * kSha256WordBytes);
    }
    for (std::size_t index = kBlockWords; index < kSha256Rounds; ++index) {
        const std::uint32_t s0 = std::rotr(schedule[index - 15U], 7) ^ std::rotr(schedule[index - 15U], 18) ^
                                 (schedule[index - 15U] >> 3U);
        const std::uint32_t s1 = std::rotr(schedule[index - 2U], 17) ^ std::rotr(schedule[index - 2U], 19) ^
                                 (schedule[index - 2U] >> 10U);
        schedule[index] = schedule[index - kBlockWords] + s0 + schedule[index - 7U] + s1;
    }
    std::array<std::uint32_t, kSha256StateWords> work = m_state;
    for (std::size_t index = 0U; index < kSha256Rounds; ++index) {
        const std::uint32_t s1 = std::rotr(work[4], 6) ^ std::rotr(work[4], 11) ^ std::rotr(work[4], 25);
        const std::uint32_t choice = (work[4] & work[5]) ^ (~work[4] & work[6]);
        const std::uint32_t temp1 = work[7] + s1 + choice + kRoundConstants[index] + schedule[index];
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

void Sha256::Update(const std::span<const std::byte> data) noexcept {
    m_totalBytes += data.size();
    for (const std::byte value : data) {
        m_buffer[m_buffered++] = std::to_integer<std::uint8_t>(value);
        if (m_buffered == kSha256BlockBytes) {
            Transform(m_buffer.data());
            m_buffered = 0U;
        }
    }
}

Sha256Digest Sha256::Finish() noexcept {
    const std::uint64_t totalBits = m_totalBytes * kBitsPerByte;
    m_buffer[m_buffered++] = kPaddingMarker;
    if (m_buffered > kSha256BlockBytes - kLengthFieldBytes) {
        while (m_buffered < kSha256BlockBytes) {
            m_buffer[m_buffered++] = 0U;
        }
        Transform(m_buffer.data());
        m_buffered = 0U;
    }
    while (m_buffered < kSha256BlockBytes - kLengthFieldBytes) {
        m_buffer[m_buffered++] = 0U;
    }
    StoreBigEndian(totalBits, kLengthFieldBytes, m_buffer.data() + m_buffered);
    m_buffered += kLengthFieldBytes;
    Transform(m_buffer.data());
    Sha256Digest digest{};
    for (std::size_t index = 0U; index < m_state.size(); ++index) {
        StoreBigEndian(m_state[index], kSha256WordBytes, digest.data() + index * kSha256WordBytes);
    }
    return digest;
}

std::string ToHex(const Sha256Digest& digest) {
    std::string text;
    text.reserve(digest.size() * kHexDigitsPerByte);
    for (const std::uint8_t value : digest) {
        text.push_back(kHexDigits[value >> kBitsPerHexDigit]);
        text.push_back(kHexDigits[value & kHexDigitMask]);
    }
    return text;
}

std::optional<Sha256Digest> ParseSha256Hex(const std::string_view text) {
    if (text.size() != kSha256DigestBytes * kHexDigitsPerByte) {
        return std::nullopt;
    }
    Sha256Digest digest{};
    for (std::size_t index = 0U; index < kSha256DigestBytes; ++index) {
        std::uint8_t value = 0U;
        for (std::size_t nibble = 0U; nibble < kHexDigitsPerByte; ++nibble) {
            const char character = static_cast<char>(std::tolower(static_cast<unsigned char>(text[index * kHexDigitsPerByte + nibble])));
            const std::size_t position = kHexDigits.find(character);
            if (position == std::string_view::npos) {
                return std::nullopt;
            }
            value = static_cast<std::uint8_t>((value << kBitsPerHexDigit) | position);
        }
        digest[index] = value;
    }
    return digest;
}

std::optional<Sha256Digest> HashFileSha256(const std::filesystem::path& file) {
    std::ifstream input(file, std::ios::binary);
    if (!input.is_open()) {
        return std::nullopt;
    }
    Sha256 hash;
    std::vector<char> chunk(kFileChunkBytes);
    while (input) {
        input.read(chunk.data(), static_cast<std::streamsize>(chunk.size()));
        const std::streamsize read = input.gcount();
        if (read > 0) {
            hash.Update(std::as_bytes(std::span<const char>(chunk.data(), static_cast<std::size_t>(read))));
        }
    }
    if (input.bad()) {
        return std::nullopt;
    }
    return hash.Finish();
}

} // namespace UVE::Online
