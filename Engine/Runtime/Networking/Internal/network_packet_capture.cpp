// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "network_packet_capture.h"

namespace UVE::Network {
namespace {

// Locale-free hex, so a Turkish locale never turns up in a hexdump.
[[nodiscard]] char Nibble(const std::uint8_t value) noexcept {
    return static_cast<char>(value < 10U ? '0' + value : 'A' + (value - 10U));
}

} // namespace

PacketCapture::PacketCapture(const PacketCaptureConfig config) : m_config(config) {}

void PacketCapture::Push(const std::vector<std::uint8_t>& datagram,
                               const PacketDirection direction, const std::uint64_t timestampMs,
                               const InspectedChannel channel, const bool multiplexed) {
    ++m_captured;
    if (m_config.maxPackets == 0U) {
        ++m_drops;
        return;
    }
    InspectedPacket packet =
        InspectPacket(datagram, direction, timestampMs, channel, multiplexed);
    if (packet.malformed) {
        ++m_malformed;
    }
    m_packets.push_back(std::move(packet));
    // A vector ring with erase-at-front: O(cap) memmove per drop, which is nothing at
    // debug scale, and keeps indexing trivial for the viewer.
    if (m_packets.size() > m_config.maxPackets) {
        if (m_packets.front().malformed) {
            --m_malformed;
        }
        m_packets.erase(m_packets.begin());
        ++m_drops;
    }
}

void PacketCapture::Clear() noexcept {
    m_packets.clear();
    m_captured = 0U;
    m_drops = 0U;
    m_malformed = 0U;
}

const InspectedPacket* PacketCapture::TryAt(const std::size_t index) const noexcept {
    return index < m_packets.size() ? &m_packets[index] : nullptr;
}

std::vector<std::size_t> PacketCapture::Filtered(
    const PacketCaptureFilter& filter) const {
    std::vector<std::size_t> indices;
    for (std::size_t i = 0U; i < m_packets.size(); ++i) {
        const InspectedPacket& packet = m_packets[i];
        if (filter.direction == PacketCaptureDirection::Outbound &&
            packet.direction != PacketDirection::Outbound) {
            continue;
        }
        if (filter.direction == PacketCaptureDirection::Inbound &&
            packet.direction != PacketDirection::Inbound) {
            continue;
        }
        if (filter.channel.has_value() && packet.channel != *filter.channel) {
            continue;
        }
        if (filter.malformedOnly && !packet.malformed) {
            continue;
        }
        if (!filter.text.empty() &&
            DescribePacket(packet).find(filter.text) == std::string::npos) {
            continue;
        }
        indices.push_back(i);
    }
    return indices;
}

std::string HexdumpPacket(const std::vector<std::uint8_t>& raw, std::size_t bytesPerRow) {
    if (raw.empty()) {
        return "";
    }
    if (bytesPerRow == 0U) {
        bytesPerRow = 16U;
    }
    if (bytesPerRow > 64U) {
        bytesPerRow = 64U;
    }
    std::string out;
    for (std::size_t row = 0U; row < raw.size(); row += bytesPerRow) {
        if (row > 0U) {
            out += '\n';
        }
        const std::size_t offset = row;
        for (int shift = 28; shift >= 0; shift -= 4) {
            out += Nibble(static_cast<std::uint8_t>((offset >> shift) & 0xFU));
        }
        out += ": ";
        const std::size_t rowBytes =
            raw.size() - row < bytesPerRow ? raw.size() - row : bytesPerRow;
        for (std::size_t i = 0U; i < bytesPerRow; ++i) {
            if (i < rowBytes) {
                const std::uint8_t byte = raw[row + i];
                out += Nibble(static_cast<std::uint8_t>(byte >> 4));
                out += Nibble(static_cast<std::uint8_t>(byte & 0xFU));
            } else {
                out += "  ";
            }
            out += ' ';
        }
        out += '|';
        for (std::size_t i = 0U; i < rowBytes; ++i) {
            const std::uint8_t byte = raw[row + i];
            out += (byte >= 0x20U && byte <= 0x7EU) ? static_cast<char>(byte) : '.';
        }
        out += '|';
    }
    return out;
}

} // namespace UVE::Network
