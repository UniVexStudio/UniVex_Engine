// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/network/network_conditions_uve.h"

namespace UVE::Network {

SimulatedTransportUVE::SimulatedTransportUVE(std::unique_ptr<ITransportEndpointUVE> inner,
                                             TransportClockUVE clock,
                                             const SimulatedTransportConfigUVE config)
    : m_inner(std::move(inner)), m_clock(std::move(clock)), m_config(config) {}

bool SimulatedTransportUVE::SendUVE(const std::vector<std::uint8_t>& payload) {
    const std::uint64_t nowMs = m_clock();
    ReleaseDueOutboundUVE(nowMs);
    ++m_outboundCount;
    if (m_config.outboundDropPeriod != 0U && m_outboundCount % m_config.outboundDropPeriod == 0U) {
        ++m_stats.outboundDropped;
        return true;
    }
    if (m_config.outboundDelayMs == 0U) {
        return m_inner->SendUVE(payload);
    }
    if (m_outboundDelayed.size() >= m_config.maxPendingDatagrams) {
        ++m_stats.outboundDroppedFull;
        return false;
    }
    m_outboundDelayed.push_back(DelayedDatagramUVE{nowMs + m_config.outboundDelayMs, payload});
    return true;
}

std::optional<std::vector<std::uint8_t>> SimulatedTransportUVE::TryReceiveUVE() {
    const std::uint64_t nowMs = m_clock();
    ReleaseDueOutboundUVE(nowMs);
    // Pull everything the inner endpoint holds through the inbound script now: drops die here,
    // survivors queue with release times, and the head answers only when due. Head-of-line
    // blocking is deliberate - skipping ahead would reorder the flow.
    for (;;) {
        std::optional<std::vector<std::uint8_t>> datagram = m_inner->TryReceiveUVE();
        if (!datagram.has_value()) {
            break;
        }
        ++m_inboundCount;
        if (m_config.inboundDropPeriod != 0U && m_inboundCount % m_config.inboundDropPeriod == 0U) {
            ++m_stats.inboundDropped;
            continue;
        }
        if (m_inboundDelayed.size() >= m_config.maxPendingDatagrams) {
            ++m_stats.inboundDroppedFull;
            continue;
        }
        m_inboundDelayed.push_back(
            DelayedDatagramUVE{nowMs + m_config.inboundDelayMs, std::move(*datagram)});
    }
    if (m_inboundDelayed.empty() || m_inboundDelayed.front().releaseAtMs > nowMs) {
        return std::nullopt;
    }
    std::vector<std::uint8_t> due = std::move(m_inboundDelayed.front().bytes);
    m_inboundDelayed.pop_front();
    return due;
}

bool SimulatedTransportUVE::HasPendingUVE() const noexcept {
    if (!m_inboundDelayed.empty()) {
        return true;
    }
    return m_inner->HasPendingUVE();
}

std::size_t SimulatedTransportUVE::PendingCountUVE() const noexcept {
    return m_inboundDelayed.size() + m_inner->PendingCountUVE();
}

void SimulatedTransportUVE::SetConfigUVE(const SimulatedTransportConfigUVE config) noexcept {
    m_config = config;
}

void SimulatedTransportUVE::ReleaseDueOutboundUVE(const std::uint64_t nowMs) {
    // Same head-of-line rule as inbound: a held head holds the whole direction, keeping the
    // flow FIFO. A rejected forward (inner queue full) stops the release too - retrying it on
    // the next call, still first.
    while (!m_outboundDelayed.empty() && m_outboundDelayed.front().releaseAtMs <= nowMs) {
        if (!m_inner->SendUVE(m_outboundDelayed.front().bytes)) {
            return;
        }
        m_outboundDelayed.pop_front();
    }
}

} // namespace UVE::Network
