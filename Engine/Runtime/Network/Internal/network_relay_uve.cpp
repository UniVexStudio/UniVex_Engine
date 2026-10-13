// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/network/network_relay_uve.h"

#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

namespace UVE::Network {

RelayPeerIdUVE NetworkRelayUVE::AddPeerUVE(NetworkSessionUVE&& session) {
    const RelayPeerIdUVE id = m_nextPeerId++;
    if (m_nextPeerId == kInvalidRelayPeerIdUVE) {
        // Wrapped past four billion peers: skip the invalid id, keep counting.
        ++m_nextPeerId;
    }
    m_peers.emplace(id, RelayPeerUVE{std::move(session)});
    return id;
}

bool NetworkRelayUVE::RemovePeerUVE(const RelayPeerIdUVE id) noexcept {
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

bool NetworkRelayUVE::SetPeerRoomUVE(const RelayPeerIdUVE id,
                                        const RelayRoomIdUVE room) noexcept {
    const auto peer = m_peers.find(id);
    if (peer == m_peers.end()) {
        return false;
    }
    peer->second.room = room;
    return true;
}

RelayRoomIdUVE NetworkRelayUVE::PeerRoomUVE(const RelayPeerIdUVE id) const noexcept {
    const auto peer = m_peers.find(id);
    if (peer == m_peers.end()) {
        return kDefaultRelayRoomUVE;
    }
    return peer->second.room;
}

bool NetworkRelayUVE::SetPeerInterestUVE(const RelayPeerIdUVE receiver,
                                         const RelayPeerIdUVE sender, const bool interested) {
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

bool NetworkRelayUVE::IsPeerInterestedUVE(const RelayPeerIdUVE receiver,
                                          const RelayPeerIdUVE sender) const noexcept {
    const auto row = m_muted.find(receiver);
    return row == m_muted.end() || !row->second.contains(sender);
}

void NetworkRelayUVE::UpdateUVE(const std::uint64_t nowMs) {
    for (auto& [id, peer] : m_peers) {
        peer.session.UpdateUVE(nowMs);
        for (;;) {
            std::optional<std::vector<std::uint8_t>> raw = peer.session.TryReceiveRawDataUVE();
            if (!raw.has_value()) {
                break;
            }
            ++m_stats.payloadsReceived;
            for (auto& [otherId, other] : m_peers) {
                if (otherId == id || other.room != peer.room ||
                    !IsPeerInterestedUVE(otherId, id) ||
                    other.session.StateUVE() != SessionStateUVE::Established) {
                    continue;
                }
                if (other.session.SendRawDataUVE(*raw)) {
                    ++m_stats.payloadsForwarded;
                } else {
                    ++m_stats.forwardsDropped;
                }
            }
        }
    }
}

NetworkSessionUVE* NetworkRelayUVE::PeerSessionUVE(const RelayPeerIdUVE id) noexcept {
    const auto peer = m_peers.find(id);
    if (peer == m_peers.end()) {
        return nullptr;
    }
    return &peer->second.session;
}

const NetworkSessionUVE* NetworkRelayUVE::PeerSessionUVE(
    const RelayPeerIdUVE id) const noexcept {
    const auto peer = m_peers.find(id);
    if (peer == m_peers.end()) {
        return nullptr;
    }
    return &peer->second.session;
}

} // namespace UVE::Network
