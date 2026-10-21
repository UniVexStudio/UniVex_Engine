// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/audio/audio_dsp_uve.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <vector>

#include <gtest/gtest.h>

namespace UVE::Audio::Tests {
namespace {

// The engine's PCM path is normalized float; every measurement below is RMS over a settled window
// so the filter transient cannot flatter or ruin the comparison.
[[nodiscard]] std::vector<float> MakeSineSamplesUVE(const float amplitude, const float frequencyHz,
                                                    const float sampleRateHz, const std::size_t frameCount) {
    std::vector<float> samples(frameCount);
    const float step = (2.0F * 3.14159265358979323846F * frequencyHz) / sampleRateHz;
    for (std::size_t index = 0U; index < frameCount; ++index) {
        samples[index] = amplitude * std::sin(step * static_cast<float>(index));
    }
    return samples;
}

[[nodiscard]] std::vector<float> MakeConstantSamplesUVE(const float value, const std::size_t frameCount) {
    return std::vector<float>(frameCount, value);
}

/// RMS over [fromFrame, samples.size()): the settled tail of a block.
[[nodiscard]] float TailRmsUVE(const std::vector<float>& samples, const std::size_t fromFrame) {
    if (fromFrame >= samples.size()) return 0.0F;
    const std::size_t count = samples.size() - fromFrame;
    double sum = 0.0;
    for (std::size_t index = fromFrame; index < samples.size(); ++index) {
        const double value = static_cast<double>(samples[index]);
        sum += value * value;
    }
    return static_cast<float>(std::sqrt(sum / static_cast<double>(count)));
}

[[nodiscard]] float PeakMagnitudeUVE(const std::vector<float>& samples, const std::size_t fromFrame) {
    float peak = 0.0F;
    for (std::size_t index = fromFrame; index < samples.size(); ++index) {
        peak = std::max(peak, std::fabs(samples[index]));
    }
    return peak;
}

constexpr float kTestSampleRateUVE = 48000.0F;
constexpr std::size_t kOneSecondUVE = 48000U;

[[nodiscard]] AudioInsertDescUVE MakeGainDescUVE(const float gain) {
    AudioInsertDescUVE desc;
    desc.kind = AudioInsertKindUVE::Gain;
    desc.params.gain = gain;
    return desc;
}

[[nodiscard]] AudioInsertDescUVE MakeFilterDescUVE(const AudioInsertKindUVE kind, const float frequencyHz,
                                                    const float q = 0.70710678F) {
    AudioInsertDescUVE desc;
    desc.kind = kind;
    desc.params.frequencyHz = frequencyHz;
    desc.params.q = q;
    return desc;
}

// --- Validation. ---

TEST(AudioInsertValidationUVETest, RejectsEveryOutOfRangeFieldForItsKind) {
    AudioInsertDescUVE desc;
    EXPECT_TRUE(IsAudioInsertDescValidUVE(desc));

    desc.kind = AudioInsertKindUVE::Gain;
    desc.params.gain = -0.1F;
    EXPECT_FALSE(IsAudioInsertDescValidUVE(desc));
    desc.params.gain = kMaximumAudioInsertGainUVE * 2.0F;
    EXPECT_FALSE(IsAudioInsertDescValidUVE(desc));
    desc.params.gain = std::numeric_limits<float>::quiet_NaN();
    EXPECT_FALSE(IsAudioInsertDescValidUVE(desc));

    desc = MakeFilterDescUVE(AudioInsertKindUVE::LowPass, 0.5F);
    EXPECT_FALSE(IsAudioInsertDescValidUVE(desc));
    desc = MakeFilterDescUVE(AudioInsertKindUVE::LowPass, 1000.0F, 0.01F);
    EXPECT_FALSE(IsAudioInsertDescValidUVE(desc));
    desc = MakeFilterDescUVE(AudioInsertKindUVE::HighPass, 1000.0F);
    EXPECT_TRUE(IsAudioInsertDescValidUVE(desc));

    desc = MakeFilterDescUVE(AudioInsertKindUVE::PeakEq, 1000.0F);
    desc.params.gainDb = kMaximumAudioEqGainDbUVE + 1.0F;
    EXPECT_FALSE(IsAudioInsertDescValidUVE(desc));
    desc.params.gainDb = kMaximumAudioEqGainDbUVE;
    EXPECT_TRUE(IsAudioInsertDescValidUVE(desc));

    desc = AudioInsertDescUVE{};
    desc.kind = AudioInsertKindUVE::Compressor;
    desc.params.ratio = 0.5F;
    EXPECT_FALSE(IsAudioInsertDescValidUVE(desc));
    desc.params.ratio = 4.0F;
    desc.params.attackMs = 0.0F;
    EXPECT_FALSE(IsAudioInsertDescValidUVE(desc));
    desc.params.attackMs = 10.0F;
    EXPECT_TRUE(IsAudioInsertDescValidUVE(desc));

    desc = AudioInsertDescUVE{};
    desc.kind = AudioInsertKindUVE::Limiter;
    desc.params.thresholdDb = 6.0F;
    EXPECT_FALSE(IsAudioInsertDescValidUVE(desc));
    desc.params.thresholdDb = -6.0F;
    EXPECT_TRUE(IsAudioInsertDescValidUVE(desc));

    desc = AudioInsertDescUVE{};
    desc.kind = AudioInsertKindUVE::Reverb;
    desc.params.roomSize = 1.5F;
    EXPECT_FALSE(IsAudioInsertDescValidUVE(desc));
    desc.params.roomSize = 0.8F;
    desc.params.wet = 2.0F;
    EXPECT_FALSE(IsAudioInsertDescValidUVE(desc));
    desc.params.wet = 0.3F;
    EXPECT_TRUE(IsAudioInsertDescValidUVE(desc));
}

TEST(AudioInsertValidationUVETest, ConfigureRejectsWithoutTouchingTheRunningInsert) {
    AudioInsertUVE insert;
    ASSERT_TRUE(insert.ConfigureUVE(MakeGainDescUVE(0.5F), kTestSampleRateUVE));
    ASSERT_TRUE(insert.IsActiveUVE());
    const AudioInsertDescUVE installed = insert.GetDescriptionUVE();

    AudioInsertDescUVE invalid;
    invalid.kind = AudioInsertKindUVE::Gain;
    invalid.params.gain = -5.0F;
    EXPECT_FALSE(insert.ConfigureUVE(invalid, kTestSampleRateUVE));
    EXPECT_EQ(insert.GetDescriptionUVE(), installed);
    EXPECT_TRUE(insert.IsActiveUVE());

    // A bad sample rate is refused too: coefficients for the wrong rate are worse than none.
    EXPECT_FALSE(insert.ConfigureUVE(MakeGainDescUVE(0.5F), 1000.0F));

    std::vector<float> samples{0.5F};
    insert.ProcessUVE(samples.data(), samples.size());
    EXPECT_FLOAT_EQ(samples[0], 0.25F);
}

TEST(AudioInsertValidationUVETest, ClearedInsertIsBypassedAndForgetsItsHistory) {
    AudioInsertUVE insert;
    ASSERT_TRUE(insert.ConfigureUVE(MakeFilterDescUVE(AudioInsertKindUVE::LowPass, 500.0F), kTestSampleRateUVE));
    std::vector<float> samples = MakeConstantSamplesUVE(1.0F, 512U);
    insert.ProcessUVE(samples.data(), samples.size());
    EXPECT_TRUE(insert.IsActiveUVE());

    insert.ClearUVE();
    EXPECT_FALSE(insert.IsActiveUVE());
    std::vector<float> passthrough{0.75F};
    insert.ProcessUVE(passthrough.data(), passthrough.size());
    EXPECT_FLOAT_EQ(passthrough[0], 0.75F);

    // Reconfiguring after a clear starts from zeroed history, not from the old filter's state.
    ASSERT_TRUE(insert.ConfigureUVE(MakeFilterDescUVE(AudioInsertKindUVE::LowPass, 500.0F), kTestSampleRateUVE));
    std::vector<float> fresh = MakeConstantSamplesUVE(1.0F, 512U);
    insert.ProcessUVE(fresh.data(), fresh.size());
    EXPECT_LT(fresh[0], 0.01F);
}

// --- Gain. ---

TEST(AudioInsertDspUVETest, GainInsertScalesByItsMultiplier) {
    AudioInsertUVE insert;
    ASSERT_TRUE(insert.ConfigureUVE(MakeGainDescUVE(2.0F), kTestSampleRateUVE));
    std::vector<float> samples{0.25F, -0.5F, 0.0F};
    insert.ProcessUVE(samples.data(), samples.size());
    EXPECT_FLOAT_EQ(samples[0], 0.5F);
    EXPECT_FLOAT_EQ(samples[1], -1.0F);
    EXPECT_FLOAT_EQ(samples[2], 0.0F);
}

// --- Filters. ---

TEST(AudioInsertDspUVETest, LowPassKeepsDcAndRejectsFrequenciesWellAboveItsCutoff) {
    AudioInsertUVE insert;
    ASSERT_TRUE(insert.ConfigureUVE(MakeFilterDescUVE(AudioInsertKindUVE::LowPass, 1000.0F), kTestSampleRateUVE));

    std::vector<float> dc = MakeConstantSamplesUVE(1.0F, 4800U);
    insert.ProcessUVE(dc.data(), dc.size());
    EXPECT_NEAR(dc.back(), 1.0F, 1.0e-4F);

    insert.ClearUVE();
    ASSERT_TRUE(insert.ConfigureUVE(MakeFilterDescUVE(AudioInsertKindUVE::LowPass, 1000.0F), kTestSampleRateUVE));
    std::vector<float> highTone = MakeSineSamplesUVE(1.0F, 12000.0F, kTestSampleRateUVE, kOneSecondUVE);
    insert.ProcessUVE(highTone.data(), highTone.size());
    const float inputRms = TailRmsUVE(MakeSineSamplesUVE(1.0F, 12000.0F, kTestSampleRateUVE, kOneSecondUVE), 0U);
    const float outputRms = TailRmsUVE(highTone, kOneSecondUVE / 2U);
    // A 2nd-order section is ~43 dB down one decade above cutoff; anything near unity means the
    // coefficients are not what they claim to be.
    EXPECT_LT(outputRms / inputRms, 0.05F);
}

TEST(AudioInsertDspUVETest, HighPassRemovesDcAndPassesFrequenciesWellAboveItsCutoff) {
    AudioInsertUVE insert;
    ASSERT_TRUE(insert.ConfigureUVE(MakeFilterDescUVE(AudioInsertKindUVE::HighPass, 200.0F), kTestSampleRateUVE));

    std::vector<float> dc = MakeConstantSamplesUVE(1.0F, 4800U);
    insert.ProcessUVE(dc.data(), dc.size());
    EXPECT_LT(std::fabs(dc.back()), 1.0e-4F);

    insert.ClearUVE();
    ASSERT_TRUE(insert.ConfigureUVE(MakeFilterDescUVE(AudioInsertKindUVE::HighPass, 1000.0F), kTestSampleRateUVE));
    const std::vector<float> input = MakeSineSamplesUVE(1.0F, 12000.0F, kTestSampleRateUVE, kOneSecondUVE);
    std::vector<float> processed = input;
    insert.ProcessUVE(processed.data(), processed.size());
    const float ratio = TailRmsUVE(processed, kOneSecondUVE / 2U) / TailRmsUVE(input, kOneSecondUVE / 2U);
    EXPECT_NEAR(ratio, 1.0F, 0.05F);
}

TEST(AudioInsertDspUVETest, PeakEqBoostsExactlyItsCentreBand) {
    AudioInsertDescUVE desc = MakeFilterDescUVE(AudioInsertKindUVE::PeakEq, 1000.0F, 2.0F);
    desc.params.gainDb = 12.0F;
    AudioInsertUVE insert;
    ASSERT_TRUE(insert.ConfigureUVE(desc, kTestSampleRateUVE));

    const std::vector<float> input = MakeSineSamplesUVE(0.5F, 1000.0F, kTestSampleRateUVE, kOneSecondUVE);
    std::vector<float> processed = input;
    insert.ProcessUVE(processed.data(), processed.size());
    const float ratio = TailRmsUVE(processed, kOneSecondUVE / 2U) / TailRmsUVE(input, kOneSecondUVE / 2U);
    // RBJ defines the peak at the centre frequency as 10^(dB/20) exactly: +12 dB is 3.98x.
    EXPECT_NEAR(ratio, 3.9810717F, 0.05F);
}

TEST(AudioInsertDspUVETest, ACutoffAboveNyquistIsClampedInsteadOfDestabilisingTheFilter) {
    AudioInsertUVE insert;
    ASSERT_TRUE(insert.ConfigureUVE(MakeFilterDescUVE(AudioInsertKindUVE::LowPass, 100000.0F), kTestSampleRateUVE));
    std::vector<float> samples = MakeSineSamplesUVE(1.0F, 1000.0F, kTestSampleRateUVE, 4800U);
    insert.ProcessUVE(samples.data(), samples.size());
    EXPECT_TRUE(std::isfinite(samples.back()));
    EXPECT_LE(PeakMagnitudeUVE(samples, 0U), 4.0F);
}

// --- Dynamics. ---

TEST(AudioInsertDspUVETest, CompressorReducesAboveItsThresholdAndIsTransparentAtUnityRatio) {
    AudioInsertDescUVE transparent;
    transparent.kind = AudioInsertKindUVE::Compressor;
    transparent.params.thresholdDb = -20.0F;
    transparent.params.ratio = 1.0F;
    AudioInsertUVE insert;
    ASSERT_TRUE(insert.ConfigureUVE(transparent, kTestSampleRateUVE));

    const std::vector<float> input = MakeSineSamplesUVE(1.0F, 1000.0F, kTestSampleRateUVE, kOneSecondUVE);
    std::vector<float> processed = input;
    insert.ProcessUVE(processed.data(), processed.size());
    for (std::size_t index = 0U; index < processed.size(); ++index) {
        ASSERT_FLOAT_EQ(processed[index], input[index]) << "index " << index;
    }

    AudioInsertDescUVE compressing = transparent;
    compressing.params.ratio = 4.0F;
    compressing.params.attackMs = 5.0F;
    compressing.params.releaseMs = 200.0F;
    ASSERT_TRUE(insert.ConfigureUVE(compressing, kTestSampleRateUVE));
    processed = input;
    insert.ProcessUVE(processed.data(), processed.size());
    // 20 dB over the threshold at 4:1 reduces by 15 dB: 0.7071 sine -> about 0.126 RMS.
    const float ratio = TailRmsUVE(processed, kOneSecondUVE / 2U) / TailRmsUVE(input, kOneSecondUVE / 2U);
    EXPECT_NEAR(ratio, 0.17782794F, 0.03F);
}

TEST(AudioInsertDspUVETest, LimiterNeverLetsASamplePastItsCeiling) {
    AudioInsertDescUVE desc;
    desc.kind = AudioInsertKindUVE::Limiter;
    desc.params.thresholdDb = -6.0F;
    desc.params.releaseMs = 100.0F;
    AudioInsertUVE insert;
    ASSERT_TRUE(insert.ConfigureUVE(desc, kTestSampleRateUVE));

    // A deliberately hot block, including a sample well past full scale.
    std::vector<float> samples = MakeSineSamplesUVE(1.0F, 220.0F, kTestSampleRateUVE, kOneSecondUVE);
    samples[100U] = 3.0F;
    samples[200U] = -3.0F;
    insert.ProcessUVE(samples.data(), samples.size());

    const float ceiling = std::pow(10.0F, -6.0F / 20.0F);
    EXPECT_LE(PeakMagnitudeUVE(samples, 0U), ceiling + 1.0e-6F);
    // The two hot samples are held exactly at the ceiling, not merely near it.
    EXPECT_NEAR(samples[100U], ceiling, 1.0e-6F);
    EXPECT_NEAR(samples[200U], -ceiling, 1.0e-6F);
}

// --- Reverb. ---

TEST(AudioInsertDspUVETest, ReverbIsTransparentWithNoWetMixAndProducesADecayingTailOtherwise) {
    AudioInsertDescUVE desc;
    desc.kind = AudioInsertKindUVE::Reverb;
    desc.params.wet = 0.0F;
    desc.params.dry = 1.0F;
    AudioInsertUVE insert;
    ASSERT_TRUE(insert.ConfigureUVE(desc, kTestSampleRateUVE));
    std::vector<float> dry{1.0F, -0.5F, 0.25F};
    insert.ProcessUVE(dry.data(), dry.size());
    EXPECT_FLOAT_EQ(dry[0], 1.0F);
    EXPECT_FLOAT_EQ(dry[1], -0.5F);
    EXPECT_FLOAT_EQ(dry[2], 0.25F);

    desc.params.wet = 1.0F;
    desc.params.dry = 0.0F;
    desc.params.roomSize = 0.8F;
    desc.params.damping = 0.2F;
    ASSERT_TRUE(insert.ConfigureUVE(desc, kTestSampleRateUVE));

    std::vector<float> tail(kOneSecondUVE * 8U, 0.0F);
    for (std::size_t index = 0U; index < 128U; ++index) tail[index] = 0.5F;
    insert.ProcessUVE(tail.data(), tail.size());

    EXPECT_GT(TailRmsUVE(tail, 4800U), 1.0e-4F);
    EXPECT_LT(PeakMagnitudeUVE(tail, tail.size() - 1000U), 1.0e-3F);
}

TEST(AudioInsertDspUVETest, ReverbSurvivesASampleRateChangeAndStaysFinite) {
    AudioInsertDescUVE desc;
    desc.kind = AudioInsertKindUVE::Reverb;
    AudioInsertUVE insert;
    ASSERT_TRUE(insert.ConfigureUVE(desc, kTestSampleRateUVE));
    ASSERT_TRUE(insert.SetSampleRateUVE(44100.0F));
    EXPECT_TRUE(insert.IsActiveUVE());

    std::vector<float> samples = MakeSineSamplesUVE(0.5F, 440.0F, 44100.0F, 4410U);
    insert.ProcessUVE(samples.data(), samples.size());
    EXPECT_TRUE(std::isfinite(samples.back()));
    EXPECT_LE(PeakMagnitudeUVE(samples, 0U), 8.0F);
}

// --- Chain. ---

TEST(AudioInsertChainUVETest, ChainRunsSlotsInOrderAndClampsExactlyOnce) {
    AudioBusInsertChainUVE chain;
    ASSERT_TRUE(chain.SetInsertUVE(0U, MakeGainDescUVE(4.0F)));
    ASSERT_TRUE(chain.SetInsertUVE(1U, MakeGainDescUVE(0.1F)));
    EXPECT_TRUE(chain.HasActiveInsertsUVE());

    const std::vector<float> input{0.9F, -0.9F};
    std::vector<float> output(input.size(), 0.0F);
    chain.ProcessUVE(input.data(), output.data(), input.size());
    // 0.9 * 4 = 3.6, * 0.1 = 0.36 - no intermediate clip, one clamp at the end (which does not bite).
    EXPECT_FLOAT_EQ(output[0], 0.36F);
    EXPECT_FLOAT_EQ(output[1], -0.36F);

    // The final clamp is real: a single huge gain still lands on full scale, not beyond it.
    ASSERT_TRUE(chain.ClearInsertUVE(1U));
    ASSERT_TRUE(chain.SetInsertUVE(1U, MakeGainDescUVE(4.0F)));
    chain.ProcessUVE(input.data(), output.data(), input.size());
    EXPECT_FLOAT_EQ(output[0], 1.0F);
    EXPECT_FLOAT_EQ(output[1], -1.0F);
}

TEST(AudioInsertChainUVETest, EmptyChainCopiesAndNonFiniteInputIsScrubbed) {
    AudioBusInsertChainUVE chain;
    EXPECT_FALSE(chain.HasActiveInsertsUVE());
    const std::vector<float> input{0.5F, std::numeric_limits<float>::quiet_NaN(),
                                   std::numeric_limits<float>::infinity(), -0.25F};
    std::vector<float> output(input.size(), 0.0F);
    chain.ProcessUVE(input.data(), output.data(), input.size());
    EXPECT_FLOAT_EQ(output[0], 0.5F);
    EXPECT_FLOAT_EQ(output[1], 0.0F);
    EXPECT_FLOAT_EQ(output[2], 0.0F);
    EXPECT_FLOAT_EQ(output[3], -0.25F);
}

TEST(AudioInsertChainUVETest, ChainValidatesSlotIndicesAndReportsConfiguredInserts) {
    AudioBusInsertChainUVE chain;
    AudioInsertDescUVE read;
    EXPECT_FALSE(chain.SetInsertUVE(kMaximumAudioBusInsertsUVE, MakeGainDescUVE(1.0F)));
    EXPECT_FALSE(chain.GetInsertUVE(0U, read));
    EXPECT_FALSE(chain.ClearInsertUVE(kMaximumAudioBusInsertsUVE));

    ASSERT_TRUE(chain.SetInsertUVE(0U, MakeGainDescUVE(0.5F)));
    ASSERT_TRUE(chain.GetInsertUVE(0U, read));
    EXPECT_EQ(read, MakeGainDescUVE(0.5F));
    EXPECT_TRUE(chain.ClearInsertUVE(0U));
    EXPECT_FALSE(chain.GetInsertUVE(0U, read));
    EXPECT_FALSE(chain.HasActiveInsertsUVE());
}

TEST(AudioInsertChainUVETest, ZeroFramesAndNullBuffersAreSafeNoOps) {
    AudioBusInsertChainUVE chain;
    ASSERT_TRUE(chain.SetInsertUVE(0U, MakeGainDescUVE(2.0F)));
    float sample = 0.5F;
    chain.ProcessInPlaceUVE(&sample, 0U);
    EXPECT_FLOAT_EQ(sample, 0.5F);
    chain.ProcessInPlaceUVE(nullptr, 4U);
    chain.ProcessUVE(nullptr, nullptr, 4U);
    EXPECT_FLOAT_EQ(sample, 0.5F);
}

} // namespace
} // namespace UVE::Audio::Tests
