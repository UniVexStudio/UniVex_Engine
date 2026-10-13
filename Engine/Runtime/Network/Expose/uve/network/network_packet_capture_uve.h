// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once
//
// Scope: the packet capture ring behind the editor's Capture dock - bounded storage for
// tapped session datagrams plus the filters and hexdump the viewer needs. Taps push raw
// datagrams (bytes, direction, timestamp, channel); the ring decodes on push through the
// inspector, drops oldest past its cap, and answers oldest-first. Zero capacity disables
// the capture (every push drops and counts) without unhooking the taps.
//
// This is deliberately live-tap-agnostic: who calls PushUVE (a test harness today, stream
// taps tomorrow) is the feeder's business - the ring only keeps, filters, and formats.
//

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "uve/network/network_inspector_uve.h"

namespace UVE::Network {

/// Ring bound: 512 decoded datagrams, drop-oldest past it. Debug-scale by design - a
/// capture is a glance at the wire, not a recording.
struct PacketCaptureConfigUVE final {
    std::size_t maxPackets = 512U;

    [[nodiscard]] bool operator==(const PacketCaptureConfigUVE&) const noexcept = default;
};

enum class PacketCaptureDirectionUVE : std::uint8_t { Both = 0, Outbound, Inbound };

/// What the viewer shows: a direction, an optional channel, a malformed-only switch, and a
/// substring matched against each packet's DescribePacketUVE line (empty matches all).
/// Stateless on purpose - the ring keeps packets, the caller keeps its filter UI.
struct PacketCaptureFilterUVE final {
    PacketCaptureDirectionUVE direction = PacketCaptureDirectionUVE::Both;
    std::optional<InspectedChannelUVE> channel;
    bool malformedOnly = false;
    std::string text;

    [[nodiscard]] bool operator==(const PacketCaptureFilterUVE&) const noexcept = default;
};

/// Bounded oldest-first ring of decoded session datagrams. Not thread-safe: taps push from
/// the single thread that pumps the watched session.
class PacketCaptureUVE final {
public:
    explicit PacketCaptureUVE(PacketCaptureConfigUVE config = PacketCaptureConfigUVE{});

    /// Decodes one datagram and appends it, dropping the oldest past the cap (counted).
    /// Timestamps come from the tap's clock; the ring never reorders, so feed it in
    /// arrival order.
    void PushUVE(const std::vector<std::uint8_t>& datagram, PacketDirectionUVE direction,
                 std::uint64_t timestampMs,
                 InspectedChannelUVE channel = InspectedChannelUVE::Unknown,
                 bool multiplexed = false);

    /// Empties the ring and zeroes every counter: the next push starts a fresh capture.
    void ClearUVE() noexcept;

    [[nodiscard]] std::size_t SizeUVE() const noexcept { return m_packets.size(); }
    [[nodiscard]] bool EmptyUVE() const noexcept { return m_packets.empty(); }
    [[nodiscard]] std::size_t CapacityUVE() const noexcept { return m_config.maxPackets; }

    /// Oldest-first packet access; nullptr past the end (the viewer holds indices across
    /// pushes, so out-of-range is a lookup miss, not a bug).
    [[nodiscard]] const InspectedPacketUVE* TryAtUVE(std::size_t index) const noexcept;

    /// Indices matching `filter`, oldest-first. An empty filter matches everything.
    [[nodiscard]] std::vector<std::size_t> FilteredUVE(
        const PacketCaptureFilterUVE& filter) const;

    /// Total pushes ever (stored plus dropped).
    [[nodiscard]] std::uint64_t CapturedUVE() const noexcept { return m_captured; }
    /// Pushes lost to the cap (plus every push while capacity is zero).
    [[nodiscard]] std::uint64_t DropsUVE() const noexcept { return m_drops; }
    /// Stored packets flagged malformed (dropped ones stop counting).
    [[nodiscard]] std::uint64_t MalformedUVE() const noexcept { return m_malformed; }

private:
    PacketCaptureConfigUVE m_config;
    std::vector<InspectedPacketUVE> m_packets;
    std::uint64_t m_captured = 0U;
    std::uint64_t m_drops = 0U;
    std::uint64_t m_malformed = 0U;
};

/// Classic hexdump rows - `00000000: 48 65 ... |Hello|` - for a captured datagram's raw
/// bytes: 8-digit hex offset, blank-padded hex slots, printable ASCII (else '.'). Rows join
/// with '\n' (no trailing newline); empty input formats empty. A zero row width means the
/// default 16, past 64 clamps to 64.
[[nodiscard]] std::string HexdumpPacketUVE(const std::vector<std::uint8_t>& raw,
                                           std::size_t bytesPerRow = 16U);

} // namespace UVE::Network
