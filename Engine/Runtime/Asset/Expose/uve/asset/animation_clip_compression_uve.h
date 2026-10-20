// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "uve/asset/animation_clip_asset_uve.h"
#include "uve/math/vector3_uve.h"

namespace UVE::Asset {

/// Error tolerances for `AnimationClipCompressionUVE`. Every field is the TOTAL bound the
/// compressed clip is designed to honour against the uncompressed samples, measured the same way
/// `MeasureAnimationCompressionErrorUVE` measures it: worst-case over probe times, position and
/// scale in euclidean distance, rotation in radians of angular distance between unit quaternions.
/// The codec splits the budget between keyframe reduction (40%) and quantization (40%), leaving
/// 20% as numerical margin for float interpolation and time rounding. Values must be finite and
/// strictly positive, and are bounded above so a mis-typed tolerance cannot silently disable
/// compression checks.
struct AnimationCompressionSettingsUVE final {
    static constexpr double kMaximumPositionErrorMetresUVE = 10.0;
    static constexpr double kMaximumRotationErrorRadiansUVE = 3.14159265358979323846;
    static constexpr double kMaximumScaleErrorUVE = 10.0;
    // The codec's floors: tolerances below these cannot be guaranteed because the quantizer
    // cannot resolve finer steps, so settings validation rejects them explicitly instead of
    // silently producing a clip that misses its stated bound.
    static constexpr double kMinimumPositionErrorMetresUVE = 1.0e-6;
    static constexpr double kMinimumRotationErrorRadiansUVE = 2.0e-4;
    static constexpr double kMinimumScaleErrorUVE = 1.0e-6;

    double maximumPositionErrorMetres = 0.001;
    double maximumRotationErrorRadians = 0.002;
    double maximumScaleError = 0.001;

    [[nodiscard]] bool operator==(const AnimationCompressionSettingsUVE&) const = default;
};

/// One compressed motion track: the clip's object samples (empty `bone`) or one bone's local
/// samples. Keys are stored as a packed little-endian byte stream of fixed-stride records so a
/// track is one allocation and the runtime decompression path walks it linearly. Record layout:
///
///   offset  0: uint32  timeTicks        (0 .. 0xFFFFFFFF over the clip duration)
///   offset  4: uint16  rotationX/Y/Z    (smallest-three, biased into [0, 65535])
///   offset 10: uint8   rotationMeta     (bits 0-1: largest component index x/y/z/w,
///                                        bit 2: largest component sign, 1 = negative)
///   offset 11: position components (x, y, z), 2 bytes each when `positionComponentBits` is 16,
///              4 bytes each when 32; quantized over the track's own range
///   offset 11 + position bytes: scale components, same scheme
///
/// A zero-extent range axis stores its constant exactly (the quantized value is always zero and
/// the decoder returns the minimum). `keyData` size must be exactly the record stride times an
/// integer key count, so the layout can never silently desynchronize.
struct CompressedAnimationTrackUVE final {
    /// Empty for the clip's object track; otherwise the bone name from the source track.
    std::string bone;
    Math::Vector3UVE positionRangeMinimum{0.0F, 0.0F, 0.0F};
    Math::Vector3UVE positionRangeExtent{0.0F, 0.0F, 0.0F};
    Math::Vector3UVE scaleRangeMinimum{1.0F, 1.0F, 1.0F};
    Math::Vector3UVE scaleRangeExtent{0.0F, 0.0F, 0.0F};
    std::uint8_t positionComponentBits = 16;
    std::uint8_t scaleComponentBits = 16;
    std::vector<std::uint8_t> keyData;

    [[nodiscard]] std::size_t KeyStrideBytesUVE() const noexcept;
    [[nodiscard]] std::size_t KeyCountUVE() const noexcept;
    [[nodiscard]] bool operator==(const CompressedAnimationTrackUVE&) const = default;
};

/// A compressed clip: the same content as `AnimationClipAssetUVE` (object track, bone tracks,
/// events, rest skeleton) with the sample streams rebuilt by `CompressAnimationClipAssetUVE`.
/// Events and the rest skeleton stay lossless - they are metadata, not per-frame payload - so
/// retargeting and gameplay timing are bit-identical before and after compression. `conformed`
/// carries over unchanged.
struct CompressedAnimationClipUVE final {
    std::string clipId;
    double durationSeconds = 0.0;
    bool conformed = false;
    /// `KeyCountUVE() == 0` means the source clip had no object samples.
    CompressedAnimationTrackUVE object;
    std::vector<CompressedAnimationTrackUVE> bones;
    std::vector<AnimationAssetEventUVE> events;
    std::vector<AnimationAssetRestBoneUVE> rest;

