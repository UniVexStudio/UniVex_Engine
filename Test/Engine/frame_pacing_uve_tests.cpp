// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/core/frame_pacing_uve.h"

#include <chrono>
#include <cstdint>
#include <optional>

#include <gtest/gtest.h>

namespace UVE::Core::Tests {
namespace {

using Clock = FramePacingDecisionUVE::Clock;

TEST(FramePacingUVETest, SelectsFocusedAndUnfocusedCapsAndLeavesHeadlessRuntimeUncapped) {
    EXPECT_EQ(SelectFrameRateCapUVE(true, true, 60U, 15U), 60U);
    EXPECT_EQ(SelectFrameRateCapUVE(true, false, 60U, 15U), 15U);
    EXPECT_EQ(SelectFrameRateCapUVE(false, true, 60U, 15U), 0U);
}

TEST(FramePacingUVETest, FirstFrameEstablishesPeriodWithoutSleeping) {
    const Clock::time_point now{};
    const FramePacingDecisionUVE decision =
        ComputeFramePacingDecisionUVE(60U, 0U, std::nullopt, now);

    EXPECT_EQ(decision.frameRateCap, 60U);
    EXPECT_EQ(decision.framePeriod,
              std::chrono::duration_cast<Clock::duration>(std::chrono::duration<double>{1.0 / 60.0}));
    EXPECT_FALSE(decision.sleepUntil.has_value());
}

TEST(FramePacingUVETest, WaitsUntilTheSameCapDeadlineButNotAtOrAfterIt) {
    const Clock::time_point start{};
    const auto period = std::chrono::duration_cast<Clock::duration>(
        std::chrono::duration<double>{1.0 / 60.0});
    const Clock::time_point deadline = start + period;

    const FramePacingDecisionUVE early = ComputeFramePacingDecisionUVE(
        60U, 60U, deadline, start + std::chrono::milliseconds{5});
    ASSERT_TRUE(early.sleepUntil.has_value());
    EXPECT_EQ(*early.sleepUntil, deadline);

    const FramePacingDecisionUVE onTime =
        ComputeFramePacingDecisionUVE(60U, 60U, deadline, deadline);
    EXPECT_FALSE(onTime.sleepUntil.has_value());

    const FramePacingDecisionUVE late = ComputeFramePacingDecisionUVE(
        60U, 60U, deadline, deadline + std::chrono::milliseconds{1});
    EXPECT_FALSE(late.sleepUntil.has_value());
}

TEST(FramePacingUVETest, CapChangeStartsANewPacingWindowInsteadOfUsingStaleDeadline) {
    const Clock::time_point start{};
    const Clock::time_point staleDeadline = start + std::chrono::seconds{1};

    const FramePacingDecisionUVE decision = ComputeFramePacingDecisionUVE(
        30U, 60U, staleDeadline, start + std::chrono::milliseconds{1});

    EXPECT_EQ(decision.frameRateCap, 30U);
    EXPECT_EQ(decision.framePeriod,
              std::chrono::duration_cast<Clock::duration>(std::chrono::duration<double>{1.0 / 30.0}));
    EXPECT_FALSE(decision.sleepUntil.has_value());
}

TEST(FramePacingUVETest, ZeroOrInvalidCapDisablesPacing) {
    const Clock::time_point now{};
    const Clock::time_point deadline = now + std::chrono::seconds{1};

    const FramePacingDecisionUVE uncapped = ComputeFramePacingDecisionUVE(0U, 60U, deadline, now);
    EXPECT_EQ(uncapped.frameRateCap, 0U);
    EXPECT_EQ(uncapped.framePeriod, Clock::duration{});
    EXPECT_FALSE(uncapped.sleepUntil.has_value());

    const FramePacingDecisionUVE invalid = ComputeFramePacingDecisionUVE(
        kMaximumFrameRateCapUVE + 1U, 0U, std::nullopt, now);
    EXPECT_EQ(invalid.frameRateCap, 0U);
    EXPECT_FALSE(invalid.sleepUntil.has_value());
}

} // namespace
} // namespace UVE::Core::Tests
