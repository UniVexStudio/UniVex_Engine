// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
#pragma once
//
// Scope: the UDP transport. Connected datagram sockets behind the ITransportEndpoint
// seam, so everything written against the loopback (sessions, replication, simulated
// conditions) runs unchanged on real networking: same contract, same single-threaded pump,
// same 1200-byte cap. Clients take exactly one peer per endpoint - the socket is
// connected(), which fixes the default remote and lets the kernel discard anything from
// anywhere else. Servers multiplex many peers onto one socket through the listener below:
// it pumps recvfrom, sorts by sender, and hands each accepted peer an
// ITransportEndpoint, so sessions written against one-socket-per-peer run unchanged on
// a shared socket. Clients resolve dotted quads plus hostnames (getaddrinfo, first IPv4
// answer wins, synchronously at setup); listener peer addresses are learned from the wire,
// never resolved.
//
#include <compare>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "network_transport.h"

namespace UVE::Network {

/// Where to bind, who to talk to. A zero local port binds an ephemeral one (read it back
/// with LocalPort); the remote is a dotted-IPv4 quad or a resolvable hostname (first
/// IPv4 answer wins) - an unresolvable name fails the factory, loudly, rather than
/// resolving halfway.
struct UdpTransportConfig final {
    std::uint16_t localPort = 0U;
    std::string remoteHost = "127.0.0.1";
    std::uint16_t remotePort = 0U;
    /// Bound on the inbound queue the socket drains into. Past it, newly arrived datagrams
    /// count as dropped-full - tail-drop, like the loopback's full-queue send.
    std::size_t maxPendingDatagrams = 256U;

    [[nodiscard]] bool operator==(const UdpTransportConfig&) const noexcept = default;
};

/// The socket's own ledger. Mirrors the loopback stats field for field: sent counts accepted
/// sends, received counts pops, and every drop names its reason.
struct UdpTransportStats final {
    std::uint64_t datagramsSent = 0U;
    std::uint64_t datagramsReceived = 0U;
    std::uint64_t datagramsDroppedFull = 0U;
    std::uint64_t datagramsDroppedOversize = 0U;

    [[nodiscard]] bool operator==(const UdpTransportStats&) const noexcept = default;
};

struct UdpSocketState;

/// One connected UDP socket: non-blocking, drained FIFO into a bounded queue on receive.
/// Copyable, and copies share the socket plus its ledger (one fd, many handles); moved-from
/// endpoints are inert - sends fail, receives stay empty, stats read zero. Pending counts
/// are a lower bound (queued here plus one if the socket visibly holds more), so pump loops
/// must break on an empty TryReceive, exactly as they already do.
class UdpTransportEndpoint final : public ITransportEndpoint {
public:
    explicit UdpTransportEndpoint(std::shared_ptr<UdpSocketState> state) noexcept;
    // Rule of five spelled out: the out-of-line destructor would otherwise suppress the
    // moves and turn std::move into a silent copy.
    UdpTransportEndpoint(const UdpTransportEndpoint&) = default;
    UdpTransportEndpoint& operator=(const UdpTransportEndpoint&) = default;
    UdpTransportEndpoint(UdpTransportEndpoint&&) noexcept = default;
    UdpTransportEndpoint& operator=(UdpTransportEndpoint&&) noexcept = default;
    ~UdpTransportEndpoint() override;

    /// Sends one datagram to the connected peer. False (dropping, counting the reason) when
    /// the payload exceeds `kTransportMaximumDatagramBytes` or the send fails - a full
    /// kernel buffer and an unreachable peer both land in dropped-full, since neither can be
    /// distinguished from here and retransmission lives above this line.
    [[nodiscard]] bool Send(const std::vector<std::uint8_t>& payload) override;
    /// Drains the socket into the queue, then pops the oldest datagram - or empty when both
    /// are dry. Never blocks. Oversize wire datagrams count as dropped-oversize even though
    /// no send of ours produced them: they arrived, and they did not fit.
    [[nodiscard]] std::optional<std::vector<std::uint8_t>> TryReceive() override;
    /// True when the queue holds anything or the socket visibly holds more.
    [[nodiscard]] bool HasPending() const noexcept override;
    /// Queued here plus one if the socket visibly holds more - a lower bound, never an
    /// overcount: the kernel backlog past the first datagram is unknowable without draining.
    [[nodiscard]] std::size_t PendingCount() const noexcept override;

