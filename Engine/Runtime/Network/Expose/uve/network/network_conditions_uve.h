// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
#pragma once
//
// Scope: the network-condition simulator - ROADMAP section 6's "simulated latency/loss for
// testing", as a decorator over any ITransportEndpointUVE. Deterministic scripts, never random
// numbers: drop every Nth datagram, delay every datagram by a fixed span, per direction. Time
// comes from a constructor clock (production passes its own, tests a fake counter) because the
// endpoint interface takes no time. Release is lazy: every Send and TryReceive first releases
// whatever came due, so there is no Update to call - a quiet simulator simply holds delayed
// mail until its next call. Jitter, reordering, and duplication are named follow-ups.
//
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <optional>
#include <vector>

#include "uve/network/network_transport_uve.h"

namespace UVE::Network {

/// Monotonic millisecond clock the simulator reads on every call. Must be non-empty; violating
/// that throws std::bad_function_call instead of silently freezing time.
using TransportClockUVE = std::function<std::uint64_t()>;

/// Drop and delay scripts, per direction. A zero period never drops; period 1 drops everything;
/// otherwise every Nth datagram is lost (1st sent ... Nth dropped, repeating). A zero delay
/// forwards immediately; anything larger holds the datagram until the clock passes send time
/// plus the span. Drops report Send success - the sender cannot distinguish loss, exactly like
/// the wire - and only the stats reveal them.
struct SimulatedTransportConfigUVE final {
    std::uint64_t outboundDropPeriod = 0U;
    std::uint64_t inboundDropPeriod = 0U;
    std::uint64_t outboundDelayMs = 0U;
    std::uint64_t inboundDelayMs = 0U;
    /// Bound on each delay queue. A send past a full outbound delay queue reports false (raw
    /// backpressure, like the transports below); the inbound side is unbounded past this only in
    /// the sense that TryReceive pulls on demand.
    std::size_t maxPendingDatagrams = 1024U;

    [[nodiscard]] bool operator==(const SimulatedTransportConfigUVE&) const noexcept = default;
};

/// What the simulator itself did: scripted drops and delay-queue overflows, per direction.
/// judgments the inner transport makes (oversize, ITS queue full) stay in ITS stats - this
/// ledger only counts the simulator's own verdicts.
struct SimulatedTransportStatsUVE final {
    std::uint64_t outboundDropped = 0U;
    std::uint64_t outboundDroppedFull = 0U;
    std::uint64_t inboundDropped = 0U;
    std::uint64_t inboundDroppedFull = 0U;

    [[nodiscard]] bool operator==(const SimulatedTransportStatsUVE&) const noexcept = default;
};

/// Loss and latency around one endpoint. Move-only (it owns its inner endpoint);
/// single-threaded like everything below it.
class SimulatedTransportUVE final : public ITransportEndpointUVE {
public:
    SimulatedTransportUVE(std::unique_ptr<ITransportEndpointUVE> inner, TransportClockUVE clock,
                          SimulatedTransportConfigUVE config = SimulatedTransportConfigUVE{});

    [[nodiscard]] bool SendUVE(const std::vector<std::uint8_t>& payload) override;
    [[nodiscard]] std::optional<std::vector<std::uint8_t>> TryReceiveUVE() override;
    /// True when a due datagram waits OR the inner endpoint holds anything - the second case is
    /// approximate (inner mail may all be delayed once pulled), so pump loops must break on an
    /// empty TryReceive, exactly as they already do.
    [[nodiscard]] bool HasPendingUVE() const noexcept override;
    /// Queued-here plus inner-held, with the same approximation as HasPendingUVE.
    [[nodiscard]] std::size_t PendingCountUVE() const noexcept override;

    /// Swaps the scripts live: loss can start halfway through a test. Queued mail keeps the
    /// release times it was given; only new datagrams follow the new script.
    void SetConfigUVE(SimulatedTransportConfigUVE config) noexcept;
    [[nodiscard]] SimulatedTransportStatsUVE StatsUVE() const noexcept { return m_stats; }

private:
    struct DelayedDatagramUVE final {
        std::uint64_t releaseAtMs = 0U;
        std::vector<std::uint8_t> bytes;
    };

    void ReleaseDueOutboundUVE(std::uint64_t nowMs);

    std::unique_ptr<ITransportEndpointUVE> m_inner;
    TransportClockUVE m_clock;
    SimulatedTransportConfigUVE m_config;
    std::uint64_t m_outboundCount = 0U;
    std::uint64_t m_inboundCount = 0U;
    std::deque<DelayedDatagramUVE> m_outboundDelayed;
    std::deque<DelayedDatagramUVE> m_inboundDelayed;
    SimulatedTransportStatsUVE m_stats;
};

} // namespace UVE::Network
