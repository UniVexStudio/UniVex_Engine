// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
#pragma once
//
// Scope: the multi-client fan-out - N server-role sessions under one pump, forwarding every
// inbound data payload to the other established peers in the sender's room as opaque bytes
// (channel bytes ride through untouched, so legacy and multiplexed clients relay
// identically). Rooms are plain tags with no registry: a room exists iff a peer carries its
// tag, and everyone starts in the default room, so untagged use is exactly broadcast. A
// pairwise interest matrix mutes fan-out per receiver (default interested, pruned on
// removal); the app syncs teams, proximity, or subscriptions into it. Handshakes,
// timeouts, and disconnects stay per-session - the relay just Updates them - and peers join
// and leave explicitly, never by surprise.
//
#include <cstddef>
#include <cstdint>
#include <map>
#include <set>

#include "uve/network/network_session_uve.h"

namespace UVE::Network {

/// Identifies one relay peer; 0 is invalid. Assigned monotonically, never reused, so a
/// removed id stays removed even if the count wraps the map.
using RelayPeerIdUVE = std::uint32_t;
inline constexpr RelayPeerIdUVE kInvalidRelayPeerIdUVE = 0U;
/// Tags one peer's fan-out room; rooms are implicit (a room exists iff a peer carries its
/// tag), so any value routes - including lobby room ids used verbatim for the binding.
using RelayRoomIdUVE = std::uint32_t;
/// The room every peer starts in: untagged use is exactly broadcast.
inline constexpr RelayRoomIdUVE kDefaultRelayRoomUVE = 0U;

struct RelayStatsUVE final {
    /// Inbound data payloads picked up across all peers.
    std::uint64_t payloadsReceived = 0U;
    /// Per-peer forward writes that sent (one inbound fans out to many of these).
    std::uint64_t payloadsForwarded = 0U;
    /// Forward writes the transport refused (destination queue full).
    std::uint64_t forwardsDropped = 0U;

    [[nodiscard]] bool operator==(const RelayStatsUVE&) const noexcept = default;
};

/// Room-scoped relay over any number of server-role sessions. Owns its sessions (they join by
/// move, already bound to their client endpoints); UpdateUVE pumps each one and forwards
/// everything it said to the other established peers in its room. Forwards ride the
/// relay-to-client
/// direction, which the relay never drains - a forward can never come back, so there is no
/// echo and no storm, only fan-out. Move-only (it owns its sessions); single-threaded like
/// everything below it.
class NetworkRelayUVE final {
public:
    NetworkRelayUVE() noexcept = default;

    /// Adds a server-role session bound to its client endpoint, returning its peer id.
    /// Anything else idles: the relay pumps what it is given, and only a server session
    /// answers a client's Hello. Hands out ids from 1, monotonically, never reused.
    [[nodiscard]] RelayPeerIdUVE AddPeerUVE(NetworkSessionUVE&& session);
    /// Drops a peer and its session. Returns false when the id is unknown (or already
    /// removed - ids never come back, so a second removal always fails).
    [[nodiscard]] bool RemovePeerUVE(RelayPeerIdUVE id) noexcept;
    /// Tags a peer's fan-out room (retags freely, takes effect on the next pump). Returns
    /// false when the id is unknown, changing nothing.
    [[nodiscard]] bool SetPeerRoomUVE(RelayPeerIdUVE id, RelayRoomIdUVE room) noexcept;
    /// The peer's fan-out room, or the default room when the id is unknown.
    [[nodiscard]] RelayRoomIdUVE PeerRoomUVE(RelayPeerIdUVE id) const noexcept;
    /// Mutes (or unmutes) `sender` for `receiver`: a muted sender's payloads never reach
    /// that receiver, though everyone else still hears them. One-way by design (teams,
    /// proximity, and subscriptions all sync through here); self-mutes are stored but never
    /// consulted, since the relay never echoes. Returns false when either id is unknown,
    /// changing nothing.
    [[nodiscard]] bool SetPeerInterestUVE(RelayPeerIdUVE receiver, RelayPeerIdUVE sender,
                                          bool interested);
    /// True unless `receiver` muted `sender` - including unknown ids, which fail open to
    /// the default.
    [[nodiscard]] bool IsPeerInterestedUVE(RelayPeerIdUVE receiver,
                                           RelayPeerIdUVE sender) const noexcept;
    /// Pumps every session in peer-id order, then forwards everything each one said to the
    /// other established, interested peers in its room. Never echoes back to the sender;
    /// peers outside the room, muted senders, and non-established peers neither send
    /// (nothing queued for them) nor receive (skipped silently, never counted dropped).
    /// A forward queued behind a later peer's pump still delivers that same tick; one queued
    /// behind an earlier peer's waits for the next - delayed at most one relay tick, never lost
    /// to ordering.
    void UpdateUVE(std::uint64_t nowMs);

    /// The peer's session for state checks and hang-ups, or nullptr when unknown. The relay
    /// keeps ownership - borrow, never move out.
    [[nodiscard]] NetworkSessionUVE* PeerSessionUVE(RelayPeerIdUVE id) noexcept;
    [[nodiscard]] const NetworkSessionUVE* PeerSessionUVE(RelayPeerIdUVE id) const noexcept;
    [[nodiscard]] RelayStatsUVE StatsUVE() const noexcept { return m_stats; }
    [[nodiscard]] std::size_t PeerCountUVE() const noexcept { return m_peers.size(); }

private:
    struct RelayPeerUVE final {
        NetworkSessionUVE session;
        RelayRoomIdUVE room = kDefaultRelayRoomUVE;
    };

    // Ordered by peer id, so every pump and every fan-out runs deterministically: the same
    // arrivals always forward in the same order.
    std::map<RelayPeerIdUVE, RelayPeerUVE> m_peers;
    // Interest exceptions only: receivers mapped to the senders they muted. Everything
    // unlisted is interested, and removals prune both directions back to that default.
    std::map<RelayPeerIdUVE, std::set<RelayPeerIdUVE>> m_muted;
    RelayPeerIdUVE m_nextPeerId = kInvalidRelayPeerIdUVE + 1U;
    RelayStatsUVE m_stats;
};

} // namespace UVE::Network
