// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "network_rpc.h"

#include <bit>
#include <cstdint>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <variant>
#include <vector>

#include <gtest/gtest.h>

namespace UVE::Network::Tests {
namespace {

[[nodiscard]] std::vector<std::uint8_t> Bytes(const std::string& text) {
    return std::vector<std::uint8_t>(text.begin(), text.end());
}

[[nodiscard]] std::vector<std::uint8_t> Tagged(const std::uint8_t tag, const std::string& text) {
    std::vector<std::uint8_t> datagram{tag};
    for (const std::uint8_t byte : Bytes(text)) {
        datagram.push_back(byte);
    }
    return datagram;
}

// Structural equality for values: plain == compares collections by shared-pointer identity,
// so a decoded-then-rebuilt twin never matches. This walks both and compares by shape.
[[nodiscard]] bool ValuesEqual(const Blueprint::Value& a, const Blueprint::Value& b) {
    if (a.index() != b.index()) {
        return false;
    }
    const auto* collectionA =
        std::get_if<std::shared_ptr<const Blueprint::CollectionValue>>(&a);
    if (collectionA == nullptr) {
        return a == b;
    }
    const auto& collectionB = std::get<std::shared_ptr<const Blueprint::CollectionValue>>(b);
    if ((*collectionA == nullptr) != (collectionB == nullptr)) {
        return false;
    }
    if (*collectionA == nullptr) {
        return true;
    }
    if ((*collectionA)->kind != collectionB->kind ||
        (*collectionA)->items.size() != collectionB->items.size()) {
        return false;
    }
    for (std::size_t i = 0U; i < (*collectionA)->items.size(); ++i) {
        if (!ValuesEqual((*collectionA)->items[i], collectionB->items[i])) {
            return false;
        }
    }
    return true;
}

struct RpcHarness final {
    LoopbackTransportPair link;
    NetworkRpc a;
    NetworkRpc b;
    // Completed outcomes by call id, per side - the callbacks file here.
    std::map<RpcCallId, RpcOutcome> doneA;
    std::map<RpcCallId, RpcOutcome> doneB;

    void UpdateBoth(const std::uint64_t nowMs) {
        a.Update(nowMs);
        b.Update(nowMs);
    }

    [[nodiscard]] bool Establish(const std::uint64_t nowMs) {
        if (!a.Session().Connect(nowMs)) {
            return false;
        }
        UpdateBoth(nowMs);
        UpdateBoth(nowMs);
        return a.Session().State() == SessionState::Established &&
               b.Session().State() == SessionState::Established;
    }

    [[nodiscard]] RpcResponseCallback FileA() {
        return [this](const RpcCallId callId, const RpcOutcome& outcome) {
            doneA.emplace(callId, outcome);
        };
    }

