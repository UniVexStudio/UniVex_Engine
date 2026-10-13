// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once
//
// Scope: the client-to-server shot protocol - the fire-fraction transport the lag
// compensator's fractional resolves were waiting for. The client fires its view tick plus
// its fire offset within that tick; the server polls intents off its session and resolves
// each through ResolveLagShotAtUVE/ResolveRayAtUVE. Fire-and-forget over a multiplexed
// channel (or a dedicated legacy session): no acks, no retries - a lost shot is a miss,
// like a lost input is a misprediction.
//
// The wire frame is a fixed 18 bytes, little-endian: version tag, shooter pawn, u32
// sequence, u64 view tick, f32 fraction. The sequence dedupes per shooter (the receiver
// drops anything at or below the last seen); the view tick passes through even when 0 -
// "no view yet" is meaningful, and the shot wiring fails it closed - while a fraction
// outside [0, 1) is corruption and never leaves the sender.
//

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <vector>

#include "uve/network/network_prediction_uve.h"
#include "uve/network/network_session_uve.h"

namespace UVE::Network {

/// Frame tag heading every shot intent (prediction's 0x01/0x02 convention, same idea).
inline constexpr std::uint8_t kShotIntentVersionUVE = 0x01U;
/// Tag + pawn + seq + view tick + fraction: the only accepted frame size, both ways.
inline constexpr std::size_t kShotIntentBytesUVE = 18U;

struct ShotIntentUVE final {
    PredictionPawnUVE pawn = kDefaultPredictionPawnUVE;
    std::uint32_t seq = 0U;
    PredictionTickUVE viewTick = kInvalidPredictionTickUVE;
    float fraction = 0.0F;

    [[nodiscard]] bool operator==(const ShotIntentUVE&) const noexcept = default;
};

/// Encodes one intent to its 18 wire bytes. No validation - the sender validates before
/// calling, and hand-encoded frames are the decoder test's business.
[[nodiscard]] std::vector<std::uint8_t> EncodeShotIntentUVE(const ShotIntentUVE& intent);

/// Strict decode: empty unless the size, version, and fraction ([0, 1), finite) all check
/// out. A zero view tick decodes fine - the shot wiring owns that guard, not the codec.
[[nodiscard]] std::optional<ShotIntentUVE> DecodeShotIntentUVE(
    const std::vector<std::uint8_t>& bytes);

/// Fires intents through a shared session: `channel` rides SendChannelDataUVE, empty rides
/// legacy SendDataUVE (a session dedicated to shots). Owns nothing; the session must
/// outlive the sender.
class ShotSenderUVE final {
public:
    explicit ShotSenderUVE(NetworkSessionUVE& session,
                           std::optional<NetworkChannelIdUVE> channel = std::nullopt) noexcept;

    /// Fires one intent, sequencing it past the last. Refuses (false, nothing sent, no seq
    /// burned) when the fraction is non-finite or outside [0, 1); a session refusal burns
    /// the seq - the shot never flew, so no gap can confuse the receiver.
    [[nodiscard]] bool FireUVE(PredictionPawnUVE pawn, PredictionTickUVE viewTick,
                               float fraction);

private:
    NetworkSessionUVE* m_session;
    std::optional<NetworkChannelIdUVE> m_channel;
    std::uint32_t m_nextSeq = 1U;
};

struct ShotReceiverStatsUVE final {
    std::uint64_t received = 0U;
    /// Malformed frames plus stale replays (seq at or below the shooter's last seen).
    std::uint64_t dropped = 0U;

    [[nodiscard]] bool operator==(const ShotReceiverStatsUVE&) const noexcept = default;
};

/// Polls intents off a shared session (`channel` mirrors the sender: set reads the
/// channel queue, empty the legacy one). Dedupes per shooter, drops malformed frames, and
/// counts both - every polled intent is well-formed and new.
class ShotReceiverUVE final {
public:
    explicit ShotReceiverUVE(NetworkSessionUVE& session,
                             std::optional<NetworkChannelIdUVE> channel = std::nullopt);

    /// Drains the queue, oldest first. Never blocks, never pumps the session - the app's
    /// own Update rhythm decides when polled intents arrive.
    [[nodiscard]] std::vector<ShotIntentUVE> PollUVE();

    [[nodiscard]] ShotReceiverStatsUVE StatsUVE() const noexcept { return m_stats; }

private:
    NetworkSessionUVE* m_session;
    std::optional<NetworkChannelIdUVE> m_channel;
    std::map<PredictionPawnUVE, std::uint32_t> m_lastSeen;
    ShotReceiverStatsUVE m_stats;
};

} // namespace UVE::Network
