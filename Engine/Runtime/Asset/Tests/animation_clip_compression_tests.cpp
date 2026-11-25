// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "animation_clip_compression.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

#include <gtest/gtest.h>

namespace UVE::Asset::Tests {
namespace {

[[nodiscard]] Math::Quaternion AxisAngle(Math::Vector3 axis,
                                               const float radians) noexcept {
    const float length = std::sqrt((axis.x * axis.x) + (axis.y * axis.y) + (axis.z * axis.z));
    if (length > 0.0F) {
        axis = Math::Vector3{axis.x / length, axis.y / length, axis.z / length};
    }
    const float half = radians * 0.5F;
    const float scale = std::sin(half);
    return Math::Quaternion{axis.x * scale, axis.y * scale, axis.z * scale, std::cos(half)};
}

[[nodiscard]] AnimationAssetPose MakePose(const Math::Vector3& position,
                                                const Math::Quaternion& rotation,
                                                const Math::Vector3& scale) noexcept {
    return AnimationAssetPose{position, rotation, scale};
}

[[nodiscard]] AnimationClipAsset MakeBaseClip(const std::string& clipId,
                                                    const double durationSeconds) {
    AnimationClipAsset clip;
    clip.clipId = clipId;
    clip.durationSeconds = durationSeconds;
    clip.rest = {
        AnimationAssetRestBone{"Hips", -1, {0.0F, 1.0F, 0.0F}, {0.0F, 0.0F, 0.0F, 1.0F},
                                  {1.0F, 1.0F, 1.0F}},
        AnimationAssetRestBone{"Spine", 0, {0.0F, 0.1F, 0.0F}, {0.0F, 0.0F, 0.0F, 1.0F},
                                  {1.0F, 1.0F, 1.0F}},
    };
    clip.events = {
        AnimationAssetEvent{durationSeconds * 0.25, "footstep_l"},
        AnimationAssetEvent{durationSeconds * 0.75, "footstep_r"},
    };
    return clip;
}

[[nodiscard]] AnimationClipAsset MakeLinearClip(const std::size_t keyCount) {
    AnimationClipAsset clip = MakeBaseClip("linear", 2.0);
    clip.samples.reserve(keyCount);
    for (std::size_t index = 0U; index < keyCount; ++index) {
        const double fraction =
            keyCount <= 1U ? 0.0 : static_cast<double>(index) / static_cast<double>(keyCount - 1U);
        clip.samples.push_back(AnimationAssetSample{
            fraction * clip.durationSeconds,
            MakePose({static_cast<float>(fraction) * 2.0F, 1.0F, 0.0F},
                        {0.0F, 0.0F, 0.0F, 1.0F}, {1.0F, 1.0F, 1.0F})});
    }
    AnimationAssetBoneTrack spine;
    spine.bone = "Spine";
    for (std::size_t index = 0U; index < keyCount; ++index) {
        const double fraction =
            keyCount <= 1U ? 0.0 : static_cast<double>(index) / static_cast<double>(keyCount - 1U);
        spine.samples.push_back(AnimationAssetSample{
            fraction * clip.durationSeconds,
            MakePose({0.0F, 0.1F, 0.0F}, AxisAngle({0.0F, 1.0F, 0.0F},
                                                        static_cast<float>(fraction) * 1.2F),
                        {1.0F, 1.0F, 1.0F})});
    }
    clip.bones.push_back(std::move(spine));
    return clip;
}

[[nodiscard]] AnimationClipAsset MakeSineClip(const std::size_t keyCount,
                                                    const std::uint32_t seed) {
    AnimationClipAsset clip = MakeBaseClip("sine", 3.0);
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
        clip.samples.push_back(AnimationAssetSample{
            fraction * clip.durationSeconds,
            MakePose({wave * 0.5F, 1.0F + (noise * 0.002F), 0.0F},
                        AxisAngle({0.0F, 1.0F, 0.0F}, wave * 2.0F), {1.0F, 1.0F, 1.0F})});
    }
    return clip;
}

[[nodiscard]] AnimationClipAsset MakeConstantClip(const std::size_t keyCount) {
    AnimationClipAsset clip = MakeBaseClip("constant", 1.0);
    for (std::size_t index = 0U; index < keyCount; ++index) {
        clip.samples.push_back(AnimationAssetSample{
            static_cast<double>(index) / static_cast<double>(keyCount),
            MakePose({3.25F, -0.5F, 0.0F}, AxisAngle({0.0F, 1.0F, 0.0F}, 0.7F),
                        {1.0F, 2.0F, 1.0F})});
    }
    return clip;
}

[[nodiscard]] AnimationCompressionSettings DefaultSettings() noexcept {
    return AnimationCompressionSettings{};
}

} // namespace

