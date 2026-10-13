// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/network/network_rpc_uve.h"

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

[[nodiscard]] std::vector<std::uint8_t> BytesUVE(const std::string& text) {
    return std::vector<std::uint8_t>(text.begin(), text.end());
}

[[nodiscard]] std::vector<std::uint8_t> TaggedUVE(const std::uint8_t tag, const std::string& text) {
    std::vector<std::uint8_t> datagram{tag};
    for (const std::uint8_t byte : BytesUVE(text)) {
        datagram.push_back(byte);
    }
    return datagram;
}

// Structural equality for values: plain == compares collections by shared-pointer identity,
// so a decoded-then-rebuilt twin never matches. This walks both and compares by shape.
[[nodiscard]] bool ValuesEqualUVE(const UVScript::ValueUVE& a, const UVScript::ValueUVE& b) {
    if (a.index() != b.index()) {
        return false;
    }
    const auto* collectionA =
        std::get_if<std::shared_ptr<const UVScript::CollectionValueUVE>>(&a);
    if (collectionA == nullptr) {
        return a == b;
    }
    const auto& collectionB = std::get<std::shared_ptr<const UVScript::CollectionValueUVE>>(b);
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
        if (!ValuesEqualUVE((*collectionA)->items[i], collectionB->items[i])) {
            return false;
        }
    }
    return true;
}

struct RpcHarnessUVE final {
    LoopbackTransportPairUVE link;
    NetworkRpcUVE a;
    NetworkRpcUVE b;
    // Completed outcomes by call id, per side - the callbacks file here.
    std::map<RpcCallIdUVE, RpcOutcomeUVE> doneA;
    std::map<RpcCallIdUVE, RpcOutcomeUVE> doneB;

    void UpdateBothUVE(const std::uint64_t nowMs) {
        a.UpdateUVE(nowMs);
        b.UpdateUVE(nowMs);
    }

    [[nodiscard]] bool EstablishUVE(const std::uint64_t nowMs) {
        if (!a.SessionUVE().ConnectUVE(nowMs)) {
            return false;
        }
        UpdateBothUVE(nowMs);
        UpdateBothUVE(nowMs);
        return a.SessionUVE().StateUVE() == SessionStateUVE::Established &&
               b.SessionUVE().StateUVE() == SessionStateUVE::Established;
    }

    [[nodiscard]] RpcResponseCallbackUVE FileAUVE() {
        return [this](const RpcCallIdUVE callId, const RpcOutcomeUVE& outcome) {
            doneA.emplace(callId, outcome);
        };
    }

    [[nodiscard]] RpcResponseCallbackUVE FileBUVE() {
        return [this](const RpcCallIdUVE callId, const RpcOutcomeUVE& outcome) {
            doneB.emplace(callId, outcome);
        };
    }
};

[[nodiscard]] RpcHarnessUVE MakeRpcUVE(
    const std::uint64_t timeoutMs = kDefaultSessionTimeoutMsUVE) {
    LoopbackTransportPairUVE pair = CreateLoopbackTransportPairUVE(256U);
    NetworkSessionUVE sessionA(SessionRoleUVE::Client,
                               std::make_unique<LoopbackEndpointUVE>(pair.a), timeoutMs);
    NetworkSessionUVE sessionB(SessionRoleUVE::Server,
                               std::make_unique<LoopbackEndpointUVE>(pair.b), timeoutMs);
    NetworkRpcUVE a(std::move(sessionA));
    NetworkRpcUVE b(std::move(sessionB));
    return RpcHarnessUVE{std::move(pair), std::move(a), std::move(b), {}, {}};
}

[[nodiscard]] RpcMethodHandlerUVE EchoHandlerUVE() {
    return [](const std::span<const UVScript::ValueUVE> args) { return RpcOkUVE(args[0]); };
}

