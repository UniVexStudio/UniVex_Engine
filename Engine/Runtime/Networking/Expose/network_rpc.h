// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
#pragma once
//
// Scope: remote procedure calls over an established session. Both sides call and serve:
// methods register by name with a handler, calls carry a u32 id plus script values, and
// responses complete the matching call - or the call times out. Async-only, always: the
// engine never blocks, so results arrive through callbacks (C++) or response events
// (scripts, via the object host's rpc.* surface), never through a return value.
//
// At-most-once, stated plainly: the network can drop a call or its response, a timeout can
// fire after the far side already executed, and a dying session fails everything in flight.
// Design methods idempotent, and treat every error as "unknown whether it ran".
//
// Arguments and results are script values (int, float, bool, str, vec3, lists, maps,
// tuples) - never object refs, which name host-local objects and cannot cross the wire.
// Completed outcomes stay readable past their callback (bounded retention, oldest evicted)
// for poll-style readers like the script getters. Calls own their session, exactly like
// the replicator owns its: one session per channel today, so RPC and replication ride
// separate sessions. Multiplexing both over one session is a named follow-up, not a
// missing piece of this one.
//
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <variant>
#include <vector>

#include "network_session.h"
#include "network_value_codec.h"
#include "blueprint_value.h"

namespace UVE::Network {

/// Call identity: nonzero, assigned by the caller, matched by the response. Zero is never
/// assigned - it means "this call never went out".
using RpcCallId = std::uint32_t;
inline constexpr RpcCallId kInvalidRpcCallId = 0U;
/// Wire frame version. A foreign version is malformed, full stop - RPC negotiates nothing.
inline constexpr std::uint8_t kRpcFrameVersion = 1U;
/// Default per-call deadline. Zero disables expiry (the call waits until the session dies) -
/// a sharp edge over lossy links, where a dropped response then waits forever.
inline constexpr std::uint64_t kDefaultRpcTimeoutMs = 5000U;
/// Deepest value nesting the codec carries each way. Deprecated alias kept for the packet
/// inspector's independent mirror - new code uses kWireValueMaxDepth.
inline constexpr std::size_t kRpcMaximumValueDepth = kWireValueMaxDepth;
/// Completed outcomes kept readable past their callback. Past this many, the oldest goes -
/// release promptly, and watch the evicted counter if results ever vanish.
inline constexpr std::size_t kRpcMaximumRetainedResults = 64U;

/// How a call ended. Exactly one side is set: a result on ok, a message otherwise.
struct RpcOutcome final {
    bool ok = false;
    Blueprint::Value value;
    std::string error;

    [[nodiscard]] bool operator==(const RpcOutcome&) const noexcept = default;
};

/// A result to hand a caller.
[[nodiscard]] inline RpcOutcome RpcOk(Blueprint::Value value) {
    RpcOutcome outcome;
    outcome.ok = true;
    outcome.value = std::move(value);
    return outcome;
}

/// A failure to hand a caller: unknown method, timeout, dead session, handler refusal.
[[nodiscard]] inline RpcOutcome RpcError(std::string message) {
    RpcOutcome outcome;
    outcome.ok = false;
    outcome.error = std::move(message);
    return outcome;
}

/// Fired exactly once per call that went out: with the response, the timeout, or the
/// session-down failure - unless cancelled first, in which case never.
using RpcResponseCallback = std::function<void(RpcCallId, const RpcOutcome&)>;
/// Serves one method: takes the call's arguments, returns its outcome. Handlers must not
/// throw, but the peer catches everything anyway - a throwing handler answers an error
/// response instead of killing the pump.
using RpcMethodHandler = std::function<RpcOutcome(std::span<const Blueprint::Value>)>;

/// The peer's own ledger: calls and responses each way, completions by cause, and every
/// way a frame or outcome dies (malformed, stale, dropped, handler-refused, evicted).
struct RpcStats final {
    std::uint64_t callsSent = 0U;
    std::uint64_t callsReceived = 0U;
    std::uint64_t responsesSent = 0U;
    std::uint64_t responsesReceived = 0U;
    std::uint64_t responsesMatched = 0U;
    std::uint64_t callsTimedOut = 0U;
    std::uint64_t callsFailedSessionDown = 0U;
    std::uint64_t responsesDropped = 0U;
    std::uint64_t framesIgnoredMalformed = 0U;
    std::uint64_t responsesIgnoredStale = 0U;
    std::uint64_t handlerErrors = 0U;
    // Appended, never inserted: positional initializers elsewhere bind by order.
    std::uint64_t resultsEvicted = 0U;

    [[nodiscard]] bool operator==(const RpcStats&) const noexcept = default;
};

/// At-most-once RPC over one session. Drives the session's pump (Update drives the session
/// first, so handshakes and timeouts flow through the same call), dispatches incoming calls
/// to registered handlers, and completes outgoing ones. Move-only (owning its session, or
/// borrowing a shared one);
/// single-threaded like everything below it. Shared mode borrows a multiplexed session and
/// rides one channel; the app owns the session and both ends agree channels out of band.
class NetworkRpc final {
public:
    /// Owning mode: moves the session in. The sink is an rvalue reference, not by value: a
    /// by-value sink would tie with the shared overload below for lvalue sessions, since the
    /// deleted copy still participates in overload resolution.
    explicit NetworkRpc(NetworkSession&& session) noexcept;

