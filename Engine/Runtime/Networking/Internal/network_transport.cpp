// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "network_transport.h"

#include <deque>

#include "reliable_packet_window.h"

namespace UVE::Network {

// A framed reliable packet must always fit the transport: if either bound moves, this forces
// the conscious decision to move (or version) the other instead of silently breaking delivery.
static_assert(kTransportMaximumDatagramBytes == kReliablePacketMaximumPayloadBytes,
              "transport datagram cap must match the reliable window payload bound");

struct LoopbackLinkState final {
    std::size_t maxQueuedDatagrams = 0U;
    std::deque<std::vector<std::uint8_t>> aToB;
    std::deque<std::vector<std::uint8_t>> bToA;
    LoopbackTransportStats statsA;
    LoopbackTransportStats statsB;
};

LoopbackEndpoint::LoopbackEndpoint(std::shared_ptr<LoopbackLinkState> link,
                                         const bool isSideA) noexcept
    : m_link(std::move(link)), m_isSideA(isSideA) {}

LoopbackEndpoint::~LoopbackEndpoint() = default;

bool LoopbackEndpoint::Send(const std::vector<std::uint8_t>& payload) {
    if (m_link == nullptr) {
        return false;
    }
    LoopbackTransportStats& stats = m_isSideA ? m_link->statsA : m_link->statsB;
    if (payload.size() > kTransportMaximumDatagramBytes) {
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

std::optional<std::vector<std::uint8_t>> LoopbackEndpoint::TryReceive() {
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

bool LoopbackEndpoint::HasPending() const noexcept {
    return PendingCount() > 0U;
}

std::size_t LoopbackEndpoint::PendingCount() const noexcept {
    if (m_link == nullptr) {
        return 0U;
    }
    const std::deque<std::vector<std::uint8_t>>& inbound = m_isSideA ? m_link->bToA : m_link->aToB;
    return inbound.size();
}

LoopbackTransportStats LoopbackEndpoint::Stats() const noexcept {
    if (m_link == nullptr) {
        return LoopbackTransportStats{};
    }
    return m_isSideA ? m_link->statsA : m_link->statsB;
}

LoopbackTransportPair CreateLoopbackTransportPair(const std::size_t maxQueuedDatagrams) {
    auto link = std::make_shared<LoopbackLinkState>();
    link->maxQueuedDatagrams = maxQueuedDatagrams;
    LoopbackEndpoint a(link, true);
    LoopbackEndpoint b(link, false);
    return LoopbackTransportPair{std::move(a), std::move(b)};
}

} // namespace UVE::Network