TEST(NetworkRpcUVETest, RegisterRules_DuplicatesAndEmptiesRejected) {
    RpcHarnessUVE harness = MakeRpcUVE();
    const RpcMethodHandlerUVE handler = [](const std::span<const UVScript::ValueUVE>) {
        return RpcOkUVE(UVScript::ValueUVE{});
    };

    EXPECT_TRUE(harness.b.RegisterMethodUVE("add", handler));
    EXPECT_FALSE(harness.b.RegisterMethodUVE("add", handler));
    EXPECT_FALSE(harness.b.RegisterMethodUVE("", handler));
    EXPECT_FALSE(harness.b.RegisterMethodUVE("sub", RpcMethodHandlerUVE{}));
    EXPECT_FALSE(harness.b.UnregisterMethodUVE("nope"));
    EXPECT_TRUE(harness.b.UnregisterMethodUVE("add"));
    EXPECT_TRUE(harness.b.RegisterMethodUVE("add", handler));
}

TEST(NetworkRpcUVETest, Call_RoundTripAnswersResult) {
    RpcHarnessUVE harness = MakeRpcUVE();
    ASSERT_TRUE(harness.b.RegisterMethodUVE(
        "add", [](const std::span<const UVScript::ValueUVE> args) {
            const std::int64_t x = std::get<std::int64_t>(args[0]);
            const std::int64_t y = std::get<std::int64_t>(args[1]);
            return RpcOkUVE(UVScript::ValueUVE{x + y});
        }));
    // A down session sends nothing: zero means the call never went out.
    EXPECT_EQ(harness.a.CallUVE("add", {}, harness.FileAUVE()), kInvalidRpcCallIdUVE);

    ASSERT_TRUE(harness.EstablishUVE(1000U));
    EXPECT_EQ(harness.a.CallUVE("", {}, harness.FileAUVE()), kInvalidRpcCallIdUVE);
    EXPECT_EQ(harness.a.CallUVE("add", {}, RpcResponseCallbackUVE{}), kInvalidRpcCallIdUVE);

    const RpcCallIdUVE callId =
        harness.a.CallUVE("add",
                          {UVScript::ValueUVE{std::int64_t{1}}, UVScript::ValueUVE{std::int64_t{2}}},
                          harness.FileAUVE());
    ASSERT_NE(callId, kInvalidRpcCallIdUVE);
    // One pump carries the call over and the response back; the second delivers it.
    harness.UpdateBothUVE(1000U);
    harness.UpdateBothUVE(1000U);

    ASSERT_EQ(harness.doneA.size(), 1U);
    EXPECT_TRUE(harness.doneA[callId].ok);
    EXPECT_EQ(harness.doneA[callId].value, UVScript::ValueUVE{std::int64_t{3}});
    EXPECT_EQ(harness.a.StatsUVE().responsesMatched, 1U);
    EXPECT_EQ(harness.b.StatsUVE().callsReceived, 1U);
    EXPECT_EQ(harness.b.StatsUVE().responsesSent, 1U);
}

