// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#include "timer.h"

#include <chrono>
#include <limits>
#include <thread>

#include <gtest/gtest.h>

namespace UVE::Utilities::Tests {
namespace {

TEST(TimerTest, InitialState_ZeroTotalTime) {
    const Timer timer;
    EXPECT_DOUBLE_EQ(timer.GetTotalTime(), 0.0);
}

TEST(TimerTest, Tick_ProducesPositiveDelta) {
    Timer timer;
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
    timer.Tick();
    EXPECT_GT(timer.GetDeltaTime(), 0.0);
}

TEST(TimerTest, Tick_AccumulatesTotalTime) {
    Timer timer;
    for (int i = 0; i < 3; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
        timer.Tick();
    }
    EXPECT_GT(timer.GetTotalTime(), 0.001);
}

TEST(TimerTest, DeltaTime_ClampedToConfiguredMax) {
    Timer timer;
    timer.SetMaxDeltaTime(0.01);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    timer.Tick();
    EXPECT_LE(timer.GetDeltaTime(), 0.01 + 1e-6);
}

TEST(TimerTest, InvalidTimingConfigurationPreservesLastValidValues) {
    Timer timer;
    timer.SetMaxDeltaTime(0.01);
    timer.SetFixedTimestep(0.01);
    timer.SetMaxDeltaTime(0.0);
    timer.SetMaxDeltaTime(-1.0);
    timer.SetMaxDeltaTime(std::numeric_limits<double>::quiet_NaN());
    timer.SetMaxDeltaTime(std::numeric_limits<double>::infinity());
    timer.SetFixedTimestep(0.0);
    timer.SetFixedTimestep(-1.0);
    timer.SetFixedTimestep(std::numeric_limits<double>::quiet_NaN());
    timer.SetFixedTimestep(std::numeric_limits<double>::infinity());

    std::this_thread::sleep_for(std::chrono::milliseconds(35));
    timer.Tick();
    EXPECT_LE(timer.GetDeltaTime(), 0.01 + 1e-6);
    const FixedStepResult result = timer.AdvanceFixedStep();
    EXPECT_EQ(result.stepsToRun, 1);
}

TEST(TimerTest, Reset_ZerosTotalTime) {
    Timer timer;
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
    timer.Tick();
    ASSERT_GT(timer.GetTotalTime(), 0.0);

    timer.Reset();
    EXPECT_DOUBLE_EQ(timer.GetTotalTime(), 0.0);
}

TEST(TimerTest, GetDeltaTimeFloat_MatchesDoubleValue) {
    Timer timer;
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
    timer.Tick();
    EXPECT_FLOAT_EQ(timer.GetDeltaTimeFloat(), static_cast<float>(timer.GetDeltaTime()));
}

TEST(TimerTest, FixedStep_AccumulatesWholeStepsOnly) {
    Timer timer;
    timer.SetFixedTimestep(0.01); // 100 Hz
    std::this_thread::sleep_for(std::chrono::milliseconds(35));
    timer.Tick();

    const FixedStepResult result = timer.AdvanceFixedStep();
    EXPECT_GE(result.stepsToRun, 2);
    EXPECT_LE(result.stepsToRun, 4);
    EXPECT_GE(result.alpha, 0.0);
    EXPECT_LT(result.alpha, 1.0);
}

TEST(TimerTest, FixedStep_CapsStepsAtMaximum) {
    Timer timer;
    timer.SetMaxDeltaTime(10.0); // disable the per-tick clamp for this test
    timer.SetFixedTimestep(0.001);
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    timer.Tick();

    const FixedStepResult result = timer.AdvanceFixedStep();
    EXPECT_LE(result.stepsToRun, 8); // Timer's default cap
}

TEST(TimerTest, FixedStep_CapIsConfigurableAndIgnoresNonPositiveValues) {
    Timer timer;
    timer.SetMaxDeltaTime(10.0);
    timer.SetFixedTimestep(0.001);
    timer.SetMaxStepsPerTick(3);
    timer.SetMaxStepsPerTick(0);  // ignored
    timer.SetMaxStepsPerTick(-5); // ignored
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    timer.Tick();

    // 50 ms of 1 ms steps is far more than the cap, so the cap is what comes back.
    const FixedStepResult result = timer.AdvanceFixedStep();
    EXPECT_EQ(result.stepsToRun, 3);
    EXPECT_LT(result.alpha, 1.0);
}

TEST(TimerTest, DiscardFixedStepAccumulator_PreservesWallClockTimeAndDropsPendingSteps) {
    Timer timer;
    timer.SetFixedTimestep(0.001);
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    timer.Tick();
    const double totalBeforeDiscard = timer.GetTotalTime();
    const double deltaBeforeDiscard = timer.GetDeltaTime();

    timer.DiscardFixedStepAccumulator();
    const FixedStepResult result = timer.AdvanceFixedStep();
    EXPECT_EQ(result.stepsToRun, 0);
    EXPECT_DOUBLE_EQ(timer.GetTotalTime(), totalBeforeDiscard);
    EXPECT_DOUBLE_EQ(timer.GetDeltaTime(), deltaBeforeDiscard);
}

} // namespace
} // namespace UVE::Utilities::Tests
