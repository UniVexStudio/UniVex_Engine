// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once
//
// Samples/PacketCapture: the tap-fed capture demo - one multiplexed loopback pair, every
// feature talking at once, and a single server-side tap feeding one small ring:
//
//   traffic:  handshake, replication snapshots, an RPC call plus its response, two shot
//             intents, then two garbage frames (a foreign tag and a tag-only data frame);
//   mapping:  the host maps its own channel bytes (replication 21, RPC 22, shots 23) to
//             inspector channels - the pattern every future host copies;
//   pressure: the ring holds 8, the script pushes more, so drops count up and the oldest
//             falls off while the malformed tail stays put;
//   proof:    direction/channel/malformed/text filters slice the mixed ring, timestamps
//             run oldest-first, and one hexdump prints to the log.
//
// Transports are loopback (in-process, deterministic, no ports and no clock but the sim's).
//
// RunPacketCaptureSampleUVE() runs the whole story synchronously and answers a verdict:
// success plus statistics, or the list of violated invariants. The binary prints the log
// and exits by the verdict; the test suite runs it silent and asserts green.
//

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace UVE::Samples {

struct PacketCaptureSampleConfigUVE final {
    /// One human-readable log line. Empty silences the story; the verdict never needs it.
    std::function<void(const std::string&)> log;
};

struct PacketCaptureSampleStatsUVE final {
    /// Datagrams the tap fed the ring (every server-side send and receive).
    std::uint64_t pushed = 0U;
    /// Packets retained (the ring cap).
    std::uint64_t size = 0U;
    /// Pushes lost to the cap.
    std::uint64_t drops = 0U;
    /// Retained packets flagged malformed.
    std::uint64_t malformed = 0U;
    /// Direction filter counts (inbound plus outbound is the ring).
    std::uint64_t inbound = 0U;
    std::uint64_t outbound = 0U;
    /// Channel filter counts over the retained ring.
    std::uint64_t replication = 0U;
    std::uint64_t rpc = 0U;
    std::uint64_t shots = 0U;
    /// Text filter hits for "fire" (the shot summaries).
    std::uint64_t fireHits = 0U;
    /// Raw bytes of the oldest retained packet (the hexdumped one).
    std::uint64_t hexdumpBytes = 0U;
};

struct PacketCaptureSampleResultUVE final {
    bool success = false;
    std::vector<std::string> failures;
    PacketCaptureSampleStatsUVE stats;
};

[[nodiscard]] PacketCaptureSampleResultUVE RunPacketCaptureSampleUVE(
    const PacketCaptureSampleConfigUVE& config = PacketCaptureSampleConfigUVE{});

} // namespace UVE::Samples