TEST(NetworkRpcUVETest, Call_AllValueKindsRoundTrip) {
    RpcHarnessUVE harness = MakeRpcUVE();
    ASSERT_TRUE(harness.b.RegisterMethodUVE("echo", EchoHandlerUVE()));
    ASSERT_TRUE(harness.EstablishUVE(1000U));

    const std::vector<UVScript::ValueUVE> values{
        UVScript::ValueUVE{},
        UVScript::ValueUVE{true},
        UVScript::ValueUVE{false},
        UVScript::ValueUVE{std::int64_t{INT64_MIN}},
        UVScript::ValueUVE{std::int64_t{INT64_MAX}},
        UVScript::ValueUVE{2.5},
        UVScript::ValueUVE{std::string{}},
        UVScript::ValueUVE{std::string{"hello"}},
        // Embedded NUL: the codec is length-based, never C-string-based.
        UVScript::ValueUVE{std::string{"a\0b", 3U}},
        UVScript::ValueUVE{UVScript::Vec3ValueUVE{1.5, -2.5, 3.25}},
        UVScript::MakeListValueUVE({UVScript::ValueUVE{std::int64_t{1}},
                                    UVScript::MakeListValueUVE({UVScript::ValueUVE{std::int64_t{2}},
                                                                UVScript::ValueUVE{std::string{"three"}}})}),
        UVScript::MakeMapValueUVE({UVScript::ValueUVE{std::string{"a"}},
                                   UVScript::ValueUVE{std::int64_t{1}},
                                   UVScript::ValueUVE{std::string{"b"}},
                                   UVScript::MakeListValueUVE({UVScript::ValueUVE{true},
                                                               UVScript::ValueUVE{1.5}})}),
        UVScript::MakeTupleValueUVE({UVScript::ValueUVE{std::int64_t{1}},
                                     UVScript::ValueUVE{std::string{"two"}}}),
    };
    std::vector<RpcCallIdUVE> callIds;
    for (const UVScript::ValueUVE& value : values) {
        const RpcCallIdUVE callId = harness.a.CallUVE("echo", {value}, harness.FileAUVE());
        ASSERT_NE(callId, kInvalidRpcCallIdUVE);
        callIds.push_back(callId);
    }
    // NaN rides separately: bit-exact through the codec, but never equal to itself.
    const RpcCallIdUVE nanId =
        harness.a.CallUVE("echo", {UVScript::ValueUVE{std::numeric_limits<double>::quiet_NaN()}},
                          harness.FileAUVE());
    ASSERT_NE(nanId, kInvalidRpcCallIdUVE);

    harness.UpdateBothUVE(1000U);
    harness.UpdateBothUVE(1000U);

    ASSERT_EQ(harness.doneA.size(), values.size() + 1U);
    for (std::size_t i = 0U; i < values.size(); ++i) {
        EXPECT_TRUE(harness.doneA[callIds[i]].ok);
        EXPECT_TRUE(ValuesEqualUVE(harness.doneA[callIds[i]].value, values[i]));
    }
    ASSERT_TRUE(harness.doneA[nanId].ok);
    const UVScript::ValueUVE& nanBack = harness.doneA[nanId].value;
    ASSERT_TRUE(std::holds_alternative<double>(nanBack));
    EXPECT_EQ(std::bit_cast<std::uint64_t>(std::get<double>(nanBack)),
              std::bit_cast<std::uint64_t>(std::numeric_limits<double>::quiet_NaN()));
}

TEST(NetworkRpcUVETest, Call_ObjectRefArgumentRefused) {
    RpcHarnessUVE harness = MakeRpcUVE();
    ASSERT_TRUE(harness.b.RegisterMethodUVE("echo", EchoHandlerUVE()));
    ASSERT_TRUE(harness.EstablishUVE(1000U));

    // Object refs name host-local objects: meaningless across the wire, refused at the door -
    // nested inside a collection, too.
    EXPECT_EQ(harness.a.CallUVE("echo", {UVScript::ValueUVE{UVScript::ObjectRefUVE{42U}}},
                                harness.FileAUVE()),
              kInvalidRpcCallIdUVE);
    EXPECT_EQ(harness.a
                  .CallUVE("echo",
                           {UVScript::MakeListValueUVE({UVScript::ValueUVE{UVScript::ObjectRefUVE{7U}}})},
                           harness.FileAUVE()),
              kInvalidRpcCallIdUVE);
    EXPECT_EQ(harness.a.StatsUVE().callsSent, 0U);
    EXPECT_EQ(harness.b.StatsUVE().callsReceived, 0U);
}

TEST(NetworkRpcUVETest, Call_UnknownMethodAnswersError) {
    RpcHarnessUVE harness = MakeRpcUVE();
    ASSERT_TRUE(harness.EstablishUVE(1000U));

    const RpcCallIdUVE callId = harness.a.CallUVE("nope", {}, harness.FileAUVE());
    ASSERT_NE(callId, kInvalidRpcCallIdUVE);
    harness.UpdateBothUVE(1000U);
    harness.UpdateBothUVE(1000U);

    ASSERT_EQ(harness.doneA.size(), 1U);
    EXPECT_FALSE(harness.doneA[callId].ok);
    EXPECT_EQ(harness.doneA[callId].error, "unknown method 'nope'");
    EXPECT_EQ(harness.b.StatsUVE().responsesSent, 1U);
    // An unknown method is version skew, not a handler failure: the handler ledger stays clean.
    EXPECT_EQ(harness.b.StatsUVE().handlerErrors, 0U);
}

