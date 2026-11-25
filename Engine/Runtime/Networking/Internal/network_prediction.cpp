// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "network_prediction.h"

#include <cmath>
#include <cstddef>
#include <optional>
#include <utility>

#include "binary_buffer.h"

namespace UVE::Network {
namespace {

// One input on the wire: tick u64, pawn u8, dt f32, move xyz, rise, jump (30 bytes). One
// state: tick u64, pawn u8, position xyz, velocity xyz, grounded, appState u32 (39 bytes).
// Both length-exact, both rejected otherwise. A dt must be finite and within the second,
// the same gate Tick applies at the call.
inline constexpr std::uint8_t kInputKind = 0x01U;
inline constexpr std::uint8_t kStateKind = 0x02U;
inline constexpr std::size_t kInputEntryBytes = 30U;
inline constexpr std::size_t kStateBytes = 39U;

// One input entry buffered for the wire: whose tick, which pawn, the dt it stepped with,
// and the intent itself.
struct WireInput final {
    PredictionTick tick = kInvalidPredictionTick;
    PredictionPawn pawn = kDefaultPredictionPawn;
    float dtSeconds = 0.0F;
    PredictedInput input;
};

[[nodiscard]] std::vector<std::byte> ToBytes(const std::vector<std::uint8_t>& payload) {
    std::vector<std::byte> buffer;
    buffer.reserve(payload.size());
    for (const std::uint8_t byte : payload) {
        buffer.push_back(static_cast<std::byte>(byte));
    }
    return buffer;
}

[[nodiscard]] std::vector<std::uint8_t> ToUint8(const std::vector<std::byte>& buffer) {
    std::vector<std::uint8_t> payload;
    payload.reserve(buffer.size());
    for (const std::byte byte : buffer) {
        payload.push_back(static_cast<std::uint8_t>(byte));
    }
    return payload;
}

[[nodiscard]] std::vector<std::byte> EncodeInput(const std::vector<WireInput>& window) {
    std::vector<std::byte> message;
    message.reserve(2U + window.size() * kInputEntryBytes);
    message.push_back(static_cast<std::byte>(kInputKind));
    message.push_back(static_cast<std::byte>(static_cast<std::uint8_t>(window.size())));
    for (const auto& entry : window) {
        Utilities::AppendUint64Le(message, entry.tick);
        message.push_back(static_cast<std::byte>(entry.pawn));
        Utilities::AppendFloatLe(message, entry.dtSeconds);
        Utilities::AppendFloatLe(message, entry.input.move.x);
        Utilities::AppendFloatLe(message, entry.input.move.y);
        Utilities::AppendFloatLe(message, entry.input.move.z);
        Utilities::AppendFloatLe(message, entry.input.rise);
        message.push_back(static_cast<std::byte>(entry.input.jumpPressed ? 0x01U : 0x00U));
    }
    return message;
}

[[nodiscard]] std::optional<std::vector<WireInput>> DecodeInput(
    const std::vector<std::uint8_t>& payload) {
    const std::vector<std::byte> buffer = ToBytes(payload);
    if (buffer.size() < 2U || static_cast<std::uint8_t>(buffer[0]) != kInputKind) {
        return std::nullopt;
    }
    const std::size_t count = static_cast<std::size_t>(static_cast<std::uint8_t>(buffer[1]));
    if (count == 0U || count > kPredictionMaximumInputsPerMessage ||
        buffer.size() != 2U + count * kInputEntryBytes) {
        return std::nullopt;
    }
    std::vector<WireInput> window;
    window.reserve(count);
    std::size_t offset = 2U;
    for (std::size_t i = 0U; i < count; ++i) {
        WireInput entry;
        std::uint8_t jump = 0U;
        if (!Utilities::ReadUint64LeFromBuffer(buffer, offset, entry.tick) ||
            offset >= buffer.size()) {
            return std::nullopt;
        }
        entry.pawn = static_cast<std::uint8_t>(buffer[offset++]);
        if (!Utilities::ReadFloatLeFromBuffer(buffer, offset, entry.dtSeconds) ||
            !Utilities::ReadFloatLeFromBuffer(buffer, offset, entry.input.move.x) ||
            !Utilities::ReadFloatLeFromBuffer(buffer, offset, entry.input.move.y) ||
            !Utilities::ReadFloatLeFromBuffer(buffer, offset, entry.input.move.z) ||
            !Utilities::ReadFloatLeFromBuffer(buffer, offset, entry.input.rise)) {
            return std::nullopt;
        }
        if (offset >= buffer.size()) {
            return std::nullopt;
        }
        jump = static_cast<std::uint8_t>(buffer[offset++]);
        if (entry.tick == kInvalidPredictionTick || jump > 0x01U ||
            !std::isfinite(entry.dtSeconds) || entry.dtSeconds <= 0.0F ||
            entry.dtSeconds > kPredictionMaximumTickDtSeconds ||
            !std::isfinite(entry.input.move.x) || !std::isfinite(entry.input.move.y) ||
            !std::isfinite(entry.input.move.z) || !std::isfinite(entry.input.rise)) {
            return std::nullopt;
        }
        entry.input.jumpPressed = jump == 0x01U;
        window.push_back(entry);
    }
    return window;
}

[[nodiscard]] bool Within(const float a, const float b, const float epsilon) noexcept {
    return std::fabs(a - b) <= epsilon;
}

/// Agreement within the config: positions and velocities per component, grounded and the app
/// word exact. What the client predicted against what the server simulated.
[[nodiscard]] bool StatesEqual(const PredictedState& predicted, const PredictedState& state,
                                  const PredictionConfig& config) noexcept {
    return Within(predicted.position.x, state.position.x, config.positionEpsilon) &&
           Within(predicted.position.y, state.position.y, config.positionEpsilon) &&
           Within(predicted.position.z, state.position.z, config.positionEpsilon) &&
           Within(predicted.velocity.x, state.velocity.x, config.velocityEpsilon) &&
           Within(predicted.velocity.y, state.velocity.y, config.velocityEpsilon) &&
           Within(predicted.velocity.z, state.velocity.z, config.velocityEpsilon) &&
           predicted.grounded == state.grounded && predicted.appState == state.appState;
}

} // namespace

std::vector<std::uint8_t> EncodePredictedState(const PredictionTick tick,
                                                  const PredictionPawn pawn,
                                                  const PredictedState& state) {
    std::vector<std::byte> message;
    message.reserve(kStateBytes);
    message.push_back(static_cast<std::byte>(kStateKind));
    Utilities::AppendUint64Le(message, tick);
    message.push_back(static_cast<std::byte>(pawn));
    Utilities::AppendFloatLe(message, state.position.x);
    Utilities::AppendFloatLe(message, state.position.y);
    Utilities::AppendFloatLe(message, state.position.z);
    Utilities::AppendFloatLe(message, state.velocity.x);
    Utilities::AppendFloatLe(message, state.velocity.y);
    Utilities::AppendFloatLe(message, state.velocity.z);
    message.push_back(static_cast<std::byte>(state.grounded ? 0x01U : 0x00U));
    Utilities::AppendUint32Le(message, state.appState);
    return ToUint8(message);
}

std::optional<PredictedStateMessage> DecodePredictedState(
    const std::vector<std::uint8_t>& payload) {
    const std::vector<std::byte> buffer = ToBytes(payload);
    if (buffer.size() != kStateBytes || static_cast<std::uint8_t>(buffer[0]) != kStateKind) {
        return std::nullopt;
    }
    std::size_t offset = 1U;
    PredictedStateMessage message;
    std::uint8_t grounded = 0U;
    if (!Utilities::ReadUint64LeFromBuffer(buffer, offset, message.tick) ||
        offset >= buffer.size()) {
        return std::nullopt;
    }
    message.pawn = static_cast<std::uint8_t>(buffer[offset++]);
    if (!Utilities::ReadFloatLeFromBuffer(buffer, offset, message.state.position.x) ||
        !Utilities::ReadFloatLeFromBuffer(buffer, offset, message.state.position.y) ||
        !Utilities::ReadFloatLeFromBuffer(buffer, offset, message.state.position.z) ||
        !Utilities::ReadFloatLeFromBuffer(buffer, offset, message.state.velocity.x) ||
        !Utilities::ReadFloatLeFromBuffer(buffer, offset, message.state.velocity.y) ||
        !Utilities::ReadFloatLeFromBuffer(buffer, offset, message.state.velocity.z)) {
        return std::nullopt;
    }
    if (offset + 2U > buffer.size()) {
        return std::nullopt;
    }
    grounded = static_cast<std::uint8_t>(buffer[offset++]);
    if (!Utilities::ReadUint32LeFromBuffer(buffer, offset, message.state.appState)) {
        return std::nullopt;
    }
    if (message.tick == kInvalidPredictionTick || grounded > 0x01U || offset != buffer.size() ||
        !std::isfinite(message.state.position.x) || !std::isfinite(message.state.position.y) ||
        !std::isfinite(message.state.position.z) || !std::isfinite(message.state.velocity.x) ||
        !std::isfinite(message.state.velocity.y) || !std::isfinite(message.state.velocity.z)) {
        return std::nullopt;
    }
    message.state.grounded = grounded == 0x01U;
    return message;
}

NetworkPrediction::NetworkPrediction(NetworkSession&& session, const SessionRole role,
                                           PredictionStepper stepper, PredictedState initial,
                                           PredictionConfig config)
    : m_session(std::move(session)),
      m_role(role),
      m_stepper(std::move(stepper)),
      m_config(config),
      m_local{{kDefaultPredictionPawn, initial}} {}

NetworkPrediction::NetworkPrediction(NetworkSession& sharedSession,
                                           const NetworkChannelId channel,
                                           const SessionRole role,
                                           PredictionStepper stepper,
                                           PredictedState initial, PredictionConfig config)
    : m_session(&sharedSession),
      m_channel(channel),
      m_role(role),
      m_stepper(std::move(stepper)),
      m_config(config),
      m_local{{kDefaultPredictionPawn, initial}} {}

bool NetworkPrediction::SendPayload(const std::vector<std::uint8_t>& payload) {
    if (std::holds_alternative<NetworkSession*>(m_session)) {
        return Session().SendChannelData(m_channel, payload);
    }
    return Session().SendData(payload);
}

std::optional<std::vector<std::uint8_t>> NetworkPrediction::ReceivePayload() {
    if (std::holds_alternative<NetworkSession*>(m_session)) {
        return Session().TryReceiveChannelData(m_channel);
    }
    return Session().TryReceiveData();
}

bool NetworkPrediction::ValidTickDt(const float dtSeconds) noexcept {
    return std::isfinite(dtSeconds) && dtSeconds > 0.0F &&
           dtSeconds <= kPredictionMaximumTickDtSeconds;
}

PredictedState NetworkPrediction::Step(const PredictedState& state,
                                                const PredictedInput& input,
                                                const float dtSeconds) const {
    if (!m_stepper) {
        return state;
    }
    return m_stepper(state, input, dtSeconds);
}

NetworkPrediction::ClientPawnStream* NetworkPrediction::ClientStream(
    const PredictionPawn pawn) {
    const auto known = m_clientPawns.find(pawn);
    if (known != m_clientPawns.end()) {
        return &known->second;
    }
    if (m_clientPawns.size() >= kPredictionMaximumPawns) {
        return nullptr;
    }
    const auto placed = m_clientPawns.emplace(pawn, ClientPawnStream{});
    m_local.try_emplace(pawn, PredictedState{});
    return &placed.first->second;
}

NetworkPrediction::ServerPawnStream* NetworkPrediction::ServerStream(
    const PredictionPawn pawn) {
    const auto known = m_serverPawns.find(pawn);
    if (known != m_serverPawns.end()) {
        return &known->second;
    }
    if (m_serverPawns.size() >= kPredictionMaximumPawns) {
        return nullptr;
    }
    const auto placed = m_serverPawns.emplace(pawn, ServerPawnStream{});
    m_local.try_emplace(pawn, PredictedState{});
    return &placed.first->second;
}

PredictionTick NetworkPrediction::Tick(const PredictedInput& input) {
    return Tick(kDefaultPredictionPawn, input, m_config.fixedDtSeconds);
}

PredictionTick NetworkPrediction::Tick(const PredictionPawn pawn,
                                                const PredictedInput& input) {
    return Tick(pawn, input, m_config.fixedDtSeconds);
}

PredictionTick NetworkPrediction::Tick(const PredictionPawn pawn,
                                                const PredictedInput& input,
                                                const float dtSeconds) {
    if (m_role != SessionRole::Client) {
        return kInvalidPredictionTick;
    }
    ClientPawnStream* const stream = ClientStream(pawn);
    if (stream == nullptr || !ValidTickDt(dtSeconds)) {
        return kInvalidPredictionTick;
    }
    const PredictionTick tick = stream->nextTick++;
    m_local[pawn] = Step(m_local[pawn], input, dtSeconds);
    stream->inputs.emplace(tick, std::make_pair(input, dtSeconds));
    stream->states.emplace(tick, m_local[pawn]);
    while (stream->inputs.size() > kPredictionMaximumHistoryTicks) {
        stream->inputs.erase(stream->inputs.begin());
    }
    while (stream->states.size() > kPredictionMaximumHistoryTicks) {
        stream->states.erase(stream->states.begin());
    }
    if (Session().State() == SessionState::Established) {
        // The oldest unacked inputs round-robin across pawns (ascending, oldest first within
        // each): everything here is unacked (acks prune), no pawn starves another, and the
        // order is deterministic for any table.
        struct Cursor {
            std::map<PredictionTick, std::pair<PredictedInput, float>>::const_iterator at;
            std::map<PredictionTick, std::pair<PredictedInput, float>>::const_iterator end;
            PredictionPawn pawn = kDefaultPredictionPawn;
        };
        std::vector<Cursor> cursors;
        for (const auto& [cursorPawn, cursorStream] : m_clientPawns) {
            if (!cursorStream.inputs.empty()) {
                cursors.push_back(
                    Cursor{cursorStream.inputs.begin(), cursorStream.inputs.end(), cursorPawn});
            }
        }
        std::vector<WireInput> window;
        while (window.size() < kPredictionMaximumInputsPerMessage && !cursors.empty()) {
            for (auto cursor = cursors.begin();
                 cursor != cursors.end() && window.size() < kPredictionMaximumInputsPerMessage;) {
                WireInput entry;
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
        if (SendPayload(ToUint8(EncodeInput(window)))) {
            ++m_stats.inputsSent;
        } else {
            ++m_stats.sendsFailed;
        }
    }
    return tick;
}

bool NetworkPrediction::SeedPawn(const PredictionPawn pawn, const PredictedState& state) {
    if (pawn == kDefaultPredictionPawn) {
        return false;
    }
    if (m_role == SessionRole::Client) {
        if (m_clientPawns.contains(pawn) || m_clientPawns.size() >= kPredictionMaximumPawns) {
            return false;
        }
        m_clientPawns.emplace(pawn, ClientPawnStream{});
    } else {
        if (m_serverPawns.contains(pawn) || m_serverPawns.size() >= kPredictionMaximumPawns) {
            return false;
        }
        m_serverPawns.emplace(pawn, ServerPawnStream{});
    }
    m_local[pawn] = state;
    return true;
}

void NetworkPrediction::Update(const std::uint64_t nowMs) {
    Session().Update(nowMs);
    m_lastSeenMs = nowMs;
    for (;;) {
        const std::optional<std::vector<std::uint8_t>> payload = ReceivePayload();
        if (!payload.has_value()) {
            break;
        }
        if (payload->empty()) {
            ++m_stats.framesIgnoredMalformed;
            continue;
        }
        const std::uint8_t kind = payload->front();
        if (m_role == SessionRole::Client) {
            if (kind == kStateKind) {
                HandleStateMessage(*payload);
            } else {
                ++m_stats.framesIgnoredMalformed;
            }
        } else {
            if (kind == kInputKind) {
                HandleInputMessage(*payload);
            } else {
                ++m_stats.framesIgnoredMalformed;
            }
        }
    }
    if (m_role == SessionRole::Server) {
        for (auto& [pawn, stream] : m_serverPawns) {
            if (stream.buffer.empty()) {
                stream.lastApplyMs = nowMs;
                continue;
            }
            const PredictionTick oldest = stream.buffer.begin()->first;
            if (oldest > stream.lastApplied + 1U &&
                nowMs - stream.lastApplyMs >= kPredictionGapTimeoutMs) {
                // The gap is unfillable (the client evicted those ticks an age ago): jump the
                // pawn's stream past it instead of holding every newer input forever. Repeats
                // of the skipped ticks land as duplicates, which is exactly what they now are.
                // Only this pawn jumps - every other stream keeps its own clock.
                stream.lastApplied = oldest - 1U;
                stream.lastApplyMs = nowMs;
                ++m_stats.inputBufferOverruns;
                ApplyBuffered(stream, pawn);
            }
        }
    }
}

void NetworkPrediction::HandleInputMessage(const std::vector<std::uint8_t>& payload) {
    const auto decoded = DecodeInput(payload);
    if (!decoded.has_value()) {
        ++m_stats.framesIgnoredMalformed;
        return;
    }
    for (const auto& entry : *decoded) {
        ++m_stats.inputsReceived;
        ServerPawnStream* const stream = ServerStream(entry.pawn);
        if (stream == nullptr) {
            ++m_stats.inputsRefusedOverPawnCap;
            continue;
        }
        if (entry.tick <= stream->lastApplied || stream->buffer.contains(entry.tick)) {
            ++m_stats.inputsIgnoredDuplicate;
            // A repeated input hints its ack never arrived: answer with the pawn's newest
            // state, whose cumulative ack covers everything the client still holds.
            if (stream->hasLastState) {
                SendState(entry.pawn, stream->lastApplied, stream->lastState);
            }
            continue;
        }
        if (stream->buffer.size() >= kPredictionMaximumServerBuffer) {
            ++m_stats.inputBufferOverruns;
            continue;
        }
        const bool wasEmpty = stream->buffer.empty();
        stream->buffer.emplace(entry.tick, std::make_pair(entry.input, entry.dtSeconds));
        if (wasEmpty) {
            stream->lastApplyMs = m_lastSeenMs;
        }
        ApplyBuffered(*stream, entry.pawn);
    }
}

void NetworkPrediction::HandleStateMessage(const std::vector<std::uint8_t>& payload) {
    const auto decoded = DecodePredictedState(payload);
    if (!decoded.has_value()) {
        ++m_stats.framesIgnoredMalformed;
        return;
    }
    ++m_stats.statesReceived;
    const PredictedStateMessage message = *decoded;
    const auto knownStream = m_clientPawns.find(message.pawn);
    if (knownStream == m_clientPawns.end()) {
        // Well-formed but unsolicited: the server only answers inputs this client sent, so a
        // state for a pawn that never ran here is foreign.
        ++m_stats.framesIgnoredMalformed;
        return;
    }
    ClientPawnStream& stream = knownStream->second;
    if (message.tick <= stream.lastAcked) {
        ++m_stats.statesIgnoredStale;
        return;
    }
    const auto known = stream.states.find(message.tick);
    if (known == stream.states.end()) {
        ++m_stats.stateHistoryMisses;
    } else if (StatesEqual(known->second, message.state, m_config)) {
        PruneThrough(stream, message.tick);
        stream.lastAcked = message.tick;
        return;
    }
    // Disagreed (or the tick aged out of history): snap to the server and replay every
    // surviving newer input with the dt it stepped with, so the present is predicted from
    // authoritative ground.
    ++m_stats.reconciles;
    m_local[message.pawn] = message.state;
    PruneThrough(stream, message.tick);
    for (const auto& [replayTick, replayEntry] : stream.inputs) {
        m_local[message.pawn] = Step(m_local[message.pawn], replayEntry.first, replayEntry.second);
        stream.states[replayTick] = m_local[message.pawn];
    }
    stream.lastAcked = message.tick;
}

void NetworkPrediction::ApplyBuffered(ServerPawnStream& stream,
                                            const PredictionPawn pawn) {
    for (;;) {
        const PredictionTick expected = stream.lastApplied + 1U;
        const auto buffered = stream.buffer.find(expected);
        if (buffered == stream.buffer.end()) {
            return;
        }
        const PredictedInput input = buffered->second.first;
        const float dtSeconds = buffered->second.second;
        stream.buffer.erase(buffered);
        m_local[pawn] = Step(m_local[pawn], input, dtSeconds);
        stream.lastApplied = expected;
        stream.lastApplyMs = m_lastSeenMs;
        ++m_stats.inputsApplied;
        stream.lastState = m_local[pawn];
        stream.hasLastState = true;
        SendState(pawn, expected, m_local[pawn]);
    }
}

void NetworkPrediction::SendState(const PredictionPawn pawn, const PredictionTick tick,
                                        const PredictedState& state) {
    if (Session().State() != SessionState::Established) {
        return;
    }
    if (SendPayload(EncodePredictedState(tick, pawn, state))) {
        ++m_stats.statesSent;
    } else {
        ++m_stats.sendsFailed;
    }
}

void NetworkPrediction::PruneThrough(ClientPawnStream& stream,
                                           const PredictionTick tick) {
    while (!stream.inputs.empty() && stream.inputs.begin()->first <= tick) {
        stream.inputs.erase(stream.inputs.begin());
    }
    while (!stream.states.empty() && stream.states.begin()->first <= tick) {
        stream.states.erase(stream.states.begin());
    }
}

PredictedState NetworkPrediction::LocalState(const PredictionPawn pawn) const {
    const auto known = m_local.find(pawn);
    if (known == m_local.end()) {
        return PredictedState{};
    }
    return known->second;
}

PredictionTick NetworkPrediction::LastAckedTick(const PredictionPawn pawn) const noexcept {
    const auto known = m_clientPawns.find(pawn);
    if (known == m_clientPawns.end()) {
        return kInvalidPredictionTick;
    }
    return known->second.lastAcked;
}

PredictionTick NetworkPrediction::LastAppliedTick(
    const PredictionPawn pawn) const noexcept {
    const auto known = m_serverPawns.find(pawn);
    if (known == m_serverPawns.end()) {
        return kInvalidPredictionTick;
    }
    return known->second.lastApplied;
}

std::size_t PredictionRelay::AddSubscriber(NetworkSession&& session) {
    m_subscribers.push_back(std::move(session));
    return m_subscribers.size() - 1U;
}

bool PredictionRelay::RemoveSubscriber(const std::size_t index) {
    if (index >= m_subscribers.size()) {
        return false;
    }
    m_subscribers.erase(m_subscribers.begin() + static_cast<std::ptrdiff_t>(index));
    return true;
}

void PredictionRelay::Update(const std::uint64_t nowMs) {
    for (auto& subscriber : m_subscribers) {
        subscriber.Update(nowMs);
    }
}

std::size_t PredictionRelay::Publish(const PredictionTick tick,
                                           const PredictionPawn pawn,
                                           const PredictedState& state) {
    const std::vector<std::uint8_t> payload = EncodePredictedState(tick, pawn, state);
    std::size_t sent = 0U;
    for (auto& subscriber : m_subscribers) {
        if (subscriber.State() != SessionState::Established) {
            continue;
        }
        if (subscriber.SendData(payload)) {
            ++m_stats.statesRelayed;
            ++sent;
        } else {
            ++m_stats.sendsFailed;
        }
    }
    return sent;
}

PredictionObserver::PredictionObserver(NetworkSession&& session)
    : m_session(std::move(session)) {}

void PredictionObserver::Update(const std::uint64_t nowMs) {
    m_session.Update(nowMs);
    for (;;) {
        const std::optional<std::vector<std::uint8_t>> payload = m_session.TryReceiveData();
        if (!payload.has_value()) {
            break;
        }
        if (payload->empty()) {
            ++m_stats.framesIgnoredMalformed;
            continue;
        }
        const auto decoded = DecodePredictedState(*payload);
        if (!decoded.has_value()) {
            ++m_stats.framesIgnoredMalformed;
            continue;
        }
        ++m_stats.statesReceived;
        const PredictedStateMessage message = *decoded;
        const auto known = m_latest.find(message.pawn);
        if (known != m_latest.end() && message.tick <= known->second.tick) {
            ++m_stats.statesIgnoredStale;
            continue;
        }
        m_latest[message.pawn] = message;
    }
}

std::optional<PredictedStateMessage> PredictionObserver::Latest(
    const PredictionPawn pawn) const {
    const auto known = m_latest.find(pawn);
    if (known == m_latest.end()) {
        return std::nullopt;
    }
    return known->second;
}

} // namespace UVE::Network
