// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#include "uve/utilities/binary_buffer_uve.h"

#include <bit>
#include <cstddef>
#include <cstring>
#include <limits>

namespace UVE::Utilities {

static_assert(std::numeric_limits<float>::is_iec559,
              "Float (de)serialization assumes IEEE-754 bit patterns.");
static_assert(sizeof(float) == sizeof(std::uint32_t), "Float must be exactly 32 bits.");

void AppendBytesUVE(std::vector<std::byte>& buffer, const void* data, std::size_t size) {
    const auto* const bytes = static_cast<const std::byte*>(data);
    buffer.insert(buffer.end(), bytes, bytes + size);
}

void AppendUint16LeUVE(std::vector<std::byte>& buffer, const std::uint16_t value) {
    for (unsigned int shift = 0U; shift < 16U; shift += 8U) {
        buffer.push_back(static_cast<std::byte>((value >> shift) & 0xFFU));
    }
}

void AppendUint32LeUVE(std::vector<std::byte>& buffer, const std::uint32_t value) {
    for (std::uint32_t shift = 0U; shift < 32U; shift += 8U) {
        buffer.push_back(static_cast<std::byte>((value >> shift) & 0xFFU));
    }
}

void AppendUint64LeUVE(std::vector<std::byte>& buffer, const std::uint64_t value) {
    for (std::uint64_t shift = 0U; shift < 64U; shift += 8U) {
        buffer.push_back(static_cast<std::byte>((value >> shift) & 0xFFU));
    }
}

void AppendFloatLeUVE(std::vector<std::byte>& buffer, const float value) {
    AppendUint32LeUVE(buffer, std::bit_cast<std::uint32_t>(value));
}

void AppendUint16BeUVE(std::vector<std::byte>& buffer, const std::uint16_t value) {
    AppendUint16LeUVE(buffer, std::byteswap(value));
}

void AppendUint32BeUVE(std::vector<std::byte>& buffer, const std::uint32_t value) {
    AppendUint32LeUVE(buffer, std::byteswap(value));
}

void AppendUint64BeUVE(std::vector<std::byte>& buffer, const std::uint64_t value) {
    AppendUint64LeUVE(buffer, std::byteswap(value));
}

void AppendFloatBeUVE(std::vector<std::byte>& buffer, const float value) {
    AppendUint32BeUVE(buffer, std::bit_cast<std::uint32_t>(value));
}

bool ReadUint16LeFromBufferUVE(const std::vector<std::byte>& buffer, std::size_t& offset,
                               std::uint16_t& outValue) {
    if (offset > buffer.size() || buffer.size() - offset < sizeof(outValue)) {
        return false;
    }
    // Accumulated in unsigned int: the lanes shift past what std::uint16_t holds.
    unsigned int value = 0U;
    for (unsigned int lane = 0U; lane < 2U; ++lane) {
        value |= static_cast<unsigned int>(std::to_integer<std::uint8_t>(buffer[offset + lane])) <<
                 (lane * 8U);
    }
    offset += sizeof(outValue);
    outValue = static_cast<std::uint16_t>(value);
    return true;
}

bool ReadUint32LeFromBufferUVE(const std::vector<std::byte>& buffer, std::size_t& offset,
                               std::uint32_t& outValue) {
    if (offset > buffer.size() || buffer.size() - offset < sizeof(outValue)) {
        return false;
    }
    std::uint32_t value = 0U;
    for (std::uint32_t lane = 0U; lane < 4U; ++lane) {
        value |= static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(buffer[offset + lane])) <<
                 (lane * 8U);
    }
    offset += sizeof(outValue);
    outValue = value;
    return true;
}

bool ReadUint64LeFromBufferUVE(const std::vector<std::byte>& buffer, std::size_t& offset,
                               std::uint64_t& outValue) {
    if (offset > buffer.size() || buffer.size() - offset < sizeof(outValue)) {
        return false;
    }
    std::uint64_t value = 0U;
    for (std::uint64_t lane = 0U; lane < 8U; ++lane) {
        value |= static_cast<std::uint64_t>(std::to_integer<std::uint8_t>(buffer[offset + lane])) <<
                 (lane * 8U);
    }
    offset += sizeof(outValue);
    outValue = value;
    return true;
}

bool ReadFloatLeFromBufferUVE(const std::vector<std::byte>& buffer, std::size_t& offset,
                              float& outValue) {
    std::uint32_t bits = 0U;
    if (!ReadUint32LeFromBufferUVE(buffer, offset, bits)) {
        return false;
    }
    outValue = std::bit_cast<float>(bits);
    return true;
}

bool ReadUint16BeFromBufferUVE(const std::vector<std::byte>& buffer, std::size_t& offset,
                               std::uint16_t& outValue) {
    std::uint16_t value = 0U;
    if (!ReadUint16LeFromBufferUVE(buffer, offset, value)) {
        return false;
    }
    outValue = std::byteswap(value);
    return true;
}

bool ReadUint32BeFromBufferUVE(const std::vector<std::byte>& buffer, std::size_t& offset,
                               std::uint32_t& outValue) {
    std::uint32_t value = 0U;
    if (!ReadUint32LeFromBufferUVE(buffer, offset, value)) {
        return false;
    }
    outValue = std::byteswap(value);
    return true;
}

bool ReadUint64BeFromBufferUVE(const std::vector<std::byte>& buffer, std::size_t& offset,
                               std::uint64_t& outValue) {
    std::uint64_t value = 0U;
    if (!ReadUint64LeFromBufferUVE(buffer, offset, value)) {
        return false;
    }
    outValue = std::byteswap(value);
    return true;
}

bool ReadFloatBeFromBufferUVE(const std::vector<std::byte>& buffer, std::size_t& offset,
                              float& outValue) {
    std::uint32_t bits = 0U;
    if (!ReadUint32BeFromBufferUVE(buffer, offset, bits)) {
        return false;
    }
    outValue = std::bit_cast<float>(bits);
    return true;
}

bool ReadBytesFromBufferUVE(const std::vector<std::byte>& buffer, std::size_t& offset, std::uint64_t length,
                             std::vector<std::byte>& outBytes) {
    if (offset > buffer.size() || length > static_cast<std::uint64_t>(buffer.size() - offset)) {
        return false;
    }
    const std::size_t safeLength = static_cast<std::size_t>(length);
    outBytes.assign(buffer.begin() + static_cast<std::ptrdiff_t>(offset),
                    buffer.begin() + static_cast<std::ptrdiff_t>(offset + safeLength));
    offset += safeLength;
    return true;
}

} // namespace UVE::Utilities