TEST(NetworkRpcUVETest, Call_HandlerErrorAndThrowBecomeErrorResponses) {
    RpcHarnessUVE harness = MakeRpcUVE();
    ASSERT_TRUE(harness.b.RegisterMethodUVE(
        "fail", [](const std::span<const UVScript::ValueUVE>) {
            return RpcErrorUVE("custom refusal");
        }));
    ASSERT_TRUE(harness.b.RegisterMethodUVE(
        "boom", [](const std::span<const UVScript::ValueUVE>) -> RpcOutcomeUVE {
            throw std::runtime_error{"handler bug"};
        }));
    ASSERT_TRUE(harness.EstablishUVE(1000U));

    const RpcCallIdUVE failId = harness.a.CallUVE("fail", {}, harness.FileAUVE());
    const RpcCallIdUVE boomId = harness.a.CallUVE("boom", {}, harness.FileAUVE());
    harness.UpdateBothUVE(1000U);
    harness.UpdateBothUVE(1000U);

    ASSERT_EQ(harness.doneA.size(), 2U);
    EXPECT_FALSE(harness.doneA[failId].ok);
    EXPECT_EQ(harness.doneA[failId].error, "custom refusal");
    EXPECT_FALSE(harness.doneA[boomId].ok);
    EXPECT_EQ(harness.doneA[boomId].error, "handler for 'boom' threw");
    EXPECT_EQ(harness.b.StatsUVE().handlerErrors, 2U);
    EXPECT_EQ(harness.b.StatsUVE().responsesSent, 2U);
}

TEST(NetworkRpcUVETest, Unregister_MethodStopsServing) {
    RpcHarnessUVE harness = MakeRpcUVE();
    ASSERT_TRUE(harness.b.RegisterMethodUVE("echo", EchoHandlerUVE()));
    ASSERT_TRUE(harness.EstablishUVE(1000U));

    // Dispatch looks the method up when the call arrives: unregistering first means even an
    // already-queued call answers unknown-method.
    const RpcCallIdUVE callId =
        harness.a.CallUVE("echo", {UVScript::ValueUVE{std::int64_t{1}}}, harness.FileAUVE());
    ASSERT_NE(callId, kInvalidRpcCallIdUVE);
    EXPECT_TRUE(harness.b.UnregisterMethodUVE("echo"));
    harness.UpdateBothUVE(1000U);
    harness.UpdateBothUVE(1000U);

    ASSERT_EQ(harness.doneA.size(), 1U);
    EXPECT_FALSE(harness.doneA[callId].ok);
    EXPECT_EQ(harness.doneA[callId].error, "unknown method 'echo'");
}

TEST(NetworkRpcUVETest, Timeout_OverdueCallFails) {
    RpcHarnessUVE harness = MakeRpcUVE();
    ASSERT_TRUE(harness.b.RegisterMethodUVE("never", EchoHandlerUVE()));
    ASSERT_TRUE(harness.EstablishUVE(1000U));

    // The server is never pumped again: the call sits in its unread queue past the deadline.
    const RpcCallIdUVE callId = harness.a.CallUVE("never", {}, harness.FileAUVE(), 100U);
    ASSERT_NE(callId, kInvalidRpcCallIdUVE);
    harness.a.UpdateUVE(1000U);
    EXPECT_TRUE(harness.doneA.empty());
    harness.a.UpdateUVE(1099U);
    EXPECT_TRUE(harness.doneA.empty());

    harness.a.UpdateUVE(1100U);
    ASSERT_EQ(harness.doneA.size(), 1U);
    EXPECT_FALSE(harness.doneA[callId].ok);
    EXPECT_EQ(harness.doneA[callId].error, "call to 'never' timed out");
    EXPECT_EQ(harness.a.StatsUVE().callsTimedOut, 1U);
    // Completed means completed: cancelling afterwards finds nothing.
    EXPECT_FALSE(harness.a.CancelUVE(callId));
}