    [[nodiscard]] RpcResponseCallback FileB() {
        return [this](const RpcCallId callId, const RpcOutcome& outcome) {
            doneB.emplace(callId, outcome);
        };
    }
};

[[nodiscard]] RpcHarness MakeRpc(
    const std::uint64_t timeoutMs = kDefaultSessionTimeoutMs) {
    LoopbackTransportPair pair = CreateLoopbackTransportPair(256U);
    NetworkSession sessionA(SessionRole::Client,
                               std::make_unique<LoopbackEndpoint>(pair.a), timeoutMs);
    NetworkSession sessionB(SessionRole::Server,
                               std::make_unique<LoopbackEndpoint>(pair.b), timeoutMs);
    NetworkRpc a(std::move(sessionA));
    NetworkRpc b(std::move(sessionB));
    return RpcHarness{std::move(pair), std::move(a), std::move(b), {}, {}};
}

[[nodiscard]] RpcMethodHandler EchoHandler() {
    return [](const std::span<const Blueprint::Value> args) { return RpcOk(args[0]); };
}

TEST(NetworkRpcTest, RegisterRules_DuplicatesAndEmptiesRejected) {
    RpcHarness harness = MakeRpc();
    const RpcMethodHandler handler = [](const std::span<const Blueprint::Value>) {
        return RpcOk(Blueprint::Value{});
    };

    EXPECT_TRUE(harness.b.RegisterMethod("add", handler));
    EXPECT_FALSE(harness.b.RegisterMethod("add", handler));
    EXPECT_FALSE(harness.b.RegisterMethod("", handler));
    EXPECT_FALSE(harness.b.RegisterMethod("sub", RpcMethodHandler{}));
    EXPECT_FALSE(harness.b.UnregisterMethod("nope"));
    EXPECT_TRUE(harness.b.UnregisterMethod("add"));
    EXPECT_TRUE(harness.b.RegisterMethod("add", handler));
}

TEST(NetworkRpcTest, Call_RoundTripAnswersResult) {
    RpcHarness harness = MakeRpc();
    ASSERT_TRUE(harness.b.RegisterMethod(
        "add", [](const std::span<const Blueprint::Value> args) {
            const std::int64_t x = std::get<std::int64_t>(args[0]);
            const std::int64_t y = std::get<std::int64_t>(args[1]);
            return RpcOk(Blueprint::Value{x + y});
        }));
    // A down session sends nothing: zero means the call never went out.
    EXPECT_EQ(harness.a.Call("add", {}, harness.FileA()), kInvalidRpcCallId);

    ASSERT_TRUE(harness.Establish(1000U));
    EXPECT_EQ(harness.a.Call("", {}, harness.FileA()), kInvalidRpcCallId);
    EXPECT_EQ(harness.a.Call("add", {}, RpcResponseCallback{}), kInvalidRpcCallId);

    const RpcCallId callId =
        harness.a.Call("add",
                          {Blueprint::Value{std::int64_t{1}}, Blueprint::Value{std::int64_t{2}}},
                          harness.FileA());
    ASSERT_NE(callId, kInvalidRpcCallId);
    // One pump carries the call over and the response back; the second delivers it.
    harness.UpdateBoth(1000U);
    harness.UpdateBoth(1000U);

    ASSERT_EQ(harness.doneA.size(), 1U);
    EXPECT_TRUE(harness.doneA[callId].ok);
    EXPECT_EQ(harness.doneA[callId].value, Blueprint::Value{std::int64_t{3}});
    EXPECT_EQ(harness.a.Stats().responsesMatched, 1U);
    EXPECT_EQ(harness.b.Stats().callsReceived, 1U);
    EXPECT_EQ(harness.b.Stats().responsesSent, 1U);
}

TEST(NetworkRpcTest, Call_AllValueKindsRoundTrip) {
    RpcHarness harness = MakeRpc();
    ASSERT_TRUE(harness.b.RegisterMethod("echo", EchoHandler()));
    ASSERT_TRUE(harness.Establish(1000U));

    const std::vector<Blueprint::Value> values{
        Blueprint::Value{},
        Blueprint::Value{true},
        Blueprint::Value{false},
        Blueprint::Value{std::int64_t{INT64_MIN}},
        Blueprint::Value{std::int64_t{INT64_MAX}},
        Blueprint::Value{2.5},
        Blueprint::Value{std::string{}},
        Blueprint::Value{std::string{"hello"}},
        // Embedded NUL: the codec is length-based, never C-string-based.
        Blueprint::Value{std::string{"a\0b", 3U}},
        Blueprint::Value{Blueprint::Vec3Value{1.5, -2.5, 3.25}},
        Blueprint::MakeListValue({Blueprint::Value{std::int64_t{1}},
                                    Blueprint::MakeListValue({Blueprint::Value{std::int64_t{2}},
                                                                Blueprint::Value{std::string{"three"}}})}),
        Blueprint::MakeMapValue({Blueprint::Value{std::string{"a"}},
                                   Blueprint::Value{std::int64_t{1}},
                                   Blueprint::Value{std::string{"b"}},
                                   Blueprint::MakeListValue({Blueprint::Value{true},
                                                               Blueprint::Value{1.5}})}),
        Blueprint::MakeTupleValue({Blueprint::Value{std::int64_t{1}},
                                     Blueprint::Value{std::string{"two"}}}),
    };
    std::vector<RpcCallId> callIds;
    for (const Blueprint::Value& value : values) {
        const RpcCallId callId = harness.a.Call("echo", {value}, harness.FileA());
        ASSERT_NE(callId, kInvalidRpcCallId);
        callIds.push_back(callId);
    }
    // NaN rides separately: bit-exact through the codec, but never equal to itself.
    const RpcCallId nanId =
        harness.a.Call("echo", {Blueprint::Value{std::numeric_limits<double>::quiet_NaN()}},
                          harness.FileA());
    ASSERT_NE(nanId, kInvalidRpcCallId);

    harness.UpdateBoth(1000U);
    harness.UpdateBoth(1000U);

    ASSERT_EQ(harness.doneA.size(), values.size() + 1U);
    for (std::size_t i = 0U; i < values.size(); ++i) {
        EXPECT_TRUE(harness.doneA[callIds[i]].ok);
        EXPECT_TRUE(ValuesEqual(harness.doneA[callIds[i]].value, values[i]));
    }
    ASSERT_TRUE(harness.doneA[nanId].ok);
    const Blueprint::Value& nanBack = harness.doneA[nanId].value;
    ASSERT_TRUE(std::holds_alternative<double>(nanBack));
    EXPECT_EQ(std::bit_cast<std::uint64_t>(std::get<double>(nanBack)),
              std::bit_cast<std::uint64_t>(std::numeric_limits<double>::quiet_NaN()));
}

TEST(NetworkRpcTest, Call_ObjectRefArgumentRefused) {
    RpcHarness harness = MakeRpc();
    ASSERT_TRUE(harness.b.RegisterMethod("echo", EchoHandler()));
    ASSERT_TRUE(harness.Establish(1000U));

    // Object refs name host-local objects: meaningless across the wire, refused at the door -
    // nested inside a collection, too.
    EXPECT_EQ(harness.a.Call("echo", {Blueprint::Value{Blueprint::ObjectRef{42U}}},
                                harness.FileA()),
              kInvalidRpcCallId);
    EXPECT_EQ(harness.a
                  .Call("echo",
                           {Blueprint::MakeListValue({Blueprint::Value{Blueprint::ObjectRef{7U}}})},
                           harness.FileA()),
              kInvalidRpcCallId);
    EXPECT_EQ(harness.a.Stats().callsSent, 0U);
    EXPECT_EQ(harness.b.Stats().callsReceived, 0U);
}

TEST(NetworkRpcTest, Call_UnknownMethodAnswersError) {
    RpcHarness harness = MakeRpc();
    ASSERT_TRUE(harness.Establish(1000U));

    const RpcCallId callId = harness.a.Call("nope", {}, harness.FileA());
    ASSERT_NE(callId, kInvalidRpcCallId);
    harness.UpdateBoth(1000U);
    harness.UpdateBoth(1000U);

    ASSERT_EQ(harness.doneA.size(), 1U);
    EXPECT_FALSE(harness.doneA[callId].ok);
    EXPECT_EQ(harness.doneA[callId].error, "unknown method 'nope'");
    EXPECT_EQ(harness.b.Stats().responsesSent, 1U);
    // An unknown method is version skew, not a handler failure: the handler ledger stays clean.
    EXPECT_EQ(harness.b.Stats().handlerErrors, 0U);
}

TEST(NetworkRpcTest, Call_HandlerErrorAndThrowBecomeErrorResponses) {
    RpcHarness harness = MakeRpc();
    ASSERT_TRUE(harness.b.RegisterMethod(
        "fail", [](const std::span<const Blueprint::Value>) {
            return RpcError("custom refusal");
        }));
    ASSERT_TRUE(harness.b.RegisterMethod(
        "boom", [](const std::span<const Blueprint::Value>) -> RpcOutcome {
            throw std::runtime_error{"handler bug"};
        }));
    ASSERT_TRUE(harness.Establish(1000U));

    const RpcCallId failId = harness.a.Call("fail", {}, harness.FileA());
    const RpcCallId boomId = harness.a.Call("boom", {}, harness.FileA());
    harness.UpdateBoth(1000U);
    harness.UpdateBoth(1000U);

    ASSERT_EQ(harness.doneA.size(), 2U);
    EXPECT_FALSE(harness.doneA[failId].ok);
    EXPECT_EQ(harness.doneA[failId].error, "custom refusal");
    EXPECT_FALSE(harness.doneA[boomId].ok);
    EXPECT_EQ(harness.doneA[boomId].error, "handler for 'boom' threw");
    EXPECT_EQ(harness.b.Stats().handlerErrors, 2U);
    EXPECT_EQ(harness.b.Stats().responsesSent, 2U);
}

TEST(NetworkRpcTest, Unregister_MethodStopsServing) {
    RpcHarness harness = MakeRpc();
    ASSERT_TRUE(harness.b.RegisterMethod("echo", EchoHandler()));
    ASSERT_TRUE(harness.Establish(1000U));

    // Dispatch looks the method up when the call arrives: unregistering first means even an
    // already-queued call answers unknown-method.
    const RpcCallId callId =
        harness.a.Call("echo", {Blueprint::Value{std::int64_t{1}}}, harness.FileA());
    ASSERT_NE(callId, kInvalidRpcCallId);
    EXPECT_TRUE(harness.b.UnregisterMethod("echo"));
    harness.UpdateBoth(1000U);
    harness.UpdateBoth(1000U);

    ASSERT_EQ(harness.doneA.size(), 1U);
    EXPECT_FALSE(harness.doneA[callId].ok);
    EXPECT_EQ(harness.doneA[callId].error, "unknown method 'echo'");
}

TEST(NetworkRpcTest, Timeout_OverdueCallFails) {
    RpcHarness harness = MakeRpc();
    ASSERT_TRUE(harness.b.RegisterMethod("never", EchoHandler()));
    ASSERT_TRUE(harness.Establish(1000U));

    // The server is never pumped again: the call sits in its unread queue past the deadline.
    const RpcCallId callId = harness.a.Call("never", {}, harness.FileA(), 100U);
    ASSERT_NE(callId, kInvalidRpcCallId);
    harness.a.Update(1000U);
    EXPECT_TRUE(harness.doneA.empty());
    harness.a.Update(1099U);
    EXPECT_TRUE(harness.doneA.empty());

    harness.a.Update(1100U);
    ASSERT_EQ(harness.doneA.size(), 1U);
    EXPECT_FALSE(harness.doneA[callId].ok);
    EXPECT_EQ(harness.doneA[callId].error, "call to 'never' timed out");
    EXPECT_EQ(harness.a.Stats().callsTimedOut, 1U);
    // Completed means completed: cancelling afterwards finds nothing.
    EXPECT_FALSE(harness.a.Cancel(callId));
}

TEST(NetworkRpcTest, Timeout_ZeroNeverExpires) {
    // Sessions with disabled timeouts: only the RPC deadline could end this call, and there
    // is none.
    RpcHarness harness = MakeRpc(0U);
    ASSERT_TRUE(harness.b.RegisterMethod("never", EchoHandler()));
    ASSERT_TRUE(harness.Establish(1000U));

    const RpcCallId callId = harness.a.Call("never", {}, harness.FileA(), 0U);
    ASSERT_NE(callId, kInvalidRpcCallId);
    harness.a.Update(99999U);
    EXPECT_TRUE(harness.doneA.empty());
    EXPECT_EQ(harness.a.Stats().callsTimedOut, 0U);
    // Still pending after all that silence: only an explicit cancel ends it.
    EXPECT_TRUE(harness.a.Cancel(callId));
}

TEST(NetworkRpcTest, SessionDown_FailsAllPending) {
    RpcHarness harness = MakeRpc();
    ASSERT_TRUE(harness.b.RegisterMethod("slow", EchoHandler()));
    ASSERT_TRUE(harness.Establish(1000U));

    const RpcCallId first = harness.a.Call("slow", {}, harness.FileA());
    const RpcCallId second = harness.a.Call("slow", {}, harness.FileA());
    ASSERT_NE(first, kInvalidRpcCallId);
    ASSERT_NE(second, kInvalidRpcCallId);

    // The server hangs up without ever pumping: both calls fail on the client's next pump.
    ASSERT_TRUE(harness.b.Session().Disconnect());
    harness.a.Update(1000U);

    ASSERT_EQ(harness.doneA.size(), 2U);
    EXPECT_FALSE(harness.doneA[first].ok);
    EXPECT_EQ(harness.doneA[first].error, "session down during call to 'slow'");
    EXPECT_FALSE(harness.doneA[second].ok);
    EXPECT_EQ(harness.a.Stats().callsFailedSessionDown, 2U);
}

TEST(NetworkRpcTest, Cancel_PendingCallNeverCompletes) {
    RpcHarness harness = MakeRpc();
    ASSERT_TRUE(harness.b.RegisterMethod("echo", EchoHandler()));
    ASSERT_TRUE(harness.Establish(1000U));
    EXPECT_FALSE(harness.a.Cancel(999U));

    const RpcCallId callId =
        harness.a.Call("echo", {Blueprint::Value{std::int64_t{1}}}, harness.FileA());
    ASSERT_NE(callId, kInvalidRpcCallId);
    // The server dispatches and answers before the cancel lands: the response crosses, then
    // dies stale on a peer that stopped waiting.
    harness.b.Update(1000U);
    EXPECT_TRUE(harness.a.Cancel(callId));
    harness.a.Update(1000U);

    EXPECT_TRUE(harness.doneA.empty());
    EXPECT_EQ(harness.a.Stats().responsesReceived, 1U);
    EXPECT_EQ(harness.a.Stats().responsesIgnoredStale, 1U);
    EXPECT_FALSE(harness.a.Cancel(callId));
}

TEST(NetworkRpcTest, Malformed_ForeignFramesIgnored) {
    RpcHarness harness = MakeRpc();
    ASSERT_TRUE(harness.b.RegisterMethod("echo", EchoHandler()));
    ASSERT_TRUE(harness.Establish(1000U));

    // Steal a real call frame off the wire to mutate: no codec duplicated in the test.
    const RpcCallId callId =
        harness.a.Call("echo", {Blueprint::Value{std::int64_t{1}}}, harness.FileA());
    ASSERT_NE(callId, kInvalidRpcCallId);
    const std::optional<std::vector<std::uint8_t>> wire = harness.link.b.TryReceive();
    ASSERT_TRUE(wire.has_value());

    std::vector<std::uint8_t> badVersion = *wire;
    badVersion[3U] = 0x09U;
    ASSERT_TRUE(harness.link.a.Send(badVersion));
    ASSERT_TRUE(harness.link.a.Send(std::vector<std::uint8_t>(wire->begin(), wire->begin() + 4)));
    std::vector<std::uint8_t> trailing = *wire;
    trailing.push_back(0xFFU);
    ASSERT_TRUE(harness.link.a.Send(trailing));
    ASSERT_TRUE(harness.link.a.Send(
        Tagged(static_cast<std::uint8_t>(SessionMessageTag::Data), "junk")));

    harness.b.Update(1000U);
    EXPECT_EQ(harness.b.Stats().framesIgnoredMalformed, 4U);
    EXPECT_EQ(harness.b.Stats().callsReceived, 0U);
    EXPECT_TRUE(harness.doneA.empty());
}

TEST(NetworkRpcTest, Response_DroppedWhenTransportFull) {
    RpcHarness harness = MakeRpc();
    ASSERT_TRUE(harness.b.RegisterMethod("echo", EchoHandler()));
    ASSERT_TRUE(harness.Establish(1000U));

    // Wedge the server's outbound queue with junk: its response send fails, the drop counts,
    // and the caller learns about it the at-most-once way - through the timeout.
    for (std::size_t i = 0U; i < 256U; ++i) {
        ASSERT_TRUE(harness.link.b.Send(Bytes("junk")));
    }
    const RpcCallId callId =
        harness.a.Call("echo", {Blueprint::Value{std::int64_t{1}}}, harness.FileA(), 100U);
    ASSERT_NE(callId, kInvalidRpcCallId);
    harness.b.Update(1000U);
    EXPECT_EQ(harness.b.Stats().responsesDropped, 1U);
    EXPECT_EQ(harness.b.Stats().responsesSent, 0U);

    harness.a.Update(1000U);
    harness.a.Update(1100U);
    ASSERT_EQ(harness.doneA.size(), 1U);
    EXPECT_FALSE(harness.doneA[callId].ok);
    EXPECT_EQ(harness.doneA[callId].error, "call to 'echo' timed out");
}

TEST(NetworkRpcTest, Stats_CountersExactAfterAMixedExchange) {
    RpcHarness harness = MakeRpc();
    ASSERT_TRUE(harness.a.RegisterMethod(
        "double", [](const std::span<const Blueprint::Value> args) {
            return RpcOk(Blueprint::Value{std::get<std::int64_t>(args[0]) * 2});
        }));
    ASSERT_TRUE(harness.b.RegisterMethod("echo", EchoHandler()));
    ASSERT_TRUE(harness.b.RegisterMethod(
        "fail", [](const std::span<const Blueprint::Value>) {
            return RpcError("custom refusal");
        }));
    ASSERT_TRUE(harness.Establish(1000U));

    ASSERT_NE(harness.a.Call("echo", {Blueprint::Value{std::int64_t{1}}}, harness.FileA()),
              kInvalidRpcCallId);
    ASSERT_NE(harness.a.Call("missing", {}, harness.FileA()), kInvalidRpcCallId);
    ASSERT_NE(harness.a.Call("fail", {}, harness.FileA()), kInvalidRpcCallId);
    ASSERT_NE(harness.b.Call("double", {Blueprint::Value{std::int64_t{21}}}, harness.FileB()),
              kInvalidRpcCallId);
    ASSERT_TRUE(harness.link.a.Send(
        Tagged(static_cast<std::uint8_t>(SessionMessageTag::Data), "junk")));
    harness.UpdateBoth(1000U);
    harness.UpdateBoth(1000U);

    ASSERT_EQ(harness.doneA.size(), 3U);
    ASSERT_EQ(harness.doneB.size(), 1U);
    EXPECT_EQ(harness.a.Stats(),
              (RpcStats{3U, 1U, 1U, 3U, 3U, 0U, 0U, 0U, 0U, 0U, 0U, 0U}));
    EXPECT_EQ(harness.b.Stats(),
              (RpcStats{1U, 3U, 3U, 1U, 1U, 0U, 0U, 0U, 1U, 0U, 1U, 0U}));
}

TEST(NetworkRpcTest, Results_RetainedPeekedForgottenAndEvicted) {
    RpcHarness harness = MakeRpc();
    ASSERT_TRUE(harness.b.RegisterMethod("echo", EchoHandler()));
    ASSERT_TRUE(harness.Establish(1000U));

    // A completed call stays readable past its callback - and peeking never consumes.
    const RpcCallId callId =
        harness.a.Call("echo", {Blueprint::Value{std::int64_t{7}}}, harness.FileA());
    ASSERT_NE(callId, kInvalidRpcCallId);
    EXPECT_FALSE(harness.a.Result(callId).has_value());
    EXPECT_FALSE(harness.a.ForgetResult(callId));
    harness.UpdateBoth(1000U);
    harness.UpdateBoth(1000U);
    ASSERT_EQ(harness.doneA.size(), 1U);

    const std::optional<RpcOutcome> peeked = harness.a.Result(callId);
    ASSERT_TRUE(peeked.has_value());
    EXPECT_TRUE(peeked->ok);
    EXPECT_EQ(peeked->value, Blueprint::Value{std::int64_t{7}});
    EXPECT_TRUE(harness.a.Result(callId).has_value());
    EXPECT_TRUE(harness.a.ForgetResult(callId));
    EXPECT_FALSE(harness.a.Result(callId).has_value());
    EXPECT_FALSE(harness.a.ForgetResult(callId));
    EXPECT_FALSE(harness.a.Result(999U).has_value());

    // Retention is bounded: past 64 completions the oldest goes, loudly counted.
    std::vector<RpcCallId> flood;
    for (std::size_t i = 0U; i < kRpcMaximumRetainedResults + 1U; ++i) {
        const RpcCallId id =
            harness.a.Call("echo", {Blueprint::Value{static_cast<std::int64_t>(i)}},
                              harness.FileA());
        ASSERT_NE(id, kInvalidRpcCallId);
        flood.push_back(id);
    }
    harness.UpdateBoth(1000U);
    harness.UpdateBoth(1000U);
    ASSERT_EQ(harness.doneA.size(), 1U + flood.size());
    EXPECT_EQ(harness.a.Stats().resultsEvicted, 1U);
    EXPECT_FALSE(harness.a.Result(flood.front()).has_value());
    EXPECT_TRUE(harness.a.Result(flood.back()).has_value());

    // Timeouts retain too: the failure stays readable like any completion.
    const RpcCallId slowId = harness.a.Call("echo", {}, harness.FileA(), 100U);
    ASSERT_NE(slowId, kInvalidRpcCallId);
    harness.a.Update(1000U);
    harness.a.Update(1100U);
    ASSERT_EQ(harness.doneA.size(), 1U + flood.size() + 1U);
    ASSERT_TRUE(harness.a.Result(slowId).has_value());
    EXPECT_FALSE(harness.a.Result(slowId)->ok);

    // So do session-down failures.
    const RpcCallId doomedId = harness.a.Call("echo", {}, harness.FileA());
    ASSERT_NE(doomedId, kInvalidRpcCallId);
    ASSERT_TRUE(harness.b.Session().Disconnect());
    harness.a.Update(1100U);
    ASSERT_TRUE(harness.a.Result(doomedId).has_value());
    EXPECT_FALSE(harness.a.Result(doomedId)->ok);
}

} // namespace
} // namespace UVE::Network::Tests
