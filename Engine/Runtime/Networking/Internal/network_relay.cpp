// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "network_relay.h"

#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

namespace UVE::Network {

RelayPeerId NetworkRelay::AddPeer(NetworkSession&& session) {
    const RelayPeerId id = m_nextPeerId++;
    if (m_nextPeerId == kInvalidRelayPeerId) {
        // Wrapped past four billion peers: skip the invalid id, keep counting.
        ++m_nextPeerId;
    }
    m_peers.emplace(id, RelayPeer{std::move(session)});
    return id;
}

bool NetworkRelay::RemovePeer(const RelayPeerId id) noexcept {
    if (m_peers.erase(id) == 0U) {
        return false;
    }
    m_muted.erase(id);
    for (auto row = m_muted.begin(); row != m_muted.end();) {
        row->second.erase(id);
        if (row->second.empty()) {
            row = m_muted.erase(row);
        } else {
            ++row;
        }
    }
    return true;
}

bool NetworkRelay::SetPeerRoom(const RelayPeerId id,
                                        const RelayRoomId room) noexcept {
    const auto peer = m_peers.find(id);
    if (peer == m_peers.end()) {
        return false;
    }
    peer->second.room = room;
    return true;
}

RelayRoomId NetworkRelay::PeerRoom(const RelayPeerId id) const noexcept {
    const auto peer = m_peers.find(id);
    if (peer == m_peers.end()) {
        return kDefaultRelayRoom;
    }
    return peer->second.room;
}

bool NetworkRelay::SetPeerInterest(const RelayPeerId receiver,
                                         const RelayPeerId sender, const bool interested) {
    if (!m_peers.contains(receiver) || !m_peers.contains(sender)) {
        return false;
    }
    if (interested) {
        const auto row = m_muted.find(receiver);
        if (row != m_muted.end()) {
            row->second.erase(sender);
            if (row->second.empty()) {
                m_muted.erase(row);
            }
        }
        return true;
    }
    m_muted[receiver].insert(sender);
    return true;
}

bool NetworkRelay::IsPeerInterested(const RelayPeerId receiver,
                                          const RelayPeerId sender) const noexcept {
    const auto row = m_muted.find(receiver);
    return row == m_muted.end() || !row->second.contains(sender);
}

void NetworkRelay::Update(const std::uint64_t nowMs) {
    for (auto& [id, peer] : m_peers) {
        peer.session.Update(nowMs);
        for (;;) {
            std::optional<std::vector<std::uint8_t>> raw = peer.session.TryReceiveRawData();
            if (!raw.has_value()) {
                break;
            }
            ++m_stats.payloadsReceived;
            for (auto& [otherId, other] : m_peers) {
                if (otherId == id || other.room != peer.room ||
                    !IsPeerInterested(otherId, id) ||
                    other.session.State() != SessionState::Established) {
                    continue;
                }
                if (other.session.SendRawData(*raw)) {
                    ++m_stats.payloadsForwarded;
                } else {
                    ++m_stats.forwardsDropped;
                }
            }
        }
    }
}

NetworkSession* NetworkRelay::PeerSession(const RelayPeerId id) noexcept {
    const auto peer = m_peers.find(id);
    if (peer == m_peers.end()) {
        return nullptr;
    }
    return &peer->second.session;
}

const NetworkSession* NetworkRelay::PeerSession(
    const RelayPeerId id) const noexcept {
    const auto peer = m_peers.find(id);
    if (peer == m_peers.end()) {
        return nullptr;
    }
    return &peer->second.session;
}

} // namespace UVE::Network
