// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "frame_pacing.h"

#include <chrono>
#include <cstdint>
#include <optional>

#include <gtest/gtest.h>

namespace UVE::Core::Tests {
namespace {

using Clock = FramePacingDecision::Clock;

TEST(FramePacingTest, SelectsFocusedAndUnfocusedCapsAndLeavesHeadlessRuntimeUncapped) {
    EXPECT_EQ(SelectFrameRateCap(true, true, 60U, 15U), 60U);
    EXPECT_EQ(SelectFrameRateCap(true, false, 60U, 15U), 15U);
    EXPECT_EQ(SelectFrameRateCap(false, true, 60U, 15U), 0U);
}

TEST(FramePacingTest, FirstFrameEstablishesPeriodWithoutSleeping) {
    const Clock::time_point now{};
    const FramePacingDecision decision =
        ComputeFramePacingDecision(60U, 0U, std::nullopt, now);

    EXPECT_EQ(decision.frameRateCap, 60U);
    EXPECT_EQ(decision.framePeriod,
              std::chrono::duration_cast<Clock::duration>(std::chrono::duration<double>{1.0 / 60.0}));
    EXPECT_FALSE(decision.sleepUntil.has_value());
}

TEST(FramePacingTest, WaitsUntilTheSameCapDeadlineButNotAtOrAfterIt) {
    const Clock::time_point start{};
    const auto period = std::chrono::duration_cast<Clock::duration>(
        std::chrono::duration<double>{1.0 / 60.0});
    const Clock::time_point deadline = start + period;

    const FramePacingDecision early = ComputeFramePacingDecision(
        60U, 60U, deadline, start + std::chrono::milliseconds{5});
    ASSERT_TRUE(early.sleepUntil.has_value());
    EXPECT_EQ(*early.sleepUntil, deadline);

    const FramePacingDecision onTime =
        ComputeFramePacingDecision(60U, 60U, deadline, deadline);
    EXPECT_FALSE(onTime.sleepUntil.has_value());

    const FramePacingDecision late = ComputeFramePacingDecision(
        60U, 60U, deadline, deadline + std::chrono::milliseconds{1});
    EXPECT_FALSE(late.sleepUntil.has_value());
}

TEST(FramePacingTest, CapChangeStartsANewPacingWindowInsteadOfUsingStaleDeadline) {
    const Clock::time_point start{};
    const Clock::time_point staleDeadline = start + std::chrono::seconds{1};

    const FramePacingDecision decision = ComputeFramePacingDecision(
        30U, 60U, staleDeadline, start + std::chrono::milliseconds{1});

    EXPECT_EQ(decision.frameRateCap, 30U);
    EXPECT_EQ(decision.framePeriod,
              std::chrono::duration_cast<Clock::duration>(std::chrono::duration<double>{1.0 / 30.0}));
    EXPECT_FALSE(decision.sleepUntil.has_value());
}

TEST(FramePacingTest, ZeroOrInvalidCapDisablesPacing) {
    const Clock::time_point now{};
    const Clock::time_point deadline = now + std::chrono::seconds{1};

    const FramePacingDecision uncapped = ComputeFramePacingDecision(0U, 60U, deadline, now);
    EXPECT_EQ(uncapped.frameRateCap, 0U);
    EXPECT_EQ(uncapped.framePeriod, Clock::duration{});
    EXPECT_FALSE(uncapped.sleepUntil.has_value());

    const FramePacingDecision invalid = ComputeFramePacingDecision(
        kMaximumFrameRateCap + 1U, 0U, std::nullopt, now);
    EXPECT_EQ(invalid.frameRateCap, 0U);
    EXPECT_FALSE(invalid.sleepUntil.has_value());
}

} // namespace
} // namespace UVE::Core::Tests
