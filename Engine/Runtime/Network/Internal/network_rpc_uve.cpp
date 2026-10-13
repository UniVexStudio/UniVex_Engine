// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/network/network_rpc_uve.h"
#include "uve/network/network_value_codec_uve.h"

#include <algorithm>
#include <bit>
#include <cstddef>
#include <optional>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#include "uve/utilities/binary_buffer_uve.h"

namespace UVE::Network {
namespace {

// Every RPC frame opens with these two bytes, so an RPC peer never mistakes foreign
// session data for a call - and foreign consumers never mistake ours for theirs.
inline constexpr std::byte kRpcMagic0UVE = static_cast<std::byte>('R');
inline constexpr std::byte kRpcMagic1UVE = static_cast<std::byte>('C');

enum class RpcFrameKindUVE : std::uint8_t { Call = 0x01, Response = 0x02 };

struct DecodedFrameUVE final {
    RpcFrameKindUVE kind = RpcFrameKindUVE::Call;
    RpcCallIdUVE callId = kInvalidRpcCallIdUVE;
    std::string method;
    std::vector<UVScript::ValueUVE> args;
    bool ok = false;
    UVScript::ValueUVE value;
    std::string error;
};

// Strict framing, like the snapshots: magic, version, kind, then exactly the declared body -
// trailing bytes fail the frame, and foreign mail fails it before anything decodes.
[[nodiscard]] std::optional<DecodedFrameUVE> DecodeFrameUVE(
    const std::vector<std::uint8_t>& payload) {
    std::vector<std::byte> in;
    in.reserve(payload.size());
    for (const std::uint8_t byte : payload) {
        in.push_back(static_cast<std::byte>(byte));
    }
    std::size_t offset = 0U;
    std::uint8_t version = 0U, kind = 0U;
    RpcCallIdUVE callId = kInvalidRpcCallIdUVE;
    if (in.size() < 8U || in[0] != kRpcMagic0UVE || in[1] != kRpcMagic1UVE) {
        return std::nullopt;
    }
    offset = 2U;
    version = std::to_integer<std::uint8_t>(in[offset++]);
    kind = std::to_integer<std::uint8_t>(in[offset++]);
    if (version != kRpcFrameVersionUVE ||
        (kind != static_cast<std::uint8_t>(RpcFrameKindUVE::Call) &&
         kind != static_cast<std::uint8_t>(RpcFrameKindUVE::Response)) ||
        !Utilities::ReadUint32LeFromBufferUVE(in, offset, callId)) {
        return std::nullopt;
    }
    DecodedFrameUVE frame;
    frame.kind = static_cast<RpcFrameKindUVE>(kind);
    frame.callId = callId;
    if (frame.kind == RpcFrameKindUVE::Call) {
        std::optional<std::string> method = ReadWireStringUVE(in, offset);
        std::uint16_t argCount = 0U;
        if (!method.has_value() || !Utilities::ReadUint16LeFromBufferUVE(in, offset, argCount)) {
            return std::nullopt;
        }
        frame.method = std::move(*method);
        for (std::uint16_t i = 0U; i < argCount; ++i) {
            std::optional<UVScript::ValueUVE> arg = ReadWireValueUVE(in, offset, 0U);
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
        std::optional<UVScript::ValueUVE> value = ReadWireValueUVE(in, offset, 0U);
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
[[nodiscard]] std::optional<std::vector<std::byte>> EncodeCallFrameUVE(
    const RpcCallIdUVE callId, const std::string& method,
    const std::vector<UVScript::ValueUVE>& args) {
    if (method.size() > 0xFFFFU || args.size() > 0xFFFFU) {
        return std::nullopt;
    }
    std::vector<std::byte> frame;
    frame.push_back(kRpcMagic0UVE);
    frame.push_back(kRpcMagic1UVE);
    frame.push_back(static_cast<std::byte>(kRpcFrameVersionUVE));
    frame.push_back(static_cast<std::byte>(static_cast<std::uint8_t>(RpcFrameKindUVE::Call)));
    Utilities::AppendUint32LeUVE(frame, callId);
    Utilities::AppendUint16LeUVE(frame, static_cast<std::uint16_t>(method.size()));
    if (!method.empty()) {
        Utilities::AppendBytesUVE(frame, method.data(), method.size());
    }
    Utilities::AppendUint16LeUVE(frame, static_cast<std::uint16_t>(args.size()));
    for (const UVScript::ValueUVE& arg : args) {
        if (!AppendWireValueUVE(frame, arg, 0U)) {
            return std::nullopt;
        }
    }
    if (frame.size() > kSessionMaximumPayloadBytesUVE) {
        return std::nullopt;
    }
    return frame;
}

// Frames one response: the same head, then a status byte plus the result - or the error
// text as a string value, so errors need no second value codec.
[[nodiscard]] std::optional<std::vector<std::byte>> EncodeResponseFrameUVE(
    const RpcCallIdUVE callId, const bool ok, const UVScript::ValueUVE& value,
    const std::string& error) {
    std::vector<std::byte> frame;
    frame.push_back(kRpcMagic0UVE);
    frame.push_back(kRpcMagic1UVE);
    frame.push_back(static_cast<std::byte>(kRpcFrameVersionUVE));
    frame.push_back(static_cast<std::byte>(static_cast<std::uint8_t>(RpcFrameKindUVE::Response)));
    Utilities::AppendUint32LeUVE(frame, callId);
    frame.push_back(ok ? static_cast<std::byte>(0x00) : static_cast<std::byte>(0x01));
    const UVScript::ValueUVE& body = ok ? value : UVScript::ValueUVE{error};
    if (!AppendWireValueUVE(frame, body, 0U)) {
        return std::nullopt;
    }
    if (frame.size() > kSessionMaximumPayloadBytesUVE) {
        return std::nullopt;
    }
    return frame;
}

} // namespace

NetworkRpcUVE::NetworkRpcUVE(NetworkSessionUVE&& session) noexcept
    : m_session(std::move(session)) {}

NetworkRpcUVE::NetworkRpcUVE(NetworkSessionUVE& sharedSession,
                              const NetworkChannelIdUVE channel) noexcept
    : m_session(&sharedSession), m_channel(channel) {}

bool NetworkRpcUVE::SendFrameUVE(const std::vector<std::byte>& frame) {
    std::vector<std::uint8_t> datagram;
    datagram.reserve(frame.size());
    for (const std::byte byte : frame) {
        datagram.push_back(std::to_integer<std::uint8_t>(byte));
    }
    if (std::holds_alternative<NetworkSessionUVE*>(m_session)) {
        return SessionUVE().SendChannelDataUVE(m_channel, datagram);
    }
    return SessionUVE().SendDataUVE(datagram);
}

std::optional<std::vector<std::uint8_t>> NetworkRpcUVE::ReceivePayloadUVE() {
    if (std::holds_alternative<NetworkSessionUVE*>(m_session)) {
        return SessionUVE().TryReceiveChannelDataUVE(m_channel);
    }
    return SessionUVE().TryReceiveDataUVE();
}

bool NetworkRpcUVE::RegisterMethodUVE(std::string method, RpcMethodHandlerUVE handler) {
    if (method.empty() || handler == nullptr || m_methods.contains(method)) {
        return false;
    }
    m_methods.emplace(std::move(method), std::move(handler));
    return true;
}

bool NetworkRpcUVE::UnregisterMethodUVE(const std::string& method) noexcept {
    return m_methods.erase(method) > 0U;
}

RpcCallIdUVE NetworkRpcUVE::CallUVE(const std::string& method,
                                    const std::vector<UVScript::ValueUVE>& args,
                                    RpcResponseCallbackUVE callback, const std::uint64_t timeoutMs) {
    if (SessionUVE().StateUVE() != SessionStateUVE::Established || method.empty() ||
        callback == nullptr) {
        return kInvalidRpcCallIdUVE;
    }
    const RpcCallIdUVE callId = NextCallIdUVE();
    const std::optional<std::vector<std::byte>> frame = EncodeCallFrameUVE(callId, method, args);
    if (!frame.has_value() || !SendFrameUVE(*frame)) {
        return kInvalidRpcCallIdUVE;
    }
    PendingCallUVE pending;
    pending.callback = std::move(callback);
    pending.method = method;
    pending.timeoutMs = timeoutMs;
    m_pending.emplace(callId, std::move(pending));
    ++m_stats.callsSent;
    return callId;
}

bool NetworkRpcUVE::CancelUVE(const RpcCallIdUVE callId) noexcept {
    return m_pending.erase(callId) > 0U;
}

std::optional<RpcOutcomeUVE> NetworkRpcUVE::ResultUVE(const RpcCallIdUVE callId) const {
    const auto retained = m_results.find(callId);
    if (retained == m_results.end()) {
        return std::nullopt;
    }
    return retained->second;
}

bool NetworkRpcUVE::ForgetResultUVE(const RpcCallIdUVE callId) {
    const auto retained = m_results.find(callId);
    if (retained == m_results.end()) {
        return false;
    }
    m_results.erase(retained);
    m_resultOrder.erase(std::remove(m_resultOrder.begin(), m_resultOrder.end(), callId),
                        m_resultOrder.end());
    return true;
}

void NetworkRpcUVE::UpdateUVE(const std::uint64_t nowMs) {
    SessionUVE().UpdateUVE(nowMs);
    for (;;) {
        const std::optional<std::vector<std::uint8_t>> datagram = ReceivePayloadUVE();
        if (!datagram.has_value()) {
            break;
        }
        HandleFrameUVE(*datagram);
    }
    if (SessionUVE().StateUVE() != SessionStateUVE::Established) {
        FailDownCallsUVE();
    }
    ExpireCallsUVE(nowMs);
}

void NetworkRpcUVE::HandleFrameUVE(const std::vector<std::uint8_t>& payload) {
    std::optional<DecodedFrameUVE> frame = DecodeFrameUVE(payload);
    if (!frame.has_value()) {
        ++m_stats.framesIgnoredMalformed;
        return;
    }
    if (frame->kind == RpcFrameKindUVE::Call) {
        ++m_stats.callsReceived;
        const auto method = m_methods.find(frame->method);
        RpcOutcomeUVE outcome;
        if (method == m_methods.end()) {
            outcome = RpcErrorUVE("unknown method '" + frame->method + "'");
        } else {
            try {
                outcome = method->second(frame->args);
            } catch (...) {
                outcome = RpcErrorUVE("handler for '" + frame->method + "' threw");
            }
            if (!outcome.ok) {
                ++m_stats.handlerErrors;
            }
        }
        std::optional<std::vector<std::byte>> wire;
        if (outcome.ok) {
            wire = EncodeResponseFrameUVE(frame->callId, true, outcome.value, {});
            if (!wire.has_value()) {
                ++m_stats.handlerErrors;
                wire = EncodeResponseFrameUVE(frame->callId, false, UVScript::ValueUVE{},
                                              "result of '" + frame->method + "' could not be sent");
            }
        } else {
            wire = EncodeResponseFrameUVE(frame->callId, false, UVScript::ValueUVE{}, outcome.error);
        }
        if (!wire.has_value() || !SendFrameUVE(*wire)) {
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
    RpcOutcomeUVE outcome = frame->ok ? RpcOkUVE(std::move(frame->value))
                                      : RpcErrorUVE(std::move(frame->error));
    RpcResponseCallbackUVE callback = std::move(pending->second.callback);
    m_pending.erase(pending);
    ++m_stats.responsesMatched;
    // Retained before the callback runs, so a callback that polls sees its own outcome.
    RetainResultUVE(frame->callId, outcome);
    callback(frame->callId, outcome);
}

void NetworkRpcUVE::FailDownCallsUVE() {
    if (m_pending.empty()) {
        return;
    }
    std::map<RpcCallIdUVE, PendingCallUVE> failed;
    failed.swap(m_pending);
    for (auto& [callId, pending] : failed) {
        ++m_stats.callsFailedSessionDown;
        RpcOutcomeUVE outcome{false, UVScript::ValueUVE{},
                              "session down during call to '" + pending.method + "'"};
        RetainResultUVE(callId, outcome);
        pending.callback(callId, outcome);
    }
}

void NetworkRpcUVE::ExpireCallsUVE(const std::uint64_t nowMs) {
    for (auto it = m_pending.begin(); it != m_pending.end();) {
        PendingCallUVE& pending = it->second;
        // Deadlines count from the first Update after the call - the call itself takes no
        // clock, so this arming is what binds a timeout to time.
        if (!pending.armed) {
            pending.armed = true;
            pending.deadlineMs = nowMs + pending.timeoutMs;
        }
        if (pending.timeoutMs != 0U && nowMs >= pending.deadlineMs) {
            const RpcCallIdUVE callId = it->first;
            RpcResponseCallbackUVE callback = std::move(pending.callback);
            std::string method = std::move(pending.method);
            it = m_pending.erase(it);
            ++m_stats.callsTimedOut;
            RpcOutcomeUVE outcome{false, UVScript::ValueUVE{}, "call to '" + method + "' timed out"};
            RetainResultUVE(callId, outcome);
            callback(callId, outcome);
        } else {
            ++it;
        }
    }
}

void NetworkRpcUVE::RetainResultUVE(const RpcCallIdUVE callId, RpcOutcomeUVE outcome) {
    if (!m_results.contains(callId)) {
        m_resultOrder.push_back(callId);
    }
    m_results.insert_or_assign(callId, std::move(outcome));
    while (m_results.size() > kRpcMaximumRetainedResultsUVE) {
        const RpcCallIdUVE oldest = m_resultOrder.front();
        m_resultOrder.pop_front();
        m_results.erase(oldest);
        ++m_stats.resultsEvicted;
    }
}

RpcCallIdUVE NetworkRpcUVE::NextCallIdUVE() noexcept {
    for (;;) {
        RpcCallIdUVE callId = m_nextCallId++;
        if (callId == kInvalidRpcCallIdUVE) {
            callId = m_nextCallId++;
        }
        if (!m_pending.contains(callId)) {
            return callId;
        }
    }
}

} // namespace UVE::Network
