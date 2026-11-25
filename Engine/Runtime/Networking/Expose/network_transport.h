// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
#pragma once
//
// Scope: the transport seam. This header owns the endpoint contract every transport speaks
// (the in-memory loopback below, UDP sockets in network_udp_uve.h) plus the loopback itself:
// two linked endpoints pushing copied datagrams through bounded FIFO queues. Sessions,
// connections, timers, retransmission, and replication all live above this line; the reliable
// window's framers (reliable_packet_window_uve.h) produce the bytes these endpoints carry, and
// the datagram cap below matches the window's payload bound so framed packets always fit.
// Deliberately single-threaded: concurrent calls from two threads are a data race, and the
// UDP endpoint keeps that rule (one thread per endpoint; copies share the socket, not a lock).
//
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

namespace UVE::Network {

/// One send pushes one receive: datagrams cross unfragmented and FIFO per direction. Sized to
/// the reliable window's payload bound on purpose (statically asserted in the .cpp) so a framed
/// packet never outgrows the transport that carries it.
inline constexpr std::size_t kTransportMaximumDatagramBytes = 1200U;

/// The endpoint contract: datagrams out, datagrams in, nothing else. A send that cannot be
/// queued reports false rather than blocking or fragmenting; a receive on an empty endpoint
/// reports empty rather than waiting. Every transport honors it, so sessions and replication
/// written against the loopback run unchanged on sockets later.
class ITransportEndpoint {
public:
    virtual ~ITransportEndpoint() = default;

    /// Queues one datagram for the peer. Returns false (dropping, counting the reason) when the
    /// payload exceeds `kTransportMaximumDatagramBytes` or the outbound queue is full.
    [[nodiscard]] virtual bool Send(const std::vector<std::uint8_t>& payload) = 0;
    /// Pops the oldest inbound datagram, or empty when none is queued. Never blocks.
    [[nodiscard]] virtual std::optional<std::vector<std::uint8_t>> TryReceive() = 0;
    [[nodiscard]] virtual bool HasPending() const noexcept = 0;
    [[nodiscard]] virtual std::size_t PendingCount() const noexcept = 0;
};

/// Per-endpoint delivery ledger: what this side sent, received, and dropped, with the drop
/// reason split so a test (and later, the net debugger) can tell "too big" from "too full".
struct LoopbackTransportStats final {
    std::uint64_t datagramsSent = 0U;
    std::uint64_t datagramsReceived = 0U;
    std::uint64_t datagramsDroppedFull = 0U;
    std::uint64_t datagramsDroppedOversize = 0U;

    [[nodiscard]] bool operator==(const LoopbackTransportStats&) const noexcept = default;
};

struct LoopbackLinkState;
struct LoopbackTransportPair;

class LoopbackEndpoint final : public ITransportEndpoint {
public:
    // Rule of five spelled out: the out-of-line destructor would otherwise suppress the
    // moves and turn std::move into a silent copy, breaking the moved-from promise below.
    LoopbackEndpoint(const LoopbackEndpoint&) = default;
    LoopbackEndpoint& operator=(const LoopbackEndpoint&) = default;
    LoopbackEndpoint(LoopbackEndpoint&&) noexcept = default;
    LoopbackEndpoint& operator=(LoopbackEndpoint&&) noexcept = default;
    ~LoopbackEndpoint() override;

    [[nodiscard]] bool Send(const std::vector<std::uint8_t>& payload) override;
    [[nodiscard]] std::optional<std::vector<std::uint8_t>> TryReceive() override;
    [[nodiscard]] bool HasPending() const noexcept override;
    [[nodiscard]] std::size_t PendingCount() const noexcept override;

    /// This endpoint's ledger. Moved-from endpoints report zeros (they are inert: sends fail,
    /// receives come back empty).
    [[nodiscard]] LoopbackTransportStats Stats() const noexcept;

private:
    friend LoopbackTransportPair CreateLoopbackTransportPair(std::size_t);
    LoopbackEndpoint(std::shared_ptr<LoopbackLinkState> link, bool isSideA) noexcept;

    std::shared_ptr<LoopbackLinkState> m_link;
    bool m_isSideA = true;
};

/// Two endpoints sharing one link: everything `a` sends waits in `b`'s inbound queue and vice
/// versa. An endpoint never receives its own sends.
struct LoopbackTransportPair final {
    LoopbackEndpoint a;
    LoopbackEndpoint b;
};

/// Links a fresh endpoint pair whose per-direction queues each hold `maxQueuedDatagrams` sends.
/// Zero is a valid configuration meaning "deliver nothing": every send drops and counts, which
/// the drop-path tests use instead of filling a queue. Endpoints copy freely (copies share the
/// link); the link dies with its last endpoint.
[[nodiscard]] LoopbackTransportPair CreateLoopbackTransportPair(std::size_t maxQueuedDatagrams);

} // namespace UVE::Network
