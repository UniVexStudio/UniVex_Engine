// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace UVE::Online {

/// SHA-256 parameters (FIPS 180-4, sections 1 and 5.3.3): 32-bit words, a 512-bit message block,
/// a 256-bit state of eight words, and 64 rounds per block.
inline constexpr std::size_t kSha256WordBytesUVE = sizeof(std::uint32_t);
inline constexpr std::size_t kSha256StateWordsUVE = 8U;
inline constexpr std::size_t kSha256BlockBytesUVE = 64U;
inline constexpr std::size_t kSha256RoundsUVE = 64U;
inline constexpr std::size_t kSha256DigestBytesUVE = kSha256StateWordsUVE * kSha256WordBytesUVE;
using Sha256DigestUVE = std::array<std::uint8_t, kSha256DigestBytesUVE>;

/// Incremental SHA-256 (FIPS 180-4), for verifying downloaded packages.
class Sha256UVE final {
public:
    Sha256UVE() noexcept;
    void UpdateUVE(std::span<const std::byte> data) noexcept;
    [[nodiscard]] Sha256DigestUVE FinishUVE() noexcept;

private:
    void TransformUVE(const std::uint8_t* block) noexcept;

    std::array<std::uint32_t, kSha256StateWordsUVE> m_state{};
    std::array<std::uint8_t, kSha256BlockBytesUVE> m_buffer{};
    std::size_t m_buffered = 0U;
    std::uint64_t m_totalBytes = 0U;
};

/// Lower-case hexadecimal form of a digest.
[[nodiscard]] std::string ToHexUVE(const Sha256DigestUVE& digest);
/// The digest a hexadecimal string of twice kSha256DigestBytesUVE characters spells (either case), if it is one.
[[nodiscard]] std::optional<Sha256DigestUVE> ParseSha256HexUVE(std::string_view text);
/// The digest of a whole file, or nothing when it cannot be read.
[[nodiscard]] std::optional<Sha256DigestUVE> HashFileSha256UVE(const std::filesystem::path& file);

} // namespace UVE::Online
