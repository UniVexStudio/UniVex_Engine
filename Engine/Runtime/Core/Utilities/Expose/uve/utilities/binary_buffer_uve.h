// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace UVE::Utilities {

/// Appends `size` raw bytes starting at `data` to `buffer`. Byte order is meaningless for an
/// opaque run, so this is the one append without an endian spelling.
void AppendBytesUVE(std::vector<std::byte>& buffer, const void* data, std::size_t size);

/// Appends `value` to `buffer` in little-endian byte order, regardless of host endianness.
/// Every persisted format (saves, asset binaries, bundles) uses the little-endian spellings.
void AppendUint16LeUVE(std::vector<std::byte>& buffer, std::uint16_t value);

/// Appends `value` to `buffer` in little-endian byte order, regardless of host endianness.
void AppendUint32LeUVE(std::vector<std::byte>& buffer, std::uint32_t value);

/// Appends `value` to `buffer` in little-endian byte order, regardless of host endianness.
void AppendUint64LeUVE(std::vector<std::byte>& buffer, std::uint64_t value);

/// Appends `value`'s IEEE-754 bit pattern to `buffer` in little-endian byte order, regardless
/// of host endianness.
void AppendFloatLeUVE(std::vector<std::byte>& buffer, float value);

/// Appends `value` to `buffer` in big-endian byte order, regardless of host endianness. No
/// in-tree format uses big-endian today; these exist for the byte-swap simulation tests and
/// for any future big-endian peer.
void AppendUint16BeUVE(std::vector<std::byte>& buffer, std::uint16_t value);

/// Appends `value` to `buffer` in big-endian byte order, regardless of host endianness.
void AppendUint32BeUVE(std::vector<std::byte>& buffer, std::uint32_t value);

/// Appends `value` to `buffer` in big-endian byte order, regardless of host endianness.
void AppendUint64BeUVE(std::vector<std::byte>& buffer, std::uint64_t value);

/// Appends `value`'s IEEE-754 bit pattern to `buffer` in big-endian byte order, regardless
/// of host endianness.
void AppendFloatBeUVE(std::vector<std::byte>& buffer, float value);

/// Reads a little-endian `std::uint16_t` from `buffer` at `offset`, advancing `offset` past
/// it on success. Returns false, leaving `offset` and `outValue` unchanged, if fewer than
/// `sizeof(outValue)` bytes remain at `offset`.
[[nodiscard]] bool ReadUint16LeFromBufferUVE(const std::vector<std::byte>& buffer, std::size_t& offset,
                                             std::uint16_t& outValue);

/// Reads a little-endian `std::uint32_t` from `buffer` at `offset`, advancing `offset` past
/// it on success. Returns false, leaving `offset` and `outValue` unchanged, if fewer than
/// `sizeof(outValue)` bytes remain at `offset`.
[[nodiscard]] bool ReadUint32LeFromBufferUVE(const std::vector<std::byte>& buffer, std::size_t& offset,
                                             std::uint32_t& outValue);

/// Reads a little-endian `std::uint64_t` from `buffer` at `offset`, advancing `offset` past
/// it on success. Returns false, leaving `offset` and `outValue` unchanged, if fewer than
/// `sizeof(outValue)` bytes remain at `offset`.
[[nodiscard]] bool ReadUint64LeFromBufferUVE(const std::vector<std::byte>& buffer, std::size_t& offset,
                                             std::uint64_t& outValue);

/// Reads a little-endian IEEE-754 `float` from `buffer` at `offset`, advancing `offset` past
/// it on success. Returns false, leaving `offset` and `outValue` unchanged, if fewer than
/// `sizeof(outValue)` bytes remain at `offset`.
[[nodiscard]] bool ReadFloatLeFromBufferUVE(const std::vector<std::byte>& buffer, std::size_t& offset,
                                            float& outValue);

/// Reads a big-endian `std::uint16_t` from `buffer` at `offset`, advancing `offset` past it on
/// success. Same failure contract as the little-endian read.
[[nodiscard]] bool ReadUint16BeFromBufferUVE(const std::vector<std::byte>& buffer, std::size_t& offset,
                                             std::uint16_t& outValue);

/// Reads a big-endian `std::uint32_t` from `buffer` at `offset`, advancing `offset` past it on
/// success. Same failure contract as the little-endian read.
[[nodiscard]] bool ReadUint32BeFromBufferUVE(const std::vector<std::byte>& buffer, std::size_t& offset,
                                             std::uint32_t& outValue);

/// Reads a big-endian `std::uint64_t` from `buffer` at `offset`, advancing `offset` past it on
/// success. Same failure contract as the little-endian read.
[[nodiscard]] bool ReadUint64BeFromBufferUVE(const std::vector<std::byte>& buffer, std::size_t& offset,
                                             std::uint64_t& outValue);

/// Reads a big-endian IEEE-754 `float` from `buffer` at `offset`, advancing `offset` past it
/// on success. Same failure contract as the little-endian read.
[[nodiscard]] bool ReadFloatBeFromBufferUVE(const std::vector<std::byte>& buffer, std::size_t& offset,
                                            float& outValue);

/// Reads `length` bytes from `buffer` at `offset` into `outBytes`, advancing `offset` past them
/// on success. Returns false, leaving `offset` and `outBytes` unchanged, if fewer than `length`
/// bytes remain at `offset`.
[[nodiscard]] bool ReadBytesFromBufferUVE(const std::vector<std::byte>& buffer, std::size_t& offset,
                                          std::uint64_t length, std::vector<std::byte>& outBytes);

} // namespace UVE::Utilities
