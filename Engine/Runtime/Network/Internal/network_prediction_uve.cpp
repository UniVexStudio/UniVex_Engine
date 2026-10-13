// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/network/network_prediction_uve.h"

#include <cmath>
#include <cstddef>
#include <optional>
#include <utility>

#include "uve/utilities/binary_buffer_uve.h"

namespace UVE::Network {
namespace {

// One input on the wire: tick u64, pawn u8, dt f32, move xyz, rise, jump (30 bytes). One
// state: tick u64, pawn u8, position xyz, velocity xyz, grounded, appState u32 (39 bytes).
// Both length-exact, both rejected otherwise. A dt must be finite and within the second,
// the same gate TickUVE applies at the call.
inline constexpr std::uint8_t kInputKindUVE = 0x01U;
inline constexpr std::uint8_t kStateKindUVE = 0x02U;
inline constexpr std::size_t kInputEntryBytesUVE = 30U;
inline constexpr std::size_t kStateBytesUVE = 39U;

// One input entry buffered for the wire: whose tick, which pawn, the dt it stepped with,
// and the intent itself.
struct WireInputUVE final {
    PredictionTickUVE tick = kInvalidPredictionTickUVE;
    PredictionPawnUVE pawn = kDefaultPredictionPawnUVE;
    float dtSeconds = 0.0F;
    PredictedInputUVE input;
};

[[nodiscard]] std::vector<std::byte> ToBytesUVE(const std::vector<std::uint8_t>& payload) {
    std::vector<std::byte> buffer;
    buffer.reserve(payload.size());
    for (const std::uint8_t byte : payload) {
        buffer.push_back(static_cast<std::byte>(byte));
    }
    return buffer;
}

[[nodiscard]] std::vector<std::uint8_t> ToUint8UVE(const std::vector<std::byte>& buffer) {
    std::vector<std::uint8_t> payload;
    payload.reserve(buffer.size());
    for (const std::byte byte : buffer) {
        payload.push_back(static_cast<std::uint8_t>(byte));
    }
    return payload;
}

[[nodiscard]] std::vector<std::byte> EncodeInputUVE(const std::vector<WireInputUVE>& window) {
    std::vector<std::byte> message;
    message.reserve(2U + window.size() * kInputEntryBytesUVE);
    message.push_back(static_cast<std::byte>(kInputKindUVE));
    message.push_back(static_cast<std::byte>(static_cast<std::uint8_t>(window.size())));
    for (const auto& entry : window) {
        Utilities::AppendUint64LeUVE(message, entry.tick);
        message.push_back(static_cast<std::byte>(entry.pawn));
        Utilities::AppendFloatLeUVE(message, entry.dtSeconds);
        Utilities::AppendFloatLeUVE(message, entry.input.move.x);
        Utilities::AppendFloatLeUVE(message, entry.input.move.y);
        Utilities::AppendFloatLeUVE(message, entry.input.move.z);
        Utilities::AppendFloatLeUVE(message, entry.input.rise);
        message.push_back(static_cast<std::byte>(entry.input.jumpPressed ? 0x01U : 0x00U));
    }
    return message;
}

[[nodiscard]] std::optional<std::vector<WireInputUVE>> DecodeInputUVE(
    const std::vector<std::uint8_t>& payload) {
    const std::vector<std::byte> buffer = ToBytesUVE(payload);
    if (buffer.size() < 2U || static_cast<std::uint8_t>(buffer[0]) != kInputKindUVE) {
        return std::nullopt;
    }
    const std::size_t count = static_cast<std::size_t>(static_cast<std::uint8_t>(buffer[1]));
    if (count == 0U || count > kPredictionMaximumInputsPerMessageUVE ||
        buffer.size() != 2U + count * kInputEntryBytesUVE) {
        return std::nullopt;
    }
    std::vector<WireInputUVE> window;
    window.reserve(count);
    std::size_t offset = 2U;
    for (std::size_t i = 0U; i < count; ++i) {
        WireInputUVE entry;
        std::uint8_t jump = 0U;
        if (!Utilities::ReadUint64LeFromBufferUVE(buffer, offset, entry.tick) ||
            offset >= buffer.size()) {
            return std::nullopt;
        }
        entry.pawn = static_cast<std::uint8_t>(buffer[offset++]);
        if (!Utilities::ReadFloatLeFromBufferUVE(buffer, offset, entry.dtSeconds) ||
            !Utilities::ReadFloatLeFromBufferUVE(buffer, offset, entry.input.move.x) ||
            !Utilities::ReadFloatLeFromBufferUVE(buffer, offset, entry.input.move.y) ||
            !Utilities::ReadFloatLeFromBufferUVE(buffer, offset, entry.input.move.z) ||
            !Utilities::ReadFloatLeFromBufferUVE(buffer, offset, entry.input.rise)) {
            return std::nullopt;
        }
        if (offset >= buffer.size()) {
            return std::nullopt;
        }
        jump = static_cast<std::uint8_t>(buffer[offset++]);
        if (entry.tick == kInvalidPredictionTickUVE || jump > 0x01U ||
            !std::isfinite(entry.dtSeconds) || entry.dtSeconds <= 0.0F ||
            entry.dtSeconds > kPredictionMaximumTickDtSecondsUVE ||
            !std::isfinite(entry.input.move.x) || !std::isfinite(entry.input.move.y) ||
            !std::isfinite(entry.input.move.z) || !std::isfinite(entry.input.rise)) {
            return std::nullopt;
        }
        entry.input.jumpPressed = jump == 0x01U;
        window.push_back(entry);
    }
    return window;
}

[[nodiscard]] bool WithinUVE(const float a, const float b, const float epsilon) noexcept {
    return std::fabs(a - b) <= epsilon;
}

/// Agreement within the config: positions and velocities per component, grounded and the app
/// word exact. What the client predicted against what the server simulated.
[[nodiscard]] bool StatesEqualUVE(const PredictedStateUVE& predicted, const PredictedStateUVE& state,
                                  const PredictionConfigUVE& config) noexcept {
    return WithinUVE(predicted.position.x, state.position.x, config.positionEpsilon) &&
           WithinUVE(predicted.position.y, state.position.y, config.positionEpsilon) &&
           WithinUVE(predicted.position.z, state.position.z, config.positionEpsilon) &&
           WithinUVE(predicted.velocity.x, state.velocity.x, config.velocityEpsilon) &&
           WithinUVE(predicted.velocity.y, state.velocity.y, config.velocityEpsilon) &&
           WithinUVE(predicted.velocity.z, state.velocity.z, config.velocityEpsilon) &&
           predicted.grounded == state.grounded && predicted.appState == state.appState;
}

} // namespace

std::vector<std::uint8_t> EncodePredictedStateUVE(const PredictionTickUVE tick,
                                                  const PredictionPawnUVE pawn,
                                                  const PredictedStateUVE& state) {
    std::vector<std::byte> message;
    message.reserve(kStateBytesUVE);
    message.push_back(static_cast<std::byte>(kStateKindUVE));
    Utilities::AppendUint64LeUVE(message, tick);
    message.push_back(static_cast<std::byte>(pawn));
    Utilities::AppendFloatLeUVE(message, state.position.x);
    Utilities::AppendFloatLeUVE(message, state.position.y);
    Utilities::AppendFloatLeUVE(message, state.position.z);
    Utilities::AppendFloatLeUVE(message, state.velocity.x);
    Utilities::AppendFloatLeUVE(message, state.velocity.y);
    Utilities::AppendFloatLeUVE(message, state.velocity.z);
    message.push_back(static_cast<std::byte>(state.grounded ? 0x01U : 0x00U));
    Utilities::AppendUint32LeUVE(message, state.appState);
    return ToUint8UVE(message);
}

std::optional<PredictedStateMessageUVE> DecodePredictedStateUVE(
    const std::vector<std::uint8_t>& payload) {
    const std::vector<std::byte> buffer = ToBytesUVE(payload);
    if (buffer.size() != kStateBytesUVE || static_cast<std::uint8_t>(buffer[0]) != kStateKindUVE) {
        return std::nullopt;
    }
    std::size_t offset = 1U;
    PredictedStateMessageUVE message;
    std::uint8_t grounded = 0U;
    if (!Utilities::ReadUint64LeFromBufferUVE(buffer, offset, message.tick) ||
        offset >= buffer.size()) {
        return std::nullopt;
    }
    message.pawn = static_cast<std::uint8_t>(buffer[offset++]);
    if (!Utilities::ReadFloatLeFromBufferUVE(buffer, offset, message.state.position.x) ||
        !Utilities::ReadFloatLeFromBufferUVE(buffer, offset, message.state.position.y) ||
        !Utilities::ReadFloatLeFromBufferUVE(buffer, offset, message.state.position.z) ||
        !Utilities::ReadFloatLeFromBufferUVE(buffer, offset, message.state.velocity.x) ||
        !Utilities::ReadFloatLeFromBufferUVE(buffer, offset, message.state.velocity.y) ||
        !Utilities::ReadFloatLeFromBufferUVE(buffer, offset, message.state.velocity.z)) {
        return std::nullopt;
    }
    if (offset + 2U > buffer.size()) {
        return std::nullopt;
    }
    grounded = static_cast<std::uint8_t>(buffer[offset++]);
    if (!Utilities::ReadUint32LeFromBufferUVE(buffer, offset, message.state.appState)) {
        return std::nullopt;
    }
    if (message.tick == kInvalidPredictionTickUVE || grounded > 0x01U || offset != buffer.size() ||
        !std::isfinite(message.state.position.x) || !std::isfinite(message.state.position.y) ||
        !std::isfinite(message.state.position.z) || !std::isfinite(message.state.velocity.x) ||
        !std::isfinite(message.state.velocity.y) || !std::isfinite(message.state.velocity.z)) {
        return std::nullopt;
    }
    message.state.grounded = grounded == 0x01U;
    return message;
}

NetworkPredictionUVE::NetworkPredictionUVE(NetworkSessionUVE&& session, const SessionRoleUVE role,
                                           PredictionStepperUVE stepper, PredictedStateUVE initial,
                                           PredictionConfigUVE config)
    : m_session(std::move(session)),
      m_role(role),
      m_stepper(std::move(stepper)),
      m_config(config),
      m_local{{kDefaultPredictionPawnUVE, initial}} {}

NetworkPredictionUVE::NetworkPredictionUVE(NetworkSessionUVE& sharedSession,
                                           const NetworkChannelIdUVE channel,
                                           const SessionRoleUVE role,
                                           PredictionStepperUVE stepper,
                                           PredictedStateUVE initial, PredictionConfigUVE config)
    : m_session(&sharedSession),
      m_channel(channel),
      m_role(role),
      m_stepper(std::move(stepper)),
      m_config(config),
      m_local{{kDefaultPredictionPawnUVE, initial}} {}

bool NetworkPredictionUVE::SendPayloadUVE(const std::vector<std::uint8_t>& payload) {
    if (std::holds_alternative<NetworkSessionUVE*>(m_session)) {
        return SessionUVE().SendChannelDataUVE(m_channel, payload);
    }
    return SessionUVE().SendDataUVE(payload);
}

std::optional<std::vector<std::uint8_t>> NetworkPredictionUVE::ReceivePayloadUVE() {
    if (std::holds_alternative<NetworkSessionUVE*>(m_session)) {
        return SessionUVE().TryReceiveChannelDataUVE(m_channel);
    }
    return SessionUVE().TryReceiveDataUVE();
}

bool NetworkPredictionUVE::ValidTickDtUVE(const float dtSeconds) noexcept {
    return std::isfinite(dtSeconds) && dtSeconds > 0.0F &&
           dtSeconds <= kPredictionMaximumTickDtSecondsUVE;
}

PredictedStateUVE NetworkPredictionUVE::StepUVE(const PredictedStateUVE& state,
                                                const PredictedInputUVE& input,
                                                const float dtSeconds) const {
    if (!m_stepper) {
        return state;
    }
    return m_stepper(state, input, dtSeconds);
}

NetworkPredictionUVE::ClientPawnStreamUVE* NetworkPredictionUVE::ClientStreamUVE(
    const PredictionPawnUVE pawn) {
    const auto known = m_clientPawns.find(pawn);
    if (known != m_clientPawns.end()) {
        return &known->second;
    }
    if (m_clientPawns.size() >= kPredictionMaximumPawnsUVE) {
        return nullptr;
    }
    const auto placed = m_clientPawns.emplace(pawn, ClientPawnStreamUVE{});
    m_local.try_emplace(pawn, PredictedStateUVE{});
    return &placed.first->second;
}

NetworkPredictionUVE::ServerPawnStreamUVE* NetworkPredictionUVE::ServerStreamUVE(
    const PredictionPawnUVE pawn) {
    const auto known = m_serverPawns.find(pawn);
    if (known != m_serverPawns.end()) {
        return &known->second;
    }
    if (m_serverPawns.size() >= kPredictionMaximumPawnsUVE) {
        return nullptr;
    }
    const auto placed = m_serverPawns.emplace(pawn, ServerPawnStreamUVE{});
    m_local.try_emplace(pawn, PredictedStateUVE{});
    return &placed.first->second;
}

PredictionTickUVE NetworkPredictionUVE::TickUVE(const PredictedInputUVE& input) {
    return TickUVE(kDefaultPredictionPawnUVE, input, m_config.fixedDtSeconds);
}

PredictionTickUVE NetworkPredictionUVE::TickUVE(const PredictionPawnUVE pawn,
                                                const PredictedInputUVE& input) {
    return TickUVE(pawn, input, m_config.fixedDtSeconds);
}

PredictionTickUVE NetworkPredictionUVE::TickUVE(const PredictionPawnUVE pawn,
                                                const PredictedInputUVE& input,
                                                const float dtSeconds) {
    if (m_role != SessionRoleUVE::Client) {
        return kInvalidPredictionTickUVE;
    }
    ClientPawnStreamUVE* const stream = ClientStreamUVE(pawn);
    if (stream == nullptr || !ValidTickDtUVE(dtSeconds)) {
        return kInvalidPredictionTickUVE;
    }
    const PredictionTickUVE tick = stream->nextTick++;
    m_local[pawn] = StepUVE(m_local[pawn], input, dtSeconds);
    stream->inputs.emplace(tick, std::make_pair(input, dtSeconds));
    stream->states.emplace(tick, m_local[pawn]);
    while (stream->inputs.size() > kPredictionMaximumHistoryTicksUVE) {
        stream->inputs.erase(stream->inputs.begin());
    }
    while (stream->states.size() > kPredictionMaximumHistoryTicksUVE) {
        stream->states.erase(stream->states.begin());
    }
    if (SessionUVE().StateUVE() == SessionStateUVE::Established) {
        // The oldest unacked inputs round-robin across pawns (ascending, oldest first within
        // each): everything here is unacked (acks prune), no pawn starves another, and the
        // order is deterministic for any table.
        struct CursorUVE {
            std::map<PredictionTickUVE, std::pair<PredictedInputUVE, float>>::const_iterator at;
            std::map<PredictionTickUVE, std::pair<PredictedInputUVE, float>>::const_iterator end;
            PredictionPawnUVE pawn = kDefaultPredictionPawnUVE;
        };
        std::vector<CursorUVE> cursors;
        for (const auto& [cursorPawn, cursorStream] : m_clientPawns) {
            if (!cursorStream.inputs.empty()) {
                cursors.push_back(
                    CursorUVE{cursorStream.inputs.begin(), cursorStream.inputs.end(), cursorPawn});
            }
        }
        std::vector<WireInputUVE> window;
        while (window.size() < kPredictionMaximumInputsPerMessageUVE && !cursors.empty()) {
            for (auto cursor = cursors.begin();
                 cursor != cursors.end() && window.size() < kPredictionMaximumInputsPerMessageUVE;) {
                WireInputUVE entry;
                entry.tick = cursor->at->first;
                entry.pawn = cursor->pawn;
                entry.dtSeconds = cursor->at->second.second;
                entry.input = cursor->at->second.first;
                window.push_back(entry);
                ++cursor->at;
                if (cursor->at == cursor->end) {
                    cursor = cursors.erase(cursor);
                } else {
                    ++cursor;
                }
            }
        }
        if (SendPayloadUVE(ToUint8UVE(EncodeInputUVE(window)))) {
            ++m_stats.inputsSent;
        } else {
            ++m_stats.sendsFailed;
        }
    }
    return tick;
}

bool NetworkPredictionUVE::SeedPawnUVE(const PredictionPawnUVE pawn, const PredictedStateUVE& state) {
    if (pawn == kDefaultPredictionPawnUVE) {
        return false;
    }
    if (m_role == SessionRoleUVE::Client) {
        if (m_clientPawns.contains(pawn) || m_clientPawns.size() >= kPredictionMaximumPawnsUVE) {
            return false;
        }
        m_clientPawns.emplace(pawn, ClientPawnStreamUVE{});
    } else {
        if (m_serverPawns.contains(pawn) || m_serverPawns.size() >= kPredictionMaximumPawnsUVE) {
            return false;
        }
        m_serverPawns.emplace(pawn, ServerPawnStreamUVE{});
    }
    m_local[pawn] = state;
    return true;
}

void NetworkPredictionUVE::UpdateUVE(const std::uint64_t nowMs) {
    SessionUVE().UpdateUVE(nowMs);
    m_lastSeenMs = nowMs;
    for (;;) {
        const std::optional<std::vector<std::uint8_t>> payload = ReceivePayloadUVE();
        if (!payload.has_value()) {
            break;
        }
        if (payload->empty()) {
            ++m_stats.framesIgnoredMalformed;
            continue;
        }
        const std::uint8_t kind = payload->front();
        if (m_role == SessionRoleUVE::Client) {
            if (kind == kStateKindUVE) {
                HandleStateMessageUVE(*payload);
            } else {
                ++m_stats.framesIgnoredMalformed;
            }
        } else {
            if (kind == kInputKindUVE) {
                HandleInputMessageUVE(*payload);
            } else {
                ++m_stats.framesIgnoredMalformed;
            }
        }
    }
    if (m_role == SessionRoleUVE::Server) {
        for (auto& [pawn, stream] : m_serverPawns) {
            if (stream.buffer.empty()) {
                stream.lastApplyMs = nowMs;
                continue;
            }
            const PredictionTickUVE oldest = stream.buffer.begin()->first;
            if (oldest > stream.lastApplied + 1U &&
                nowMs - stream.lastApplyMs >= kPredictionGapTimeoutMsUVE) {
                // The gap is unfillable (the client evicted those ticks an age ago): jump the
                // pawn's stream past it instead of holding every newer input forever. Repeats
                // of the skipped ticks land as duplicates, which is exactly what they now are.
                // Only this pawn jumps - every other stream keeps its own clock.
                stream.lastApplied = oldest - 1U;
                stream.lastApplyMs = nowMs;
                ++m_stats.inputBufferOverruns;
                ApplyBufferedUVE(stream, pawn);
            }
        }
    }
}

void NetworkPredictionUVE::HandleInputMessageUVE(const std::vector<std::uint8_t>& payload) {
    const auto decoded = DecodeInputUVE(payload);
    if (!decoded.has_value()) {
        ++m_stats.framesIgnoredMalformed;
        return;
    }
    for (const auto& entry : *decoded) {
        ++m_stats.inputsReceived;
        ServerPawnStreamUVE* const stream = ServerStreamUVE(entry.pawn);
        if (stream == nullptr) {
            ++m_stats.inputsRefusedOverPawnCap;
            continue;
        }
        if (entry.tick <= stream->lastApplied || stream->buffer.contains(entry.tick)) {
            ++m_stats.inputsIgnoredDuplicate;
            // A repeated input hints its ack never arrived: answer with the pawn's newest
            // state, whose cumulative ack covers everything the client still holds.
            if (stream->hasLastState) {
                SendStateUVE(entry.pawn, stream->lastApplied, stream->lastState);
            }
            continue;
        }
        if (stream->buffer.size() >= kPredictionMaximumServerBufferUVE) {
            ++m_stats.inputBufferOverruns;
            continue;
        }
        const bool wasEmpty = stream->buffer.empty();
        stream->buffer.emplace(entry.tick, std::make_pair(entry.input, entry.dtSeconds));
        if (wasEmpty) {
            stream->lastApplyMs = m_lastSeenMs;
        }
        ApplyBufferedUVE(*stream, entry.pawn);
    }
}

void NetworkPredictionUVE::HandleStateMessageUVE(const std::vector<std::uint8_t>& payload) {
    const auto decoded = DecodePredictedStateUVE(payload);
    if (!decoded.has_value()) {
        ++m_stats.framesIgnoredMalformed;
        return;
    }
    ++m_stats.statesReceived;
    const PredictedStateMessageUVE message = *decoded;
    const auto knownStream = m_clientPawns.find(message.pawn);
    if (knownStream == m_clientPawns.end()) {
        // Well-formed but unsolicited: the server only answers inputs this client sent, so a
        // state for a pawn that never ran here is foreign.
        ++m_stats.framesIgnoredMalformed;
        return;
    }
    ClientPawnStreamUVE& stream = knownStream->second;
    if (message.tick <= stream.lastAcked) {
        ++m_stats.statesIgnoredStale;
        return;
    }
    const auto known = stream.states.find(message.tick);
    if (known == stream.states.end()) {
        ++m_stats.stateHistoryMisses;
    } else if (StatesEqualUVE(known->second, message.state, m_config)) {
        PruneThroughUVE(stream, message.tick);
        stream.lastAcked = message.tick;
        return;
    }
    // Disagreed (or the tick aged out of history): snap to the server and replay every
    // surviving newer input with the dt it stepped with, so the present is predicted from
    // authoritative ground.
    ++m_stats.reconciles;
    m_local[message.pawn] = message.state;
    PruneThroughUVE(stream, message.tick);
    for (const auto& [replayTick, replayEntry] : stream.inputs) {
        m_local[message.pawn] = StepUVE(m_local[message.pawn], replayEntry.first, replayEntry.second);
        stream.states[replayTick] = m_local[message.pawn];
    }
    stream.lastAcked = message.tick;
}

void NetworkPredictionUVE::ApplyBufferedUVE(ServerPawnStreamUVE& stream,
                                            const PredictionPawnUVE pawn) {
    for (;;) {
        const PredictionTickUVE expected = stream.lastApplied + 1U;
        const auto buffered = stream.buffer.find(expected);
        if (buffered == stream.buffer.end()) {
            return;
        }
        const PredictedInputUVE input = buffered->second.first;
        const float dtSeconds = buffered->second.second;
        stream.buffer.erase(buffered);
        m_local[pawn] = StepUVE(m_local[pawn], input, dtSeconds);
        stream.lastApplied = expected;
        stream.lastApplyMs = m_lastSeenMs;
        ++m_stats.inputsApplied;
        stream.lastState = m_local[pawn];
        stream.hasLastState = true;
        SendStateUVE(pawn, expected, m_local[pawn]);
    }
}

void NetworkPredictionUVE::SendStateUVE(const PredictionPawnUVE pawn, const PredictionTickUVE tick,
                                        const PredictedStateUVE& state) {
    if (SessionUVE().StateUVE() != SessionStateUVE::Established) {
        return;
    }
    if (SendPayloadUVE(EncodePredictedStateUVE(tick, pawn, state))) {
        ++m_stats.statesSent;
    } else {
        ++m_stats.sendsFailed;
    }
}

void NetworkPredictionUVE::PruneThroughUVE(ClientPawnStreamUVE& stream,
                                           const PredictionTickUVE tick) {
    while (!stream.inputs.empty() && stream.inputs.begin()->first <= tick) {
        stream.inputs.erase(stream.inputs.begin());
    }
    while (!stream.states.empty() && stream.states.begin()->first <= tick) {
        stream.states.erase(stream.states.begin());
    }
}

PredictedStateUVE NetworkPredictionUVE::LocalStateUVE(const PredictionPawnUVE pawn) const {
    const auto known = m_local.find(pawn);
    if (known == m_local.end()) {
        return PredictedStateUVE{};
    }
    return known->second;
}

PredictionTickUVE NetworkPredictionUVE::LastAckedTickUVE(const PredictionPawnUVE pawn) const noexcept {
    const auto known = m_clientPawns.find(pawn);
    if (known == m_clientPawns.end()) {
        return kInvalidPredictionTickUVE;
    }
    return known->second.lastAcked;
}

PredictionTickUVE NetworkPredictionUVE::LastAppliedTickUVE(
    const PredictionPawnUVE pawn) const noexcept {
    const auto known = m_serverPawns.find(pawn);
    if (known == m_serverPawns.end()) {
        return kInvalidPredictionTickUVE;
    }
    return known->second.lastApplied;
}

std::size_t PredictionRelayUVE::AddSubscriberUVE(NetworkSessionUVE&& session) {
    m_subscribers.push_back(std::move(session));
    return m_subscribers.size() - 1U;
}

bool PredictionRelayUVE::RemoveSubscriberUVE(const std::size_t index) {
    if (index >= m_subscribers.size()) {
        return false;
    }
    m_subscribers.erase(m_subscribers.begin() + static_cast<std::ptrdiff_t>(index));
    return true;
}

void PredictionRelayUVE::UpdateUVE(const std::uint64_t nowMs) {
    for (auto& subscriber : m_subscribers) {
        subscriber.UpdateUVE(nowMs);
    }
}

std::size_t PredictionRelayUVE::PublishUVE(const PredictionTickUVE tick,
                                           const PredictionPawnUVE pawn,
                                           const PredictedStateUVE& state) {
    const std::vector<std::uint8_t> payload = EncodePredictedStateUVE(tick, pawn, state);
    std::size_t sent = 0U;
    for (auto& subscriber : m_subscribers) {
        if (subscriber.StateUVE() != SessionStateUVE::Established) {
            continue;
        }
        if (subscriber.SendDataUVE(payload)) {
            ++m_stats.statesRelayed;
            ++sent;
        } else {
            ++m_stats.sendsFailed;
        }
    }
    return sent;
}

PredictionObserverUVE::PredictionObserverUVE(NetworkSessionUVE&& session)
    : m_session(std::move(session)) {}

void PredictionObserverUVE::UpdateUVE(const std::uint64_t nowMs) {
    m_session.UpdateUVE(nowMs);
    for (;;) {
        const std::optional<std::vector<std::uint8_t>> payload = m_session.TryReceiveDataUVE();
        if (!payload.has_value()) {
            break;
        }
        if (payload->empty()) {
            ++m_stats.framesIgnoredMalformed;
            continue;
        }
        const auto decoded = DecodePredictedStateUVE(*payload);
        if (!decoded.has_value()) {
            ++m_stats.framesIgnoredMalformed;
            continue;
        }
        ++m_stats.statesReceived;
        const PredictedStateMessageUVE message = *decoded;
        const auto known = m_latest.find(message.pawn);
        if (known != m_latest.end() && message.tick <= known->second.tick) {
            ++m_stats.statesIgnoredStale;
            continue;
        }
        m_latest[message.pawn] = message;
    }
}

std::optional<PredictedStateMessageUVE> PredictionObserverUVE::LatestUVE(
    const PredictionPawnUVE pawn) const {
    const auto known = m_latest.find(pawn);
    if (known == m_latest.end()) {
        return std::nullopt;
    }
    return known->second;
}

} // namespace UVE::Network
