// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/network/network_transport_uve.h"

#include <deque>

#include "uve/network/reliable_packet_window_uve.h"

namespace UVE::Network {

// A framed reliable packet must always fit the transport: if either bound moves, this forces
// the conscious decision to move (or version) the other instead of silently breaking delivery.
static_assert(kTransportMaximumDatagramBytesUVE == kReliablePacketMaximumPayloadBytesUVE,
              "transport datagram cap must match the reliable window payload bound");

struct LoopbackLinkStateUVE final {
    std::size_t maxQueuedDatagrams = 0U;
    std::deque<std::vector<std::uint8_t>> aToB;
    std::deque<std::vector<std::uint8_t>> bToA;
    LoopbackTransportStatsUVE statsA;
    LoopbackTransportStatsUVE statsB;
};

LoopbackEndpointUVE::LoopbackEndpointUVE(std::shared_ptr<LoopbackLinkStateUVE> link,
                                         const bool isSideA) noexcept
    : m_link(std::move(link)), m_isSideA(isSideA) {}

LoopbackEndpointUVE::~LoopbackEndpointUVE() = default;

bool LoopbackEndpointUVE::SendUVE(const std::vector<std::uint8_t>& payload) {
    if (m_link == nullptr) {
        return false;
    }
    LoopbackTransportStatsUVE& stats = m_isSideA ? m_link->statsA : m_link->statsB;
    if (payload.size() > kTransportMaximumDatagramBytesUVE) {
        ++stats.datagramsDroppedOversize;
        return false;
    }
    std::deque<std::vector<std::uint8_t>>& outbound = m_isSideA ? m_link->aToB : m_link->bToA;
    if (outbound.size() >= m_link->maxQueuedDatagrams) {
        ++stats.datagramsDroppedFull;
        return false;
    }
    outbound.push_back(payload);
    ++stats.datagramsSent;
    return true;
}

std::optional<std::vector<std::uint8_t>> LoopbackEndpointUVE::TryReceiveUVE() {
    if (m_link == nullptr) {
        return std::nullopt;
    }
    std::deque<std::vector<std::uint8_t>>& inbound = m_isSideA ? m_link->bToA : m_link->aToB;
    if (inbound.empty()) {
        return std::nullopt;
    }
    std::vector<std::uint8_t> datagram = std::move(inbound.front());
    inbound.pop_front();
    if (m_isSideA) {
        ++m_link->statsA.datagramsReceived;
    } else {
        ++m_link->statsB.datagramsReceived;
    }
    return datagram;
}

bool LoopbackEndpointUVE::HasPendingUVE() const noexcept {
    return PendingCountUVE() > 0U;
}

std::size_t LoopbackEndpointUVE::PendingCountUVE() const noexcept {
    if (m_link == nullptr) {
        return 0U;
    }
    const std::deque<std::vector<std::uint8_t>>& inbound = m_isSideA ? m_link->bToA : m_link->aToB;
    return inbound.size();
}

LoopbackTransportStatsUVE LoopbackEndpointUVE::StatsUVE() const noexcept {
    if (m_link == nullptr) {
        return LoopbackTransportStatsUVE{};
    }
    return m_isSideA ? m_link->statsA : m_link->statsB;
}

LoopbackTransportPairUVE CreateLoopbackTransportPairUVE(const std::size_t maxQueuedDatagrams) {
    auto link = std::make_shared<LoopbackLinkStateUVE>();
    link->maxQueuedDatagrams = maxQueuedDatagrams;
    LoopbackEndpointUVE a(link, true);
    LoopbackEndpointUVE b(link, false);
    return LoopbackTransportPairUVE{std::move(a), std::move(b)};
}

} // namespace UVE::Network
