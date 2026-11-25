// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "audio_dsp.h"

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
[[nodiscard]] std::vector<float> MakeSineSamples(const float amplitude, const float frequencyHz,
                                                    const float sampleRateHz, const std::size_t frameCount) {
    std::vector<float> samples(frameCount);
    const float step = (2.0F * 3.14159265358979323846F * frequencyHz) / sampleRateHz;
    for (std::size_t index = 0U; index < frameCount; ++index) {
        samples[index] = amplitude * std::sin(step * static_cast<float>(index));
    }
    return samples;
}

[[nodiscard]] std::vector<float> MakeConstantSamples(const float value, const std::size_t frameCount) {
    return std::vector<float>(frameCount, value);
}

/// RMS over [fromFrame, samples.size()): the settled tail of a block.
[[nodiscard]] float TailRms(const std::vector<float>& samples, const std::size_t fromFrame) {
    if (fromFrame >= samples.size()) return 0.0F;
    const std::size_t count = samples.size() - fromFrame;
    double sum = 0.0;
    for (std::size_t index = fromFrame; index < samples.size(); ++index) {
        const double value = static_cast<double>(samples[index]);
        sum += value * value;
    }
    return static_cast<float>(std::sqrt(sum / static_cast<double>(count)));
}

[[nodiscard]] float PeakMagnitude(const std::vector<float>& samples, const std::size_t fromFrame) {
    float peak = 0.0F;
    for (std::size_t index = fromFrame; index < samples.size(); ++index) {
        peak = std::max(peak, std::fabs(samples[index]));
    }
    return peak;
}

constexpr float kTestSampleRate = 48000.0F;
constexpr std::size_t kOneSecond = 48000U;

[[nodiscard]] AudioInsertDesc MakeGainDesc(const float gain) {
    AudioInsertDesc desc;
    desc.kind = AudioInsertKind::Gain;
    desc.params.gain = gain;
    return desc;
}

[[nodiscard]] AudioInsertDesc MakeFilterDesc(const AudioInsertKind kind, const float frequencyHz,
                                                    const float q = 0.70710678F) {
    AudioInsertDesc desc;
    desc.kind = kind;
    desc.params.frequencyHz = frequencyHz;
    desc.params.q = q;
    return desc;
}

// --- Validation. ---

TEST(AudioInsertValidationTest, RejectsEveryOutOfRangeFieldForItsKind) {
    AudioInsertDesc desc;
    EXPECT_TRUE(IsAudioInsertDescValid(desc));

    desc.kind = AudioInsertKind::Gain;
    desc.params.gain = -0.1F;
    EXPECT_FALSE(IsAudioInsertDescValid(desc));
    desc.params.gain = kMaximumAudioInsertGain * 2.0F;
    EXPECT_FALSE(IsAudioInsertDescValid(desc));
    desc.params.gain = std::numeric_limits<float>::quiet_NaN();
    EXPECT_FALSE(IsAudioInsertDescValid(desc));

    desc = MakeFilterDesc(AudioInsertKind::LowPass, 0.5F);
    EXPECT_FALSE(IsAudioInsertDescValid(desc));
    desc = MakeFilterDesc(AudioInsertKind::LowPass, 1000.0F, 0.01F);
    EXPECT_FALSE(IsAudioInsertDescValid(desc));
    desc = MakeFilterDesc(AudioInsertKind::HighPass, 1000.0F);
    EXPECT_TRUE(IsAudioInsertDescValid(desc));

    desc = MakeFilterDesc(AudioInsertKind::PeakEq, 1000.0F);
    desc.params.gainDb = kMaximumAudioEqGainDb + 1.0F;
    EXPECT_FALSE(IsAudioInsertDescValid(desc));
    desc.params.gainDb = kMaximumAudioEqGainDb;
    EXPECT_TRUE(IsAudioInsertDescValid(desc));

    desc = AudioInsertDesc{};
    desc.kind = AudioInsertKind::Compressor;
    desc.params.ratio = 0.5F;
    EXPECT_FALSE(IsAudioInsertDescValid(desc));
    desc.params.ratio = 4.0F;
    desc.params.attackMs = 0.0F;
    EXPECT_FALSE(IsAudioInsertDescValid(desc));
    desc.params.attackMs = 10.0F;
    EXPECT_TRUE(IsAudioInsertDescValid(desc));

    desc = AudioInsertDesc{};
    desc.kind = AudioInsertKind::Limiter;
    desc.params.thresholdDb = 6.0F;
    EXPECT_FALSE(IsAudioInsertDescValid(desc));
    desc.params.thresholdDb = -6.0F;
    EXPECT_TRUE(IsAudioInsertDescValid(desc));

    desc = AudioInsertDesc{};
    desc.kind = AudioInsertKind::Reverb;
    desc.params.roomSize = 1.5F;
    EXPECT_FALSE(IsAudioInsertDescValid(desc));
    desc.params.roomSize = 0.8F;
    desc.params.wet = 2.0F;
    EXPECT_FALSE(IsAudioInsertDescValid(desc));
    desc.params.wet = 0.3F;
    EXPECT_TRUE(IsAudioInsertDescValid(desc));
}