TEST(NetworkRpcUVETest, Timeout_ZeroNeverExpires) {
    // Sessions with disabled timeouts: only the RPC deadline could end this call, and there
    // is none.
    RpcHarnessUVE harness = MakeRpcUVE(0U);
    ASSERT_TRUE(harness.b.RegisterMethodUVE("never", EchoHandlerUVE()));
    ASSERT_TRUE(harness.EstablishUVE(1000U));

    const RpcCallIdUVE callId = harness.a.CallUVE("never", {}, harness.FileAUVE(), 0U);
    ASSERT_NE(callId, kInvalidRpcCallIdUVE);
    harness.a.UpdateUVE(99999U);
    EXPECT_TRUE(harness.doneA.empty());
    EXPECT_EQ(harness.a.StatsUVE().callsTimedOut, 0U);
    // Still pending after all that silence: only an explicit cancel ends it.
    EXPECT_TRUE(harness.a.CancelUVE(callId));
}

TEST(NetworkRpcUVETest, SessionDown_FailsAllPending) {
    RpcHarnessUVE harness = MakeRpcUVE();
    ASSERT_TRUE(harness.b.RegisterMethodUVE("slow", EchoHandlerUVE()));
    ASSERT_TRUE(harness.EstablishUVE(1000U));

    const RpcCallIdUVE first = harness.a.CallUVE("slow", {}, harness.FileAUVE());
    const RpcCallIdUVE second = harness.a.CallUVE("slow", {}, harness.FileAUVE());
    ASSERT_NE(first, kInvalidRpcCallIdUVE);
    ASSERT_NE(second, kInvalidRpcCallIdUVE);

    // The server hangs up without ever pumping: both calls fail on the client's next pump.
    ASSERT_TRUE(harness.b.SessionUVE().DisconnectUVE());
    harness.a.UpdateUVE(1000U);

    ASSERT_EQ(harness.doneA.size(), 2U);
    EXPECT_FALSE(harness.doneA[first].ok);
    EXPECT_EQ(harness.doneA[first].error, "session down during call to 'slow'");
    EXPECT_FALSE(harness.doneA[second].ok);
    EXPECT_EQ(harness.a.StatsUVE().callsFailedSessionDown, 2U);
}

TEST(NetworkRpcUVETest, Cancel_PendingCallNeverCompletes) {
    RpcHarnessUVE harness = MakeRpcUVE();
    ASSERT_TRUE(harness.b.RegisterMethodUVE("echo", EchoHandlerUVE()));
    ASSERT_TRUE(harness.EstablishUVE(1000U));
    EXPECT_FALSE(harness.a.CancelUVE(999U));

    const RpcCallIdUVE callId =
        harness.a.CallUVE("echo", {UVScript::ValueUVE{std::int64_t{1}}}, harness.FileAUVE());
    ASSERT_NE(callId, kInvalidRpcCallIdUVE);
    // The server dispatches and answers before the cancel lands: the response crosses, then
    // dies stale on a peer that stopped waiting.
    harness.b.UpdateUVE(1000U);
    EXPECT_TRUE(harness.a.CancelUVE(callId));
    harness.a.UpdateUVE(1000U);

    EXPECT_TRUE(harness.doneA.empty());
    EXPECT_EQ(harness.a.StatsUVE().responsesReceived, 1U);
    EXPECT_EQ(harness.a.StatsUVE().responsesIgnoredStale, 1U);
    EXPECT_FALSE(harness.a.CancelUVE(callId));
}

TEST(NetworkRpcUVETest, Malformed_ForeignFramesIgnored) {
    RpcHarnessUVE harness = MakeRpcUVE();
    ASSERT_TRUE(harness.b.RegisterMethodUVE("echo", EchoHandlerUVE()));
    ASSERT_TRUE(harness.EstablishUVE(1000U));

    // Steal a real call frame off the wire to mutate: no codec duplicated in the test.
    const RpcCallIdUVE callId =
        harness.a.CallUVE("echo", {UVScript::ValueUVE{std::int64_t{1}}}, harness.FileAUVE());
    ASSERT_NE(callId, kInvalidRpcCallIdUVE);
    const std::optional<std::vector<std::uint8_t>> wire = harness.link.b.TryReceiveUVE();
    ASSERT_TRUE(wire.has_value());

    std::vector<std::uint8_t> badVersion = *wire;
    badVersion[3U] = 0x09U;
    ASSERT_TRUE(harness.link.a.SendUVE(badVersion));
    ASSERT_TRUE(harness.link.a.SendUVE(std::vector<std::uint8_t>(wire->begin(), wire->begin() + 4)));
    std::vector<std::uint8_t> trailing = *wire;
    trailing.push_back(0xFFU);
    ASSERT_TRUE(harness.link.a.SendUVE(trailing));
    ASSERT_TRUE(harness.link.a.SendUVE(
        TaggedUVE(static_cast<std::uint8_t>(SessionMessageTagUVE::Data), "junk")));

    harness.b.UpdateUVE(1000U);
    EXPECT_EQ(harness.b.StatsUVE().framesIgnoredMalformed, 4U);
    EXPECT_EQ(harness.b.StatsUVE().callsReceived, 0U);
    EXPECT_TRUE(harness.doneA.empty());
}

