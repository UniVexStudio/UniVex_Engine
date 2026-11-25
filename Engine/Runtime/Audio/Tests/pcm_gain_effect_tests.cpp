// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
#include "pcm_gain_effect.h"
#include <gtest/gtest.h>
#include <limits>
namespace UVE::Audio::Tests {
namespace {
TEST(PcmGainEffectTest, PcmGainEffectSchedule_AppliesQueuedWindowsAndConsumesOnSuccess) {
    PcmGainEffectSchedule schedule;
    ASSERT_TRUE(schedule.ScheduleWindow({0U, 3U, 2.0F}));
    ASSERT_TRUE(schedule.ScheduleWindow({1U, 1U, 0.5F}));
    EXPECT_EQ(schedule.GetPendingWindowCount(), 2U);
    const std::vector<float> input{-0.5F, 0.25F, 0.75F, -0.25F};
    std::vector<float> output;
    ASSERT_TRUE(schedule.ApplyPending(input, output));
    EXPECT_EQ(output, (std::vector<float>{-1.0F, 0.25F, 1.0F, -0.25F}));
    EXPECT_EQ(schedule.GetPendingWindowCount(), 0U);
}

TEST(PcmGainEffectTest, PcmGainEffectSchedule_RetainsQueuedWindowsWhenApplicationFails) {
    PcmGainEffectSchedule schedule;
    ASSERT_TRUE(schedule.ScheduleWindow({1U, 2U, 2.0F}));
    const std::vector<float> original{0.75F};
    std::vector<float> output = original;
    EXPECT_FALSE(schedule.ApplyPending({0.5F}, output));
    EXPECT_EQ(output, original);
    EXPECT_EQ(schedule.GetPendingWindowCount(), 1U);
}

TEST(PcmGainEffectTest, PcmGainEffectSchedule_RejectsInvalidWindowsAndQueueOverflow) {
    PcmGainEffectSchedule schedule;
    EXPECT_FALSE(schedule.ScheduleWindow({0U, 0U, 1.0F}));
    EXPECT_FALSE(schedule.ScheduleWindow({0U, 1U, -1.0F}));
    EXPECT_FALSE(schedule.ScheduleWindow({0U, 1U, std::numeric_limits<float>::quiet_NaN()}));
    for (std::size_t i = 0U; i < kMaximumPcmGainScheduledWindows; ++i) {
        ASSERT_TRUE(schedule.ScheduleWindow({0U, 1U, 1.0F}));
    }
    EXPECT_FALSE(schedule.ScheduleWindow({0U, 1U, 1.0F}));
    EXPECT_EQ(schedule.GetPendingWindowCount(), kMaximumPcmGainScheduledWindows);
}

TEST(PcmGainEffectTest, AppliesOrderedGainChainAndClampsEachStage) {
    const std::vector<float> input{-0.75F, 0.25F, 0.9F};
    std::vector<float> output;
    ASSERT_TRUE(ApplyPcmGainEffectChain(input, {2.0F, 0.5F}, output));
    EXPECT_FLOAT_EQ(output[0], -0.5F);
    EXPECT_FLOAT_EQ(output[1], 0.25F);
    EXPECT_FLOAT_EQ(output[2], 0.5F);
}

TEST(PcmGainEffectTest, EmptyGainChainCopiesFiniteInput) {
    const std::vector<float> input{-0.5F, 0.25F};
    std::vector<float> output{0.75F};
    ASSERT_TRUE(ApplyPcmGainEffectChain(input, {}, output));
    EXPECT_EQ(output, input);
}

TEST(PcmGainEffectTest, InvalidChainStageOrCountLeavesOutputUnchanged) {
    const std::vector<float> input{0.5F};
    std::vector<float> output{0.25F};
    EXPECT_FALSE(ApplyPcmGainEffectChain(input, {-1.0F}, output));
    EXPECT_EQ(output, std::vector<float>{0.25F});
    EXPECT_FALSE(ApplyPcmGainEffectChain(input, {std::numeric_limits<float>::quiet_NaN()}, output));
    EXPECT_EQ(output, std::vector<float>{0.25F});
    EXPECT_FALSE(ApplyPcmGainEffectChain(input, std::vector<float>(kMaximumPcmGainChainEffects + 1U, 1.0F), output));
    EXPECT_EQ(output, std::vector<float>{0.25F});
}

TEST(PcmGainEffectTest, ApplyScheduledPcmGainEffects_AppliesOrderedWindows) {
    const std::vector<float> input{-0.5F, 0.25F, 0.75F, -0.25F};
    std::vector<float> output;
    ASSERT_TRUE(ApplyScheduledPcmGainEffects(
        input, {{1U, 2U, 2.0F}, {2U, 1U, 0.5F}}, output));
    EXPECT_EQ(output, (std::vector<float>{-0.5F, 0.5F, 0.5F, -0.25F}));
    ASSERT_TRUE(ApplyScheduledPcmGainEffects(
        input, {{0U, 3U, 3.0F}}, output));
    EXPECT_EQ(output, (std::vector<float>{-1.0F, 0.75F, 1.0F, -0.25F}));
}

TEST(PcmGainEffectTest, ApplyScheduledPcmGainEffects_RejectsInvalidWindowsAtomically) {
    const std::vector<float> input{0.5F, -0.25F};
    const std::vector<float> original{0.75F};
    std::vector<float> output = original;
    EXPECT_FALSE(ApplyScheduledPcmGainEffects(input, {{0U, 0U, 1.0F}}, output));
    EXPECT_EQ(output, original);
    EXPECT_FALSE(ApplyScheduledPcmGainEffects(input, {{1U, 2U, 1.0F}}, output));
    EXPECT_EQ(output, original);
    EXPECT_FALSE(ApplyScheduledPcmGainEffects(input, {{0U, 1U, -1.0F}}, output));
    EXPECT_EQ(output, original);
    EXPECT_FALSE(ApplyScheduledPcmGainEffects(
        input, std::vector<PcmGainEffectWindow>(kMaximumPcmGainChainEffects + 1U, {0U, 1U, 1.0F}), output));
    EXPECT_EQ(output, original);
}

TEST(PcmGainEffectTest, AppliesGainAndClampsNormalizedSamples) {
    const std::vector<float> input{-0.75F, 0.25F, 0.9F};
    std::vector<float> output;
    ASSERT_TRUE(ApplyPcmGainEffect(input, 2.0F, output));
    EXPECT_FLOAT_EQ(output[0], -1.0F);
    EXPECT_FLOAT_EQ(output[1], 0.5F);
    EXPECT_FLOAT_EQ(output[2], 1.0F);
}
TEST(PcmGainEffectTest, InvalidGainOrSampleLeavesOutputUnchanged) {
    const std::vector<float> input{0.5F};
    std::vector<float> output{0.25F};
    EXPECT_FALSE(ApplyPcmGainEffect(input, -1.0F, output));
    EXPECT_EQ(output, std::vector<float>{0.25F});
    EXPECT_FALSE(ApplyPcmGainEffect(input, std::numeric_limits<float>::quiet_NaN(), output));
    EXPECT_EQ(output, std::vector<float>{0.25F});
    const std::vector<float> nonFinite{0.5F, std::numeric_limits<float>::infinity()};
    EXPECT_FALSE(ApplyPcmGainEffect(nonFinite, 1.0F, output));
    EXPECT_EQ(output, std::vector<float>{0.25F});
}
TEST(PcmGainEffectTest, SampleCountCapFailsClosed) {
    const std::vector<float> input(kMaximumPcmGainSamples + 1U, 0.0F);
    std::vector<float> output{0.5F};
    EXPECT_FALSE(ApplyPcmGainEffect(input, 1.0F, output));
    EXPECT_EQ(output, std::vector<float>{0.5F});
}
} // namespace
} // namespace UVE::Audio::Tests