TEST(AnimationClipCompressionTest, IsAnimationCompressionSettingsValid_RejectsMisbehavingTolerances) {
    EXPECT_TRUE(IsAnimationCompressionSettingsValid(DefaultSettings()));

    AnimationCompressionSettings settings = DefaultSettings();
    settings.maximumPositionErrorMetres = 0.0;
    EXPECT_FALSE(IsAnimationCompressionSettingsValid(settings));
    settings.maximumPositionErrorMetres = -0.001;
    EXPECT_FALSE(IsAnimationCompressionSettingsValid(settings));
    settings.maximumPositionErrorMetres = std::numeric_limits<double>::quiet_NaN();
    EXPECT_FALSE(IsAnimationCompressionSettingsValid(settings));
    settings = DefaultSettings();
    settings.maximumRotationErrorRadians =
        AnimationCompressionSettings::kMinimumRotationErrorRadians * 0.5;
    EXPECT_FALSE(IsAnimationCompressionSettingsValid(settings));
    settings = DefaultSettings();
    settings.maximumScaleError =
        AnimationCompressionSettings::kMaximumScaleError * 2.0;
    EXPECT_FALSE(IsAnimationCompressionSettingsValid(settings));
}

TEST(AnimationClipCompressionTest, CompressAnimationClipAsset_RejectsInvalidInputAndFailsClosed) {
    const AnimationCompressionSettings settings = DefaultSettings();
    CompressedAnimationClip sentinel;
    sentinel.clipId = "untouched";
    sentinel.durationSeconds = 42.0;
    const CompressedAnimationClip originalSentinel = sentinel;

    AnimationClipAsset clip = MakeLinearClip(8U);
    clip.samples[3].pose.position.x = std::numeric_limits<float>::quiet_NaN();
    EXPECT_FALSE(CompressAnimationClipAsset(clip, settings, sentinel));
    EXPECT_EQ(sentinel, originalSentinel);

    clip = MakeLinearClip(8U);
    clip.samples[2].timeSeconds = 0.1;
    clip.samples[3].timeSeconds = 0.05; // unsorted
    EXPECT_FALSE(CompressAnimationClipAsset(clip, settings, sentinel));
    EXPECT_EQ(sentinel, originalSentinel);

    clip = MakeLinearClip(8U);
    clip.durationSeconds = 0.0;
    EXPECT_FALSE(CompressAnimationClipAsset(clip, settings, sentinel));
    EXPECT_EQ(sentinel, originalSentinel);

    AnimationCompressionSettings invalidSettings = settings;
    invalidSettings.maximumRotationErrorRadians = -1.0;
    clip = MakeLinearClip(8U);
    EXPECT_FALSE(CompressAnimationClipAsset(clip, invalidSettings, sentinel));
    EXPECT_EQ(sentinel, originalSentinel);
}