TEST(NetworkRpcUVETest, Response_DroppedWhenTransportFull) {
    RpcHarnessUVE harness = MakeRpcUVE();
    ASSERT_TRUE(harness.b.RegisterMethodUVE("echo", EchoHandlerUVE()));
    ASSERT_TRUE(harness.EstablishUVE(1000U));

    // Wedge the server's outbound queue with junk: its response send fails, the drop counts,
    // and the caller learns about it the at-most-once way - through the timeout.
    for (std::size_t i = 0U; i < 256U; ++i) {
        ASSERT_TRUE(harness.link.b.SendUVE(BytesUVE("junk")));
    }
    const RpcCallIdUVE callId =
        harness.a.CallUVE("echo", {UVScript::ValueUVE{std::int64_t{1}}}, harness.FileAUVE(), 100U);
    ASSERT_NE(callId, kInvalidRpcCallIdUVE);
    harness.b.UpdateUVE(1000U);
    EXPECT_EQ(harness.b.StatsUVE().responsesDropped, 1U);
    EXPECT_EQ(harness.b.StatsUVE().responsesSent, 0U);

    harness.a.UpdateUVE(1000U);
    harness.a.UpdateUVE(1100U);
    ASSERT_EQ(harness.doneA.size(), 1U);
    EXPECT_FALSE(harness.doneA[callId].ok);
    EXPECT_EQ(harness.doneA[callId].error, "call to 'echo' timed out");
}

TEST(NetworkRpcUVETest, Stats_CountersExactAfterAMixedExchange) {
    RpcHarnessUVE harness = MakeRpcUVE();
    ASSERT_TRUE(harness.a.RegisterMethodUVE(
        "double", [](const std::span<const UVScript::ValueUVE> args) {
            return RpcOkUVE(UVScript::ValueUVE{std::get<std::int64_t>(args[0]) * 2});
        }));
    ASSERT_TRUE(harness.b.RegisterMethodUVE("echo", EchoHandlerUVE()));
    ASSERT_TRUE(harness.b.RegisterMethodUVE(
        "fail", [](const std::span<const UVScript::ValueUVE>) {
            return RpcErrorUVE("custom refusal");
        }));
    ASSERT_TRUE(harness.EstablishUVE(1000U));

    ASSERT_NE(harness.a.CallUVE("echo", {UVScript::ValueUVE{std::int64_t{1}}}, harness.FileAUVE()),
              kInvalidRpcCallIdUVE);
    ASSERT_NE(harness.a.CallUVE("missing", {}, harness.FileAUVE()), kInvalidRpcCallIdUVE);
    ASSERT_NE(harness.a.CallUVE("fail", {}, harness.FileAUVE()), kInvalidRpcCallIdUVE);
    ASSERT_NE(harness.b.CallUVE("double", {UVScript::ValueUVE{std::int64_t{21}}}, harness.FileBUVE()),
              kInvalidRpcCallIdUVE);
    ASSERT_TRUE(harness.link.a.SendUVE(
        TaggedUVE(static_cast<std::uint8_t>(SessionMessageTagUVE::Data), "junk")));
    harness.UpdateBothUVE(1000U);
    harness.UpdateBothUVE(1000U);

    ASSERT_EQ(harness.doneA.size(), 3U);
    ASSERT_EQ(harness.doneB.size(), 1U);
    EXPECT_EQ(harness.a.StatsUVE(),
              (RpcStatsUVE{3U, 1U, 1U, 3U, 3U, 0U, 0U, 0U, 0U, 0U, 0U, 0U}));
    EXPECT_EQ(harness.b.StatsUVE(),
              (RpcStatsUVE{1U, 3U, 3U, 1U, 1U, 0U, 0U, 0U, 1U, 0U, 1U, 0U}));
}

