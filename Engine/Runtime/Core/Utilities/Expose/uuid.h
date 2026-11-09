// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>

namespace UVE::Utilities {

/// `UuidUVE` is the general 128-bit identity type: session ids, request correlation, anything
/// that needs uniqueness without a central counter. `AssetGuidUVE` stays the asset-specific one
/// (path-independent content identity is a different contract); this is the "just unique" one.
///
/// Randomness is caller-supplied (`MakeRandomV4UVE` takes two `uint64`s — typically
/// `RandomUVE::NextU64UVE()` draws) so this header stays dependency-free and determinism stays
/// the caller's choice: seeded draws give reproducible ids, `std::random_device` gives real
/// ones. Version and variant bits are set by construction, so every id this makes is a valid
/// RFC 4122 v4 UUID. Text form is the canonical lowercase `8-4-4-4-12` hex.
struct UuidUVE final {
    std::array<std::uint8_t, 16U> bytes{};

    /// The nil UUID (all zeros). Also the default.
    [[nodiscard]] static UuidUVE NilUVE() noexcept { return UuidUVE{}; }

    /// Wraps 16 raw bytes verbatim (no version/variant fixup — for ids read back from storage).
    [[nodiscard]] static UuidUVE FromBytesUVE(const std::array<std::uint8_t, 16U>& raw) noexcept {
        UuidUVE id;
        id.bytes = raw;
        return id;
    }

    /// A random v4 UUID from 122 bits of caller entropy: `high` supplies bytes 0..7, `low`
    /// bytes 8..15, then byte 6's high nibble becomes `0100` (version 4) and byte 8's high bits
    /// become `10` (RFC variant).
    [[nodiscard]] static UuidUVE MakeRandomV4UVE(const std::uint64_t high,
                                                 const std::uint64_t low) noexcept {
        UuidUVE id;
        for (std::size_t i = 0U; i < 8U; ++i) {
            id.bytes[i] = static_cast<std::uint8_t>(high >> ((7U - i) * 8U));
            id.bytes[i + 8U] = static_cast<std::uint8_t>(low >> ((7U - i) * 8U));
        }
        id.bytes[6] = static_cast<std::uint8_t>((id.bytes[6] & 0x0FU) | 0x40U);
        id.bytes[8] = static_cast<std::uint8_t>((id.bytes[8] & 0x3FU) | 0x80U);
        return id;
    }

    /// Parses the canonical form (36 chars, dashes at 8/13/18/23, hex case-insensitive).
    /// Anything else — wrong length, misplaced dashes, non-hex — returns nullopt.
    [[nodiscard]] static std::optional<UuidUVE> TryParseUVE(const std::string_view text) noexcept {
        if (text.size() != 36U || text[8] != '-' || text[13] != '-' || text[18] != '-' ||
            text[23] != '-') {
            return std::nullopt;
        }
        UuidUVE id;
        std::size_t byte = 0U;
        for (std::size_t i = 0U; i < 36U; ++i) {
            if (text[i] == '-') {
                continue;
            }
            const char hi = text[i];
            ++i;
            if (i >= 36U || text[i] == '-') {
                return std::nullopt;
            }
            const int high = HexValueUVE(hi);
            const int low = HexValueUVE(text[i]);
            if (high < 0 || low < 0) {
                return std::nullopt;
            }
            id.bytes[byte] = static_cast<std::uint8_t>((high << 4) | low);
            ++byte;
        }
        return id;
    }

    /// The canonical lowercase form.
    [[nodiscard]] std::string ToStringUVE() const {
        static constexpr char kHexUVE[] = "0123456789abcdef";
        std::string text;
        text.reserve(36U);
        for (std::size_t byte = 0U; byte < 16U; ++byte) {
            if (byte == 4U || byte == 6U || byte == 8U || byte == 10U) {
                text.push_back('-');
            }
            text.push_back(kHexUVE[bytes[byte] >> 4U]);
            text.push_back(kHexUVE[bytes[byte] & 0x0FU]);
        }
        return text;
    }

    /// True iff every byte is zero.
    [[nodiscard]] bool IsNilUVE() const noexcept {
        for (const std::uint8_t byte : bytes) {
            if (byte != 0U) {
                return false;
            }
        }
        return true;
    }

    [[nodiscard]] friend bool operator==(const UuidUVE& lhs, const UuidUVE& rhs) noexcept {
        return lhs.bytes == rhs.bytes;
    }

    [[nodiscard]] friend bool operator!=(const UuidUVE& lhs, const UuidUVE& rhs) noexcept {
        return !(lhs == rhs);
    }

private:
    [[nodiscard]] static int HexValueUVE(const char c) noexcept {
        if (c >= '0' && c <= '9') {
            return c - '0';
        }
        if (c >= 'a' && c <= 'f') {
            return c - 'a' + 10;
        }
        if (c >= 'A' && c <= 'F') {
            return c - 'A' + 10;
        }
        return -1;
    }
};

} // namespace UVE::Utilities

namespace std {

template <>
struct hash<UVE::Utilities::UuidUVE> final {
    [[nodiscard]] std::size_t operator()(const UVE::Utilities::UuidUVE& id) const noexcept {
        // FNV-1a over the bytes: order-sensitive, cheap, good enough for a table key.
        std::size_t acc = 1469598103934665603ULL;
        for (const std::uint8_t byte : id.bytes) {
            acc ^= static_cast<std::size_t>(byte);
            acc *= 1099511628211ULL;
        }
        return acc;
    }
};

} // namespace std
