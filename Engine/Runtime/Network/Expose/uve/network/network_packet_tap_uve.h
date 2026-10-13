// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once
//
// Scope: the live stream tap record behind session taps - one copied datagram plus the
// direction, clock, and channel the capture ring needs. The session owns the tap points
// (every send funnel, every drained datagram); the sink owns what happens next, usually
// a PacketCaptureUVE push with the host's own channel-byte mapping.
//

#include <cstdint>
#include <functional>
#include <optional>
#include <vector>

#include "uve/network/network_packet_direction_uve.h"

namespace UVE::Network {

/// One tapped datagram, copied at tap time: the wire buffer may be reused the moment the
/// send returns. `channel` carries the multiplexed data frame's channel byte (a
/// NetworkChannelIdUVE when set - the tap header stays lean by naming the raw type), and
/// is empty for untagged handshake frames and legacy data.
struct PacketTapUVE final {
    std::vector<std::uint8_t> datagram;
    PacketDirectionUVE direction = PacketDirectionUVE::Outbound;
    std::uint64_t timestampMs = 0U;
    bool multiplexed = false;
    std::optional<std::uint8_t> channel;

    [[nodiscard]] bool operator==(const PacketTapUVE&) const noexcept = default;
};

/// Synchronous tap sink: invoked on the tapping thread, inside the send or pump call. Must
/// not send on the tapped session (the tap would recurse); feeding a capture ring is safe.
using PacketTapSinkUVE = std::function<void(const PacketTapUVE&)>;

} // namespace UVE::Network