TEST(NetworkRpcUVETest, Results_RetainedPeekedForgottenAndEvicted) {
    RpcHarnessUVE harness = MakeRpcUVE();
    ASSERT_TRUE(harness.b.RegisterMethodUVE("echo", EchoHandlerUVE()));
    ASSERT_TRUE(harness.EstablishUVE(1000U));

    // A completed call stays readable past its callback - and peeking never consumes.
    const RpcCallIdUVE callId =
        harness.a.CallUVE("echo", {UVScript::ValueUVE{std::int64_t{7}}}, harness.FileAUVE());
    ASSERT_NE(callId, kInvalidRpcCallIdUVE);
    EXPECT_FALSE(harness.a.ResultUVE(callId).has_value());
    EXPECT_FALSE(harness.a.ForgetResultUVE(callId));
    harness.UpdateBothUVE(1000U);
    harness.UpdateBothUVE(1000U);
    ASSERT_EQ(harness.doneA.size(), 1U);

    const std::optional<RpcOutcomeUVE> peeked = harness.a.ResultUVE(callId);
    ASSERT_TRUE(peeked.has_value());
    EXPECT_TRUE(peeked->ok);
    EXPECT_EQ(peeked->value, UVScript::ValueUVE{std::int64_t{7}});
    EXPECT_TRUE(harness.a.ResultUVE(callId).has_value());
    EXPECT_TRUE(harness.a.ForgetResultUVE(callId));
    EXPECT_FALSE(harness.a.ResultUVE(callId).has_value());
    EXPECT_FALSE(harness.a.ForgetResultUVE(callId));
    EXPECT_FALSE(harness.a.ResultUVE(999U).has_value());

    // Retention is bounded: past 64 completions the oldest goes, loudly counted.
    std::vector<RpcCallIdUVE> flood;
    for (std::size_t i = 0U; i < kRpcMaximumRetainedResultsUVE + 1U; ++i) {
        const RpcCallIdUVE id =
            harness.a.CallUVE("echo", {UVScript::ValueUVE{static_cast<std::int64_t>(i)}},
                              harness.FileAUVE());
        ASSERT_NE(id, kInvalidRpcCallIdUVE);
        flood.push_back(id);
    }
    harness.UpdateBothUVE(1000U);
    harness.UpdateBothUVE(1000U);
    ASSERT_EQ(harness.doneA.size(), 1U + flood.size());
    EXPECT_EQ(harness.a.StatsUVE().resultsEvicted, 1U);
    EXPECT_FALSE(harness.a.ResultUVE(flood.front()).has_value());
    EXPECT_TRUE(harness.a.ResultUVE(flood.back()).has_value());

    // Timeouts retain too: the failure stays readable like any completion.
    const RpcCallIdUVE slowId = harness.a.CallUVE("echo", {}, harness.FileAUVE(), 100U);
    ASSERT_NE(slowId, kInvalidRpcCallIdUVE);
    harness.a.UpdateUVE(1000U);
    harness.a.UpdateUVE(1100U);
    ASSERT_EQ(harness.doneA.size(), 1U + flood.size() + 1U);
    ASSERT_TRUE(harness.a.ResultUVE(slowId).has_value());
    EXPECT_FALSE(harness.a.ResultUVE(slowId)->ok);

    // So do session-down failures.
    const RpcCallIdUVE doomedId = harness.a.CallUVE("echo", {}, harness.FileAUVE());
    ASSERT_NE(doomedId, kInvalidRpcCallIdUVE);
    ASSERT_TRUE(harness.b.SessionUVE().DisconnectUVE());
    harness.a.UpdateUVE(1100U);
    ASSERT_TRUE(harness.a.ResultUVE(doomedId).has_value());
    EXPECT_FALSE(harness.a.ResultUVE(doomedId)->ok);
}

} // namespace
} // namespace UVE::Network::Tests
