// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
#include "reliable_packet_window.h"
#include <limits>
#include <gtest/gtest.h>
namespace UVE::Network::Tests {
namespace {
TEST(ReliablePacketWindowTest, BuildsReliablePayloadFragments_SplitsAndReassemblesPayload) {
    const std::vector<std::uint8_t> original{1U, 2U, 3U, 4U, 5U};
    std::vector<ReliablePayloadFragment> fragments;
    ASSERT_TRUE(BuildReliablePayloadFragments(17U, original, 2U, fragments));
    ASSERT_EQ(fragments.size(), 3U);
    EXPECT_EQ(fragments[0].fragmentIndex, 0U);
    EXPECT_EQ(fragments[0].fragmentCount, 3U);
    EXPECT_EQ(fragments[0].payloadBytes, (std::vector<std::uint8_t>{1U, 2U}));
    EXPECT_EQ(fragments[2].payloadBytes, (std::vector<std::uint8_t>{5U}));

    ReliablePayloadReassemblyState state;
    std::vector<std::uint8_t> decoded;
    for (const std::size_t index : {2U, 0U, 1U}) {
        const ReliablePayloadFragment& fragment = fragments[index];
        const auto status = AcceptReliablePayloadFragment(
            fragment.messageId, fragment.fragmentIndex, fragment.fragmentCount, fragment.payloadBytes, state, decoded);
        EXPECT_EQ(status, index == 1U ? ReliablePayloadReassemblyStatus::Complete
                                      : ReliablePayloadReassemblyStatus::Accepted);
    }
    EXPECT_EQ(decoded, original);
}

TEST(ReliablePacketWindowTest, BuildsReliablePayloadFragments_RejectsInvalidInputsAtomically) {
    const std::vector<ReliablePayloadFragment> original{{17U, 0U, 1U, {9U}}};
    std::vector<ReliablePayloadFragment> fragments = original;
    EXPECT_FALSE(BuildReliablePayloadFragments(0U, {1U}, 1U, fragments));
    EXPECT_EQ(fragments, original);
    EXPECT_FALSE(BuildReliablePayloadFragments(17U, {}, 1U, fragments));
    EXPECT_EQ(fragments, original);
    EXPECT_FALSE(BuildReliablePayloadFragments(17U, {1U}, 0U, fragments));
    EXPECT_EQ(fragments, original);
    EXPECT_FALSE(BuildReliablePayloadFragments(17U, {1U}, kReliablePacketMaximumPayloadBytes + 1U, fragments));
    EXPECT_EQ(fragments, original);
    EXPECT_FALSE(BuildReliablePayloadFragments(
        17U, std::vector<std::uint8_t>(kReliablePacketMaximumPayloadBytes + 1U, 0U), 1U, fragments));
    EXPECT_EQ(fragments, original);
    EXPECT_FALSE(BuildReliablePayloadFragments(
        17U, std::vector<std::uint8_t>(kReliablePacketMaximumFragmentCount + 1U, 0U), 1U, fragments));
    EXPECT_EQ(fragments, original);
}

TEST(ReliablePacketWindowTest, ReliablePacketFragmentWireRoundTrip_ComposesValidatedEnvelopes) {
    const ReliablePacketHeader originalHeader{7U, 3U, 0x01020304U};
    const ReliablePayloadFragment originalFragment{17U, 1U, 2U, {0xAAU, 0xBBU}};
    std::vector<std::uint8_t> bytes;
    ASSERT_TRUE(SerializeReliablePacketFragment(originalHeader, originalFragment, bytes));
    EXPECT_EQ(bytes, (std::vector<std::uint8_t>{0x07U, 0x00U, 0x00U, 0x00U,
                                                  0x03U, 0x00U, 0x00U, 0x00U,
                                                  0x04U, 0x03U, 0x02U, 0x01U,
                                                  0x11U, 0x00U, 0x00U, 0x00U,
                                                  0x01U, 0x00U, 0x00U, 0x00U,
                                                  0x02U, 0x00U, 0x00U, 0x00U,
                                                  0x02U, 0x00U, 0x00U, 0x00U,
                                                  0xAAU, 0xBBU}));
    ReliablePacketHeader decodedHeader;
    ReliablePayloadFragment decodedFragment;
    ASSERT_TRUE(DeserializeReliablePacketFragment(bytes, decodedHeader, decodedFragment));
    EXPECT_EQ(decodedHeader.sequence, originalHeader.sequence);
    EXPECT_EQ(decodedHeader.acknowledgedSequence, originalHeader.acknowledgedSequence);
    EXPECT_EQ(decodedHeader.selectiveAcknowledgementBits, originalHeader.selectiveAcknowledgementBits);
    EXPECT_EQ(decodedFragment.messageId, originalFragment.messageId);
    EXPECT_EQ(decodedFragment.fragmentIndex, originalFragment.fragmentIndex);
    EXPECT_EQ(decodedFragment.fragmentCount, originalFragment.fragmentCount);
    EXPECT_EQ(decodedFragment.payloadBytes, originalFragment.payloadBytes);
}

TEST(ReliablePacketWindowTest, ReliablePacketFragmentWireRejectsInvalidAndMalformedInputsAtomically) {
    const ReliablePacketHeader originalHeader{7U, 3U, 9U};
    const ReliablePayloadFragment originalFragment{17U, 1U, 2U, {0xAAU}};
    ReliablePacketHeader decodedHeader = originalHeader;
    ReliablePayloadFragment decodedFragment = originalFragment;
    EXPECT_FALSE(DeserializeReliablePacketFragment({1U, 2U, 3U}, decodedHeader, decodedFragment));
    EXPECT_EQ(decodedHeader.sequence, originalHeader.sequence);
    EXPECT_EQ(decodedFragment.payloadBytes, originalFragment.payloadBytes);

    std::vector<std::uint8_t> bytes{4U, 5U};
    EXPECT_FALSE(SerializeReliablePacketFragment(
        ReliablePacketHeader{0U, 3U, 0U}, originalFragment, bytes));
    EXPECT_EQ(bytes, (std::vector<std::uint8_t>{4U, 5U}));
    EXPECT_FALSE(SerializeReliablePacketFragment(
        originalHeader, ReliablePayloadFragment{17U, 2U, 2U, {0xAAU}}, bytes));
    EXPECT_EQ(bytes, (std::vector<std::uint8_t>{4U, 5U}));

    EXPECT_TRUE(SerializeReliablePacketFragment(originalHeader, originalFragment, bytes));
    bytes.pop_back();
    EXPECT_FALSE(DeserializeReliablePacketFragment(bytes, decodedHeader, decodedFragment));
    EXPECT_EQ(decodedHeader.sequence, originalHeader.sequence);
    EXPECT_EQ(decodedFragment.payloadBytes, originalFragment.payloadBytes);
}

TEST(ReliablePacketWindowTest, ReliablePayloadFragmentWireRoundTrip_IsLittleEndianAndExact) {
    const ReliablePayloadFragment original{0x12345678U, 2U, 3U, {0xAAU, 0xBBU}};
    std::vector<std::uint8_t> bytes;
    ASSERT_TRUE(SerializeReliablePayloadFragment(original, bytes));
    EXPECT_EQ(bytes, (std::vector<std::uint8_t>{0x78U, 0x56U, 0x34U, 0x12U,
                                                  0x02U, 0x00U, 0x00U, 0x00U,
                                                  0x03U, 0x00U, 0x00U, 0x00U,
                                                  0x02U, 0x00U, 0x00U, 0x00U,
                                                  0xAAU, 0xBBU}));
    ReliablePayloadFragment decoded;
    ASSERT_TRUE(DeserializeReliablePayloadFragment(bytes, decoded));
    EXPECT_EQ(decoded.messageId, original.messageId);
    EXPECT_EQ(decoded.fragmentIndex, original.fragmentIndex);
    EXPECT_EQ(decoded.fragmentCount, original.fragmentCount);
    EXPECT_EQ(decoded.payloadBytes, original.payloadBytes);
}

TEST(ReliablePacketWindowTest, ReliablePayloadFragmentWireRejectsInvalidAndMalformedInputsAtomically) {
    const ReliablePayloadFragment original{9U, 1U, 2U, {7U}};
    ReliablePayloadFragment decoded = original;
    EXPECT_FALSE(DeserializeReliablePayloadFragment({1U, 2U, 3U}, decoded));
    EXPECT_EQ(decoded.messageId, original.messageId);
    EXPECT_EQ(decoded.payloadBytes, original.payloadBytes);

    std::vector<std::uint8_t> bytes{4U, 5U};
    EXPECT_FALSE(SerializeReliablePayloadFragment(ReliablePayloadFragment{0U, 0U, 1U, {1U}}, bytes));
    EXPECT_EQ(bytes, (std::vector<std::uint8_t>{4U, 5U}));
    EXPECT_FALSE(SerializeReliablePayloadFragment(
        ReliablePayloadFragment{1U, 2U, 2U, {1U}}, bytes));
    EXPECT_EQ(bytes, (std::vector<std::uint8_t>{4U, 5U}));
    EXPECT_FALSE(SerializeReliablePayloadFragment(
        ReliablePayloadFragment{1U, 0U, 1U, std::vector<std::uint8_t>(kReliablePacketMaximumPayloadBytes + 1U, 0U)},
        bytes));
    EXPECT_EQ(bytes, (std::vector<std::uint8_t>{4U, 5U}));

    std::vector<std::uint8_t> malformed(16U, 0U);
    malformed[0] = 1U;
    malformed[8] = 1U;
    malformed[12] = 2U;
    EXPECT_FALSE(DeserializeReliablePayloadFragment(malformed, decoded));
    EXPECT_EQ(decoded.messageId, original.messageId);
    EXPECT_EQ(decoded.payloadBytes, original.payloadBytes);
}

TEST(ReliablePacketWindowTest, PlansReliablePayloadFragments_CalculatesBoundaries) {
    ReliablePayloadFragmentPlan plan{99U, 99U, 99U, true};
    ASSERT_TRUE(PlanReliablePayloadFragments(500U, 1200U, plan));
    EXPECT_EQ(plan.fragmentCount, 1U);
    EXPECT_EQ(plan.maximumFragmentBytes, 1200U);
    EXPECT_EQ(plan.finalFragmentBytes, 500U);
    EXPECT_FALSE(plan.fragmented);
    ASSERT_TRUE(PlanReliablePayloadFragments(2500U, 1000U, plan));
    EXPECT_EQ(plan.fragmentCount, 3U);
    EXPECT_EQ(plan.finalFragmentBytes, 500U);
    EXPECT_TRUE(plan.fragmented);
}

TEST(ReliablePacketWindowTest, PlansReliablePayloadFragments_RejectsInvalidAndOverCapInputsAtomically) {
    const ReliablePayloadFragmentPlan original{7U, 8U, 9U, true};
    ReliablePayloadFragmentPlan plan = original;
    EXPECT_FALSE(PlanReliablePayloadFragments(0U, 100U, plan));
    EXPECT_EQ(plan, original);
    EXPECT_FALSE(PlanReliablePayloadFragments(100U, 0U, plan));
    EXPECT_EQ(plan, original);
    EXPECT_FALSE(PlanReliablePayloadFragments(100U, kReliablePacketMaximumPayloadBytes + 1U, plan));
    EXPECT_EQ(plan, original);
    EXPECT_FALSE(PlanReliablePayloadFragments(2500U, 1000U, plan, 2U));
    EXPECT_EQ(plan, original);
    EXPECT_FALSE(PlanReliablePayloadFragments(1025U, 1U, plan, 2048U));
    EXPECT_EQ(plan, original);
}

TEST(ReliablePacketWindowTest, ValidateReliablePayloadBudget_AcceptsBoundedPayloads) {
    EXPECT_TRUE(ValidateReliablePayloadBudget(0U));
    EXPECT_TRUE(ValidateReliablePayloadBudget(kReliablePacketMaximumPayloadBytes));
    EXPECT_TRUE(ValidateReliablePayloadBudget(512U, 512U));
}
TEST(ReliablePacketWindowTest, ValidateReliablePayloadBudget_RejectsOversizedPayloads) {
    EXPECT_FALSE(ValidateReliablePayloadBudget(kReliablePacketMaximumPayloadBytes + 1U));
    EXPECT_FALSE(ValidateReliablePayloadBudget(1U, 0U));
    EXPECT_TRUE(ValidateReliablePayloadBudget(kReliablePacketMaximumPayloadBytes,
                                                 kReliablePacketMaximumPayloadBytes + 1U));
    EXPECT_FALSE(ValidateReliablePayloadBudget(kReliablePacketMaximumPayloadBytes + 1U,
                                                  kReliablePacketMaximumPayloadBytes + 1U));
}
TEST(ReliablePacketWindowTest, RetransmitPolicyClassifiesWaitingDueAndExhausted) {
    EXPECT_EQ(EvaluateReliableRetransmitPolicy({0.25F, 1.0F, 0U, 3U}), ReliableRetransmitStatus::Waiting);
    EXPECT_EQ(EvaluateReliableRetransmitPolicy({1.0F, 1.0F, 1U, 3U}), ReliableRetransmitStatus::Due);
    EXPECT_EQ(EvaluateReliableRetransmitPolicy({4.0F, 1.0F, 3U, 3U}), ReliableRetransmitStatus::Exhausted);
}
TEST(ReliablePacketWindowTest, RetransmitPolicyRejectsInvalidTimingAndRetryInputs) {
    EXPECT_EQ(EvaluateReliableRetransmitPolicy({-0.1F, 1.0F, 0U, 3U}), ReliableRetransmitStatus::Invalid);
    EXPECT_EQ(EvaluateReliableRetransmitPolicy({0.1F, 0.0F, 0U, 3U}), ReliableRetransmitStatus::Invalid);
    EXPECT_EQ(EvaluateReliableRetransmitPolicy({0.1F, 1.0F, 4U, 3U}), ReliableRetransmitStatus::Invalid);
}
TEST(ReliablePacketWindowTest, RetryTimeoutBackoffDoublesAndClamps) {
    float timeout = 99.0F;
    ASSERT_TRUE(ComputeReliableRetryTimeout(0.25F, 0U, 4.0F, timeout));
    EXPECT_FLOAT_EQ(timeout, 0.25F);
    ASSERT_TRUE(ComputeReliableRetryTimeout(0.25F, 3U, 4.0F, timeout));
    EXPECT_FLOAT_EQ(timeout, 2.0F);
    ASSERT_TRUE(ComputeReliableRetryTimeout(0.25F, 5U, 4.0F, timeout));
    EXPECT_FLOAT_EQ(timeout, 4.0F);
}

TEST(ReliablePacketWindowTest, RetryTimeoutBackoffRejectsInvalidInputsAtomically) {
    float timeout = 7.0F;
    EXPECT_FALSE(ComputeReliableRetryTimeout(0.0F, 0U, 4.0F, timeout));
    EXPECT_FLOAT_EQ(timeout, 7.0F);
    EXPECT_FALSE(ComputeReliableRetryTimeout(-0.1F, 0U, 4.0F, timeout));
    EXPECT_FLOAT_EQ(timeout, 7.0F);
    EXPECT_FALSE(ComputeReliableRetryTimeout(1.0F, 0U, 0.5F, timeout));
    EXPECT_FLOAT_EQ(timeout, 7.0F);
    EXPECT_FALSE(ComputeReliableRetryTimeout(1.0F, 32U, 4.0F, timeout));
    EXPECT_FLOAT_EQ(timeout, 7.0F);
    EXPECT_FALSE(ComputeReliableRetryTimeout(std::numeric_limits<float>::quiet_NaN(), 0U, 4.0F, timeout));
    EXPECT_FLOAT_EQ(timeout, 7.0F);
}

TEST(ReliablePacketWindowTest, RejectsInconsistentReceiveStateAtomically) {
    ReliableAcknowledgementState activeState{0U, 1U, true};
    EXPECT_EQ(AcceptReliableSequence(7U, activeState), ReliablePacketReceiveStatus::Invalid);
    EXPECT_EQ(activeState.latestReceivedSequence, 0U);
    EXPECT_EQ(activeState.receivedHistoryBits, 1U);
    EXPECT_TRUE(activeState.hasReceivedSequence);

    ReliableAcknowledgementState inactiveState{9U, 0U, false};
    EXPECT_EQ(AcceptReliableSequence(7U, inactiveState), ReliablePacketReceiveStatus::Invalid);
    EXPECT_EQ(inactiveState.latestReceivedSequence, 9U);
    EXPECT_EQ(inactiveState.receivedHistoryBits, 0U);
    EXPECT_FALSE(inactiveState.hasReceivedSequence);
}

TEST(ReliablePacketWindowTest, FirstSequenceIsAcceptedAndAdvertised) {
    ReliableAcknowledgementState state;
    EXPECT_EQ(AcceptReliableSequence(7U, state), ReliablePacketReceiveStatus::Accepted);
    EXPECT_TRUE(state.hasReceivedSequence);
    EXPECT_EQ(state.latestReceivedSequence, 7U);
    EXPECT_EQ(state.receivedHistoryBits, 0U);
}
TEST(ReliablePacketWindowTest, NewAndOutOfOrderSequencesUpdateBoundedHistory) {
    ReliableAcknowledgementState state;
    ASSERT_EQ(AcceptReliableSequence(10U, state), ReliablePacketReceiveStatus::Accepted);
    EXPECT_EQ(AcceptReliableSequence(12U, state), ReliablePacketReceiveStatus::Accepted);
    EXPECT_EQ(state.latestReceivedSequence, 12U);
    EXPECT_EQ(state.receivedHistoryBits, 2U);
    EXPECT_EQ(AcceptReliableSequence(11U, state), ReliablePacketReceiveStatus::Accepted);
    EXPECT_EQ(state.receivedHistoryBits, 3U);
    EXPECT_EQ(AcceptReliableSequence(11U, state), ReliablePacketReceiveStatus::Duplicate);
}
TEST(ReliablePacketWindowTest, ExactThirtyTwoSequenceAdvancePreservesOldestHistoryBit) {
    ReliableAcknowledgementState state;
    ASSERT_EQ(AcceptReliableSequence(100U, state), ReliablePacketReceiveStatus::Accepted);

    EXPECT_EQ(AcceptReliableSequence(132U, state), ReliablePacketReceiveStatus::Accepted);
    EXPECT_EQ(state.latestReceivedSequence, 132U);
    EXPECT_EQ(state.receivedHistoryBits, 0x80000000U);

    EXPECT_EQ(AcceptReliableSequence(100U, state), ReliablePacketReceiveStatus::Duplicate);
    EXPECT_EQ(state.receivedHistoryBits, 0x80000000U);
}

TEST(ReliablePacketWindowTest, ZeroAndTooOldSequencesAreRejected) {
    ReliableAcknowledgementState state;
    EXPECT_EQ(AcceptReliableSequence(0U, state), ReliablePacketReceiveStatus::Invalid);
    ASSERT_EQ(AcceptReliableSequence(100U, state), ReliablePacketReceiveStatus::Accepted);
    ASSERT_EQ(AcceptReliableSequence(133U, state), ReliablePacketReceiveStatus::Accepted);
    EXPECT_EQ(AcceptReliableSequence(100U, state), ReliablePacketReceiveStatus::TooOld);
    EXPECT_EQ(state.latestReceivedSequence, 133U);
}
TEST(ReliablePacketWindowTest, SequenceOrderingWrapsAcrossUint32Boundary) {
    ReliableAcknowledgementState state;
    ASSERT_EQ(AcceptReliableSequence(0xFFFFFFFEU, state), ReliablePacketReceiveStatus::Accepted);
    EXPECT_EQ(AcceptReliableSequence(1U, state), ReliablePacketReceiveStatus::Accepted);
    EXPECT_EQ(state.latestReceivedSequence, 1U);
    EXPECT_EQ(state.receivedHistoryBits, 4U);
}
TEST(ReliablePacketWindowTest, CumulativeAcknowledgementClearsPendingPrefix) {
    std::uint32_t pending = 0xFFFFFFFFU;
    const ReliablePacketHeader header{102U, 102U, 0U};
    EXPECT_TRUE(ApplyReliableAcknowledgements(header, pending, 100U));
    EXPECT_EQ(pending, 0xFFFFFFF8U);
}
TEST(ReliablePacketWindowTest, CumulativeAcknowledgementClearsFullThirtyTwoEntryWindow) {
    std::uint32_t pending = 0xFFFFFFFFU;
    const ReliablePacketHeader header{132U, 131U, 0U};
    EXPECT_TRUE(ApplyReliableAcknowledgements(header, pending, 100U));
    EXPECT_EQ(pending, 0U);
}
TEST(ReliablePacketWindowTest, CumulativeAcknowledgementBeyondThirtyTwoClearsAllPendingBits) {
    std::uint32_t pending = 0xFFFFFFFFU;
    const ReliablePacketHeader header{150U, 140U, 0U};
    EXPECT_TRUE(ApplyReliableAcknowledgements(header, pending, 100U));
    EXPECT_EQ(pending, 0U);
}
TEST(ReliablePacketWindowTest, SelectiveAcknowledgementClearsForwardSlots) {
    std::uint32_t pending = 0xFFFFFFFFU;
    const ReliablePacketHeader header{200U, 200U, (1U << 1U) | (1U << 4U)};
    EXPECT_TRUE(ApplyReliableAcknowledgements(header, pending, 200U));
    EXPECT_EQ(pending, ~(1U << 0U | 1U << 2U | 1U << 5U));
}
TEST(ReliablePacketWindowTest, SelectiveAcknowledgementRejectsReservedZeroAfterUint32Wrap) {
    std::uint32_t pending = 0xFFFFFFFFU;
    const ReliablePacketHeader header{5U, 0xFFFFFFFEU, (1U << 0U) | (1U << 1U)};
    EXPECT_TRUE(ApplyReliableAcknowledgements(header, pending, 0xFFFFFFFEU));
    EXPECT_EQ(pending, ~((1U << 0U) | (1U << 1U)));

    pending = 0xFFFFFFFEU;
    const ReliablePacketHeader reservedOnly{5U, 0xFFFFFFFEU, (1U << 1U)};
    EXPECT_FALSE(ApplyReliableAcknowledgements(reservedOnly, pending, 0xFFFFFFFEU));
    EXPECT_EQ(pending, 0xFFFFFFFEU);
}

TEST(ReliablePacketWindowTest, SelectiveAcknowledgementAcceptsValidNearBoundarySequence) {
    std::uint32_t pending = 1U << 2U;
    const ReliablePacketHeader header{5U, 0xFFFFFFFDU, 1U << 1U};
    EXPECT_TRUE(ApplyReliableAcknowledgements(header, pending, 0xFFFFFFFDU));
    EXPECT_EQ(pending, 0U);
}

TEST(ReliablePacketWindowTest, InvalidAcknowledgementDoesNotMutatePendingMask) {
    std::uint32_t pending = 0xA5A5A5A5U;
    const ReliablePacketHeader header{1U, 0U, 0xFFFFFFFFU};
    EXPECT_FALSE(ApplyReliableAcknowledgements(header, pending, 1U));
    EXPECT_EQ(pending, 0xA5A5A5A5U);
}
TEST(ReliablePacketWindowTest, AcknowledgementRejectsReservedZeroPacketSequenceAtomically) {
    std::uint32_t pending = 0xA5A5A5A5U;
    const ReliablePacketHeader header{0U, 8U, 0xFFFFFFFFU};
    EXPECT_FALSE(ApplyReliableAcknowledgements(header, pending, 1U));
    EXPECT_EQ(pending, 0xA5A5A5A5U);
}

TEST(ReliablePacketWindowTest, ReliableRetrySchedule_AccumulatesCommitsAndExhausts) {
    ReliableRetrySchedule schedule;
    ASSERT_TRUE(schedule.Configure(0.5F, 2.0F, 2U));
    EXPECT_EQ(schedule.GetRetryCount(), 0U);
    EXPECT_EQ(schedule.Advance(0.25F), ReliableRetransmitStatus::Waiting);
    EXPECT_EQ(schedule.Advance(0.25F), ReliableRetransmitStatus::Due);
    ASSERT_TRUE(schedule.CommitRetry());
    EXPECT_EQ(schedule.GetRetryCount(), 1U);
    EXPECT_FLOAT_EQ(schedule.GetElapsedSeconds(), 0.0F);
    EXPECT_EQ(schedule.Advance(0.99F), ReliableRetransmitStatus::Waiting);
    EXPECT_EQ(schedule.Advance(0.01F), ReliableRetransmitStatus::Due);
    ASSERT_TRUE(schedule.CommitRetry());
    EXPECT_EQ(schedule.GetRetryCount(), 2U);
    EXPECT_EQ(schedule.Advance(2.0F), ReliableRetransmitStatus::Exhausted);
    EXPECT_FALSE(schedule.CommitRetry());
    schedule.Reset();
    EXPECT_FALSE(schedule.IsConfigured());
    EXPECT_EQ(schedule.GetRetryCount(), 0U);
}

TEST(ReliablePacketWindowTest, ReliableRetrySchedule_RejectsInvalidInputsAtomically) {
    ReliableRetrySchedule schedule;
    ASSERT_TRUE(schedule.Configure(0.5F, 2.0F, 3U));
    const ReliableRetrySchedule original = schedule;
    EXPECT_EQ(schedule.Advance(-0.1F), ReliableRetransmitStatus::Invalid);
    EXPECT_EQ(schedule.GetElapsedSeconds(), original.GetElapsedSeconds());
    EXPECT_EQ(schedule.GetRetryCount(), original.GetRetryCount());
    EXPECT_FALSE(schedule.Configure(0.0F, 2.0F, 3U));
    EXPECT_FALSE(schedule.Configure(0.5F, 0.25F, 3U));
    EXPECT_FALSE(schedule.Configure(0.5F, 2.0F, 32U));
    EXPECT_EQ(schedule.GetRetryCount(), original.GetRetryCount());
    EXPECT_TRUE(schedule.IsConfigured());
    EXPECT_EQ(schedule.Advance(std::numeric_limits<float>::max()), ReliableRetransmitStatus::Due);
    const float finiteElapsed = schedule.GetElapsedSeconds();
    EXPECT_EQ(schedule.Advance(std::numeric_limits<float>::max()), ReliableRetransmitStatus::Invalid);
    EXPECT_EQ(schedule.GetElapsedSeconds(), finiteElapsed);
}

TEST(ReliablePacketWindowTest, ReliablePacketHeaderWireRoundTrip_IsLittleEndianAndExactSize) {
    const ReliablePacketHeader original{0x12345678U, 0x90ABCDEFU, 0x01020304U};
    std::vector<std::uint8_t> bytes;
    ASSERT_TRUE(SerializeReliablePacketHeader(original, bytes));
    EXPECT_EQ(bytes, (std::vector<std::uint8_t>{0x78U, 0x56U, 0x34U, 0x12U,
                                                  0xEFU, 0xCDU, 0xABU, 0x90U,
                                                  0x04U, 0x03U, 0x02U, 0x01U}));
    ReliablePacketHeader decoded;
    ASSERT_TRUE(DeserializeReliablePacketHeader(bytes, decoded));
    EXPECT_EQ(decoded.sequence, original.sequence);
    EXPECT_EQ(decoded.acknowledgedSequence, original.acknowledgedSequence);
    EXPECT_EQ(decoded.selectiveAcknowledgementBits, original.selectiveAcknowledgementBits);
}

TEST(ReliablePacketWindowTest, ReliablePacketHeaderWireRejectsInvalidAndMalformedInputsAtomically) {
    const ReliablePacketHeader original{7U, 8U, 9U};
    ReliablePacketHeader decoded = original;
    EXPECT_FALSE(DeserializeReliablePacketHeader({1U, 2U, 3U}, decoded));
    EXPECT_EQ(decoded.sequence, original.sequence);
    EXPECT_EQ(decoded.acknowledgedSequence, original.acknowledgedSequence);
    EXPECT_EQ(decoded.selectiveAcknowledgementBits, original.selectiveAcknowledgementBits);

    std::vector<std::uint8_t> bytes{5U, 6U};
    EXPECT_FALSE(SerializeReliablePacketHeader(ReliablePacketHeader{0U, 2U, 3U}, bytes));
    EXPECT_EQ(bytes, (std::vector<std::uint8_t>{5U, 6U}));
    EXPECT_FALSE(SerializeReliablePacketHeader(ReliablePacketHeader{1U, 0U, 3U}, bytes));
    EXPECT_EQ(bytes, (std::vector<std::uint8_t>{5U, 6U}));
    EXPECT_FALSE(DeserializeReliablePacketHeader(
        std::vector<std::uint8_t>{0U, 0U, 0U, 0U, 2U, 0U, 0U, 0U, 3U, 0U, 0U, 0U}, decoded));
    EXPECT_EQ(decoded.sequence, original.sequence);
    EXPECT_FALSE(DeserializeReliablePacketHeader(
        std::vector<std::uint8_t>{1U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 3U, 0U, 0U, 0U}, decoded));
    EXPECT_EQ(decoded.acknowledgedSequence, original.acknowledgedSequence);
}

TEST(ReliablePacketWindowTest, ReassemblesOutOfOrderFragmentsAndResetsState) {
    ReliablePayloadReassemblyState state;
    std::vector<std::uint8_t> payload;

    EXPECT_EQ(AcceptReliablePayloadFragment(17U, 1U, 2U, {3U, 4U}, state, payload),
              ReliablePayloadReassemblyStatus::Accepted);
    EXPECT_TRUE(state.IsActive());
    EXPECT_EQ(state.receivedFragmentCount, 1U);
    EXPECT_EQ(AcceptReliablePayloadFragment(17U, 0U, 2U, {1U, 2U}, state, payload),
              ReliablePayloadReassemblyStatus::Complete);
    EXPECT_EQ(payload, (std::vector<std::uint8_t>{1U, 2U, 3U, 4U}));
    EXPECT_FALSE(state.IsActive());
    EXPECT_EQ(state.receivedFragmentCount, 0U);
}

TEST(ReliablePacketWindowTest, DuplicateFragmentIsIdempotentAndConflictDoesNotMutateState) {
    ReliablePayloadReassemblyState state;
    std::vector<std::uint8_t> payload;
    ASSERT_EQ(AcceptReliablePayloadFragment(19U, 0U, 2U, {7U}, state, payload),
              ReliablePayloadReassemblyStatus::Accepted);

    EXPECT_EQ(AcceptReliablePayloadFragment(19U, 0U, 2U, {7U}, state, payload),
              ReliablePayloadReassemblyStatus::Duplicate);
    EXPECT_EQ(AcceptReliablePayloadFragment(19U, 0U, 2U, {8U}, state, payload),
              ReliablePayloadReassemblyStatus::Conflict);
    EXPECT_EQ(state.receivedFragmentCount, 1U);
    EXPECT_TRUE(payload.empty());
}

TEST(ReliablePacketWindowTest, ReassemblyRejectsInvalidBoundsAndMessageConflicts) {
    ReliablePayloadReassemblyState state;
    std::vector<std::uint8_t> payload{9U};

    EXPECT_EQ(AcceptReliablePayloadFragment(0U, 0U, 1U, {1U}, state, payload),
              ReliablePayloadReassemblyStatus::Invalid);
    EXPECT_EQ(AcceptReliablePayloadFragment(20U, 2U, 2U, {1U}, state, payload),
              ReliablePayloadReassemblyStatus::Invalid);
    EXPECT_EQ(AcceptReliablePayloadFragment(20U, 0U, kReliablePacketMaximumFragmentCount + 1U, {1U},
                                               state, payload),
              ReliablePayloadReassemblyStatus::Invalid);
    EXPECT_EQ(AcceptReliablePayloadFragment(20U, 0U, 2U, {1U}, state, payload),
              ReliablePayloadReassemblyStatus::Accepted);
    EXPECT_EQ(AcceptReliablePayloadFragment(21U, 1U, 2U, {2U}, state, payload),
              ReliablePayloadReassemblyStatus::Conflict);
    EXPECT_EQ(state.messageId, 20U);
    EXPECT_EQ(state.receivedFragmentCount, 1U);
    EXPECT_EQ(payload, (std::vector<std::uint8_t>{9U}));
}

TEST(ReliablePacketWindowTest, ReassemblyRejectsMalformedInactiveStateAtomically) {
    ReliablePayloadReassemblyState state;
    state.fragmentCount = 1U;
    state.receivedByteCount = 1U;
    state.fragments.resize(1U);
    state.fragments[0U] = {7U};
    std::vector<std::uint8_t> payload{9U};

    EXPECT_EQ(AcceptReliablePayloadFragment(25U, 0U, 1U, {1U}, state, payload),
              ReliablePayloadReassemblyStatus::Invalid);
    EXPECT_EQ(state.messageId, 0U);
    EXPECT_EQ(state.fragmentCount, 1U);
    EXPECT_EQ(state.receivedByteCount, 1U);
    EXPECT_EQ(state.fragments[0U], (std::vector<std::uint8_t>{7U}));
    EXPECT_EQ(payload, (std::vector<std::uint8_t>{9U}));
}

TEST(ReliablePacketWindowTest, ReassemblyRejectsOversizedStoredFragmentAtomically) {
    ReliablePayloadReassemblyState state;
    state.messageId = 26U;
    state.fragmentCount = 2U;
    state.receivedFragmentCount = 1U;
    state.receivedByteCount = kReliablePacketMaximumPayloadBytes + 1U;
    state.fragments.resize(2U);
    state.fragments[0U].resize(kReliablePacketMaximumPayloadBytes + 1U, 7U);
    std::vector<std::uint8_t> payload{9U};

    EXPECT_EQ(AcceptReliablePayloadFragment(26U, 1U, 2U, {2U}, state, payload),
              ReliablePayloadReassemblyStatus::Invalid);
    EXPECT_EQ(state.messageId, 26U);
    EXPECT_EQ(state.receivedFragmentCount, 1U);
    EXPECT_EQ(state.receivedByteCount, kReliablePacketMaximumPayloadBytes + 1U);
    EXPECT_EQ(state.fragments[0U].size(), kReliablePacketMaximumPayloadBytes + 1U);
    EXPECT_TRUE(state.fragments[1U].empty());
    EXPECT_EQ(payload, (std::vector<std::uint8_t>{9U}));
}

TEST(ReliablePacketWindowTest, ReassemblyRejectsOverBudgetAggregateStateAtomically) {
    ReliablePayloadReassemblyState state;
    state.messageId = 23U;
    state.fragmentCount = 2U;
    state.receivedFragmentCount = 1U;
    state.receivedByteCount = kReliablePacketMaximumReassembledPayloadBytes;
    state.fragments.resize(2U);
    state.fragments[0U] = {1U};
    std::vector<std::uint8_t> payload{9U};

    EXPECT_EQ(AcceptReliablePayloadFragment(23U, 1U, 2U, {2U}, state, payload),
              ReliablePayloadReassemblyStatus::Invalid);
    EXPECT_EQ(state.receivedByteCount, kReliablePacketMaximumReassembledPayloadBytes);
    EXPECT_EQ(state.receivedFragmentCount, 1U);
    EXPECT_TRUE(state.fragments[1U].empty());
    EXPECT_EQ(payload, (std::vector<std::uint8_t>{9U}));
}

TEST(ReliablePacketWindowTest, ReassemblyRejectsMalformedActiveAccountingAtomically) {
    ReliablePayloadReassemblyState state;
    state.messageId = 24U;
    state.fragmentCount = 2U;
    state.receivedFragmentCount = 2U;
    state.receivedByteCount = 1U;
    state.fragments.resize(2U);
    state.fragments[0U] = {1U};
    std::vector<std::uint8_t> payload{9U};

    EXPECT_EQ(AcceptReliablePayloadFragment(24U, 1U, 2U, {2U}, state, payload),
              ReliablePayloadReassemblyStatus::Invalid);
    EXPECT_EQ(state.messageId, 24U);
    EXPECT_EQ(state.receivedFragmentCount, 2U);
    EXPECT_EQ(state.receivedByteCount, 1U);
    EXPECT_EQ(state.fragments[0U], (std::vector<std::uint8_t>{1U}));
    EXPECT_TRUE(state.fragments[1U].empty());
    EXPECT_EQ(payload, (std::vector<std::uint8_t>{9U}));
}

TEST(ReliablePacketWindowTest, SingleFragmentPublishesCopiedPayload) {
    ReliablePayloadReassemblyState state;
    std::vector<std::uint8_t> payload;
    const std::vector<std::uint8_t> fragment{4U, 5U, 6U};

    EXPECT_EQ(AcceptReliablePayloadFragment(22U, 0U, 1U, fragment, state, payload),
              ReliablePayloadReassemblyStatus::Complete);
    EXPECT_EQ(payload, fragment);
    EXPECT_FALSE(state.IsActive());
}

} // namespace
} // namespace UVE::Network::Tests
