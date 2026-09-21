// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace UVE::Utilities {

/// Appends `size` raw bytes starting at `data` to `buffer`.
void AppendBytesUVE(std::vector<std::byte>& buffer, const void* data, std::size_t size);

/// Appends `value` to `buffer` in host byte order.
void AppendUint32UVE(std::vector<std::byte>& buffer, std::uint32_t value);

/// Appends `value` to `buffer` in host byte order.
void AppendUint64UVE(std::vector<std::byte>& buffer, std::uint64_t value);

/// Appends `value` to `buffer` in host byte order.
void AppendFloatUVE(std::vector<std::byte>& buffer, float value);

/// Reads a `std::uint32_t` from `buffer` at `offset` (host byte order), advancing `offset` past
/// it on success. Returns false, leaving `offset` and `outValue` unchanged, if fewer than
/// `sizeof(outValue)` bytes remain at `offset`.
[[nodiscard]] bool ReadUint32FromBufferUVE(const std::vector<std::byte>& buffer, std::size_t& offset,
                                           std::uint32_t& outValue);

/// Reads a `std::uint64_t` from `buffer` at `offset` (host byte order), advancing `offset` past
/// it on success. Returns false, leaving `offset` and `outValue` unchanged, if fewer than
/// `sizeof(outValue)` bytes remain at `offset`.
[[nodiscard]] bool ReadUint64FromBufferUVE(const std::vector<std::byte>& buffer, std::size_t& offset,
                                           std::uint64_t& outValue);

/// Reads a `float` from `buffer` at `offset` (host byte order), advancing `offset` past it on
/// success. Returns false, leaving `offset` and `outValue` unchanged, if fewer than
/// `sizeof(outValue)` bytes remain at `offset`.
[[nodiscard]] bool ReadFloatFromBufferUVE(const std::vector<std::byte>& buffer, std::size_t& offset,
                                          float& outValue);

/// Reads `length` bytes from `buffer` at `offset` into `outBytes`, advancing `offset` past them
/// on success. Returns false, leaving `offset` and `outBytes` unchanged, if fewer than `length`
/// bytes remain at `offset`.
[[nodiscard]] bool ReadBytesFromBufferUVE(const std::vector<std::byte>& buffer, std::size_t& offset,
                                          std::uint64_t length, std::vector<std::byte>& outBytes);

} // namespace UVE::Utilities