    [[nodiscard]] UdpTransportStats Stats() const noexcept;
    /// The bound local port: the configured one, or the ephemeral one the OS picked. Zero
    /// when moved-from.
    [[nodiscard]] std::uint16_t LocalPort() const noexcept;

private:
    std::shared_ptr<UdpSocketState> m_state;
};

/// Binds, connects, and flips the socket non-blocking; empty when anything fails (bad
/// remote, socket/bind/connect refused, or the local port already taken). No exceptions,
/// no half-built sockets: failure closes whatever got opened.
[[nodiscard]] std::optional<UdpTransportEndpoint> CreateUdpTransportEndpoint(
    const UdpTransportConfig& config);

/// Resolves `host` to an IPv4 address in host byte order (the UdpEndpointAddress
/// convention): dotted quads parse directly without touching the name service, anything
/// else goes through getaddrinfo with the first AF_INET answer winning. Empty when nothing
/// resolves - unknown names, IPv6-only answers, and garbage all fail closed, never
/// halfway. Synchronous: name-service lookups can block, so this runs at setup (the
/// endpoint factory calls it) and never on the pump path.
[[nodiscard]] std::optional<std::uint32_t> ResolveUdpHost(const std::string& host);

/// Who sent a datagram: an IPv4 host plus a port, both in host byte order, learned from
/// recvfrom - the demux key one socket serves many clients by. Ordered, so peer and
/// stranger tables iterate deterministically.
struct UdpEndpointAddress final {
    std::uint32_t host = 0U;
    std::uint16_t port = 0U;

    [[nodiscard]] auto operator<=>(const UdpEndpointAddress&) const noexcept = default;
    /// "127.0.0.1:41741" - for logs and the net debugger, never parsed back.
    [[nodiscard]] std::string ToString() const;
};

/// Where to listen and how much to hold. A zero local port binds an ephemeral one (read it
/// back with LocalPort), exactly like the connected endpoint.
struct UdpListenerConfig final {
    std::uint16_t localPort = 0U;
    /// Distinct unknown senders held at once; past it, new senders' datagrams drop and count.
    std::size_t maxStrangers = 64U;
    /// Datagrams held per unknown sender until accept-or-refuse; past it, the excess drops
    /// and counts. Sized for the accept gap (a Hello plus a redial or two), not a queue.
    std::size_t maxStrangerDatagrams = 16U;
    /// recvfrom calls per pump at most: a flooding stranger cannot pin Update in the
    /// kernel, and the next pump resumes where this one stopped.
    std::size_t maxPumpDatagrams = 1024U;

    [[nodiscard]] bool operator==(const UdpListenerConfig&) const noexcept = default;
};

/// The listener's own ledger. Every drained datagram lands in exactly one bucket - routed,
/// held, full, oversize, or stranger-full - so received always equals their sum.
struct UdpListenerStats final {
    std::uint64_t datagramsReceived = 0U;
    std::uint64_t datagramsRouted = 0U;
    std::uint64_t datagramsHeldForStrangers = 0U;
    std::uint64_t datagramsDroppedFull = 0U;
    std::uint64_t datagramsDroppedOversize = 0U;
    std::uint64_t strangersDroppedFull = 0U;

    [[nodiscard]] bool operator==(const UdpListenerStats&) const noexcept = default;
};

struct UdpListenerState;

/// One accepted client on a shared socket: sends go out by sendto to its address, receives
/// pop the queue the listener's pump feeds - this endpoint never touches the socket for
/// input, so PendingCount is exact (no lower-bound fudge like the connected endpoint's).
/// Copyable, and copies share the socket plus the peer's queue and ledger; moved-from
/// endpoints are inert. A removed peer orphans its endpoints: sends fail, receives run dry.
class UdpListenerPeer final : public ITransportEndpoint {
public:
    UdpListenerPeer(std::shared_ptr<UdpListenerState> state,
                       UdpEndpointAddress remote) noexcept;
    // Rule of five spelled out, like the connected endpoint: the out-of-line destructor
    // would otherwise suppress the moves and turn std::move into a silent copy.
    UdpListenerPeer(const UdpListenerPeer&) = default;
    UdpListenerPeer& operator=(const UdpListenerPeer&) = default;
    UdpListenerPeer(UdpListenerPeer&&) noexcept = default;
    UdpListenerPeer& operator=(UdpListenerPeer&&) noexcept = default;
    ~UdpListenerPeer() override;