    /// Shares a multiplexed session: calls and responses ride `channel` while other features
    /// ride theirs. The session must be multiplexed (both ends agree out of band) and must
    /// outlive the peer; Update pumps it like an owned one, so any feature's Update
    /// drives the handshake for all.
    explicit NetworkRpc(NetworkSession& sharedSession, NetworkChannelId channel) noexcept;

    /// Serves `method` with `handler`. Returns false (registering nothing) on an empty
    /// method name, an empty handler, or a duplicate - unregister first to rebind.
    [[nodiscard]] bool RegisterMethod(std::string method, RpcMethodHandler handler);
    /// Stops serving `method`. Looked up at dispatch time: a call queued before the
    /// unregister still answers unknown-method. Returns false when never registered.
    [[nodiscard]] bool UnregisterMethod(const std::string& method) noexcept;
    /// Calls the far side's `method` with `args`, completing `callback` exactly once -
    /// unless cancelled. Returns the call id, or zero when the call never went out:
    /// session not established, empty method or callback, or unencodable arguments
    /// (object refs, over-deep nesting, or a frame past the session payload budget).
    [[nodiscard]] RpcCallId Call(const std::string& method,
                                       const std::vector<Blueprint::Value>& args,
                                       RpcResponseCallback callback,
                                       std::uint64_t timeoutMs = kDefaultRpcTimeoutMs);
    /// Gives up on a pending call: the callback never fires, nothing is retained, and a
    /// late response counts as stale. Returns false when the id is unknown or already
    /// completed.
    [[nodiscard]] bool Cancel(RpcCallId callId) noexcept;
    /// Reads back a completed outcome without consuming it: poll-style completion for
    /// readers that cannot take callbacks. Empty when the call is still pending, was
    /// cancelled, was forgotten, was evicted, or never existed.
    [[nodiscard]] std::optional<RpcOutcome> Result(RpcCallId callId) const;
    /// Forgets a retained outcome. Returns false when nothing was retained (still
    /// pending calls must cancel first - forgetting only releases the completed).
    [[nodiscard]] bool ForgetResult(RpcCallId callId);
    /// Pumps the session, dispatches every queued call and matches every queued response,
    /// then fails what remains when the session is down and expires what is overdue. A
    /// response queued behind the drop still completes - salvage before failure, always.
    void Update(std::uint64_t nowMs);

    /// The session, owned or shared: handshake, disconnect, and state live here, not duplicated.
    [[nodiscard]] NetworkSession& Session() noexcept {
        if (std::holds_alternative<NetworkSession>(m_session)) {
            return std::get<NetworkSession>(m_session);
        }
        return *std::get<NetworkSession*>(m_session);
    }
    [[nodiscard]] const NetworkSession& Session() const noexcept {
        if (std::holds_alternative<NetworkSession>(m_session)) {
            return std::get<NetworkSession>(m_session);
        }
        return *std::get<NetworkSession*>(m_session);
    }
    [[nodiscard]] RpcStats Stats() const noexcept { return m_stats; }

private:
    struct PendingCall final {
        RpcResponseCallback callback;
        std::string method;
        std::uint64_t timeoutMs = kDefaultRpcTimeoutMs;
        std::uint64_t deadlineMs = 0U;
        // Deadlines arm on the first Update after the call: the call itself takes no clock.
        bool armed = false;
    };

    void HandleFrame(const std::vector<std::uint8_t>& payload);
    void FailDownCalls();
    void ExpireCalls(std::uint64_t nowMs);
    /// Files a completion for later polling, evicting the oldest past the cap.
    void RetainResult(RpcCallId callId, RpcOutcome outcome);
    [[nodiscard]] RpcCallId NextCallId() noexcept;
    [[nodiscard]] bool SendFrame(const std::vector<std::byte>& frame);
    [[nodiscard]] std::optional<std::vector<std::uint8_t>> ReceivePayload();

    // Owned or borrowed: owning mode moves the session in, shared mode borrows a multiplexed
    // one (which must outlive the feature). Moves stay correct either way - a moved borrow
    // still points at the same shared session.
    std::variant<NetworkSession, NetworkSession*> m_session;
    // The multiplexed channel, meaningful only when shared; owned mode sends untagged.
    NetworkChannelId m_channel = 0U;
    std::map<std::string, RpcMethodHandler> m_methods;
    std::map<RpcCallId, PendingCall> m_pending;
    std::map<RpcCallId, RpcOutcome> m_results;
    std::deque<RpcCallId> m_resultOrder;
    RpcCallId m_nextCallId = 1U;
    RpcStats m_stats;
};

} // namespace UVE::Network
