// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/asset/animation_clip_compression_uve.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

#include <gtest/gtest.h>

namespace UVE::Asset::Tests {
namespace {

[[nodiscard]] Math::QuaternionUVE AxisAngleUVE(Math::Vector3UVE axis,
                                               const float radians) noexcept {
    const float length = std::sqrt((axis.x * axis.x) + (axis.y * axis.y) + (axis.z * axis.z));
    if (length > 0.0F) {
        axis = Math::Vector3UVE{axis.x / length, axis.y / length, axis.z / length};
    }
    const float half = radians * 0.5F;
    const float scale = std::sin(half);
    return Math::QuaternionUVE{axis.x * scale, axis.y * scale, axis.z * scale, std::cos(half)};
}

[[nodiscard]] AnimationAssetPoseUVE MakePoseUVE(const Math::Vector3UVE& position,
                                                const Math::QuaternionUVE& rotation,
                                                const Math::Vector3UVE& scale) noexcept {
    return AnimationAssetPoseUVE{position, rotation, scale};
}

[[nodiscard]] AnimationClipAssetUVE MakeBaseClipUVE(const std::string& clipId,
                                                    const double durationSeconds) {
    AnimationClipAssetUVE clip;
    clip.clipId = clipId;
    clip.durationSeconds = durationSeconds;
    clip.rest = {
        AnimationAssetRestBoneUVE{"Hips", -1, {0.0F, 1.0F, 0.0F}, {0.0F, 0.0F, 0.0F, 1.0F},
                                  {1.0F, 1.0F, 1.0F}},
        AnimationAssetRestBoneUVE{"Spine", 0, {0.0F, 0.1F, 0.0F}, {0.0F, 0.0F, 0.0F, 1.0F},
                                  {1.0F, 1.0F, 1.0F}},
    };
    clip.events = {
        AnimationAssetEventUVE{durationSeconds * 0.25, "footstep_l"},
        AnimationAssetEventUVE{durationSeconds * 0.75, "footstep_r"},
    };
    return clip;
}

[[nodiscard]] AnimationClipAssetUVE MakeLinearClipUVE(const std::size_t keyCount) {
    AnimationClipAssetUVE clip = MakeBaseClipUVE("linear", 2.0);
    clip.samples.reserve(keyCount);
    for (std::size_t index = 0U; index < keyCount; ++index) {
        const double fraction =
            keyCount <= 1U ? 0.0 : static_cast<double>(index) / static_cast<double>(keyCount - 1U);
        clip.samples.push_back(AnimationAssetSampleUVE{
            fraction * clip.durationSeconds,
            MakePoseUVE({static_cast<float>(fraction) * 2.0F, 1.0F, 0.0F},
                        {0.0F, 0.0F, 0.0F, 1.0F}, {1.0F, 1.0F, 1.0F})});
    }
    AnimationAssetBoneTrackUVE spine;
    spine.bone = "Spine";
    for (std::size_t index = 0U; index < keyCount; ++index) {
        const double fraction =
            keyCount <= 1U ? 0.0 : static_cast<double>(index) / static_cast<double>(keyCount - 1U);
        spine.samples.push_back(AnimationAssetSampleUVE{
            fraction * clip.durationSeconds,
            MakePoseUVE({0.0F, 0.1F, 0.0F}, AxisAngleUVE({0.0F, 1.0F, 0.0F},
                                                        static_cast<float>(fraction) * 1.2F),
                        {1.0F, 1.0F, 1.0F})});
    }
    clip.bones.push_back(std::move(spine));
    return clip;
}

[[nodiscard]] AnimationClipAssetUVE MakeSineClipUVE(const std::size_t keyCount,
                                                    const std::uint32_t seed) {
    AnimationClipAssetUVE clip = MakeBaseClipUVE("sine", 3.0);
    std::uint32_t state = seed;
    const auto next = [&state]() noexcept {
        state = (state * 1664525U) + 1013904223U;
        return state;
    };
    for (std::size_t index = 0U; index < keyCount; ++index) {
        const double fraction =
            keyCount <= 1U ? 0.0 : static_cast<double>(index) / static_cast<double>(keyCount - 1U);
        const float wave = std::sin(static_cast<float>(fraction) * 25.1327412287F); // 8 full turns
        const float noise = (static_cast<float>(next() % 2048U) / 2048.0F) - 0.5F;
        clip.samples.push_back(AnimationAssetSampleUVE{
            fraction * clip.durationSeconds,
            MakePoseUVE({wave * 0.5F, 1.0F + (noise * 0.002F), 0.0F},
                        AxisAngleUVE({0.0F, 1.0F, 0.0F}, wave * 2.0F), {1.0F, 1.0F, 1.0F})});
    }
    return clip;
}

[[nodiscard]] AnimationClipAssetUVE MakeConstantClipUVE(const std::size_t keyCount) {
    AnimationClipAssetUVE clip = MakeBaseClipUVE("constant", 1.0);
    for (std::size_t index = 0U; index < keyCount; ++index) {
        clip.samples.push_back(AnimationAssetSampleUVE{
            static_cast<double>(index) / static_cast<double>(keyCount),
            MakePoseUVE({3.25F, -0.5F, 0.0F}, AxisAngleUVE({0.0F, 1.0F, 0.0F}, 0.7F),
                        {1.0F, 2.0F, 1.0F})});
    }
    return clip;
}

[[nodiscard]] AnimationCompressionSettingsUVE DefaultSettingsUVE() noexcept {
    return AnimationCompressionSettingsUVE{};
}

} // namespace

TEST(AnimationClipCompressionUVETest, IsAnimationCompressionSettingsValidUVE_RejectsMisbehavingTolerances) {
    EXPECT_TRUE(IsAnimationCompressionSettingsValidUVE(DefaultSettingsUVE()));

    AnimationCompressionSettingsUVE settings = DefaultSettingsUVE();
    settings.maximumPositionErrorMetres = 0.0;
    EXPECT_FALSE(IsAnimationCompressionSettingsValidUVE(settings));
    settings.maximumPositionErrorMetres = -0.001;
    EXPECT_FALSE(IsAnimationCompressionSettingsValidUVE(settings));
    settings.maximumPositionErrorMetres = std::numeric_limits<double>::quiet_NaN();
    EXPECT_FALSE(IsAnimationCompressionSettingsValidUVE(settings));
    settings = DefaultSettingsUVE();
    settings.maximumRotationErrorRadians =
        AnimationCompressionSettingsUVE::kMinimumRotationErrorRadiansUVE * 0.5;
    EXPECT_FALSE(IsAnimationCompressionSettingsValidUVE(settings));
    settings = DefaultSettingsUVE();
    settings.maximumScaleError =
        AnimationCompressionSettingsUVE::kMaximumScaleErrorUVE * 2.0;
    EXPECT_FALSE(IsAnimationCompressionSettingsValidUVE(settings));
}

TEST(AnimationClipCompressionUVETest, CompressAnimationClipAssetUVE_RejectsInvalidInputAndFailsClosed) {
    const AnimationCompressionSettingsUVE settings = DefaultSettingsUVE();
    CompressedAnimationClipUVE sentinel;
    sentinel.clipId = "untouched";
    sentinel.durationSeconds = 42.0;
    const CompressedAnimationClipUVE originalSentinel = sentinel;

    AnimationClipAssetUVE clip = MakeLinearClipUVE(8U);
    clip.samples[3].pose.position.x = std::numeric_limits<float>::quiet_NaN();
    EXPECT_FALSE(CompressAnimationClipAssetUVE(clip, settings, sentinel));
    EXPECT_EQ(sentinel, originalSentinel);

    clip = MakeLinearClipUVE(8U);
    clip.samples[2].timeSeconds = 0.1;
    clip.samples[3].timeSeconds = 0.05; // unsorted
    EXPECT_FALSE(CompressAnimationClipAssetUVE(clip, settings, sentinel));
    EXPECT_EQ(sentinel, originalSentinel);

    clip = MakeLinearClipUVE(8U);
    clip.durationSeconds = 0.0;
    EXPECT_FALSE(CompressAnimationClipAssetUVE(clip, settings, sentinel));
    EXPECT_EQ(sentinel, originalSentinel);

    AnimationCompressionSettingsUVE invalidSettings = settings;
    invalidSettings.maximumRotationErrorRadians = -1.0;
    clip = MakeLinearClipUVE(8U);
    EXPECT_FALSE(CompressAnimationClipAssetUVE(clip, invalidSettings, sentinel));
    EXPECT_EQ(sentinel, originalSentinel);
}

TEST(AnimationClipCompressionUVETest, CompressAnimationClipAssetUVE_MeetsErrorBoundsOnLinearMotion) {
    const AnimationClipAssetUVE clip = MakeLinearClipUVE(1000U);
    const AnimationCompressionSettingsUVE settings = DefaultSettingsUVE();
    CompressedAnimationClipUVE compressed;
    ASSERT_TRUE(CompressAnimationClipAssetUVE(clip, settings, compressed));
    ASSERT_TRUE(IsCompressedAnimationClipValidUVE(compressed));

    const AnimationCompressionErrorReportUVE report =
        MeasureAnimationCompressionErrorUVE(clip, compressed, 512U);
    EXPECT_EQ(report.probeCount, 512U);
    EXPECT_LE(report.maximumPositionErrorMetres, settings.maximumPositionErrorMetres);
    EXPECT_LE(report.maximumRotationErrorRadians, settings.maximumRotationErrorRadians);
    EXPECT_LE(report.maximumScaleError, settings.maximumScaleError);

    EXPECT_EQ(compressed.clipId, clip.clipId);
    EXPECT_EQ(compressed.durationSeconds, clip.durationSeconds);
    EXPECT_EQ(compressed.events, clip.events);
    EXPECT_EQ(compressed.rest, clip.rest);
    EXPECT_EQ(compressed.bones.size(), clip.bones.size());
    EXPECT_EQ(compressed.bones[0].bone, "Spine");
}

TEST(AnimationClipCompressionUVETest, CompressAnimationClipAssetUVE_ReducesRedundantKeysAndShrinksBytes) {
    const AnimationClipAssetUVE clip = MakeLinearClipUVE(1000U);
    CompressedAnimationClipUVE compressed;
    ASSERT_TRUE(CompressAnimationClipAssetUVE(clip, DefaultSettingsUVE(), compressed));

    EXPECT_EQ(compressed.object.KeyCountUVE(), 2U); // exactly linear: endpoints suffice
    EXPECT_LE(compressed.bones[0].KeyCountUVE(), 64U); // smooth arc: far fewer than 1000
    const std::size_t compressedBytes = GetCompressedAnimationClipByteSizeUVE(compressed);
    const std::size_t referenceBytes = GetAnimationClipAssetSampleByteSizeUVE(clip);
    EXPECT_GT(referenceBytes, 0U);
    EXPECT_LT(compressedBytes, referenceBytes / 2U);
}

TEST(AnimationClipCompressionUVETest, CompressAnimationClipAssetUVE_IsDeterministic) {
    const AnimationClipAssetUVE clip = MakeSineClipUVE(257U, 7U);
    CompressedAnimationClipUVE first;
    CompressedAnimationClipUVE second;
    ASSERT_TRUE(CompressAnimationClipAssetUVE(clip, DefaultSettingsUVE(), first));
    ASSERT_TRUE(CompressAnimationClipAssetUVE(clip, DefaultSettingsUVE(), second));
    EXPECT_EQ(first, second);
}

TEST(AnimationClipCompressionUVETest, CompressAnimationClipAssetUVE_HandlesSingleTwoAndConstantKeyClips) {
    for (const std::size_t keyCount : {1U, 2U, 17U}) {
        const AnimationClipAssetUVE clip = MakeConstantClipUVE(keyCount);
        CompressedAnimationClipUVE compressed;
        ASSERT_TRUE(CompressAnimationClipAssetUVE(clip, DefaultSettingsUVE(), compressed))
            << "keyCount=" << keyCount;
        ASSERT_TRUE(IsCompressedAnimationClipValidUVE(compressed)) << "keyCount=" << keyCount;
        EXPECT_LE(compressed.object.KeyCountUVE(), 2U) << "keyCount=" << keyCount;
        const AnimationCompressionErrorReportUVE report =
            MeasureAnimationCompressionErrorUVE(clip, compressed, 64U);
        EXPECT_LE(report.maximumPositionErrorMetres,
                  DefaultSettingsUVE().maximumPositionErrorMetres)
            << "keyCount=" << keyCount;
    }
}

TEST(AnimationClipCompressionUVETest, TrySampleCompressedAnimationTrackUVE_DecodesConstantAxesExactly) {
    AnimationClipAssetUVE clip = MakeBaseClipUVE("axes", 1.0);
    clip.samples = {
        AnimationAssetSampleUVE{0.0, MakePoseUVE({1.25F, 0.5F, -2.0F}, {0.0F, 0.0F, 0.0F, 1.0F},
                                                 {1.0F, 1.0F, 1.0F})},
        AnimationAssetSampleUVE{1.0, MakePoseUVE({3.25F, 0.5F, -2.0F}, {0.0F, 0.0F, 0.0F, 1.0F},
                                                 {1.0F, 1.0F, 1.0F})},
    };
    CompressedAnimationClipUVE compressed;
    ASSERT_TRUE(CompressAnimationClipAssetUVE(clip, DefaultSettingsUVE(), compressed));
    ASSERT_EQ(compressed.object.KeyCountUVE(), 2U);
    EXPECT_FLOAT_EQ(compressed.object.positionRangeExtent.y, 0.0F);
    EXPECT_FLOAT_EQ(compressed.object.positionRangeExtent.z, 0.0F);

    AnimationAssetPoseUVE pose{};
    ASSERT_TRUE(TrySampleCompressedAnimationTrackUVE(compressed.object, compressed.durationSeconds,
                                                     0.3, false, pose));
    EXPECT_FLOAT_EQ(pose.position.y, 0.5F); // zero-extent axes decode exactly
    EXPECT_FLOAT_EQ(pose.position.z, -2.0F);
    EXPECT_FLOAT_EQ(pose.scale.x, 1.0F);
    EXPECT_NEAR(pose.position.x, 1.85F, 1.0e-3); // lerp midpoint within tolerance
}

TEST(AnimationClipCompressionUVETest, CompressAnimationClipAssetUVE_HandlesQuaternionSignFlipsAndExtremePairs) {
    // q and -q are the same rotation: alternating signs must not produce rotation error.
    const Math::QuaternionUVE rotation = AxisAngleUVE({0.0F, 1.0F, 0.0F}, 0.9F);
    AnimationClipAssetUVE clip = MakeBaseClipUVE("signs", 1.0);
    clip.samples.clear();
    for (std::size_t index = 0U; index < 5U; ++index) {
        const float sign = (index % 2U) == 0U ? 1.0F : -1.0F;
        const Math::QuaternionUVE signedRotation{rotation.x * sign, rotation.y * sign,
                                                 rotation.z * sign, rotation.w * sign};
        clip.samples.push_back(AnimationAssetSampleUVE{
            static_cast<double>(index) / 4.0,
            MakePoseUVE({0.0F, 0.0F, 0.0F}, signedRotation, {1.0F, 1.0F, 1.0F})});
    }
    CompressedAnimationClipUVE compressed;
    ASSERT_TRUE(CompressAnimationClipAssetUVE(clip, DefaultSettingsUVE(), compressed));
    const AnimationCompressionErrorReportUVE signReport =
        MeasureAnimationCompressionErrorUVE(clip, compressed, 128U);
    EXPECT_LE(signReport.maximumRotationErrorRadians,
              DefaultSettingsUVE().maximumRotationErrorRadians);

    // A near-180-degree pair exercises the nlerp path without a degenerate midpoint.
    clip = MakeBaseClipUVE("extreme", 1.0);
    clip.samples = {
        AnimationAssetSampleUVE{0.0,
                                MakePoseUVE({0.0F, 0.0F, 0.0F},
                                            AxisAngleUVE({0.0F, 1.0F, 0.0F}, 0.0F),
                                            {1.0F, 1.0F, 1.0F})},
        AnimationAssetSampleUVE{1.0,
                                MakePoseUVE({0.0F, 0.0F, 0.0F},
                                            AxisAngleUVE({0.0F, 1.0F, 0.0F}, 3.13159265F),
                                            {1.0F, 1.0F, 1.0F})},
    };
    ASSERT_TRUE(CompressAnimationClipAssetUVE(clip, DefaultSettingsUVE(), compressed));
    const AnimationCompressionErrorReportUVE extremeReport =
        MeasureAnimationCompressionErrorUVE(clip, compressed, 128U);
    EXPECT_LE(extremeReport.maximumRotationErrorRadians,
              DefaultSettingsUVE().maximumRotationErrorRadians);
}

TEST(AnimationClipCompressionUVETest, TrySampleCompressedAnimationTrackUVE_MatchesClampAndLoopSemantics) {
    const AnimationClipAssetUVE clip = MakeSineClipUVE(64U, 11U);
    CompressedAnimationClipUVE compressed;
    ASSERT_TRUE(CompressAnimationClipAssetUVE(clip, DefaultSettingsUVE(), compressed));
    const double duration = compressed.durationSeconds;

    AnimationAssetPoseUVE atStart{};
    AnimationAssetPoseUVE clampedBefore{};
    ASSERT_TRUE(TrySampleCompressedAnimationTrackUVE(compressed.object, duration, 0.0, false,
                                                     atStart));
    ASSERT_TRUE(TrySampleCompressedAnimationTrackUVE(compressed.object, duration, -1.5, false,
                                                     clampedBefore));
    EXPECT_NEAR(clampedBefore.position.x, atStart.position.x, 1.0e-6);
    EXPECT_NEAR(clampedBefore.position.y, atStart.position.y, 1.0e-6);

    AnimationAssetPoseUVE atEnd{};
    AnimationAssetPoseUVE clampedAfter{};
    ASSERT_TRUE(TrySampleCompressedAnimationTrackUVE(compressed.object, duration, duration, false,
                                                     atEnd));
    ASSERT_TRUE(TrySampleCompressedAnimationTrackUVE(compressed.object, duration, duration + 2.5,
                                                     false, clampedAfter));
    EXPECT_NEAR(clampedAfter.position.x, atEnd.position.x, 1.0e-6);

    AnimationAssetPoseUVE wrapped{};
    AnimationAssetPoseUVE direct{};
    ASSERT_TRUE(TrySampleCompressedAnimationTrackUVE(compressed.object, duration,
                                                     duration + (duration * 0.5), true, wrapped));
    ASSERT_TRUE(TrySampleCompressedAnimationTrackUVE(compressed.object, duration, duration * 0.5,
                                                     false, direct));
    EXPECT_NEAR(wrapped.position.x, direct.position.x, 1.0e-6);
    EXPECT_NEAR(wrapped.rotation.x, direct.rotation.x, 1.0e-5);
    EXPECT_NEAR(wrapped.rotation.w, direct.rotation.w, 1.0e-5);

    AnimationAssetPoseUVE negativeWrap{};
    ASSERT_TRUE(TrySampleCompressedAnimationTrackUVE(compressed.object, duration, -0.5, true,
                                                     negativeWrap));
    AnimationAssetPoseUVE negativeDirect{};
    ASSERT_TRUE(TrySampleCompressedAnimationTrackUVE(compressed.object, duration, duration - 0.5,
                                                     false, negativeDirect));
    EXPECT_NEAR(negativeWrap.position.x, negativeDirect.position.x, 1.0e-6);
}

TEST(AnimationClipCompressionUVETest, TryDecompressAnimationClipAssetUVE_ResamplesAtStoredKeys) {
    const AnimationClipAssetUVE clip = MakeSineClipUVE(97U, 23U);
    CompressedAnimationClipUVE compressed;
    ASSERT_TRUE(CompressAnimationClipAssetUVE(clip, DefaultSettingsUVE(), compressed));

    AnimationClipAssetUVE decompressed;
    ASSERT_TRUE(TryDecompressAnimationClipAssetUVE(compressed, decompressed));
    ASSERT_TRUE(IsAnimationClipAssetValidUVE(decompressed));
    EXPECT_EQ(decompressed.clipId, clip.clipId);
    EXPECT_EQ(decompressed.events, clip.events);
    EXPECT_EQ(decompressed.rest, clip.rest);
    EXPECT_EQ(decompressed.samples.size(), compressed.object.KeyCountUVE());
    ASSERT_EQ(decompressed.bones.size(), compressed.bones.size());
    for (std::size_t index = 0U; index < decompressed.bones.size(); ++index) {
        EXPECT_EQ(decompressed.bones[index].samples.size(), compressed.bones[index].KeyCountUVE());
    }

    for (const AnimationAssetSampleUVE& sample : decompressed.samples) {
        AnimationAssetPoseUVE sampled{};
        ASSERT_TRUE(TrySampleCompressedAnimationTrackUVE(compressed.object, compressed.durationSeconds,
                                                         sample.timeSeconds, false, sampled));
        EXPECT_NEAR(sampled.position.x, sample.pose.position.x, 1.0e-5);
        EXPECT_NEAR(sampled.position.y, sample.pose.position.y, 1.0e-5);
        EXPECT_NEAR(sampled.rotation.w, sample.pose.rotation.w, 1.0e-5);
    }
}

TEST(AnimationClipCompressionUVETest, CompressAnimationClipAssetUVE_UsesAdaptiveBitsForHugeRanges) {
    AnimationClipAssetUVE clip = MakeBaseClipUVE("huge", 1.0);
    clip.samples = {
        AnimationAssetSampleUVE{0.0, MakePoseUVE({0.0F, 0.0F, 0.0F}, {0.0F, 0.0F, 0.0F, 1.0F},
                                                 {1.0F, 1.0F, 1.0F})},
        AnimationAssetSampleUVE{1.0, MakePoseUVE({500.0F, 0.0F, 0.0F}, {0.0F, 0.0F, 0.0F, 1.0F},
                                                 {1.0F, 8000.0F, 1.0F})},
    };
    CompressedAnimationClipUVE compressed;
    ASSERT_TRUE(CompressAnimationClipAssetUVE(clip, DefaultSettingsUVE(), compressed));
    EXPECT_EQ(compressed.object.positionComponentBits, 32U);
    EXPECT_EQ(compressed.object.scaleComponentBits, 32U); // exercises the 35-byte record stride

    const AnimationCompressionErrorReportUVE report =
        MeasureAnimationCompressionErrorUVE(clip, compressed, 256U);
    EXPECT_LE(report.maximumPositionErrorMetres,
              DefaultSettingsUVE().maximumPositionErrorMetres);
    EXPECT_LE(report.maximumScaleError, DefaultSettingsUVE().maximumScaleError);
}

TEST(AnimationClipCompressionUVETest, CompressAnimationClipAssetUVE_KeepsHighFrequencyMotionWithinBounds) {
    const AnimationClipAssetUVE clip = MakeSineClipUVE(400U, 4242U);
    const AnimationCompressionSettingsUVE settings = DefaultSettingsUVE();
    CompressedAnimationClipUVE compressed;
    ASSERT_TRUE(CompressAnimationClipAssetUVE(clip, settings, compressed));
    ASSERT_TRUE(IsCompressedAnimationClipValidUVE(compressed));
    EXPECT_LE(compressed.object.KeyCountUVE(), 400U);

    const AnimationCompressionErrorReportUVE report =
        MeasureAnimationCompressionErrorUVE(clip, compressed, 1024U);
    EXPECT_LE(report.maximumPositionErrorMetres, settings.maximumPositionErrorMetres);
    EXPECT_LE(report.maximumRotationErrorRadians, settings.maximumRotationErrorRadians);
    EXPECT_LE(report.maximumScaleError, settings.maximumScaleError);
}

TEST(AnimationClipCompressionUVETest, CompressAnimationClipAssetUVE_KeepsRootMotionTravelWithinBounds) {
    AnimationClipAssetUVE clip = MakeBaseClipUVE("root", 2.0);
    // Uneven gait: positions advance with pauses and bursts, the classic root-motion case.
    const double times[6] = {0.0, 0.2, 0.9, 1.1, 1.35, 2.0};
    const float travel[6] = {0.0F, 0.02F, 1.6F, 1.75F, 1.9F, 2.0F};
    clip.samples.clear();
    for (std::size_t index = 0U; index < 6U; ++index) {
        clip.samples.push_back(AnimationAssetSampleUVE{
            times[index],
            MakePoseUVE({travel[index], 0.0F, 0.0F}, {0.0F, 0.0F, 0.0F, 1.0F},
                        {1.0F, 1.0F, 1.0F})});
    }
    CompressedAnimationClipUVE compressed;
    ASSERT_TRUE(CompressAnimationClipAssetUVE(clip, DefaultSettingsUVE(), compressed));
    const AnimationCompressionErrorReportUVE report =
        MeasureAnimationCompressionErrorUVE(clip, compressed, 512U);
    EXPECT_LE(report.maximumPositionErrorMetres,
              DefaultSettingsUVE().maximumPositionErrorMetres);
}

TEST(AnimationClipCompressionUVETest, IsCompressedAnimationClipValidUVE_RejectsCorruptStreams) {
    const AnimationClipAssetUVE clip = MakeLinearClipUVE(8U);
    CompressedAnimationClipUVE compressed;
    ASSERT_TRUE(CompressAnimationClipAssetUVE(clip, DefaultSettingsUVE(), compressed));
    ASSERT_TRUE(IsCompressedAnimationClipValidUVE(compressed));

    CompressedAnimationClipUVE corrupt = compressed;
    corrupt.object.keyData.pop_back(); // stride mismatch
    EXPECT_FALSE(IsCompressedAnimationClipValidUVE(corrupt));

    corrupt = compressed;
    corrupt.object.positionComponentBits = 24U; // illegal precision
    EXPECT_FALSE(IsCompressedAnimationClipValidUVE(corrupt));

    corrupt = compressed;
    corrupt.object.positionRangeMinimum.x = std::numeric_limits<float>::quiet_NaN();
    EXPECT_FALSE(IsCompressedAnimationClipValidUVE(corrupt));

    corrupt = compressed;
    corrupt.object.positionRangeExtent.y = -1.0F;
    EXPECT_FALSE(IsCompressedAnimationClipValidUVE(corrupt));

    corrupt = compressed;
    corrupt.bones.push_back(corrupt.bones.front()); // duplicate bone name
    EXPECT_FALSE(IsCompressedAnimationClipValidUVE(corrupt));

    corrupt = compressed;
    corrupt.bones.front().bone.clear();
    EXPECT_FALSE(IsCompressedAnimationClipValidUVE(corrupt));

    corrupt = compressed;
    corrupt.durationSeconds = -1.0;
    EXPECT_FALSE(IsCompressedAnimationClipValidUVE(corrupt));

    AnimationAssetPoseUVE pose{};
    EXPECT_FALSE(TrySampleCompressedAnimationTrackUVE(compressed.object, -1.0, 0.5, false, pose));
    corrupt = compressed;
    corrupt.object.keyData.pop_back(); // stride mismatch
    EXPECT_FALSE(TrySampleCompressedAnimationTrackUVE(
        corrupt.object, compressed.durationSeconds, 0.5, false, pose));
    EXPECT_FALSE(TrySampleCompressedAnimationTrackUVE(compressed.object, 1.0,
                                                      std::numeric_limits<double>::quiet_NaN(),
                                                      false, pose));
}

TEST(AnimationClipCompressionUVETest, MeasureAnimationCompressionErrorUVE_RejectsInvalidInputs) {
    const AnimationClipAssetUVE clip = MakeLinearClipUVE(4U);
    CompressedAnimationClipUVE compressed;
    ASSERT_TRUE(CompressAnimationClipAssetUVE(clip, DefaultSettingsUVE(), compressed));

    const AnimationCompressionErrorReportUVE zeroProbes =
        MeasureAnimationCompressionErrorUVE(clip, compressed, 0U);
    EXPECT_EQ(zeroProbes.probeCount, 0U);

    CompressedAnimationClipUVE invalid;
    const AnimationCompressionErrorReportUVE invalidReport =
        MeasureAnimationCompressionErrorUVE(clip, invalid, 16U);
    EXPECT_EQ(invalidReport.probeCount, 0U);
    EXPECT_DOUBLE_EQ(invalidReport.maximumPositionErrorMetres, 0.0);
}

TEST(AnimationClipCompressionUVETest, CompressAnimationClipAssetUVE_AllowsEmptyObjectTrack) {
    AnimationClipAssetUVE clip = MakeBaseClipUVE("bones-only", 1.0);
    AnimationAssetBoneTrackUVE hips;
    hips.bone = "Hips";
    hips.samples = {AnimationAssetSampleUVE{0.0, MakePoseUVE({0.0F, 1.0F, 0.0F},
                                                             {0.0F, 0.0F, 0.0F, 1.0F},
                                                             {1.0F, 1.0F, 1.0F})},
                    AnimationAssetSampleUVE{1.0, MakePoseUVE({0.0F, 1.2F, 0.0F},
                                                              {0.0F, 0.0F, 0.0F, 1.0F},
                                                              {1.0F, 1.0F, 1.0F})}};
    clip.bones.push_back(std::move(hips));
    ASSERT_TRUE(IsAnimationClipAssetValidUVE(clip));

    CompressedAnimationClipUVE compressed;
    ASSERT_TRUE(CompressAnimationClipAssetUVE(clip, DefaultSettingsUVE(), compressed));
    EXPECT_EQ(compressed.object.KeyCountUVE(), 0U);
    EXPECT_EQ(compressed.bones.size(), 1U);
    EXPECT_EQ(compressed.bones[0].bone, "Hips");
    ASSERT_TRUE(IsCompressedAnimationClipValidUVE(compressed));
}

} // namespace UVE::Asset::Tests
