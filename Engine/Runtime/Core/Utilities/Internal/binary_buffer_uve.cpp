// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#include "uve/utilities/binary_buffer_uve.h"

#include <cstring>

namespace UVE::Utilities {

void AppendBytesUVE(std::vector<std::byte>& buffer, const void* data, std::size_t size) {
    const auto* const bytes = static_cast<const std::byte*>(data);
    buffer.insert(buffer.end(), bytes, bytes + size);
}

void AppendUint32UVE(std::vector<std::byte>& buffer, std::uint32_t value) {
    AppendBytesUVE(buffer, &value, sizeof(value));
}

void AppendUint64UVE(std::vector<std::byte>& buffer, std::uint64_t value) {
    AppendBytesUVE(buffer, &value, sizeof(value));
}

void AppendFloatUVE(std::vector<std::byte>& buffer, float value) {
    AppendBytesUVE(buffer, &value, sizeof(value));
}

bool ReadUint32FromBufferUVE(const std::vector<std::byte>& buffer, std::size_t& offset,
                              std::uint32_t& outValue) {
    if (offset > buffer.size() || buffer.size() - offset < sizeof(outValue)) {
        return false;
    }
    std::memcpy(&outValue, buffer.data() + offset, sizeof(outValue));
    offset += sizeof(outValue);
    return true;
}

bool ReadUint64FromBufferUVE(const std::vector<std::byte>& buffer, std::size_t& offset,
                              std::uint64_t& outValue) {
    if (offset > buffer.size() || buffer.size() - offset < sizeof(outValue)) {
        return false;
    }
    std::memcpy(&outValue, buffer.data() + offset, sizeof(outValue));
    offset += sizeof(outValue);
    return true;
}

bool ReadFloatFromBufferUVE(const std::vector<std::byte>& buffer, std::size_t& offset, float& outValue) {
    if (offset > buffer.size() || buffer.size() - offset < sizeof(outValue)) {
        return false;
    }
    std::memcpy(&outValue, buffer.data() + offset, sizeof(outValue));
    offset += sizeof(outValue);
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
