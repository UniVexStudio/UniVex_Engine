// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/audio/audio_meter_uve.h"

#include <cmath>
#include <limits>

#include <gtest/gtest.h>

namespace UVE::Audio::Tests {
namespace {

TEST(AudioMeterUVETest, SilenceIsTheFloorNotNegativeInfinity) {
    EXPECT_FLOAT_EQ(LinearAudioLevelToDecibelsUVE(0.0F), kAudioMeterFloorDbUVE);
    // A panel that prints "-inf" is honest; one that prints "nan" is a bug report.
    EXPECT_FLOAT_EQ(LinearAudioLevelToDecibelsUVE(-1.0F), kAudioMeterFloorDbUVE);
    EXPECT_FLOAT_EQ(LinearAudioLevelToDecibelsUVE(std::nanf("")), kAudioMeterFloorDbUVE);
    EXPECT_FLOAT_EQ(LinearAudioLevelToDecibelsUVE(std::numeric_limits<float>::infinity()), kAudioMeterFloorDbUVE);
}

TEST(AudioMeterUVETest, DecibelAndLinearConversionsAreInversesInsideTheRange) {
    EXPECT_FLOAT_EQ(LinearAudioLevelToDecibelsUVE(1.0F), 0.0F);
    EXPECT_NEAR(LinearAudioLevelToDecibelsUVE(0.5F), -6.0206F, 1.0e-3F);
    EXPECT_NEAR(LinearAudioLevelToDecibelsUVE(0.1F), -20.0F, 1.0e-4F);

    EXPECT_FLOAT_EQ(DecibelsToLinearAudioLevelUVE(0.0F), 1.0F);
    EXPECT_NEAR(DecibelsToLinearAudioLevelUVE(-6.0206F), 0.5F, 1.0e-4F);
    // Outside the meter's range the conversion clamps instead of returning 0 or an infinity.
    EXPECT_FLOAT_EQ(DecibelsToLinearAudioLevelUVE(-1000.0F), DecibelsToLinearAudioLevelUVE(kAudioMeterFloorDbUVE));
    EXPECT_FLOAT_EQ(DecibelsToLinearAudioLevelUVE(20.0F), 1.0F);
    EXPECT_FLOAT_EQ(DecibelsToLinearAudioLevelUVE(std::nanf("")), 0.0F);
}

TEST(AudioMeterUVETest, NormalizedLevelUsesTheDecibelScale) {
    EXPECT_FLOAT_EQ(NormalizedAudioMeterLevelUVE(0.0F), 0.0F);
    EXPECT_FLOAT_EQ(NormalizedAudioMeterLevelUVE(1.0F), 1.0F);
    EXPECT_FLOAT_EQ(NormalizedAudioMeterLevelUVE(4.0F), 1.0F); // over full scale cannot overflow a bar
    // -48 dB is exactly halfway up a -96..0 dB bar. On a linear bar it would sit at 0.4 %.
    EXPECT_NEAR(NormalizedAudioMeterLevelUVE(DecibelsToLinearAudioLevelUVE(-48.0F)), 0.5F, 1.0e-3F);
    // Monotone across the whole range.
    float previous = -1.0F;
    for (int step = 0; step <= 20; ++step) {
        const float level = NormalizedAudioMeterLevelUVE(DecibelsToLinearAudioLevelUVE(
            -96.0F + 96.0F * static_cast<float>(step) / 20.0F));
        EXPECT_GE(level, previous);
        previous = level;
    }
}

TEST(AudioMeterUVETest, APeakHoldsThenReleasesAtAConstantDecibelRate) {
    // A block at half scale, then silence: the reading holds the peak at first...
    const AudioMeterReadingUVE hit = IntegrateAudioMeterUVE(kSilentAudioMeterUVE, 0.5F, 0.25F, 0.01F);
    EXPECT_FLOAT_EQ(hit.peak, 0.5F);

    // ...then falls 20 dB per second, i.e. a tenth of its amplitude every second.
    const AudioMeterReadingUVE afterOneSecond = IntegrateAudioMeterUVE(hit, 0.0F, 0.0F, 1.0F);
    EXPECT_NEAR(afterOneSecond.peak, 0.05F, 1.0e-4F);
}

TEST(AudioMeterUVETest, APeakNeverFallsBelowTheFloorAndNeverRisesWithoutABlock) {
    AudioMeterReadingUVE reading = IntegrateAudioMeterUVE(kSilentAudioMeterUVE, 1.0F, 1.0F, 0.01F);
    for (int step = 0; step < 100; ++step) {
        reading = IntegrateAudioMeterUVE(reading, 0.0F, 0.0F, 1.0F);
    }
    // Reaching the floor is exactly zero, not the floor's own amplitude: "silent" stays a value a
    // caller can compare against.
    EXPECT_FLOAT_EQ(reading.peak, 0.0F);
}

TEST(AudioMeterUVETest, TheRmsAverageFollowsTheWindowNotTheBlock) {
    // One loud block does not jump an empty average to that block's level: the one-pole average
    // moves by a fraction of the distance, which is what keeps a meter from flickering.
    const AudioMeterReadingUVE oneBlock = IntegrateAudioMeterUVE(kSilentAudioMeterUVE, 1.0F, 1.0F, 1.0F / 60.0F);
    EXPECT_GT(oneBlock.rms, 0.0F);
    EXPECT_LT(oneBlock.rms, 0.5F);

    // A steady tone converges to its own level within a few windows.
    AudioMeterReadingUVE steady = kSilentAudioMeterUVE;
    for (int block = 0; block < 600; ++block) {
        steady = IntegrateAudioMeterUVE(steady, 0.5F, 0.25F, 1.0F / 60.0F);
    }
    EXPECT_NEAR(steady.rms, 0.5F, 1.0e-3F);
    EXPECT_NEAR(steady.peak, 0.5F, 1.0e-6F);
}

TEST(AudioMeterUVETest, AZeroDeltaStillAccountsForTheBlock) {
    // Offline rendering and tests pass no elapsed time. The block must still be visible rather
    // than discarded, and nothing may divide by zero.
    const AudioMeterReadingUVE reading = IntegrateAudioMeterUVE(kSilentAudioMeterUVE, 0.8F, 0.64F, 0.0F);
    EXPECT_FLOAT_EQ(reading.peak, 0.8F);
    EXPECT_FLOAT_EQ(reading.rms, 0.8F);
    const AudioMeterReadingUVE negative = IntegrateAudioMeterUVE(reading, 0.0F, 0.0F, -1.0F);
    EXPECT_FLOAT_EQ(negative.peak, 0.8F); // nothing released, nothing lost
}

TEST(AudioMeterUVETest, BadBlockValuesAreIgnoredRatherThanPoisoningTheReading) {
    const AudioMeterReadingUVE reading =
        IntegrateAudioMeterUVE(kSilentAudioMeterUVE, std::nanf(""), -4.0F, 1.0F / 60.0F);
    EXPECT_TRUE(std::isfinite(reading.peak));
    EXPECT_TRUE(std::isfinite(reading.rms));
    EXPECT_FLOAT_EQ(reading.peak, 0.0F);
    EXPECT_FLOAT_EQ(reading.rms, 0.0F);

    const AudioMeterReadingUVE recovered = IntegrateAudioMeterUVE(reading, 0.25F, 0.0625F, 1.0F / 60.0F);
    EXPECT_NEAR(recovered.peak, 0.25F, 1.0e-6F);
}

TEST(AudioMeterUVETest, CombiningTwoSourcesAddsTheirPowerNotTheirAmplitude) {
    const AudioMeterReadingUVE one{0.5F, 0.5F};
    const AudioMeterReadingUVE same{0.5F, 0.5F};
    const AudioMeterReadingUVE combined = CombineAudioMeterReadingsUVE(one, same);
    // Two uncorrelated 0.5 sources make 0.707, not 1.0: the second voice is not a doubling.
    EXPECT_NEAR(combined.peak, 0.70710678F, 1.0e-5F);
    EXPECT_NEAR(combined.rms, 0.70710678F, 1.0e-5F);

    // Combining with silence is the identity, which is what makes an empty bus read as its one
    // routed voice rather than as something quieter.
    EXPECT_FLOAT_EQ(CombineAudioMeterReadingsUVE(one, kSilentAudioMeterUVE).peak, 0.5F);
    EXPECT_FLOAT_EQ(CombineAudioMeterReadingsUVE(kSilentAudioMeterUVE, one).rms, 0.5F);
}

TEST(AudioMeterUVETest, CombiningIgnoresNonFiniteReadings) {
    const AudioMeterReadingUVE bad{std::nanf(""), std::numeric_limits<float>::infinity()};
    const AudioMeterReadingUVE good{0.25F, 0.25F};
    const AudioMeterReadingUVE combined = CombineAudioMeterReadingsUVE(bad, good);
    EXPECT_FLOAT_EQ(combined.peak, 0.25F);
    EXPECT_FLOAT_EQ(combined.rms, 0.25F);
}

TEST(AudioMeterUVETest, OnlyAPeakPastFullScaleCountsAsClipping) {
    EXPECT_FALSE(IsAudioMeterClippingUVE(kSilentAudioMeterUVE));
    EXPECT_FALSE(IsAudioMeterClippingUVE(AudioMeterReadingUVE{1.0F, 2.0F}));
    EXPECT_TRUE(IsAudioMeterClippingUVE(AudioMeterReadingUVE{1.0001F, 1.0F}));
    // An RMS above full scale with a legal peak is impossible in practice and must not flag.
    EXPECT_FALSE(IsAudioMeterClippingUVE(AudioMeterReadingUVE{0.5F, 1.5F}));
}

} // namespace
} // namespace UVE::Audio::Tests