TEST(AudioInsertValidationTest, ConfigureRejectsWithoutTouchingTheRunningInsert) {
    AudioInsert insert;
    ASSERT_TRUE(insert.Configure(MakeGainDesc(0.5F), kTestSampleRate));
    ASSERT_TRUE(insert.IsActive());
    const AudioInsertDesc installed = insert.GetDescription();

    AudioInsertDesc invalid;
    invalid.kind = AudioInsertKind::Gain;
    invalid.params.gain = -5.0F;
    EXPECT_FALSE(insert.Configure(invalid, kTestSampleRate));
    EXPECT_EQ(insert.GetDescription(), installed);
    EXPECT_TRUE(insert.IsActive());

    // A bad sample rate is refused too: coefficients for the wrong rate are worse than none.
    EXPECT_FALSE(insert.Configure(MakeGainDesc(0.5F), 1000.0F));

    std::vector<float> samples{0.5F};
    insert.Process(samples.data(), samples.size());
    EXPECT_FLOAT_EQ(samples[0], 0.25F);
}

TEST(AudioInsertValidationTest, ClearedInsertIsBypassedAndForgetsItsHistory) {
    AudioInsert insert;
    ASSERT_TRUE(insert.Configure(MakeFilterDesc(AudioInsertKind::LowPass, 500.0F), kTestSampleRate));
    std::vector<float> samples = MakeConstantSamples(1.0F, 512U);
    insert.Process(samples.data(), samples.size());
    EXPECT_TRUE(insert.IsActive());

    insert.Clear();
    EXPECT_FALSE(insert.IsActive());
    std::vector<float> passthrough{0.75F};
    insert.Process(passthrough.data(), passthrough.size());
    EXPECT_FLOAT_EQ(passthrough[0], 0.75F);

    // Reconfiguring after a clear starts from zeroed history, not from the old filter's state.
    ASSERT_TRUE(insert.Configure(MakeFilterDesc(AudioInsertKind::LowPass, 500.0F), kTestSampleRate));
    std::vector<float> fresh = MakeConstantSamples(1.0F, 512U);
    insert.Process(fresh.data(), fresh.size());
    EXPECT_LT(fresh[0], 0.01F);
}

// --- Gain. ---

TEST(AudioInsertDspTest, GainInsertScalesByItsMultiplier) {
    AudioInsert insert;
    ASSERT_TRUE(insert.Configure(MakeGainDesc(2.0F), kTestSampleRate));
    std::vector<float> samples{0.25F, -0.5F, 0.0F};
    insert.Process(samples.data(), samples.size());
    EXPECT_FLOAT_EQ(samples[0], 0.5F);
    EXPECT_FLOAT_EQ(samples[1], -1.0F);
    EXPECT_FLOAT_EQ(samples[2], 0.0F);
}

// --- Filters. ---

