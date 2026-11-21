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

inline constexpr std::size_t kSha256DigestBytesUVE = 32U;
using Sha256DigestUVE = std::array<std::uint8_t, kSha256DigestBytesUVE>;

/// Incremental SHA-256 (FIPS 180-4), for verifying downloaded packages.
class Sha256UVE final {
public:
    Sha256UVE() noexcept;
    void UpdateUVE(std::span<const std::byte> data) noexcept;
    [[nodiscard]] Sha256DigestUVE FinishUVE() noexcept;

private:
    void TransformUVE(const std::uint8_t* block) noexcept;

    std::array<std::uint32_t, 8U> m_state{};
    std::array<std::uint8_t, 64U> m_buffer{};
    std::size_t m_buffered = 0U;
    std::uint64_t m_totalBytes = 0U;
};

/// Lower-case hexadecimal form of a digest.
[[nodiscard]] std::string ToHexUVE(const Sha256DigestUVE& digest);
/// The digest a 64-character hexadecimal string spells (either case), if it is one.
[[nodiscard]] std::optional<Sha256DigestUVE> ParseSha256HexUVE(std::string_view text);
/// The digest of a whole file, or nothing when it cannot be read.
[[nodiscard]] std::optional<Sha256DigestUVE> HashFileSha256UVE(const std::filesystem::path& file);

} // namespace UVE::Online