    [[nodiscard]] bool operator==(const CompressedAnimationClipUVE&) const = default;
};

struct AnimationCompressionErrorReportUVE final {
    double maximumPositionErrorMetres = 0.0;
    double maximumRotationErrorRadians = 0.0;
    double maximumScaleError = 0.0;
    std::size_t probeCount = 0U;

    [[nodiscard]] bool operator==(const AnimationCompressionErrorReportUVE&) const = default;
};

[[nodiscard]] bool IsAnimationCompressionSettingsValidUVE(
    const AnimationCompressionSettingsUVE& settings) noexcept;

/// Bounds and layout validation for a compressed clip: finite ranges, legal component bits,
/// stride-consistent key streams, bounded key and track counts, and duplicate-free bone names.
[[nodiscard]] bool IsCompressedAnimationClipValidUVE(const CompressedAnimationClipUVE& clip) noexcept;

/// Keyframe-reduces and quantizes a validated clip's sample streams. The source is validated with
/// `IsAnimationClipAssetValidUVE`; events and the rest skeleton are copied through losslessly.
/// Fails closed and leaves `outCompressed` untouched on invalid input, invalid settings, or a
/// capacity overflow. Compression is deterministic: the same input and settings always produce an
/// equal `CompressedAnimationClipUVE`.
[[nodiscard]] bool CompressAnimationClipAssetUVE(const AnimationClipAssetUVE& clip,
                                                 const AnimationCompressionSettingsUVE& settings,
                                                 CompressedAnimationClipUVE& outCompressed);

/// The runtime decompression path: reconstructs one track's pose at `timeSeconds` from its packed
/// keys. Semantics mirror the uncompressed clip sampler exactly: times clamp at both ends, position
/// and scale lerp linearly, rotations nlerp and re-normalize (no shortest-path sign flip, matching
/// `InterpolatePoseUVE`), and `looping` wraps through the clip duration. Fails closed on invalid
/// tracks, non-finite times, or a non-positive duration.
[[nodiscard]] bool TrySampleCompressedAnimationTrackUVE(const CompressedAnimationTrackUVE& track,
                                                        double durationSeconds, double timeSeconds,
                                                        bool looping,
                                                        AnimationAssetPoseUVE& outPose) noexcept;

/// Rebuilds an uncompressed clip from a compressed one. Keys decode to their quantized values, so
/// the result is within the codec's error bounds of the original rather than bit-identical to it;
/// events and the rest skeleton round-trip exactly. Intended for tools and tests that need the
/// sample model, not for the runtime path.
[[nodiscard]] bool TryDecompressAnimationClipAssetUVE(const CompressedAnimationClipUVE& compressed,
                                                      AnimationClipAssetUVE& outClip);

/// Samples both representations on a uniform time grid and reports the worst deviation per
/// channel, in the same units the settings use. Used by the codec's tests and by tools that want
/// to prove a particular compressed clip still meets its tolerances.
[[nodiscard]] AnimationCompressionErrorReportUVE MeasureAnimationCompressionErrorUVE(
    const AnimationClipAssetUVE& original, const CompressedAnimationClipUVE& compressed,
    std::size_t probeCount = 256U);

/// Reference (uncompressed) sample-stream size in bytes: every source sample is its time plus
/// position, rotation and scale floats. Compare against `GetCompressedAnimationClipByteSizeUVE`.
[[nodiscard]] std::size_t GetAnimationClipAssetSampleByteSizeUVE(
    const AnimationClipAssetUVE& clip) noexcept;

/// Total heap footprint of a compressed clip's own containers (tracks, key streams, events, rest).
[[nodiscard]] std::size_t GetCompressedAnimationClipByteSizeUVE(
    const CompressedAnimationClipUVE& clip) noexcept;

} // namespace UVE::Asset