TEST(AudioInsertDspTest, LowPassKeepsDcAndRejectsFrequenciesWellAboveItsCutoff) {
    AudioInsert insert;
    ASSERT_TRUE(insert.Configure(MakeFilterDesc(AudioInsertKind::LowPass, 1000.0F), kTestSampleRate));

    std::vector<float> dc = MakeConstantSamples(1.0F, 4800U);
    insert.Process(dc.data(), dc.size());
    EXPECT_NEAR(dc.back(), 1.0F, 1.0e-4F);

    insert.Clear();
    ASSERT_TRUE(insert.Configure(MakeFilterDesc(AudioInsertKind::LowPass, 1000.0F), kTestSampleRate));
    std::vector<float> highTone = MakeSineSamples(1.0F, 12000.0F, kTestSampleRate, kOneSecond);
    insert.Process(highTone.data(), highTone.size());
    const float inputRms = TailRms(MakeSineSamples(1.0F, 12000.0F, kTestSampleRate, kOneSecond), 0U);
    const float outputRms = TailRms(highTone, kOneSecond / 2U);
    // A 2nd-order section is ~43 dB down one decade above cutoff; anything near unity means the
    // coefficients are not what they claim to be.
    EXPECT_LT(outputRms / inputRms, 0.05F);
}

TEST(AudioInsertDspTest, HighPassRemovesDcAndPassesFrequenciesWellAboveItsCutoff) {
    AudioInsert insert;
    ASSERT_TRUE(insert.Configure(MakeFilterDesc(AudioInsertKind::HighPass, 200.0F), kTestSampleRate));

    std::vector<float> dc = MakeConstantSamples(1.0F, 4800U);
    insert.Process(dc.data(), dc.size());
    EXPECT_LT(std::fabs(dc.back()), 1.0e-4F);

    insert.Clear();
    ASSERT_TRUE(insert.Configure(MakeFilterDesc(AudioInsertKind::HighPass, 1000.0F), kTestSampleRate));
    const std::vector<float> input = MakeSineSamples(1.0F, 12000.0F, kTestSampleRate, kOneSecond);
    std::vector<float> processed = input;
    insert.Process(processed.data(), processed.size());
    const float ratio = TailRms(processed, kOneSecond / 2U) / TailRms(input, kOneSecond / 2U);
    EXPECT_NEAR(ratio, 1.0F, 0.05F);
}

TEST(AudioInsertDspTest, PeakEqBoostsExactlyItsCentreBand) {
    AudioInsertDesc desc = MakeFilterDesc(AudioInsertKind::PeakEq, 1000.0F, 2.0F);
    desc.params.gainDb = 12.0F;
    AudioInsert insert;
    ASSERT_TRUE(insert.Configure(desc, kTestSampleRate));

    const std::vector<float> input = MakeSineSamples(0.5F, 1000.0F, kTestSampleRate, kOneSecond);
    std::vector<float> processed = input;
    insert.Process(processed.data(), processed.size());
    const float ratio = TailRms(processed, kOneSecond / 2U) / TailRms(input, kOneSecond / 2U);
    // RBJ defines the peak at the centre frequency as 10^(dB/20) exactly: +12 dB is 3.98x.
    EXPECT_NEAR(ratio, 3.9810717F, 0.05F);
}

TEST(AudioInsertDspTest, ACutoffAboveNyquistIsClampedInsteadOfDestabilisingTheFilter) {
    AudioInsert insert;
    ASSERT_TRUE(insert.Configure(MakeFilterDesc(AudioInsertKind::LowPass, 100000.0F), kTestSampleRate));
    std::vector<float> samples = MakeSineSamples(1.0F, 1000.0F, kTestSampleRate, 4800U);
    insert.Process(samples.data(), samples.size());
    EXPECT_TRUE(std::isfinite(samples.back()));
    EXPECT_LE(PeakMagnitude(samples, 0U), 4.0F);
}

// --- Dynamics. ---

