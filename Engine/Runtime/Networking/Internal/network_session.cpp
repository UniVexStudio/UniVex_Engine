// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "network_session.h"

namespace UVE::Network {
namespace {

[[nodiscard]] std::vector<std::uint8_t> Tagged(const SessionMessageTag tag) {
    return std::vector<std::uint8_t>{static_cast<std::uint8_t>(tag)};
}

// The tap's inbound channel: the byte past the tag on multiplexed data frames, empty for
// handshake frames, legacy data, and anything too short to hold a channel.
[[nodiscard]] std::optional<NetworkChannelId> InboundTapChannel(
    const std::vector<std::uint8_t>& datagram, const bool multiplexed) {
    if (!multiplexed || datagram.size() < 2U ||
        datagram[0] != static_cast<std::uint8_t>(SessionMessageTag::Data)) {
        return std::nullopt;
    }
    return datagram[1];
}

} // namespace

NetworkSession::NetworkSession(const SessionRole role,
                                     std::unique_ptr<ITransportEndpoint> endpoint,
                                     const std::uint64_t timeoutMs) noexcept
    : m_role(role), m_endpoint(std::move(endpoint)), m_timeoutMs(timeoutMs) {}

bool NetworkSession::Connect(const std::uint64_t nowMs, const std::uint64_t token) {
    if (m_role != SessionRole::Client || m_state != SessionState::Disconnected ||
        m_endpoint == nullptr) {
        return false;
    }
    m_lastPumpMs = nowMs;
    m_sessionToken = token;
    m_didResume = false;
    m_reconnectAttempts = 0U;
    const bool helloSent = SendHello();
    m_state = SessionState::Connecting;
    m_closeReason = SessionCloseReason::None;
    m_lastActivityMs = nowMs;
    return helloSent;
}

bool NetworkSession::Disconnect() {
    if (m_state == SessionState::Disconnected || m_endpoint == nullptr) {
        return false;
    }
    const std::vector<std::uint8_t> bye = Tagged(SessionMessageTag::Bye);
    const bool byeSent = m_endpoint->Send(bye);
    if (byeSent) {
        Tap(bye, PacketDirection::Outbound, m_lastPumpMs, std::nullopt);
    }
    if (m_role == SessionRole::Server) {
        // Hanging up locally ends the partnership: forget the peer, so the next Hello -
        // even from the same token - arrives as a stranger.
        m_lastPeerToken = kAnonymousSessionToken;
    }
    TransitionToDisconnected(SessionCloseReason::LocalDisconnect);
    return byeSent;
}

void NetworkSession::Update(const std::uint64_t nowMs) {
    m_lastPumpMs = nowMs;
    if (m_endpoint != nullptr) {
        for (;;) {
            const std::optional<std::vector<std::uint8_t>> datagram = m_endpoint->TryReceive();
            if (!datagram.has_value()) {
                break;
            }
            Tap(*datagram, PacketDirection::Inbound, nowMs,
                   InboundTapChannel(*datagram, m_multiplexed));
            HandleDatagram(*datagram, nowMs);
        }
    }
    // Drain first, so a Welcome landing in the same pump establishes before the scheduler
    // runs and no Hello goes out answered.
    PumpReconnect(nowMs);
    if (m_timeoutMs != 0U &&
        (m_state == SessionState::Connecting || m_state == SessionState::Established) &&
        nowMs >= m_lastActivityMs && nowMs - m_lastActivityMs >= m_timeoutMs) {
        DropWithRedial(SessionCloseReason::Timeout, nowMs);
    }
}

bool NetworkSession::SendData(const std::vector<std::uint8_t>& payload) {
    if (m_multiplexed) {
        return false;
    }
    return SendFramed(payload);
}

bool NetworkSession::SendFramed(const std::vector<std::uint8_t>& payload) {
    if (m_state != SessionState::Established || m_endpoint == nullptr ||
        payload.size() > kSessionMaximumPayloadBytes) {
        return false;
    }
    std::vector<std::uint8_t> datagram;
    datagram.reserve(payload.size() + 1U);
    datagram.push_back(static_cast<std::uint8_t>(SessionMessageTag::Data));
    datagram.insert(datagram.end(), payload.begin(), payload.end());
    const bool sent = m_endpoint->Send(datagram);
    if (sent) {
        // Legacy sends refuse on multiplexed sessions, so a muxed frame through here is
        // always caller-channeled raw bytes: its first payload byte is the channel.
        std::optional<NetworkChannelId> channel;
        if (m_multiplexed && !payload.empty()) {
            channel = payload[0];
        }
        Tap(datagram, PacketDirection::Outbound, m_lastPumpMs, channel);
    }
    return sent;
}

bool NetworkSession::SendRawData(const std::vector<std::uint8_t>& payload) {
    return SendFramed(payload);
}

std::optional<std::vector<std::uint8_t>> NetworkSession::TryReceiveData() {
    if (m_inboundData.empty()) {
        return std::nullopt;
    }
    std::vector<std::uint8_t> payload = std::move(m_inboundData.front());
    m_inboundData.pop_front();
    return payload;
}

bool NetworkSession::EnableMultiplexing() noexcept {
    if (m_state != SessionState::Disconnected) {
        return false;
    }
    m_multiplexed = true;
    return true;
}

bool NetworkSession::SendChannelData(const NetworkChannelId channel,
                                           const std::vector<std::uint8_t>& payload) {
    if (!m_multiplexed || m_state != SessionState::Established || m_endpoint == nullptr ||
        payload.size() > kSessionMaximumChannelPayloadBytes) {
        return false;
    }
    std::vector<std::uint8_t> datagram;
    datagram.reserve(payload.size() + 2U);
    datagram.push_back(static_cast<std::uint8_t>(SessionMessageTag::Data));
    datagram.push_back(channel);
    datagram.insert(datagram.end(), payload.begin(), payload.end());
    const bool sent = m_endpoint->Send(datagram);
    if (sent) {
        Tap(datagram, PacketDirection::Outbound, m_lastPumpMs, channel);
    }
    return sent;
}

std::optional<std::vector<std::uint8_t>> NetworkSession::TryReceiveChannelData(
    const NetworkChannelId channel) {
    const auto queued = m_inboundChannels.find(channel);
    if (queued == m_inboundChannels.end() || queued->second.empty()) {
        return std::nullopt;
    }
    std::vector<std::uint8_t> payload = std::move(queued->second.front());
    queued->second.pop_front();
    return payload;
}

std::optional<std::vector<std::uint8_t>> NetworkSession::TryReceiveRawData() {
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

bool NetworkSession::SendHello() {
    // Token travels little-endian in bytes 3-10, like the version in bytes 1-2.
    std::vector<std::uint8_t> hello;
    hello.reserve(kSessionHelloBytes);
    hello.push_back(static_cast<std::uint8_t>(SessionMessageTag::Hello));
    hello.push_back(static_cast<std::uint8_t>(kSessionProtocolVersion & 0xFFU));
    hello.push_back(static_cast<std::uint8_t>((kSessionProtocolVersion >> 8) & 0xFFU));
    for (int shift = 0; shift < 64; shift += 8) {
        hello.push_back(static_cast<std::uint8_t>((m_sessionToken >> shift) & 0xFFU));
    }
    const bool sent = m_endpoint->Send(hello);
    if (sent) {
        Tap(hello, PacketDirection::Outbound, m_lastPumpMs, std::nullopt);
    }
    return sent;
}

void NetworkSession::DropWithRedial(const SessionCloseReason reason,
                                          const std::uint64_t nowMs) {
    const bool redialable = m_state == SessionState::Connecting ||
                            m_state == SessionState::Established;
    if (redialable && m_role == SessionRole::Client && m_reconnect.retryDelayMs != 0U) {
        m_state = SessionState::Reconnecting;
        m_closeReason = SessionCloseReason::None;
        m_redialReason = reason;
        m_reconnectAttempts = 0U;
        m_nextAttemptAtMs = nowMs + m_reconnect.retryDelayMs;
        m_inboundData.clear();
        m_inboundChannels.clear();
        return;
    }
    TransitionToDisconnected(reason);
}

void NetworkSession::PumpReconnect(const std::uint64_t nowMs) {
    if (m_state != SessionState::Reconnecting) {
        return;
    }
    if (m_reconnect.retryDelayMs == 0U) {
        // Disabled mid-redial: stop trying, and report the drop that started it all.
        TransitionToDisconnected(m_redialReason);
        return;
    }
    if (m_reconnect.maxAttempts != 0U && m_reconnectAttempts >= m_reconnect.maxAttempts) {
        TransitionToDisconnected(SessionCloseReason::ReconnectFailed);
        return;
    }
    if (m_endpoint != nullptr && nowMs >= m_nextAttemptAtMs) {
        SendHello();
        ++m_reconnectAttempts;
        m_nextAttemptAtMs = nowMs + m_reconnect.retryDelayMs;
    }
}

void NetworkSession::HandleDatagram(const std::vector<std::uint8_t>& datagram,
                                          const std::uint64_t nowMs) {
    if (datagram.empty()) {
        return;
    }
    switch (static_cast<SessionMessageTag>(datagram[0])) {
        case SessionMessageTag::Hello: {
            // One peer per session: a Hello only opens a listening (Disconnected) server.
            if (m_role != SessionRole::Server || m_state != SessionState::Disconnected) {
                return;
            }
            if (datagram.size() < 3U) {
                return;
            }
            const std::uint16_t version = static_cast<std::uint16_t>(
                static_cast<std::uint16_t>(datagram[1]) |
                (static_cast<std::uint16_t>(datagram[2]) << 8));
            if (version != kSessionProtocolVersion) {
                const std::vector<std::uint8_t> refuse = Tagged(SessionMessageTag::Bye);
                if (m_endpoint->Send(refuse)) {
                    Tap(refuse, PacketDirection::Outbound, m_lastPumpMs, std::nullopt);
                }
                return;
            }
            if (datagram.size() < kSessionHelloBytes) {
                return;
            }
            std::uint64_t token = 0U;
            for (std::size_t i = 0U; i < 8U; ++i) {
                token |= static_cast<std::uint64_t>(datagram[3U + i]) << (8U * i);
            }
            m_didResume =
                token != kAnonymousSessionToken && token == m_lastPeerToken;
            m_sessionToken = token;
            m_lastPeerToken = token;
            const std::vector<std::uint8_t> welcome = Tagged(SessionMessageTag::Welcome);
            if (m_endpoint->Send(welcome)) {
                Tap(welcome, PacketDirection::Outbound, m_lastPumpMs, std::nullopt);
            }
            m_state = SessionState::Established;
            m_closeReason = SessionCloseReason::None;
            m_lastActivityMs = nowMs;
            return;
        }
        case SessionMessageTag::Welcome: {
            if (m_role != SessionRole::Client ||
                (m_state != SessionState::Connecting &&
                 m_state != SessionState::Reconnecting)) {
                return;
            }
            m_didResume = m_state == SessionState::Reconnecting;
            m_state = SessionState::Established;
            m_closeReason = SessionCloseReason::None;
            m_lastActivityMs = nowMs;
            return;
        }
        case SessionMessageTag::Bye: {
            if (m_state == SessionState::Disconnected) {
                return;
            }
            DropWithRedial(SessionCloseReason::RemoteDisconnect, nowMs);
            return;
        }
        case SessionMessageTag::Data: {
            if (m_state != SessionState::Established) {
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

void NetworkSession::TransitionToDisconnected(const SessionCloseReason reason) noexcept {
    m_state = SessionState::Disconnected;
    m_closeReason = reason;
    m_inboundData.clear();
    m_inboundChannels.clear();
}

void NetworkSession::SetTap(PacketTapSink sink) {
    m_tap = std::move(sink);
}

void NetworkSession::ClearTap() noexcept {
    m_tap = nullptr;
}

void NetworkSession::Tap(const std::vector<std::uint8_t>& datagram,
                               const PacketDirection direction,
                               const std::uint64_t timestampMs,
                               const std::optional<NetworkChannelId> channel) {
    if (!m_tap) {
        return;
    }
    PacketTap tap;
    tap.datagram = datagram;
    tap.direction = direction;
    tap.timestampMs = timestampMs;
    tap.multiplexed = m_multiplexed;
    tap.channel = channel;
    m_tap(tap);
}

} // namespace UVE::Network