TEST(AnimationClipCompressionTest, CompressAnimationClipAsset_MeetsErrorBoundsOnLinearMotion) {
    const AnimationClipAsset clip = MakeLinearClip(1000U);
    const AnimationCompressionSettings settings = DefaultSettings();
    CompressedAnimationClip compressed;
    ASSERT_TRUE(CompressAnimationClipAsset(clip, settings, compressed));
    ASSERT_TRUE(IsCompressedAnimationClipValid(compressed));

    const AnimationCompressionErrorReport report =
        MeasureAnimationCompressionError(clip, compressed, 512U);
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

TEST(AnimationClipCompressionTest, CompressAnimationClipAsset_ReducesRedundantKeysAndShrinksBytes) {
    const AnimationClipAsset clip = MakeLinearClip(1000U);
    CompressedAnimationClip compressed;
    ASSERT_TRUE(CompressAnimationClipAsset(clip, DefaultSettings(), compressed));

    EXPECT_EQ(compressed.object.KeyCount(), 2U); // exactly linear: endpoints suffice
    EXPECT_LE(compressed.bones[0].KeyCount(), 64U); // smooth arc: far fewer than 1000
    const std::size_t compressedBytes = GetCompressedAnimationClipByteSize(compressed);
    const std::size_t referenceBytes = GetAnimationClipAssetSampleByteSize(clip);
    EXPECT_GT(referenceBytes, 0U);
    EXPECT_LT(compressedBytes, referenceBytes / 2U);
}

TEST(AnimationClipCompressionTest, CompressAnimationClipAsset_IsDeterministic) {
    const AnimationClipAsset clip = MakeSineClip(257U, 7U);
    CompressedAnimationClip first;
    CompressedAnimationClip second;
    ASSERT_TRUE(CompressAnimationClipAsset(clip, DefaultSettings(), first));
    ASSERT_TRUE(CompressAnimationClipAsset(clip, DefaultSettings(), second));
    EXPECT_EQ(first, second);
}

TEST(AnimationClipCompressionTest, CompressAnimationClipAsset_HandlesSingleTwoAndConstantKeyClips) {
    for (const std::size_t keyCount : {1U, 2U, 17U}) {
        const AnimationClipAsset clip = MakeConstantClip(keyCount);
        CompressedAnimationClip compressed;
        ASSERT_TRUE(CompressAnimationClipAsset(clip, DefaultSettings(), compressed))
            << "keyCount=" << keyCount;
        ASSERT_TRUE(IsCompressedAnimationClipValid(compressed)) << "keyCount=" << keyCount;
        EXPECT_LE(compressed.object.KeyCount(), 2U) << "keyCount=" << keyCount;
        const AnimationCompressionErrorReport report =
            MeasureAnimationCompressionError(clip, compressed, 64U);
        EXPECT_LE(report.maximumPositionErrorMetres,
                  DefaultSettings().maximumPositionErrorMetres)
            << "keyCount=" << keyCount;
    }
}

TEST(AnimationClipCompressionTest, TrySampleCompressedAnimationTrack_DecodesConstantAxesExactly) {
    AnimationClipAsset clip = MakeBaseClip("axes", 1.0);
    clip.samples = {
        AnimationAssetSample{0.0, MakePose({1.25F, 0.5F, -2.0F}, {0.0F, 0.0F, 0.0F, 1.0F},
                                                 {1.0F, 1.0F, 1.0F})},
        AnimationAssetSample{1.0, MakePose({3.25F, 0.5F, -2.0F}, {0.0F, 0.0F, 0.0F, 1.0F},
                                                 {1.0F, 1.0F, 1.0F})},
    };
    CompressedAnimationClip compressed;
    ASSERT_TRUE(CompressAnimationClipAsset(clip, DefaultSettings(), compressed));
    ASSERT_EQ(compressed.object.KeyCount(), 2U);
    EXPECT_FLOAT_EQ(compressed.object.positionRangeExtent.y, 0.0F);
    EXPECT_FLOAT_EQ(compressed.object.positionRangeExtent.z, 0.0F);

    AnimationAssetPose pose{};
    ASSERT_TRUE(TrySampleCompressedAnimationTrack(compressed.object, compressed.durationSeconds,
                                                     0.3, false, pose));
    EXPECT_FLOAT_EQ(pose.position.y, 0.5F); // zero-extent axes decode exactly
    EXPECT_FLOAT_EQ(pose.position.z, -2.0F);
    EXPECT_FLOAT_EQ(pose.scale.x, 1.0F);
    EXPECT_NEAR(pose.position.x, 1.85F, 1.0e-3); // lerp midpoint within tolerance
}

TEST(AnimationClipCompressionTest, CompressAnimationClipAsset_HandlesQuaternionSignFlipsAndExtremePairs) {
    // q and -q are the same rotation: alternating signs must not produce rotation error.
    const Math::Quaternion rotation = AxisAngle({0.0F, 1.0F, 0.0F}, 0.9F);
    AnimationClipAsset clip = MakeBaseClip("signs", 1.0);
    clip.samples.clear();
    for (std::size_t index = 0U; index < 5U; ++index) {
        const float sign = (index % 2U) == 0U ? 1.0F : -1.0F;
        const Math::Quaternion signedRotation{rotation.x * sign, rotation.y * sign,
                                                 rotation.z * sign, rotation.w * sign};
        clip.samples.push_back(AnimationAssetSample{
            static_cast<double>(index) / 4.0,
            MakePose({0.0F, 0.0F, 0.0F}, signedRotation, {1.0F, 1.0F, 1.0F})});
    }
    CompressedAnimationClip compressed;
    ASSERT_TRUE(CompressAnimationClipAsset(clip, DefaultSettings(), compressed));
    const AnimationCompressionErrorReport signReport =
        MeasureAnimationCompressionError(clip, compressed, 128U);
    EXPECT_LE(signReport.maximumRotationErrorRadians,
              DefaultSettings().maximumRotationErrorRadians);

    // A near-180-degree pair exercises the nlerp path without a degenerate midpoint.
    clip = MakeBaseClip("extreme", 1.0);
    clip.samples = {
        AnimationAssetSample{0.0,
                                MakePose({0.0F, 0.0F, 0.0F},
                                            AxisAngle({0.0F, 1.0F, 0.0F}, 0.0F),
                                            {1.0F, 1.0F, 1.0F})},
        AnimationAssetSample{1.0,
                                MakePose({0.0F, 0.0F, 0.0F},
                                            AxisAngle({0.0F, 1.0F, 0.0F}, 3.13159265F),
                                            {1.0F, 1.0F, 1.0F})},
    };
    ASSERT_TRUE(CompressAnimationClipAsset(clip, DefaultSettings(), compressed));
    const AnimationCompressionErrorReport extremeReport =
        MeasureAnimationCompressionError(clip, compressed, 128U);
    EXPECT_LE(extremeReport.maximumRotationErrorRadians,
              DefaultSettings().maximumRotationErrorRadians);
}

TEST(AnimationClipCompressionTest, TrySampleCompressedAnimationTrack_MatchesClampAndLoopSemantics) {
    const AnimationClipAsset clip = MakeSineClip(64U, 11U);
    CompressedAnimationClip compressed;
    ASSERT_TRUE(CompressAnimationClipAsset(clip, DefaultSettings(), compressed));
    const double duration = compressed.durationSeconds;

    AnimationAssetPose atStart{};
    AnimationAssetPose clampedBefore{};
    ASSERT_TRUE(TrySampleCompressedAnimationTrack(compressed.object, duration, 0.0, false,
                                                     atStart));
    ASSERT_TRUE(TrySampleCompressedAnimationTrack(compressed.object, duration, -1.5, false,
                                                     clampedBefore));
    EXPECT_NEAR(clampedBefore.position.x, atStart.position.x, 1.0e-6);
    EXPECT_NEAR(clampedBefore.position.y, atStart.position.y, 1.0e-6);

    AnimationAssetPose atEnd{};
    AnimationAssetPose clampedAfter{};
    ASSERT_TRUE(TrySampleCompressedAnimationTrack(compressed.object, duration, duration, false,
                                                     atEnd));
    ASSERT_TRUE(TrySampleCompressedAnimationTrack(compressed.object, duration, duration + 2.5,
                                                     false, clampedAfter));
    EXPECT_NEAR(clampedAfter.position.x, atEnd.position.x, 1.0e-6);

    AnimationAssetPose wrapped{};
    AnimationAssetPose direct{};
    ASSERT_TRUE(TrySampleCompressedAnimationTrack(compressed.object, duration,
                                                     duration + (duration * 0.5), true, wrapped));
    ASSERT_TRUE(TrySampleCompressedAnimationTrack(compressed.object, duration, duration * 0.5,
                                                     false, direct));
    EXPECT_NEAR(wrapped.position.x, direct.position.x, 1.0e-6);
    EXPECT_NEAR(wrapped.rotation.x, direct.rotation.x, 1.0e-5);
    EXPECT_NEAR(wrapped.rotation.w, direct.rotation.w, 1.0e-5);

    AnimationAssetPose negativeWrap{};
    ASSERT_TRUE(TrySampleCompressedAnimationTrack(compressed.object, duration, -0.5, true,
                                                     negativeWrap));
    AnimationAssetPose negativeDirect{};
    ASSERT_TRUE(TrySampleCompressedAnimationTrack(compressed.object, duration, duration - 0.5,
                                                     false, negativeDirect));
    EXPECT_NEAR(negativeWrap.position.x, negativeDirect.position.x, 1.0e-6);
}

TEST(AnimationClipCompressionTest, TryDecompressAnimationClipAsset_ResamplesAtStoredKeys) {
    const AnimationClipAsset clip = MakeSineClip(97U, 23U);
    CompressedAnimationClip compressed;
    ASSERT_TRUE(CompressAnimationClipAsset(clip, DefaultSettings(), compressed));

    AnimationClipAsset decompressed;
    ASSERT_TRUE(TryDecompressAnimationClipAsset(compressed, decompressed));
    ASSERT_TRUE(IsAnimationClipAssetValid(decompressed));
    EXPECT_EQ(decompressed.clipId, clip.clipId);
    EXPECT_EQ(decompressed.events, clip.events);
    EXPECT_EQ(decompressed.rest, clip.rest);
    EXPECT_EQ(decompressed.samples.size(), compressed.object.KeyCount());
    ASSERT_EQ(decompressed.bones.size(), compressed.bones.size());
    for (std::size_t index = 0U; index < decompressed.bones.size(); ++index) {
        EXPECT_EQ(decompressed.bones[index].samples.size(), compressed.bones[index].KeyCount());
    }

    for (const AnimationAssetSample& sample : decompressed.samples) {
        AnimationAssetPose sampled{};
        ASSERT_TRUE(TrySampleCompressedAnimationTrack(compressed.object, compressed.durationSeconds,
                                                         sample.timeSeconds, false, sampled));
        EXPECT_NEAR(sampled.position.x, sample.pose.position.x, 1.0e-5);
        EXPECT_NEAR(sampled.position.y, sample.pose.position.y, 1.0e-5);
        EXPECT_NEAR(sampled.rotation.w, sample.pose.rotation.w, 1.0e-5);
    }
}

TEST(AnimationClipCompressionTest, CompressAnimationClipAsset_UsesAdaptiveBitsForHugeRanges) {
    AnimationClipAsset clip = MakeBaseClip("huge", 1.0);
    clip.samples = {
        AnimationAssetSample{0.0, MakePose({0.0F, 0.0F, 0.0F}, {0.0F, 0.0F, 0.0F, 1.0F},
                                                 {1.0F, 1.0F, 1.0F})},
        AnimationAssetSample{1.0, MakePose({500.0F, 0.0F, 0.0F}, {0.0F, 0.0F, 0.0F, 1.0F},
                                                 {1.0F, 8000.0F, 1.0F})},
    };
    CompressedAnimationClip compressed;
    ASSERT_TRUE(CompressAnimationClipAsset(clip, DefaultSettings(), compressed));
    EXPECT_EQ(compressed.object.positionComponentBits, 32U);
    EXPECT_EQ(compressed.object.scaleComponentBits, 32U); // exercises the 35-byte record stride

    const AnimationCompressionErrorReport report =
        MeasureAnimationCompressionError(clip, compressed, 256U);
    EXPECT_LE(report.maximumPositionErrorMetres,
              DefaultSettings().maximumPositionErrorMetres);
    EXPECT_LE(report.maximumScaleError, DefaultSettings().maximumScaleError);
}

TEST(AnimationClipCompressionTest, CompressAnimationClipAsset_KeepsHighFrequencyMotionWithinBounds) {
    const AnimationClipAsset clip = MakeSineClip(400U, 4242U);
    const AnimationCompressionSettings settings = DefaultSettings();
    CompressedAnimationClip compressed;
    ASSERT_TRUE(CompressAnimationClipAsset(clip, settings, compressed));
    ASSERT_TRUE(IsCompressedAnimationClipValid(compressed));
    EXPECT_LE(compressed.object.KeyCount(), 400U);

    const AnimationCompressionErrorReport report =
        MeasureAnimationCompressionError(clip, compressed, 1024U);
    EXPECT_LE(report.maximumPositionErrorMetres, settings.maximumPositionErrorMetres);
    EXPECT_LE(report.maximumRotationErrorRadians, settings.maximumRotationErrorRadians);
    EXPECT_LE(report.maximumScaleError, settings.maximumScaleError);
}

TEST(AnimationClipCompressionTest, CompressAnimationClipAsset_KeepsRootMotionTravelWithinBounds) {
    AnimationClipAsset clip = MakeBaseClip("root", 2.0);
    // Uneven gait: positions advance with pauses and bursts, the classic root-motion case.
    const double times[6] = {0.0, 0.2, 0.9, 1.1, 1.35, 2.0};
    const float travel[6] = {0.0F, 0.02F, 1.6F, 1.75F, 1.9F, 2.0F};
    clip.samples.clear();
    for (std::size_t index = 0U; index < 6U; ++index) {
        clip.samples.push_back(AnimationAssetSample{
            times[index],
            MakePose({travel[index], 0.0F, 0.0F}, {0.0F, 0.0F, 0.0F, 1.0F},
                        {1.0F, 1.0F, 1.0F})});
    }
    CompressedAnimationClip compressed;
    ASSERT_TRUE(CompressAnimationClipAsset(clip, DefaultSettings(), compressed));
    const AnimationCompressionErrorReport report =
        MeasureAnimationCompressionError(clip, compressed, 512U);
    EXPECT_LE(report.maximumPositionErrorMetres,
              DefaultSettings().maximumPositionErrorMetres);
}

TEST(AnimationClipCompressionTest, IsCompressedAnimationClipValid_RejectsCorruptStreams) {
    const AnimationClipAsset clip = MakeLinearClip(8U);
    CompressedAnimationClip compressed;
    ASSERT_TRUE(CompressAnimationClipAsset(clip, DefaultSettings(), compressed));
    ASSERT_TRUE(IsCompressedAnimationClipValid(compressed));

    CompressedAnimationClip corrupt = compressed;
    corrupt.object.keyData.pop_back(); // stride mismatch
    EXPECT_FALSE(IsCompressedAnimationClipValid(corrupt));

    corrupt = compressed;
    corrupt.object.positionComponentBits = 24U; // illegal precision
    EXPECT_FALSE(IsCompressedAnimationClipValid(corrupt));

    corrupt = compressed;
    corrupt.object.positionRangeMinimum.x = std::numeric_limits<float>::quiet_NaN();
    EXPECT_FALSE(IsCompressedAnimationClipValid(corrupt));

    corrupt = compressed;
    corrupt.object.positionRangeExtent.y = -1.0F;
    EXPECT_FALSE(IsCompressedAnimationClipValid(corrupt));

    corrupt = compressed;
    corrupt.bones.push_back(corrupt.bones.front()); // duplicate bone name
    EXPECT_FALSE(IsCompressedAnimationClipValid(corrupt));

    corrupt = compressed;
    corrupt.bones.front().bone.clear();
    EXPECT_FALSE(IsCompressedAnimationClipValid(corrupt));

    corrupt = compressed;
    corrupt.durationSeconds = -1.0;
    EXPECT_FALSE(IsCompressedAnimationClipValid(corrupt));

    AnimationAssetPose pose{};
    EXPECT_FALSE(TrySampleCompressedAnimationTrack(compressed.object, -1.0, 0.5, false, pose));
    corrupt = compressed;
    corrupt.object.keyData.pop_back(); // stride mismatch
    EXPECT_FALSE(TrySampleCompressedAnimationTrack(
        corrupt.object, compressed.durationSeconds, 0.5, false, pose));
    EXPECT_FALSE(TrySampleCompressedAnimationTrack(compressed.object, 1.0,
                                                      std::numeric_limits<double>::quiet_NaN(),
                                                      false, pose));
}

TEST(AnimationClipCompressionTest, MeasureAnimationCompressionError_RejectsInvalidInputs) {
    const AnimationClipAsset clip = MakeLinearClip(4U);
    CompressedAnimationClip compressed;
    ASSERT_TRUE(CompressAnimationClipAsset(clip, DefaultSettings(), compressed));

    const AnimationCompressionErrorReport zeroProbes =
        MeasureAnimationCompressionError(clip, compressed, 0U);
    EXPECT_EQ(zeroProbes.probeCount, 0U);

    CompressedAnimationClip invalid;
    const AnimationCompressionErrorReport invalidReport =
        MeasureAnimationCompressionError(clip, invalid, 16U);
    EXPECT_EQ(invalidReport.probeCount, 0U);
    EXPECT_DOUBLE_EQ(invalidReport.maximumPositionErrorMetres, 0.0);
}

TEST(AnimationClipCompressionTest, CompressAnimationClipAsset_AllowsEmptyObjectTrack) {
    AnimationClipAsset clip = MakeBaseClip("bones-only", 1.0);
    AnimationAssetBoneTrack hips;
    hips.bone = "Hips";
    hips.samples = {AnimationAssetSample{0.0, MakePose({0.0F, 1.0F, 0.0F},
                                                             {0.0F, 0.0F, 0.0F, 1.0F},
                                                             {1.0F, 1.0F, 1.0F})},
                    AnimationAssetSample{1.0, MakePose({0.0F, 1.2F, 0.0F},
                                                              {0.0F, 0.0F, 0.0F, 1.0F},
                                                              {1.0F, 1.0F, 1.0F})}};
    clip.bones.push_back(std::move(hips));
    ASSERT_TRUE(IsAnimationClipAssetValid(clip));

    CompressedAnimationClip compressed;
    ASSERT_TRUE(CompressAnimationClipAsset(clip, DefaultSettings(), compressed));
    EXPECT_EQ(compressed.object.KeyCount(), 0U);
    EXPECT_EQ(compressed.bones.size(), 1U);
    EXPECT_EQ(compressed.bones[0].bone, "Hips");
    ASSERT_TRUE(IsCompressedAnimationClipValid(compressed));
}

} // namespace UVE::Asset::Tests