TEST(AudioInsertDspTest, CompressorReducesAboveItsThresholdAndIsTransparentAtUnityRatio) {
    AudioInsertDesc transparent;
    transparent.kind = AudioInsertKind::Compressor;
    transparent.params.thresholdDb = -20.0F;
    transparent.params.ratio = 1.0F;
    AudioInsert insert;
    ASSERT_TRUE(insert.Configure(transparent, kTestSampleRate));

    const std::vector<float> input = MakeSineSamples(1.0F, 1000.0F, kTestSampleRate, kOneSecond);
    std::vector<float> processed = input;
    insert.Process(processed.data(), processed.size());
    for (std::size_t index = 0U; index < processed.size(); ++index) {
        ASSERT_FLOAT_EQ(processed[index], input[index]) << "index " << index;
    }

    AudioInsertDesc compressing = transparent;
    compressing.params.ratio = 4.0F;
    compressing.params.attackMs = 5.0F;
    compressing.params.releaseMs = 200.0F;
    ASSERT_TRUE(insert.Configure(compressing, kTestSampleRate));
    processed = input;
    insert.Process(processed.data(), processed.size());
    // 20 dB over the threshold at 4:1 reduces by 15 dB: 0.7071 sine -> about 0.126 RMS.
    const float ratio = TailRms(processed, kOneSecond / 2U) / TailRms(input, kOneSecond / 2U);
    EXPECT_NEAR(ratio, 0.17782794F, 0.03F);
}

TEST(AudioInsertDspTest, LimiterNeverLetsASamplePastItsCeiling) {
    AudioInsertDesc desc;
    desc.kind = AudioInsertKind::Limiter;
    desc.params.thresholdDb = -6.0F;
    desc.params.releaseMs = 100.0F;
    AudioInsert insert;
    ASSERT_TRUE(insert.Configure(desc, kTestSampleRate));

    // A deliberately hot block, including a sample well past full scale.
    std::vector<float> samples = MakeSineSamples(1.0F, 220.0F, kTestSampleRate, kOneSecond);
    samples[100U] = 3.0F;
    samples[200U] = -3.0F;
    insert.Process(samples.data(), samples.size());

    const float ceiling = std::pow(10.0F, -6.0F / 20.0F);
    EXPECT_LE(PeakMagnitude(samples, 0U), ceiling + 1.0e-6F);
    // The two hot samples are held exactly at the ceiling, not merely near it.
    EXPECT_NEAR(samples[100U], ceiling, 1.0e-6F);
    EXPECT_NEAR(samples[200U], -ceiling, 1.0e-6F);
}

// --- Reverb. ---

TEST(AudioInsertDspTest, ReverbIsTransparentWithNoWetMixAndProducesADecayingTailOtherwise) {
    AudioInsertDesc desc;
    desc.kind = AudioInsertKind::Reverb;
    desc.params.wet = 0.0F;
    desc.params.dry = 1.0F;
    AudioInsert insert;
    ASSERT_TRUE(insert.Configure(desc, kTestSampleRate));
    std::vector<float> dry{1.0F, -0.5F, 0.25F};
    insert.Process(dry.data(), dry.size());
    EXPECT_FLOAT_EQ(dry[0], 1.0F);
    EXPECT_FLOAT_EQ(dry[1], -0.5F);
    EXPECT_FLOAT_EQ(dry[2], 0.25F);

    desc.params.wet = 1.0F;
    desc.params.dry = 0.0F;
    desc.params.roomSize = 0.8F;
    desc.params.damping = 0.2F;
    ASSERT_TRUE(insert.Configure(desc, kTestSampleRate));

    std::vector<float> tail(kOneSecond * 8U, 0.0F);
    for (std::size_t index = 0U; index < 128U; ++index) tail[index] = 0.5F;
    insert.Process(tail.data(), tail.size());

    EXPECT_GT(TailRms(tail, 4800U), 1.0e-4F);
    EXPECT_LT(PeakMagnitude(tail, tail.size() - 1000U), 1.0e-3F);
}

