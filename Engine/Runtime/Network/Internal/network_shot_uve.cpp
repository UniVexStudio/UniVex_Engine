// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/network/network_shot_uve.h"

#include <bit>
#include <cmath>

namespace UVE::Network {
namespace {

[[nodiscard]] bool ValidFractionUVE(const float fraction) noexcept {
    return std::isfinite(fraction) && fraction >= 0.0F && fraction < 1.0F;
}

void AppendU32UVE(std::vector<std::uint8_t>& bytes, const std::uint32_t value) {
    for (int shift = 0; shift < 32; shift += 8) {
        bytes.push_back(static_cast<std::uint8_t>((value >> shift) & 0xFFU));
    }
}

void AppendU64UVE(std::vector<std::uint8_t>& bytes, const std::uint64_t value) {
    for (int shift = 0; shift < 64; shift += 8) {
        bytes.push_back(static_cast<std::uint8_t>((value >> shift) & 0xFFU));
    }
}

[[nodiscard]] std::uint32_t ReadU32UVE(const std::vector<std::uint8_t>& bytes,
                                       std::size_t& at) noexcept {
    std::uint32_t value = 0U;
    for (int shift = 0; shift < 32; shift += 8) {
        value |= static_cast<std::uint32_t>(bytes[at++]) << shift;
    }
    return value;
}

[[nodiscard]] std::uint64_t ReadU64UVE(const std::vector<std::uint8_t>& bytes,
                                       std::size_t& at) noexcept {
    std::uint64_t value = 0U;
    for (int shift = 0; shift < 64; shift += 8) {
        value |= static_cast<std::uint64_t>(bytes[at++]) << shift;
    }
    return value;
}

} // namespace

std::vector<std::uint8_t> EncodeShotIntentUVE(const ShotIntentUVE& intent) {
    std::vector<std::uint8_t> bytes;
    bytes.reserve(kShotIntentBytesUVE);
    bytes.push_back(kShotIntentVersionUVE);
    bytes.push_back(intent.pawn);
    AppendU32UVE(bytes, intent.seq);
    AppendU64UVE(bytes, intent.viewTick);
    AppendU32UVE(bytes, std::bit_cast<std::uint32_t>(intent.fraction));
    return bytes;
}

std::optional<ShotIntentUVE> DecodeShotIntentUVE(const std::vector<std::uint8_t>& bytes) {
    if (bytes.size() != kShotIntentBytesUVE || bytes[0] != kShotIntentVersionUVE) {
        return std::nullopt;
    }
    std::size_t at = 2U;
    ShotIntentUVE intent;
    intent.pawn = bytes[1];
    intent.seq = ReadU32UVE(bytes, at);
    intent.viewTick = ReadU64UVE(bytes, at);
    intent.fraction = std::bit_cast<float>(ReadU32UVE(bytes, at));
    if (!ValidFractionUVE(intent.fraction)) {
        return std::nullopt;
    }
    return intent;
}

ShotSenderUVE::ShotSenderUVE(NetworkSessionUVE& session,
                             const std::optional<NetworkChannelIdUVE> channel) noexcept
    : m_session(&session), m_channel(channel) {}

bool ShotSenderUVE::FireUVE(const PredictionPawnUVE pawn, const PredictionTickUVE viewTick,
                             const float fraction) {
    if (!ValidFractionUVE(fraction)) {
        return false;
    }
    ShotIntentUVE intent;
    intent.pawn = pawn;
    intent.seq = m_nextSeq++;
    intent.viewTick = viewTick;
    intent.fraction = fraction;
    const std::vector<std::uint8_t> bytes = EncodeShotIntentUVE(intent);
    if (m_channel.has_value()) {
        return m_session->SendChannelDataUVE(*m_channel, bytes);
    }
    return m_session->SendDataUVE(bytes);
}

ShotReceiverUVE::ShotReceiverUVE(NetworkSessionUVE& session,
                                 const std::optional<NetworkChannelIdUVE> channel)
    : m_session(&session), m_channel(channel) {}

std::vector<ShotIntentUVE> ShotReceiverUVE::PollUVE() {
    std::vector<ShotIntentUVE> intents;
    for (;;) {
        const std::optional<std::vector<std::uint8_t>> bytes =
            m_channel.has_value() ? m_session->TryReceiveChannelDataUVE(*m_channel)
                                  : m_session->TryReceiveDataUVE();
        if (!bytes.has_value()) {
            break;
        }
        const std::optional<ShotIntentUVE> intent = DecodeShotIntentUVE(*bytes);
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
