// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/audio/audio_occlusion_uve.h"

#include <cmath>
#include <limits>

#include <gtest/gtest.h>

namespace UVE::Audio::Tests {
namespace {

TEST(AudioOcclusionUVETest, ClearPathIsExactlyFullGainAndNoFilter) {
    EXPECT_FLOAT_EQ(ComputeAudioOcclusionGainUVE(0.0F, 0.35F), 1.0F);
    // 0 is the device contract for "no filter" - not a very high cutoff.
    EXPECT_FLOAT_EQ(ComputeAudioOcclusionCutoffUVE(0.0F, 700.0F), 0.0F);
}

TEST(AudioOcclusionUVETest, FullOcclusionLandsOnTheAuthoredFloor) {
    EXPECT_NEAR(ComputeAudioOcclusionGainUVE(1.0F, 0.35F), 0.35F, 1.0e-6F);
    EXPECT_NEAR(ComputeAudioOcclusionCutoffUVE(1.0F, 700.0F), 700.0F, 1.0e-3F);
}

TEST(AudioOcclusionUVETest, GainFallsMonotonicallyAndCutoffFallsLogarithmically) {
    float previousGain = 2.0F;
    float previousCutoff = 1.0e9F;
    for (int step = 0; step <= 10; ++step) {
        const float occlusion = static_cast<float>(step) / 10.0F;
        const float gain = ComputeAudioOcclusionGainUVE(occlusion, 0.35F);
        const float cutoff = step == 0 ? kAudioOcclusionClearCutoffHzUVE
                                       : ComputeAudioOcclusionCutoffUVE(occlusion, 700.0F);
        EXPECT_LE(gain, previousGain) << "occlusion " << occlusion;
        EXPECT_LE(cutoff, previousCutoff) << "occlusion " << occlusion;
        previousGain = gain;
        previousCutoff = cutoff;
    }
    // Half occlusion is the geometric mean of the two corners, not the arithmetic one: 700 Hz and
    // 20 kHz meet near 3.7 kHz, which is what a half-covered source actually sounds like.
    EXPECT_NEAR(ComputeAudioOcclusionCutoffUVE(0.5F, 700.0F), std::sqrt(700.0F * 20000.0F), 1.0F);
}

TEST(AudioOcclusionUVETest, NonFiniteAndOutOfRangeInputsAreContained) {
    // A broken provider must not silence the game...
    EXPECT_FLOAT_EQ(ComputeAudioOcclusionGainUVE(std::nanf(""), 0.35F), 1.0F);
    EXPECT_FLOAT_EQ(ComputeAudioOcclusionCutoffUVE(std::nanf(""), 700.0F), 0.0F);
    EXPECT_FLOAT_EQ(ComputeAudioOcclusionGainUVE(std::numeric_limits<float>::infinity(), 0.35F), 1.0F);
    // ...and an authored floor outside [0, 1] is clamped rather than trusted.
    EXPECT_FLOAT_EQ(ComputeAudioOcclusionGainUVE(1.0F, 4.0F), 1.0F);
    EXPECT_FLOAT_EQ(ComputeAudioOcclusionGainUVE(1.0F, -3.0F), 0.0F);
    // Out-of-range occlusion itself is clamped into the model.
    EXPECT_NEAR(ComputeAudioOcclusionGainUVE(9.0F, 0.35F), 0.35F, 1.0e-6F);
    EXPECT_FLOAT_EQ(ComputeAudioOcclusionGainUVE(-9.0F, 0.35F), 1.0F);
    // A cutoff outside the audible range is clamped, not propagated to the device.
    EXPECT_NEAR(ComputeAudioOcclusionCutoffUVE(1.0F, 1.0F), kMinimumAudioOcclusionCutoffHzUVE, 1.0e-3F);
    EXPECT_NEAR(ComputeAudioOcclusionCutoffUVE(1.0F, 90000.0F), kMaximumAudioOcclusionCutoffHzUVE, 1.0e-3F);
}

TEST(AudioOcclusionUVETest, NegligibleOcclusionBypassesInsteadOfMuffling) {
    // A source grazing a corner keeps its bandwidth rather than being slightly dulled forever.
    EXPECT_FLOAT_EQ(ComputeAudioOcclusionCutoffUVE(kAudioOcclusionBypassThresholdUVE * 0.5F, 700.0F), 0.0F);
    EXPECT_GT(ComputeAudioOcclusionCutoffUVE(kAudioOcclusionBypassThresholdUVE * 2.0F, 700.0F), 0.0F);
}

TEST(AudioOcclusionSmoothingUVETest, ZeroDeltaSnapsAndPositiveDeltaConverges) {
    // No time has passed: the exact target, not a value that never arrives.
    EXPECT_FLOAT_EQ(SmoothAudioOcclusionUVE(0.0F, 1.0F, 0.0F), 1.0F);
    EXPECT_FLOAT_EQ(SmoothAudioOcclusionUVE(0.7F, 0.0F, -1.0F), 0.0F);

    float value = 0.0F;
    for (int frame = 0; frame < 60; ++frame) {
        value = SmoothAudioOcclusionUVE(value, 1.0F, 1.0F / 60.0F);
    }
    EXPECT_GT(value, 0.99F); // one second against a 60 ms attack is fully converged
}

TEST(AudioOcclusionSmoothingUVETest, BlockingIsFasterThanClearing) {
    constexpr float kStep = 1.0F / 60.0F;
    const float afterAttack = SmoothAudioOcclusionUVE(0.0F, 1.0F, kStep);
    const float afterRelease = 1.0F - SmoothAudioOcclusionUVE(1.0F, 0.0F, kStep);
    // One frame moves block-in further than clear-out, so a source crossing an edge fades in
    // behind cover faster than it opens up.
    EXPECT_GT(afterAttack, afterRelease);
    EXPECT_GT(afterAttack, 0.0F);
    EXPECT_LT(afterAttack, 1.0F);
}

TEST(AudioOcclusionSmoothingUVETest, BadInputIsTreatedAsClear) {
    // A NaN target means "nothing reported": the smoothed value heads to clear, never to blocked.
    EXPECT_FLOAT_EQ(SmoothAudioOcclusionUVE(0.5F, std::nanf(""), 0.0F), 0.0F);
    EXPECT_LT(SmoothAudioOcclusionUVE(0.5F, std::nanf(""), 0.016F), 0.5F);
    // A NaN current is treated as clear, so the value climbs toward a real target from zero.
    EXPECT_FLOAT_EQ(SmoothAudioOcclusionUVE(std::nanf(""), 1.0F, 0.0F), 1.0F);
    const float fromBadCurrent = SmoothAudioOcclusionUVE(std::nanf(""), 1.0F, 0.016F);
    EXPECT_GT(fromBadCurrent, 0.0F);
    EXPECT_LT(fromBadCurrent, 1.0F);
    // Out-of-range targets are clamped into the model rather than overshooting it.
    EXPECT_FLOAT_EQ(SmoothAudioOcclusionUVE(0.5F, 5.0F, 0.0F), 1.0F);
    EXPECT_LE(SmoothAudioOcclusionUVE(0.5F, 5.0F, 0.016F), 1.0F);
}

TEST(AudioLowPassUVETest, AClearVoiceBypassesTheFilterBitForBit) {
    AudioVoiceLowPassUVE filter;
    EXPECT_TRUE(filter.IsBypassedUVE());
    float left = 0.25F;
    float right = -0.5F;
    filter.ProcessUVE(left, right);
    EXPECT_FLOAT_EQ(left, 0.25F);
    EXPECT_FLOAT_EQ(right, -0.5F);

    filter.ConfigureUVE(0.0F, 48000.0F);
    EXPECT_TRUE(filter.IsBypassedUVE());
    filter.ConfigureUVE(1000.0F, 48000.0F);
    EXPECT_FALSE(filter.IsBypassedUVE());
}

TEST(AudioLowPassUVETest, CoefficientsStayInsideTheUnitInterval) {
    EXPECT_FLOAT_EQ(ComputeAudioLowPassCoefficientUVE(0.0F, 48000.0F), 1.0F);
    EXPECT_FLOAT_EQ(ComputeAudioLowPassCoefficientUVE(-5.0F, 48000.0F), 1.0F);
    EXPECT_FLOAT_EQ(ComputeAudioLowPassCoefficientUVE(1000.0F, 0.0F), 1.0F);
    EXPECT_FLOAT_EQ(ComputeAudioLowPassCoefficientUVE(std::nanf(""), 48000.0F), 1.0F);

    const float low = ComputeAudioLowPassCoefficientUVE(200.0F, 48000.0F);
    const float high = ComputeAudioLowPassCoefficientUVE(8000.0F, 48000.0F);
    EXPECT_GT(low, 0.0F);
    EXPECT_LT(low, high);
    EXPECT_LE(high, 1.0F);
    // Above Nyquist the corner is pulled to 0.45 * fs instead of aliasing the coefficient.
    EXPECT_LE(ComputeAudioLowPassCoefficientUVE(40000.0F, 48000.0F), 1.0F);
}

TEST(AudioLowPassUVETest, SteadyStateIsUnitySoOcclusionNeverChangesLevelByItself) {
    AudioVoiceLowPassUVE filter;
    filter.ConfigureUVE(500.0F, 48000.0F);
    float left = 0.0F;
    float right = 0.0F;
    for (int frame = 0; frame < 48000; ++frame) {
        left = 0.5F;
        right = -0.25F;
        filter.ProcessUVE(left, right);
    }
    // A one-pole low-pass has unity DC gain: the gain drop an occluded source hears comes from
    // ComputeAudioOcclusionGainUVE(), not from the filter absorbing energy.
    EXPECT_NEAR(left, 0.5F, 1.0e-4F);
    EXPECT_NEAR(right, -0.25F, 1.0e-4F);
}

TEST(AudioLowPassUVETest, HighsAreAttenuatedMoreThanLows) {
    const auto measure = [](const float toneHz) {
        AudioVoiceLowPassUVE filter;
        filter.ConfigureUVE(400.0F, 48000.0F);
        double peak = 0.0;
        for (int frame = 0; frame < 48000; ++frame) {
            const auto sample = static_cast<float>(
                std::sin(6.28318530717958647692 * static_cast<double>(toneHz) * static_cast<double>(frame) / 48000.0));
            float left = sample;
            float right = sample;
            filter.ProcessUVE(left, right);
            if (frame > 24000) { // ignore the first half second of settling
                peak = std::max(peak, std::abs(static_cast<double>(left)));
            }
        }
        return peak;
    };
    const double lowTone = measure(100.0F);
    const double highTone = measure(6000.0F);
    EXPECT_GT(lowTone, 0.8);
    EXPECT_LT(highTone, 0.2); // 15x above the corner: heavily rolled off
    EXPECT_LT(highTone, lowTone);
}

TEST(AudioLowPassUVETest, ResetClearsTheStateSoAReconfiguredVoiceDoesNotRing) {
    AudioVoiceLowPassUVE filter;
    filter.ConfigureUVE(300.0F, 48000.0F);
    float left = 0.0F;
    float right = 0.0F;
    for (int frame = 0; frame < 1000; ++frame) {
        left = 1.0F;
        filter.ProcessUVE(left, right);
    }
    EXPECT_GT(left, 0.5F);
    filter.ResetUVE();
    left = 1.0F;
    right = 0.0F;
    filter.ProcessUVE(left, right);
    // From a cleared state the first sample is scaled by alpha, not carried over from the old one.
    EXPECT_LT(left, 0.5F);
}

} // namespace
} // namespace UVE::Audio::Tests