TEST(AudioInsertDspTest, ReverbSurvivesASampleRateChangeAndStaysFinite) {
    AudioInsertDesc desc;
    desc.kind = AudioInsertKind::Reverb;
    AudioInsert insert;
    ASSERT_TRUE(insert.Configure(desc, kTestSampleRate));
    ASSERT_TRUE(insert.SetSampleRate(44100.0F));
    EXPECT_TRUE(insert.IsActive());

    std::vector<float> samples = MakeSineSamples(0.5F, 440.0F, 44100.0F, 4410U);
    insert.Process(samples.data(), samples.size());
    EXPECT_TRUE(std::isfinite(samples.back()));
    EXPECT_LE(PeakMagnitude(samples, 0U), 8.0F);
}

// --- Chain. ---

TEST(AudioInsertChainTest, ChainRunsSlotsInOrderAndClampsExactlyOnce) {
    AudioBusInsertChain chain;
    ASSERT_TRUE(chain.SetInsert(0U, MakeGainDesc(4.0F)));
    ASSERT_TRUE(chain.SetInsert(1U, MakeGainDesc(0.1F)));
    EXPECT_TRUE(chain.HasActiveInserts());

    const std::vector<float> input{0.9F, -0.9F};
    std::vector<float> output(input.size(), 0.0F);
    chain.Process(input.data(), output.data(), input.size());
    // 0.9 * 4 = 3.6, * 0.1 = 0.36 - no intermediate clip, one clamp at the end (which does not bite).
    EXPECT_FLOAT_EQ(output[0], 0.36F);
    EXPECT_FLOAT_EQ(output[1], -0.36F);

    // The final clamp is real: a single huge gain still lands on full scale, not beyond it.
    ASSERT_TRUE(chain.ClearInsert(1U));
    ASSERT_TRUE(chain.SetInsert(1U, MakeGainDesc(4.0F)));
    chain.Process(input.data(), output.data(), input.size());
    EXPECT_FLOAT_EQ(output[0], 1.0F);
    EXPECT_FLOAT_EQ(output[1], -1.0F);
}

TEST(AudioInsertChainTest, EmptyChainCopiesAndNonFiniteInputIsScrubbed) {
    AudioBusInsertChain chain;
    EXPECT_FALSE(chain.HasActiveInserts());
    const std::vector<float> input{0.5F, std::numeric_limits<float>::quiet_NaN(),
                                   std::numeric_limits<float>::infinity(), -0.25F};
    std::vector<float> output(input.size(), 0.0F);
    chain.Process(input.data(), output.data(), input.size());
    EXPECT_FLOAT_EQ(output[0], 0.5F);
    EXPECT_FLOAT_EQ(output[1], 0.0F);
    EXPECT_FLOAT_EQ(output[2], 0.0F);
    EXPECT_FLOAT_EQ(output[3], -0.25F);
}

TEST(AudioInsertChainTest, ChainValidatesSlotIndicesAndReportsConfiguredInserts) {
    AudioBusInsertChain chain;
    AudioInsertDesc read;
    EXPECT_FALSE(chain.SetInsert(kMaximumAudioBusInserts, MakeGainDesc(1.0F)));
    EXPECT_FALSE(chain.GetInsert(0U, read));
    EXPECT_FALSE(chain.ClearInsert(kMaximumAudioBusInserts));

    ASSERT_TRUE(chain.SetInsert(0U, MakeGainDesc(0.5F)));
    ASSERT_TRUE(chain.GetInsert(0U, read));
    EXPECT_EQ(read, MakeGainDesc(0.5F));
    EXPECT_TRUE(chain.ClearInsert(0U));
    EXPECT_FALSE(chain.GetInsert(0U, read));
    EXPECT_FALSE(chain.HasActiveInserts());
}

TEST(AudioInsertChainTest, ZeroFramesAndNullBuffersAreSafeNoOps) {
    AudioBusInsertChain chain;
    ASSERT_TRUE(chain.SetInsert(0U, MakeGainDesc(2.0F)));
    float sample = 0.5F;
    chain.ProcessInPlace(&sample, 0U);
    EXPECT_FLOAT_EQ(sample, 0.5F);
    chain.ProcessInPlace(nullptr, 4U);
    chain.Process(nullptr, nullptr, 4U);
    EXPECT_FLOAT_EQ(sample, 0.5F);
}

} // namespace
} // namespace UVE::Audio::Tests
