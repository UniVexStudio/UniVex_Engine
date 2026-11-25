// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "network_rpc.h"
#include "network_value_codec.h"

#include <algorithm>
#include <bit>
#include <cstddef>
#include <optional>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#include "binary_buffer.h"

namespace UVE::Network {
namespace {

// Every RPC frame opens with these two bytes, so an RPC peer never mistakes foreign
// session data for a call - and foreign consumers never mistake ours for theirs.
inline constexpr std::byte kRpcMagic0 = static_cast<std::byte>('R');
inline constexpr std::byte kRpcMagic1 = static_cast<std::byte>('C');

enum class RpcFrameKind : std::uint8_t { Call = 0x01, Response = 0x02 };

struct DecodedFrame final {
    RpcFrameKind kind = RpcFrameKind::Call;
    RpcCallId callId = kInvalidRpcCallId;
    std::string method;
    std::vector<Blueprint::Value> args;
    bool ok = false;
    Blueprint::Value value;
    std::string error;
};

// Strict framing, like the snapshots: magic, version, kind, then exactly the declared body -
// trailing bytes fail the frame, and foreign mail fails it before anything decodes.
[[nodiscard]] std::optional<DecodedFrame> DecodeFrame(
    const std::vector<std::uint8_t>& payload) {
    std::vector<std::byte> in;
    in.reserve(payload.size());
    for (const std::uint8_t byte : payload) {
        in.push_back(static_cast<std::byte>(byte));
    }
    std::size_t offset = 0U;
    std::uint8_t version = 0U, kind = 0U;
    RpcCallId callId = kInvalidRpcCallId;
    if (in.size() < 8U || in[0] != kRpcMagic0 || in[1] != kRpcMagic1) {
        return std::nullopt;
    }
    offset = 2U;
    version = std::to_integer<std::uint8_t>(in[offset++]);
    kind = std::to_integer<std::uint8_t>(in[offset++]);
    if (version != kRpcFrameVersion ||
        (kind != static_cast<std::uint8_t>(RpcFrameKind::Call) &&
         kind != static_cast<std::uint8_t>(RpcFrameKind::Response)) ||
        !Utilities::ReadUint32LeFromBuffer(in, offset, callId)) {
        return std::nullopt;
    }
    DecodedFrame frame;
    frame.kind = static_cast<RpcFrameKind>(kind);
    frame.callId = callId;
    if (frame.kind == RpcFrameKind::Call) {
        std::optional<std::string> method = ReadWireString(in, offset);
        std::uint16_t argCount = 0U;
        if (!method.has_value() || !Utilities::ReadUint16LeFromBuffer(in, offset, argCount)) {
            return std::nullopt;
        }
        frame.method = std::move(*method);
        for (std::uint16_t i = 0U; i < argCount; ++i) {
            std::optional<Blueprint::Value> arg = ReadWireValue(in, offset, 0U);
            if (!arg.has_value()) {
                return std::nullopt;
            }
            frame.args.push_back(std::move(*arg));
        }
    } else {
        if (offset >= in.size()) {
            return std::nullopt;
        }
        const std::uint8_t status = std::to_integer<std::uint8_t>(in[offset++]);
        if (status != 0x00U && status != 0x01U) {
            return std::nullopt;
        }
        std::optional<Blueprint::Value> value = ReadWireValue(in, offset, 0U);
        if (!value.has_value()) {
            return std::nullopt;
        }
        frame.ok = status == 0x00U;
        if (frame.ok) {
            frame.value = std::move(*value);
        } else if (std::holds_alternative<std::string>(*value)) {
            frame.error = std::move(std::get<std::string>(*value));
        } else {
            return std::nullopt;
        }
    }
    if (offset != in.size()) {
        return std::nullopt;
    }
    return frame;
}

// Frames one call: magic, version, kind, id, method, then the arguments. Empty when the
// call cannot cross - unencodable arguments, or a frame past the session payload budget.
[[nodiscard]] std::optional<std::vector<std::byte>> EncodeCallFrame(
    const RpcCallId callId, const std::string& method,
    const std::vector<Blueprint::Value>& args) {
    if (method.size() > 0xFFFFU || args.size() > 0xFFFFU) {
        return std::nullopt;
    }
    std::vector<std::byte> frame;
    frame.push_back(kRpcMagic0);
    frame.push_back(kRpcMagic1);
    frame.push_back(static_cast<std::byte>(kRpcFrameVersion));
    frame.push_back(static_cast<std::byte>(static_cast<std::uint8_t>(RpcFrameKind::Call)));
    Utilities::AppendUint32Le(frame, callId);
    Utilities::AppendUint16Le(frame, static_cast<std::uint16_t>(method.size()));
    if (!method.empty()) {
        Utilities::AppendBytes(frame, method.data(), method.size());
    }
    Utilities::AppendUint16Le(frame, static_cast<std::uint16_t>(args.size()));
    for (const Blueprint::Value& arg : args) {
        if (!AppendWireValue(frame, arg, 0U)) {
            return std::nullopt;
        }
    }
    if (frame.size() > kSessionMaximumPayloadBytes) {
        return std::nullopt;
    }
    return frame;
}

// Frames one response: the same head, then a status byte plus the result - or the error
// text as a string value, so errors need no second value codec.
[[nodiscard]] std::optional<std::vector<std::byte>> EncodeResponseFrame(
    const RpcCallId callId, const bool ok, const Blueprint::Value& value,
    const std::string& error) {
    std::vector<std::byte> frame;
    frame.push_back(kRpcMagic0);
    frame.push_back(kRpcMagic1);
    frame.push_back(static_cast<std::byte>(kRpcFrameVersion));
    frame.push_back(static_cast<std::byte>(static_cast<std::uint8_t>(RpcFrameKind::Response)));
    Utilities::AppendUint32Le(frame, callId);
    frame.push_back(ok ? static_cast<std::byte>(0x00) : static_cast<std::byte>(0x01));
    const Blueprint::Value& body = ok ? value : Blueprint::Value{error};
    if (!AppendWireValue(frame, body, 0U)) {
        return std::nullopt;
    }
    if (frame.size() > kSessionMaximumPayloadBytes) {
        return std::nullopt;
    }
    return frame;
}

} // namespace

NetworkRpc::NetworkRpc(NetworkSession&& session) noexcept
    : m_session(std::move(session)) {}

NetworkRpc::NetworkRpc(NetworkSession& sharedSession,
                              const NetworkChannelId channel) noexcept
    : m_session(&sharedSession), m_channel(channel) {}

bool NetworkRpc::SendFrame(const std::vector<std::byte>& frame) {
    std::vector<std::uint8_t> datagram;
    datagram.reserve(frame.size());
    for (const std::byte byte : frame) {
        datagram.push_back(std::to_integer<std::uint8_t>(byte));
    }
    if (std::holds_alternative<NetworkSession*>(m_session)) {
        return Session().SendChannelData(m_channel, datagram);
    }
    return Session().SendData(datagram);
}

std::optional<std::vector<std::uint8_t>> NetworkRpc::ReceivePayload() {
    if (std::holds_alternative<NetworkSession*>(m_session)) {
        return Session().TryReceiveChannelData(m_channel);
    }
    return Session().TryReceiveData();
}

bool NetworkRpc::RegisterMethod(std::string method, RpcMethodHandler handler) {
    if (method.empty() || handler == nullptr || m_methods.contains(method)) {
        return false;
    }
    m_methods.emplace(std::move(method), std::move(handler));
    return true;
}

bool NetworkRpc::UnregisterMethod(const std::string& method) noexcept {
    return m_methods.erase(method) > 0U;
}

RpcCallId NetworkRpc::Call(const std::string& method,
                                    const std::vector<Blueprint::Value>& args,
                                    RpcResponseCallback callback, const std::uint64_t timeoutMs) {
    if (Session().State() != SessionState::Established || method.empty() ||
        callback == nullptr) {
        return kInvalidRpcCallId;
    }
    const RpcCallId callId = NextCallId();
    const std::optional<std::vector<std::byte>> frame = EncodeCallFrame(callId, method, args);
    if (!frame.has_value() || !SendFrame(*frame)) {
        return kInvalidRpcCallId;
    }
    PendingCall pending;
    pending.callback = std::move(callback);
    pending.method = method;
    pending.timeoutMs = timeoutMs;
    m_pending.emplace(callId, std::move(pending));
    ++m_stats.callsSent;
    return callId;
}

bool NetworkRpc::Cancel(const RpcCallId callId) noexcept {
    return m_pending.erase(callId) > 0U;
}

std::optional<RpcOutcome> NetworkRpc::Result(const RpcCallId callId) const {
    const auto retained = m_results.find(callId);
    if (retained == m_results.end()) {
        return std::nullopt;
    }
    return retained->second;
}

bool NetworkRpc::ForgetResult(const RpcCallId callId) {
    const auto retained = m_results.find(callId);
    if (retained == m_results.end()) {
        return false;
    }
    m_results.erase(retained);
    m_resultOrder.erase(std::remove(m_resultOrder.begin(), m_resultOrder.end(), callId),
                        m_resultOrder.end());
    return true;
}

void NetworkRpc::Update(const std::uint64_t nowMs) {
    Session().Update(nowMs);
    for (;;) {
        const std::optional<std::vector<std::uint8_t>> datagram = ReceivePayload();
        if (!datagram.has_value()) {
            break;
        }
        HandleFrame(*datagram);
    }
    if (Session().State() != SessionState::Established) {
        FailDownCalls();
    }
    ExpireCalls(nowMs);
}

void NetworkRpc::HandleFrame(const std::vector<std::uint8_t>& payload) {
    std::optional<DecodedFrame> frame = DecodeFrame(payload);
    if (!frame.has_value()) {
        ++m_stats.framesIgnoredMalformed;
        return;
    }
    if (frame->kind == RpcFrameKind::Call) {
        ++m_stats.callsReceived;
        const auto method = m_methods.find(frame->method);
        RpcOutcome outcome;
        if (method == m_methods.end()) {
            outcome = RpcError("unknown method '" + frame->method + "'");
        } else {
            try {
                outcome = method->second(frame->args);
            } catch (...) {
                outcome = RpcError("handler for '" + frame->method + "' threw");
            }
            if (!outcome.ok) {
                ++m_stats.handlerErrors;
            }
        }
        std::optional<std::vector<std::byte>> wire;
        if (outcome.ok) {
            wire = EncodeResponseFrame(frame->callId, true, outcome.value, {});
            if (!wire.has_value()) {
                ++m_stats.handlerErrors;
                wire = EncodeResponseFrame(frame->callId, false, Blueprint::Value{},
                                              "result of '" + frame->method + "' could not be sent");
            }
        } else {
            wire = EncodeResponseFrame(frame->callId, false, Blueprint::Value{}, outcome.error);
        }
        if (!wire.has_value() || !SendFrame(*wire)) {
            ++m_stats.responsesDropped;
            return;
        }
        ++m_stats.responsesSent;
        return;
    }
    ++m_stats.responsesReceived;
    const auto pending = m_pending.find(frame->callId);
    if (pending == m_pending.end()) {
        ++m_stats.responsesIgnoredStale;
        return;
    }
    RpcOutcome outcome = frame->ok ? RpcOk(std::move(frame->value))
                                      : RpcError(std::move(frame->error));
    RpcResponseCallback callback = std::move(pending->second.callback);
    m_pending.erase(pending);
    ++m_stats.responsesMatched;
    // Retained before the callback runs, so a callback that polls sees its own outcome.
    RetainResult(frame->callId, outcome);
    callback(frame->callId, outcome);
}

void NetworkRpc::FailDownCalls() {
    if (m_pending.empty()) {
        return;
    }
    std::map<RpcCallId, PendingCall> failed;
    failed.swap(m_pending);
    for (auto& [callId, pending] : failed) {
        ++m_stats.callsFailedSessionDown;
        RpcOutcome outcome{false, Blueprint::Value{},
                              "session down during call to '" + pending.method + "'"};
        RetainResult(callId, outcome);
        pending.callback(callId, outcome);
    }
}

void NetworkRpc::ExpireCalls(const std::uint64_t nowMs) {
    for (auto it = m_pending.begin(); it != m_pending.end();) {
        PendingCall& pending = it->second;
        // Deadlines count from the first Update after the call - the call itself takes no
        // clock, so this arming is what binds a timeout to time.
        if (!pending.armed) {
            pending.armed = true;
            pending.deadlineMs = nowMs + pending.timeoutMs;
        }
        if (pending.timeoutMs != 0U && nowMs >= pending.deadlineMs) {
            const RpcCallId callId = it->first;
            RpcResponseCallback callback = std::move(pending.callback);
            std::string method = std::move(pending.method);
            it = m_pending.erase(it);
            ++m_stats.callsTimedOut;
            RpcOutcome outcome{false, Blueprint::Value{}, "call to '" + method + "' timed out"};
            RetainResult(callId, outcome);
            callback(callId, outcome);
        } else {
            ++it;
        }
    }
}

void NetworkRpc::RetainResult(const RpcCallId callId, RpcOutcome outcome) {
    if (!m_results.contains(callId)) {
        m_resultOrder.push_back(callId);
    }
    m_results.insert_or_assign(callId, std::move(outcome));
    while (m_results.size() > kRpcMaximumRetainedResults) {
        const RpcCallId oldest = m_resultOrder.front();
        m_resultOrder.pop_front();
        m_results.erase(oldest);
        ++m_stats.resultsEvicted;
    }
}

RpcCallId NetworkRpc::NextCallId() noexcept {
    for (;;) {
        RpcCallId callId = m_nextCallId++;
        if (callId == kInvalidRpcCallId) {
            callId = m_nextCallId++;
        }
        if (!m_pending.contains(callId)) {
            return callId;
        }
    }
}

} // namespace UVE::Network
