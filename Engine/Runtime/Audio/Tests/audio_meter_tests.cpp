// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "audio_meter.h"

#include <cmath>
#include <limits>

#include <gtest/gtest.h>

namespace UVE::Audio::Tests {
namespace {

TEST(AudioMeterTest, SilenceIsTheFloorNotNegativeInfinity) {
    EXPECT_FLOAT_EQ(LinearAudioLevelToDecibels(0.0F), kAudioMeterFloorDb);
    // A panel that prints "-inf" is honest; one that prints "nan" is a bug report.
    EXPECT_FLOAT_EQ(LinearAudioLevelToDecibels(-1.0F), kAudioMeterFloorDb);
    EXPECT_FLOAT_EQ(LinearAudioLevelToDecibels(std::nanf("")), kAudioMeterFloorDb);
    EXPECT_FLOAT_EQ(LinearAudioLevelToDecibels(std::numeric_limits<float>::infinity()), kAudioMeterFloorDb);
}

TEST(AudioMeterTest, DecibelAndLinearConversionsAreInversesInsideTheRange) {
    EXPECT_FLOAT_EQ(LinearAudioLevelToDecibels(1.0F), 0.0F);
    EXPECT_NEAR(LinearAudioLevelToDecibels(0.5F), -6.0206F, 1.0e-3F);
    EXPECT_NEAR(LinearAudioLevelToDecibels(0.1F), -20.0F, 1.0e-4F);

    EXPECT_FLOAT_EQ(DecibelsToLinearAudioLevel(0.0F), 1.0F);
    EXPECT_NEAR(DecibelsToLinearAudioLevel(-6.0206F), 0.5F, 1.0e-4F);
    // Outside the meter's range the conversion clamps instead of returning 0 or an infinity.
    EXPECT_FLOAT_EQ(DecibelsToLinearAudioLevel(-1000.0F), DecibelsToLinearAudioLevel(kAudioMeterFloorDb));
    EXPECT_FLOAT_EQ(DecibelsToLinearAudioLevel(20.0F), 1.0F);
    EXPECT_FLOAT_EQ(DecibelsToLinearAudioLevel(std::nanf("")), 0.0F);
}

TEST(AudioMeterTest, NormalizedLevelUsesTheDecibelScale) {
    EXPECT_FLOAT_EQ(NormalizedAudioMeterLevel(0.0F), 0.0F);
    EXPECT_FLOAT_EQ(NormalizedAudioMeterLevel(1.0F), 1.0F);
    EXPECT_FLOAT_EQ(NormalizedAudioMeterLevel(4.0F), 1.0F); // over full scale cannot overflow a bar
    // -48 dB is exactly halfway up a -96..0 dB bar. On a linear bar it would sit at 0.4 %.
    EXPECT_NEAR(NormalizedAudioMeterLevel(DecibelsToLinearAudioLevel(-48.0F)), 0.5F, 1.0e-3F);
    // Monotone across the whole range.
    float previous = -1.0F;
    for (int step = 0; step <= 20; ++step) {
        const float level = NormalizedAudioMeterLevel(DecibelsToLinearAudioLevel(
            -96.0F + 96.0F * static_cast<float>(step) / 20.0F));
        EXPECT_GE(level, previous);
        previous = level;
    }
}

TEST(AudioMeterTest, APeakHoldsThenReleasesAtAConstantDecibelRate) {
    // A block at half scale, then silence: the reading holds the peak at first...
    const AudioMeterReading hit = IntegrateAudioMeter(kSilentAudioMeter, 0.5F, 0.25F, 0.01F);
    EXPECT_FLOAT_EQ(hit.peak, 0.5F);

    // ...then falls 20 dB per second, i.e. a tenth of its amplitude every second.
    const AudioMeterReading afterOneSecond = IntegrateAudioMeter(hit, 0.0F, 0.0F, 1.0F);
    EXPECT_NEAR(afterOneSecond.peak, 0.05F, 1.0e-4F);
}

TEST(AudioMeterTest, APeakNeverFallsBelowTheFloorAndNeverRisesWithoutABlock) {
    AudioMeterReading reading = IntegrateAudioMeter(kSilentAudioMeter, 1.0F, 1.0F, 0.01F);
    for (int step = 0; step < 100; ++step) {
        reading = IntegrateAudioMeter(reading, 0.0F, 0.0F, 1.0F);
    }
    // Reaching the floor is exactly zero, not the floor's own amplitude: "silent" stays a value a
    // caller can compare against.
    EXPECT_FLOAT_EQ(reading.peak, 0.0F);
}

TEST(AudioMeterTest, TheRmsAverageFollowsTheWindowNotTheBlock) {
    // One loud block does not jump an empty average to that block's level: the one-pole average
    // moves by a fraction of the distance, which is what keeps a meter from flickering.
    const AudioMeterReading oneBlock = IntegrateAudioMeter(kSilentAudioMeter, 1.0F, 1.0F, 1.0F / 60.0F);
    EXPECT_GT(oneBlock.rms, 0.0F);
    EXPECT_LT(oneBlock.rms, 0.5F);

    // A steady tone converges to its own level within a few windows.
    AudioMeterReading steady = kSilentAudioMeter;
    for (int block = 0; block < 600; ++block) {
        steady = IntegrateAudioMeter(steady, 0.5F, 0.25F, 1.0F / 60.0F);
    }
    EXPECT_NEAR(steady.rms, 0.5F, 1.0e-3F);
    EXPECT_NEAR(steady.peak, 0.5F, 1.0e-6F);
}

TEST(AudioMeterTest, AZeroDeltaStillAccountsForTheBlock) {
    // Offline rendering and tests pass no elapsed time. The block must still be visible rather
    // than discarded, and nothing may divide by zero.
    const AudioMeterReading reading = IntegrateAudioMeter(kSilentAudioMeter, 0.8F, 0.64F, 0.0F);
    EXPECT_FLOAT_EQ(reading.peak, 0.8F);
    EXPECT_FLOAT_EQ(reading.rms, 0.8F);
    const AudioMeterReading negative = IntegrateAudioMeter(reading, 0.0F, 0.0F, -1.0F);
    EXPECT_FLOAT_EQ(negative.peak, 0.8F); // nothing released, nothing lost
}

TEST(AudioMeterTest, BadBlockValuesAreIgnoredRatherThanPoisoningTheReading) {
    const AudioMeterReading reading =
        IntegrateAudioMeter(kSilentAudioMeter, std::nanf(""), -4.0F, 1.0F / 60.0F);
    EXPECT_TRUE(std::isfinite(reading.peak));
    EXPECT_TRUE(std::isfinite(reading.rms));
    EXPECT_FLOAT_EQ(reading.peak, 0.0F);
    EXPECT_FLOAT_EQ(reading.rms, 0.0F);

    const AudioMeterReading recovered = IntegrateAudioMeter(reading, 0.25F, 0.0625F, 1.0F / 60.0F);
    EXPECT_NEAR(recovered.peak, 0.25F, 1.0e-6F);
}

TEST(AudioMeterTest, CombiningTwoSourcesAddsTheirPowerNotTheirAmplitude) {
    const AudioMeterReading one{0.5F, 0.5F};
    const AudioMeterReading same{0.5F, 0.5F};
    const AudioMeterReading combined = CombineAudioMeterReadings(one, same);
    // Two uncorrelated 0.5 sources make 0.707, not 1.0: the second voice is not a doubling.
    EXPECT_NEAR(combined.peak, 0.70710678F, 1.0e-5F);
    EXPECT_NEAR(combined.rms, 0.70710678F, 1.0e-5F);

    // Combining with silence is the identity, which is what makes an empty bus read as its one
    // routed voice rather than as something quieter.
    EXPECT_FLOAT_EQ(CombineAudioMeterReadings(one, kSilentAudioMeter).peak, 0.5F);
    EXPECT_FLOAT_EQ(CombineAudioMeterReadings(kSilentAudioMeter, one).rms, 0.5F);
}

TEST(AudioMeterTest, CombiningIgnoresNonFiniteReadings) {
    const AudioMeterReading bad{std::nanf(""), std::numeric_limits<float>::infinity()};
    const AudioMeterReading good{0.25F, 0.25F};
    const AudioMeterReading combined = CombineAudioMeterReadings(bad, good);
    EXPECT_FLOAT_EQ(combined.peak, 0.25F);
    EXPECT_FLOAT_EQ(combined.rms, 0.25F);
}

TEST(AudioMeterTest, OnlyAPeakPastFullScaleCountsAsClipping) {
    EXPECT_FALSE(IsAudioMeterClipping(kSilentAudioMeter));
    EXPECT_FALSE(IsAudioMeterClipping(AudioMeterReading{1.0F, 2.0F}));
    EXPECT_TRUE(IsAudioMeterClipping(AudioMeterReading{1.0001F, 1.0F}));
    // An RMS above full scale with a legal peak is impossible in practice and must not flag.
    EXPECT_FALSE(IsAudioMeterClipping(AudioMeterReading{0.5F, 1.5F}));
}

} // namespace
} // namespace UVE::Audio::Tests
