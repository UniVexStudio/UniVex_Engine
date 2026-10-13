// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/network/network_session_uve.h"

namespace UVE::Network {
namespace {

[[nodiscard]] std::vector<std::uint8_t> TaggedUVE(const SessionMessageTagUVE tag) {
    return std::vector<std::uint8_t>{static_cast<std::uint8_t>(tag)};
}

// The tap's inbound channel: the byte past the tag on multiplexed data frames, empty for
// handshake frames, legacy data, and anything too short to hold a channel.
[[nodiscard]] std::optional<NetworkChannelIdUVE> InboundTapChannelUVE(
    const std::vector<std::uint8_t>& datagram, const bool multiplexed) {
    if (!multiplexed || datagram.size() < 2U ||
        datagram[0] != static_cast<std::uint8_t>(SessionMessageTagUVE::Data)) {
        return std::nullopt;
    }
    return datagram[1];
}

} // namespace

NetworkSessionUVE::NetworkSessionUVE(const SessionRoleUVE role,
                                     std::unique_ptr<ITransportEndpointUVE> endpoint,
                                     const std::uint64_t timeoutMs) noexcept
    : m_role(role), m_endpoint(std::move(endpoint)), m_timeoutMs(timeoutMs) {}

bool NetworkSessionUVE::ConnectUVE(const std::uint64_t nowMs, const std::uint64_t token) {
    if (m_role != SessionRoleUVE::Client || m_state != SessionStateUVE::Disconnected ||
        m_endpoint == nullptr) {
        return false;
    }
    m_lastPumpMs = nowMs;
    m_sessionToken = token;
    m_didResume = false;
    m_reconnectAttempts = 0U;
    const bool helloSent = SendHelloUVE();
    m_state = SessionStateUVE::Connecting;
    m_closeReason = SessionCloseReasonUVE::None;
    m_lastActivityMs = nowMs;
    return helloSent;
}

bool NetworkSessionUVE::DisconnectUVE() {
    if (m_state == SessionStateUVE::Disconnected || m_endpoint == nullptr) {
        return false;
    }
    const std::vector<std::uint8_t> bye = TaggedUVE(SessionMessageTagUVE::Bye);
    const bool byeSent = m_endpoint->SendUVE(bye);
    if (byeSent) {
        TapUVE(bye, PacketDirectionUVE::Outbound, m_lastPumpMs, std::nullopt);
    }
    if (m_role == SessionRoleUVE::Server) {
        // Hanging up locally ends the partnership: forget the peer, so the next Hello -
        // even from the same token - arrives as a stranger.
        m_lastPeerToken = kAnonymousSessionTokenUVE;
    }
    TransitionToDisconnectedUVE(SessionCloseReasonUVE::LocalDisconnect);
    return byeSent;
}

void NetworkSessionUVE::UpdateUVE(const std::uint64_t nowMs) {
    m_lastPumpMs = nowMs;
    if (m_endpoint != nullptr) {
        for (;;) {
            const std::optional<std::vector<std::uint8_t>> datagram = m_endpoint->TryReceiveUVE();
            if (!datagram.has_value()) {
                break;
            }
            TapUVE(*datagram, PacketDirectionUVE::Inbound, nowMs,
                   InboundTapChannelUVE(*datagram, m_multiplexed));
            HandleDatagramUVE(*datagram, nowMs);
        }
    }
    // Drain first, so a Welcome landing in the same pump establishes before the scheduler
    // runs and no Hello goes out answered.
    PumpReconnectUVE(nowMs);
    if (m_timeoutMs != 0U &&
        (m_state == SessionStateUVE::Connecting || m_state == SessionStateUVE::Established) &&
        nowMs >= m_lastActivityMs && nowMs - m_lastActivityMs >= m_timeoutMs) {
        DropWithRedialUVE(SessionCloseReasonUVE::Timeout, nowMs);
    }
}

bool NetworkSessionUVE::SendDataUVE(const std::vector<std::uint8_t>& payload) {
    if (m_multiplexed) {
        return false;
    }
    return SendFramedUVE(payload);
}

bool NetworkSessionUVE::SendFramedUVE(const std::vector<std::uint8_t>& payload) {
    if (m_state != SessionStateUVE::Established || m_endpoint == nullptr ||
        payload.size() > kSessionMaximumPayloadBytesUVE) {
        return false;
    }
    std::vector<std::uint8_t> datagram;
    datagram.reserve(payload.size() + 1U);
    datagram.push_back(static_cast<std::uint8_t>(SessionMessageTagUVE::Data));
    datagram.insert(datagram.end(), payload.begin(), payload.end());
    const bool sent = m_endpoint->SendUVE(datagram);
    if (sent) {
        // Legacy sends refuse on multiplexed sessions, so a muxed frame through here is
        // always caller-channeled raw bytes: its first payload byte is the channel.
        std::optional<NetworkChannelIdUVE> channel;
        if (m_multiplexed && !payload.empty()) {
            channel = payload[0];
        }
        TapUVE(datagram, PacketDirectionUVE::Outbound, m_lastPumpMs, channel);
    }
    return sent;
}

bool NetworkSessionUVE::SendRawDataUVE(const std::vector<std::uint8_t>& payload) {
    return SendFramedUVE(payload);
}

std::optional<std::vector<std::uint8_t>> NetworkSessionUVE::TryReceiveDataUVE() {
    if (m_inboundData.empty()) {
        return std::nullopt;
    }
    std::vector<std::uint8_t> payload = std::move(m_inboundData.front());
    m_inboundData.pop_front();
    return payload;
}

bool NetworkSessionUVE::EnableMultiplexingUVE() noexcept {
    if (m_state != SessionStateUVE::Disconnected) {
        return false;
    }
    m_multiplexed = true;
    return true;
}

bool NetworkSessionUVE::SendChannelDataUVE(const NetworkChannelIdUVE channel,
                                           const std::vector<std::uint8_t>& payload) {
    if (!m_multiplexed || m_state != SessionStateUVE::Established || m_endpoint == nullptr ||
        payload.size() > kSessionMaximumChannelPayloadBytesUVE) {
        return false;
    }
    std::vector<std::uint8_t> datagram;
    datagram.reserve(payload.size() + 2U);
    datagram.push_back(static_cast<std::uint8_t>(SessionMessageTagUVE::Data));
    datagram.push_back(channel);
    datagram.insert(datagram.end(), payload.begin(), payload.end());
    const bool sent = m_endpoint->SendUVE(datagram);
    if (sent) {
        TapUVE(datagram, PacketDirectionUVE::Outbound, m_lastPumpMs, channel);
    }
    return sent;
}

std::optional<std::vector<std::uint8_t>> NetworkSessionUVE::TryReceiveChannelDataUVE(
    const NetworkChannelIdUVE channel) {
    const auto queued = m_inboundChannels.find(channel);
    if (queued == m_inboundChannels.end() || queued->second.empty()) {
        return std::nullopt;
    }
    std::vector<std::uint8_t> payload = std::move(queued->second.front());
    queued->second.pop_front();
    return payload;
}

std::optional<std::vector<std::uint8_t>> NetworkSessionUVE::TryReceiveRawDataUVE() {
    if (!m_inboundData.empty()) {
        std::vector<std::uint8_t> payload = std::move(m_inboundData.front());
        m_inboundData.pop_front();
        return payload;
    }
    // The demux below split channel from payload at pump time; re-tagging restores the exact
    // wire bytes, so a relay forwards what the sender sent, byte for byte.
    for (auto& [channel, queued] : m_inboundChannels) {
        if (queued.empty()) {
            continue;
        }
        std::vector<std::uint8_t> raw;
        raw.reserve(queued.front().size() + 1U);
        raw.push_back(channel);
        raw.insert(raw.end(), queued.front().begin(), queued.front().end());
        queued.pop_front();
        return raw;
    }
    return std::nullopt;
}

bool NetworkSessionUVE::SendHelloUVE() {
    // Token travels little-endian in bytes 3-10, like the version in bytes 1-2.
    std::vector<std::uint8_t> hello;
    hello.reserve(kSessionHelloBytesUVE);
    hello.push_back(static_cast<std::uint8_t>(SessionMessageTagUVE::Hello));
    hello.push_back(static_cast<std::uint8_t>(kSessionProtocolVersionUVE & 0xFFU));
    hello.push_back(static_cast<std::uint8_t>((kSessionProtocolVersionUVE >> 8) & 0xFFU));
    for (int shift = 0; shift < 64; shift += 8) {
        hello.push_back(static_cast<std::uint8_t>((m_sessionToken >> shift) & 0xFFU));
    }
    const bool sent = m_endpoint->SendUVE(hello);
    if (sent) {
        TapUVE(hello, PacketDirectionUVE::Outbound, m_lastPumpMs, std::nullopt);
    }
    return sent;
}

void NetworkSessionUVE::DropWithRedialUVE(const SessionCloseReasonUVE reason,
                                          const std::uint64_t nowMs) {
    const bool redialable = m_state == SessionStateUVE::Connecting ||
                            m_state == SessionStateUVE::Established;
    if (redialable && m_role == SessionRoleUVE::Client && m_reconnect.retryDelayMs != 0U) {
        m_state = SessionStateUVE::Reconnecting;
        m_closeReason = SessionCloseReasonUVE::None;
        m_redialReason = reason;
        m_reconnectAttempts = 0U;
        m_nextAttemptAtMs = nowMs + m_reconnect.retryDelayMs;
        m_inboundData.clear();
        m_inboundChannels.clear();
        return;
    }
    TransitionToDisconnectedUVE(reason);
}

void NetworkSessionUVE::PumpReconnectUVE(const std::uint64_t nowMs) {
    if (m_state != SessionStateUVE::Reconnecting) {
        return;
    }
    if (m_reconnect.retryDelayMs == 0U) {
        // Disabled mid-redial: stop trying, and report the drop that started it all.
        TransitionToDisconnectedUVE(m_redialReason);
        return;
    }
    if (m_reconnect.maxAttempts != 0U && m_reconnectAttempts >= m_reconnect.maxAttempts) {
        TransitionToDisconnectedUVE(SessionCloseReasonUVE::ReconnectFailed);
        return;
    }
    if (m_endpoint != nullptr && nowMs >= m_nextAttemptAtMs) {
        SendHelloUVE();
        ++m_reconnectAttempts;
        m_nextAttemptAtMs = nowMs + m_reconnect.retryDelayMs;
    }
}

void NetworkSessionUVE::HandleDatagramUVE(const std::vector<std::uint8_t>& datagram,
                                          const std::uint64_t nowMs) {
    if (datagram.empty()) {
        return;
    }
    switch (static_cast<SessionMessageTagUVE>(datagram[0])) {
        case SessionMessageTagUVE::Hello: {
            // One peer per session: a Hello only opens a listening (Disconnected) server.
            if (m_role != SessionRoleUVE::Server || m_state != SessionStateUVE::Disconnected) {
                return;
            }
            if (datagram.size() < 3U) {
                return;
            }
            const std::uint16_t version = static_cast<std::uint16_t>(
                static_cast<std::uint16_t>(datagram[1]) |
                (static_cast<std::uint16_t>(datagram[2]) << 8));
            if (version != kSessionProtocolVersionUVE) {
                const std::vector<std::uint8_t> refuse = TaggedUVE(SessionMessageTagUVE::Bye);
                if (m_endpoint->SendUVE(refuse)) {
                    TapUVE(refuse, PacketDirectionUVE::Outbound, m_lastPumpMs, std::nullopt);
                }
                return;
            }
            if (datagram.size() < kSessionHelloBytesUVE) {
                return;
            }
            std::uint64_t token = 0U;
            for (std::size_t i = 0U; i < 8U; ++i) {
                token |= static_cast<std::uint64_t>(datagram[3U + i]) << (8U * i);
            }
            m_didResume =
                token != kAnonymousSessionTokenUVE && token == m_lastPeerToken;
            m_sessionToken = token;
            m_lastPeerToken = token;
            const std::vector<std::uint8_t> welcome = TaggedUVE(SessionMessageTagUVE::Welcome);
            if (m_endpoint->SendUVE(welcome)) {
                TapUVE(welcome, PacketDirectionUVE::Outbound, m_lastPumpMs, std::nullopt);
            }
            m_state = SessionStateUVE::Established;
            m_closeReason = SessionCloseReasonUVE::None;
            m_lastActivityMs = nowMs;
            return;
        }
        case SessionMessageTagUVE::Welcome: {
            if (m_role != SessionRoleUVE::Client ||
                (m_state != SessionStateUVE::Connecting &&
                 m_state != SessionStateUVE::Reconnecting)) {
                return;
            }
            m_didResume = m_state == SessionStateUVE::Reconnecting;
            m_state = SessionStateUVE::Established;
            m_closeReason = SessionCloseReasonUVE::None;
            m_lastActivityMs = nowMs;
            return;
        }
        case SessionMessageTagUVE::Bye: {
            if (m_state == SessionStateUVE::Disconnected) {
                return;
            }
            DropWithRedialUVE(SessionCloseReasonUVE::RemoteDisconnect, nowMs);
            return;
        }
        case SessionMessageTagUVE::Data: {
            if (m_state != SessionStateUVE::Established) {
                return;
            }
            // Multiplexed data carries its channel past the tag; a bare tag has no channel
            // to route to, so it is garbage - ignored, and the clock never hears of it.
            if (m_multiplexed) {
                if (datagram.size() < 2U) {
                    return;
                }
                m_inboundChannels[datagram[1]].emplace_back(datagram.begin() + 2, datagram.end());
                m_lastActivityMs = nowMs;
                return;
            }
            m_inboundData.emplace_back(datagram.begin() + 1, datagram.end());
            m_lastActivityMs = nowMs;
            return;
        }
    }
}

void NetworkSessionUVE::TransitionToDisconnectedUVE(const SessionCloseReasonUVE reason) noexcept {
    m_state = SessionStateUVE::Disconnected;
    m_closeReason = reason;
    m_inboundData.clear();
    m_inboundChannels.clear();
}

void NetworkSessionUVE::SetTapUVE(PacketTapSinkUVE sink) {
    m_tap = std::move(sink);
}

void NetworkSessionUVE::ClearTapUVE() noexcept {
    m_tap = nullptr;
}

void NetworkSessionUVE::TapUVE(const std::vector<std::uint8_t>& datagram,
                               const PacketDirectionUVE direction,
                               const std::uint64_t timestampMs,
                               const std::optional<NetworkChannelIdUVE> channel) {
    if (!m_tap) {
        return;
    }
    PacketTapUVE tap;
    tap.datagram = datagram;
    tap.direction = direction;
    tap.timestampMs = timestampMs;
    tap.multiplexed = m_multiplexed;
    tap.channel = channel;
    m_tap(tap);
}

} // namespace UVE::Network