    /// Sends one datagram to this peer's address. False (dropping, counting the reason) when
    /// the payload exceeds the cap, the send fails, or the peer was removed.
    [[nodiscard]] bool Send(const std::vector<std::uint8_t>& payload) override;
    /// Pops the oldest queued datagram - or empty when the queue is dry. Never blocks, never
    /// drains the socket: only the listener's Update feeds these queues, so a peer whose
    /// listener never pumps never receives.
    [[nodiscard]] std::optional<std::vector<std::uint8_t>> TryReceive() override;
    [[nodiscard]] bool HasPending() const noexcept override;
    /// The queued datagram count, exact: peers hold no socket backlog of their own.
    [[nodiscard]] std::size_t PendingCount() const noexcept override;

    /// This peer's ledger (send-side oversize only: wire-oversize never reaches a peer, the
    /// listener counts it). Moved-from - and removed-peer - endpoints report zeros.
    [[nodiscard]] UdpTransportStats Stats() const noexcept;
    /// Who this peer is: the address its sends go to and its receives came from.
    [[nodiscard]] UdpEndpointAddress Remote() const noexcept;

private:
    std::shared_ptr<UdpListenerState> m_state;
    UdpEndpointAddress m_remote;
};

/// One unconnected UDP socket serving many clients: Update drains recvfrom and sorts by
/// sender - accepted peers' datagrams queue for their endpoints, unknown senders' hold in
/// the stranger backlog until accept-or-refuse. Accepting replays the held datagrams into
/// the new peer's queue first, so the client's opening Hello survives the accept gap and no
/// redial is needed. Move-only (it owns the bind and the pump); the socket closes with the
/// last handle, listener or peer. Single-threaded like everything below the session.
class UdpListener final {
public:
    explicit UdpListener(std::shared_ptr<UdpListenerState> state) noexcept;
    UdpListener(const UdpListener&) = delete;
    UdpListener& operator=(const UdpListener&) = delete;
    UdpListener(UdpListener&&) noexcept = default;
    UdpListener& operator=(UdpListener&&) noexcept = default;
    ~UdpListener() = default;

    /// Drains the socket (at most the configured pump budget) and sorts every datagram:
    /// accepted senders route to their peer queue (full queues drop and count), unknown
    /// senders hold in the stranger backlog (bounded per sender and in sender count; the
    /// overflow drops and counts). Oversize wire datagrams count and never route.
    void Update();
    /// True when any unknown sender waits; the oldest waiter's address, if so.
    [[nodiscard]] bool HasStrangers() const noexcept;
    [[nodiscard]] std::optional<UdpEndpointAddress> NextStranger() const;
    [[nodiscard]] std::size_t StrangerCount() const noexcept;
    /// Accepts the oldest stranger (or the named one): replays its held datagrams into the
    /// new peer's queue in arrival order - even past the new bound, lossless beats tidy -
    /// then returns its endpoint. Empty when nobody waits or, for the named form, when that
    /// address is not a waiting stranger: accept hands out each peer exactly once.
    [[nodiscard]] std::optional<UdpListenerPeer> AcceptPeer(
        std::size_t maxPendingDatagrams = 256U);
    [[nodiscard]] std::optional<UdpListenerPeer> AcceptPeer(
        UdpEndpointAddress stranger, std::size_t maxPendingDatagrams = 256U);
    /// Drops a stranger's held datagrams and forgets it. False when the address is not a
    /// waiting stranger. Refusal is forgetting, not banning: its next datagram strangers it
    /// again.
    [[nodiscard]] bool RefuseStranger(UdpEndpointAddress stranger);
    /// Drops a peer, its queue, and its ledger. False when unknown. Live endpoints for it
    /// orphan (sends fail, receives run dry); its next datagram strangers it again.
    [[nodiscard]] bool RemovePeer(UdpEndpointAddress peer);
    [[nodiscard]] bool HasPeer(UdpEndpointAddress peer) const noexcept;
    [[nodiscard]] std::size_t PeerCount() const noexcept;

    [[nodiscard]] UdpListenerStats Stats() const noexcept;
    /// The bound local port: the configured one, or the ephemeral one the OS picked.
    [[nodiscard]] std::uint16_t LocalPort() const noexcept;

private:
    std::shared_ptr<UdpListenerState> m_state;
};

/// Binds an unconnected socket and flips it non-blocking; empty when anything fails (socket
/// refused, or the local port already taken). No exceptions, no half-built sockets.
[[nodiscard]] std::optional<UdpListener> CreateUdpListener(
    const UdpListenerConfig& config);

} // namespace UVE::Network
