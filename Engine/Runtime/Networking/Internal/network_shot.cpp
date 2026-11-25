// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "network_shot.h"

#include <bit>
#include <cmath>

namespace UVE::Network {
namespace {

[[nodiscard]] bool ValidFraction(const float fraction) noexcept {
    return std::isfinite(fraction) && fraction >= 0.0F && fraction < 1.0F;
}

void AppendU32(std::vector<std::uint8_t>& bytes, const std::uint32_t value) {
    for (int shift = 0; shift < 32; shift += 8) {
        bytes.push_back(static_cast<std::uint8_t>((value >> shift) & 0xFFU));
    }
}

void AppendU64(std::vector<std::uint8_t>& bytes, const std::uint64_t value) {
    for (int shift = 0; shift < 64; shift += 8) {
        bytes.push_back(static_cast<std::uint8_t>((value >> shift) & 0xFFU));
    }
}

[[nodiscard]] std::uint32_t ReadU32(const std::vector<std::uint8_t>& bytes,
                                       std::size_t& at) noexcept {
    std::uint32_t value = 0U;
    for (int shift = 0; shift < 32; shift += 8) {
        value |= static_cast<std::uint32_t>(bytes[at++]) << shift;
    }
    return value;
}

[[nodiscard]] std::uint64_t ReadU64(const std::vector<std::uint8_t>& bytes,
                                       std::size_t& at) noexcept {
    std::uint64_t value = 0U;
    for (int shift = 0; shift < 64; shift += 8) {
        value |= static_cast<std::uint64_t>(bytes[at++]) << shift;
    }
    return value;
}

} // namespace

std::vector<std::uint8_t> EncodeShotIntent(const ShotIntent& intent) {
    std::vector<std::uint8_t> bytes;
    bytes.reserve(kShotIntentBytes);
    bytes.push_back(kShotIntentVersion);
    bytes.push_back(intent.pawn);
    AppendU32(bytes, intent.seq);
    AppendU64(bytes, intent.viewTick);
    AppendU32(bytes, std::bit_cast<std::uint32_t>(intent.fraction));
    return bytes;
}

std::optional<ShotIntent> DecodeShotIntent(const std::vector<std::uint8_t>& bytes) {
    if (bytes.size() != kShotIntentBytes || bytes[0] != kShotIntentVersion) {
        return std::nullopt;
    }
    std::size_t at = 2U;
    ShotIntent intent;
    intent.pawn = bytes[1];
    intent.seq = ReadU32(bytes, at);
    intent.viewTick = ReadU64(bytes, at);
    intent.fraction = std::bit_cast<float>(ReadU32(bytes, at));
    if (!ValidFraction(intent.fraction)) {
        return std::nullopt;
    }
    return intent;
}

ShotSender::ShotSender(NetworkSession& session,
                             const std::optional<NetworkChannelId> channel) noexcept
    : m_session(&session), m_channel(channel) {}

bool ShotSender::Fire(const PredictionPawn pawn, const PredictionTick viewTick,
                             const float fraction) {
    if (!ValidFraction(fraction)) {
        return false;
    }
    ShotIntent intent;
    intent.pawn = pawn;
    intent.seq = m_nextSeq++;
    intent.viewTick = viewTick;
    intent.fraction = fraction;
    const std::vector<std::uint8_t> bytes = EncodeShotIntent(intent);
    if (m_channel.has_value()) {
        return m_session->SendChannelData(*m_channel, bytes);
    }
    return m_session->SendData(bytes);
}

ShotReceiver::ShotReceiver(NetworkSession& session,
                                 const std::optional<NetworkChannelId> channel)
    : m_session(&session), m_channel(channel) {}

std::vector<ShotIntent> ShotReceiver::Poll() {
    std::vector<ShotIntent> intents;
    for (;;) {
        const std::optional<std::vector<std::uint8_t>> bytes =
            m_channel.has_value() ? m_session->TryReceiveChannelData(*m_channel)
                                  : m_session->TryReceiveData();
        if (!bytes.has_value()) {
            break;
        }
        const std::optional<ShotIntent> intent = DecodeShotIntent(*bytes);
        if (!intent.has_value()) {
            ++m_stats.dropped;
            continue;
        }
        const auto seen = m_lastSeen.find(intent->pawn);
        if (seen != m_lastSeen.end() && intent->seq <= seen->second) {
            ++m_stats.dropped;
            continue;
        }
        m_lastSeen[intent->pawn] = intent->seq;
        ++m_stats.received;
        intents.push_back(*intent);
    }
    return intents;
}

} // namespace UVE::Network
