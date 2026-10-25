// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/asset/animation_clip_compression_uve.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <limits>
#include <vector>

namespace UVE::Asset {
namespace {

// ---------------------------------------------------------------------------
// Codec constants
// ---------------------------------------------------------------------------

constexpr std::uint32_t kTimeTicksMaxUVE = 0xFFFFFFFFu;
constexpr double kTimeTicksMaxAsDoubleUVE = 4294967295.0;

// Smallest-three quaternion storage: the three smaller components each live in
// [-1/sqrt(2), 1/sqrt(2)], biased and quantized to 16 bits over that range.
constexpr float kSmallestThreeHalfRangeUVE = 0.7071067811865476F;
constexpr float kSmallestThreeEncodeScaleUVE =
    65535.0F / (2.0F * kSmallestThreeHalfRangeUVE);

// Budget split: keyframe reduction and quantization each target 40% of the total tolerance,
// leaving 20% as numerical margin (float interpolation, time rounding, nlerp normalization).
constexpr double kReductionBudgetShareUVE = 0.4;
constexpr double kQuantizationBudgetShareUVE = 0.4;

// The codec's own floors live on `AnimationCompressionSettingsUVE` (header) so callers can see
// them; settings validation rejects tolerances below them explicitly rather than silently
// producing a clip that misses its stated bound.

constexpr std::size_t kTimeBytesUVE = 4U;
constexpr std::size_t kRotationBytesUVE = 6U;
constexpr std::size_t kRotationMetaBytesUVE = 1U;

// ---------------------------------------------------------------------------
// Small helpers
// ---------------------------------------------------------------------------

[[nodiscard]] bool IsFinitePositiveUVE(const double value) noexcept {
    return std::isfinite(value) && value > 0.0;
}

[[nodiscard]] bool IsFiniteVector3UVE(const Math::Vector3UVE& value) noexcept {
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

[[nodiscard]] std::size_t ComponentBytesUVE(const std::uint8_t bits) noexcept {
    return bits == 16U ? 2U : (bits == 32U ? 4U : 0U);
}

[[nodiscard]] double NormalizeSampleTimeUVE(const double timeSeconds,
                                            const double durationSeconds,
                                            const bool looping) noexcept {
    if (!looping || durationSeconds <= 0.0) {
        return std::clamp(timeSeconds, 0.0, durationSeconds);
    }
    double normalized = std::fmod(timeSeconds, durationSeconds);
    if (normalized < 0.0) {
        normalized += durationSeconds;
    }
    return normalized;
}

[[nodiscard]] double QuaternionDotUVE(const Math::QuaternionUVE& lhs,
                                      const Math::QuaternionUVE& rhs) noexcept {
    return static_cast<double>(lhs.x) * static_cast<double>(rhs.x) +
           static_cast<double>(lhs.y) * static_cast<double>(rhs.y) +
           static_cast<double>(lhs.z) * static_cast<double>(rhs.z) +
           static_cast<double>(lhs.w) * static_cast<double>(rhs.w);
}

/// Angular distance in radians between two unit quaternions. q and -q are the same rotation, so
/// the absolute dot product decides the angle.
[[nodiscard]] double QuaternionAngularDistanceUVE(const Math::QuaternionUVE& lhs,
                                                  const Math::QuaternionUVE& rhs) noexcept {
    const double dot = std::clamp(std::abs(QuaternionDotUVE(lhs, rhs)), 0.0, 1.0);
    return 2.0 * std::acos(dot);
}

[[nodiscard]] double Vector3DistanceUVE(const Math::Vector3UVE& lhs,
                                        const Math::Vector3UVE& rhs) noexcept {
    const double dx = static_cast<double>(lhs.x) - static_cast<double>(rhs.x);
    const double dy = static_cast<double>(lhs.y) - static_cast<double>(rhs.y);
    const double dz = static_cast<double>(lhs.z) - static_cast<double>(rhs.z);
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

/// Mirrors the uncompressed clip sampler's pose interpolation exactly (lerp position/scale,
/// nlerp + normalize rotation, no shortest-path sign flip, left-key fallback on a degenerate
/// normalize) so decompressed sampling is semantically identical to `InterpolatePoseUVE`.
[[nodiscard]] bool IsFiniteAnimationAssetPoseUVE(const AnimationAssetPoseUVE& pose) noexcept {
    return Math::IsFiniteUVE(pose.position) && Math::IsFiniteUVE(pose.scale) &&
           Math::IsFiniteUVE(pose.rotation);
}

[[nodiscard]] bool TryNormalizeAnimationAssetPoseUVE(const AnimationAssetPoseUVE& pose,
                                                     AnimationAssetPoseUVE& outNormalized) noexcept {
    if (!IsFiniteAnimationAssetPoseUVE(pose)) {
        return false;
    }
    Math::QuaternionUVE normalizedRotation{};
    if (!Math::TryNormalizeUVE(pose.rotation, normalizedRotation)) {
        return false;
    }
    outNormalized = pose;
    outNormalized.rotation = normalizedRotation;
    return true;
}

[[nodiscard]] AnimationAssetPoseUVE InterpolatePoseUVE(const AnimationAssetPoseUVE& left,
                                                       const AnimationAssetPoseUVE& right,
                                                       const double alpha) noexcept {
    const float factor = static_cast<float>(std::clamp(alpha, 0.0, 1.0));
    AnimationAssetPoseUVE result;
    result.position = left.position * (1.0F - factor) + right.position * factor;
    result.scale = left.scale * (1.0F - factor) + right.scale * factor;
    result.rotation = Math::QuaternionUVE{
        left.rotation.x * (1.0F - factor) + right.rotation.x * factor,
        left.rotation.y * (1.0F - factor) + right.rotation.y * factor,
        left.rotation.z * (1.0F - factor) + right.rotation.z * factor,
        left.rotation.w * (1.0F - factor) + right.rotation.w * factor,
    };
    AnimationAssetPoseUVE normalized;
    if (!TryNormalizeAnimationAssetPoseUVE(result, normalized)) {
        return left;
    }
    return normalized;
}

// ---------------------------------------------------------------------------
// Time quantization
// ---------------------------------------------------------------------------

[[nodiscard]] std::uint32_t TimeToTicksUVE(const double timeSeconds,
                                           const double durationSeconds) noexcept {
    if (durationSeconds <= 0.0 || timeSeconds <= 0.0) {
        return 0U;
    }
    if (timeSeconds >= durationSeconds) {
        return kTimeTicksMaxUVE;
    }
    const double scaled = (timeSeconds / durationSeconds) * kTimeTicksMaxAsDoubleUVE;
    return static_cast<std::uint32_t>(std::llround(std::clamp(scaled, 0.0, kTimeTicksMaxAsDoubleUVE)));
}

[[nodiscard]] double TicksToTimeUVE(const std::uint32_t ticks,
                                    const double durationSeconds) noexcept {
    return (static_cast<double>(ticks) / kTimeTicksMaxAsDoubleUVE) * durationSeconds;
}

// ---------------------------------------------------------------------------
// Byte-stream record access (little-endian, memcpy-based: no alignment assumptions)
// ---------------------------------------------------------------------------

void WriteU16UVE(std::uint8_t* const destination, const std::uint16_t value) noexcept {
    std::memcpy(destination, &value, sizeof(value));
}

void WriteU32UVE(std::uint8_t* const destination, const std::uint32_t value) noexcept {
    std::memcpy(destination, &value, sizeof(value));
}

[[nodiscard]] std::uint16_t ReadU16UVE(const std::uint8_t* const source) noexcept {
    std::uint16_t value = 0U;
    std::memcpy(&value, source, sizeof(value));
    return value;
}

[[nodiscard]] std::uint32_t ReadU32UVE(const std::uint8_t* const source) noexcept {
    std::uint32_t value = 0U;
    std::memcpy(&value, source, sizeof(value));
    return value;
}

// ---------------------------------------------------------------------------
// Smallest-three quaternion quantization
// ---------------------------------------------------------------------------

struct SmallestThreeEncodingUVE final {
    std::uint16_t components[3] = {0U, 0U, 0U};
    std::uint8_t meta = 0U; // bits 0-1: largest index, bit 2: largest sign (1 = negative)
};

[[nodiscard]] SmallestThreeEncodingUVE EncodeRotationUVE(const Math::QuaternionUVE& input) noexcept {
    Math::QuaternionUVE unit{};
    const Math::QuaternionUVE source = TryNormalizeUVE(input, unit) ? unit : Math::QuaternionUVE{0.0F, 0.0F, 0.0F, 1.0F};
    const float values[4] = {source.x, source.y, source.z, source.w};
    std::size_t largestIndex = 0U;
    float largestMagnitude = std::abs(values[0]);
    for (std::size_t index = 1U; index < 4U; ++index) {
        const float magnitude = std::abs(values[index]);
        if (magnitude > largestMagnitude) {
            largestMagnitude = magnitude;
            largestIndex = index;
        }
    }
    SmallestThreeEncodingUVE encoding;
    encoding.meta = static_cast<std::uint8_t>(
        (largestIndex & 0x3U) | ((values[largestIndex] < 0.0F) ? 0x4U : 0x0U));
    std::size_t outputIndex = 0U;
    for (std::size_t index = 0U; index < 4U; ++index) {
        if (index == largestIndex) {
            continue;
        }
        const float biased =
            (values[index] + kSmallestThreeHalfRangeUVE) * kSmallestThreeEncodeScaleUVE;
        const float clamped = std::clamp(biased, 0.0F, 65535.0F);
        encoding.components[outputIndex] = static_cast<std::uint16_t>(std::lround(clamped));
        ++outputIndex;
    }
    return encoding;
}

[[nodiscard]] Math::QuaternionUVE DecodeRotationUVE(const SmallestThreeEncodingUVE& encoding) noexcept {
    const float decoded[3] = {
        (static_cast<float>(encoding.components[0]) / kSmallestThreeEncodeScaleUVE) -
            kSmallestThreeHalfRangeUVE,
        (static_cast<float>(encoding.components[1]) / kSmallestThreeEncodeScaleUVE) -
            kSmallestThreeHalfRangeUVE,
        (static_cast<float>(encoding.components[2]) / kSmallestThreeEncodeScaleUVE) -
            kSmallestThreeHalfRangeUVE,
    };
    float values[4] = {0.0F, 0.0F, 0.0F, 0.0F};
    const std::size_t largestIndex = static_cast<std::size_t>(encoding.meta & 0x3U);
    const bool largestNegative = (encoding.meta & 0x4U) != 0U;
    double remaining = 1.0;
    std::size_t decodedIndex = 0U;
    for (std::size_t index = 0U; index < 4U; ++index) {
        if (index == largestIndex) {
            continue;
        }
        values[index] = decoded[decodedIndex];
        remaining -= static_cast<double>(decoded[decodedIndex]) * static_cast<double>(decoded[decodedIndex]);
        ++decodedIndex;
    }
    const float largest = static_cast<float>(std::sqrt(std::max(remaining, 0.0))) *
                          (largestNegative ? -1.0F : 1.0F);
    values[largestIndex] = largest;
    return Math::QuaternionUVE{values[0], values[1], values[2], values[3]};
}

// ---------------------------------------------------------------------------
// Channel quantization (positions and scales)
// ---------------------------------------------------------------------------

struct ChannelRangeUVE final {
    Math::Vector3UVE minimum{0.0F, 0.0F, 0.0F};
    Math::Vector3UVE extent{0.0F, 0.0F, 0.0F};
};

[[nodiscard]] ChannelRangeUVE ComputeRangeUVE(const std::vector<AnimationAssetSampleUVE>& samples,
                                              const std::vector<std::size_t>& kept) noexcept {
    Math::Vector3UVE minimum{std::numeric_limits<float>::max(), std::numeric_limits<float>::max(),
                             std::numeric_limits<float>::max()};
    Math::Vector3UVE maximum{std::numeric_limits<float>::lowest(),
                             std::numeric_limits<float>::lowest(),
                             std::numeric_limits<float>::lowest()};
    for (const std::size_t index : kept) {
        const Math::Vector3UVE& value = samples[index].pose.position;
        minimum.x = std::min(minimum.x, value.x);
        minimum.y = std::min(minimum.y, value.y);
        minimum.z = std::min(minimum.z, value.z);
        maximum.x = std::max(maximum.x, value.x);
        maximum.y = std::max(maximum.y, value.y);
        maximum.z = std::max(maximum.z, value.z);
    }
    return ChannelRangeUVE{minimum, maximum - minimum};
}

[[nodiscard]] ChannelRangeUVE ComputeScaleRangeUVE(const std::vector<AnimationAssetSampleUVE>& samples,
                                                   const std::vector<std::size_t>& kept) noexcept {
    Math::Vector3UVE minimum{std::numeric_limits<float>::max(), std::numeric_limits<float>::max(),
                             std::numeric_limits<float>::max()};
    Math::Vector3UVE maximum{std::numeric_limits<float>::lowest(),
                             std::numeric_limits<float>::lowest(),
                             std::numeric_limits<float>::lowest()};
    for (const std::size_t index : kept) {
        const Math::Vector3UVE& value = samples[index].pose.scale;
        minimum.x = std::min(minimum.x, value.x);
        minimum.y = std::min(minimum.y, value.y);
        minimum.z = std::min(minimum.z, value.z);
        maximum.x = std::max(maximum.x, value.x);
        maximum.y = std::max(maximum.y, value.y);
        maximum.z = std::max(maximum.z, value.z);
    }
    return ChannelRangeUVE{minimum, maximum - minimum};
}

/// Quantization half-step error budget for a 3D channel over `extent` at `bits`. Uses the widest
/// axis conservatively and scales by sqrt(3) for the euclidean worst case.
[[nodiscard]] bool ChannelQuantizationMeetsBudgetUVE(const Math::Vector3UVE& extent,
                                                     const std::uint32_t bits,
                                                     const double budget) noexcept {
    const double levels = (bits == 16U) ? 65535.0 : 4294967295.0;
    const double widest = std::max({static_cast<double>(extent.x), static_cast<double>(extent.y),
                                    static_cast<double>(extent.z)});
    const double worstCase = (widest / levels) * 0.5 * 1.7320508075688772;
    return worstCase <= budget;
}

[[nodiscard]] bool ChooseChannelBitsUVE(const Math::Vector3UVE& extent, const double budget,
                                        std::uint8_t& outBits) noexcept {
    if (ChannelQuantizationMeetsBudgetUVE(extent, 16U, budget)) {
        outBits = 16U;
        return true;
    }
    if (ChannelQuantizationMeetsBudgetUVE(extent, 32U, budget)) {
        outBits = 32U;
        return true;
    }
    return false;
}

[[nodiscard]] std::uint32_t QuantizeChannelComponentUVE(const float value, const float minimum,
                                                        const float extent,
                                                        const std::uint8_t bits) noexcept {
    if (extent <= 0.0F) {
        return 0U;
    }
    const double levels = (bits == 16U) ? 65535.0 : 4294967295.0;
    const double normalized = (static_cast<double>(value) - static_cast<double>(minimum)) /
                              static_cast<double>(extent);
    const double scaled = normalized * levels;
    return static_cast<std::uint32_t>(
        std::llround(std::clamp(scaled, 0.0, levels)));
}

[[nodiscard]] float DequantizeChannelComponentUVE(const std::uint32_t quantized, const float minimum,
                                                  const float extent,
                                                  const std::uint8_t bits) noexcept {
    if (extent <= 0.0F) {
        return minimum;
    }
    const double levels = (bits == 16U) ? 65535.0 : 4294967295.0;
    return minimum + static_cast<float>((static_cast<double>(quantized) / levels) *
                                        static_cast<double>(extent));
}

void AppendQuantizedVectorUVE(std::vector<std::uint8_t>& stream, const Math::Vector3UVE& value,
                              const ChannelRangeUVE& range, const std::uint8_t bits) noexcept {
    const std::uint32_t q[3] = {
        QuantizeChannelComponentUVE(value.x, range.minimum.x, range.extent.x, bits),
        QuantizeChannelComponentUVE(value.y, range.minimum.y, range.extent.y, bits),
        QuantizeChannelComponentUVE(value.z, range.minimum.z, range.extent.z, bits),
    };
    for (const std::uint32_t component : q) {
        if (bits == 16U) {
            std::array<std::uint8_t, 2> bytes{};
            WriteU16UVE(bytes.data(), static_cast<std::uint16_t>(component));
            stream.insert(stream.end(), bytes.begin(), bytes.end());
        } else {
            std::array<std::uint8_t, 4> bytes{};
            WriteU32UVE(bytes.data(), component);
            stream.insert(stream.end(), bytes.begin(), bytes.end());
        }
    }
}

[[nodiscard]] Math::Vector3UVE ReadQuantizedVectorUVE(const std::uint8_t*& cursor,
                                                      const ChannelRangeUVE& range,
                                                      const std::uint8_t bits) noexcept {
    std::uint32_t q[3] = {0U, 0U, 0U};
    for (std::size_t index = 0U; index < 3U; ++index) {
        if (bits == 16U) {
            q[index] = ReadU16UVE(cursor);
            cursor += 2U;
        } else {
            q[index] = ReadU32UVE(cursor);
            cursor += 4U;
        }
    }
    return Math::Vector3UVE{
        DequantizeChannelComponentUVE(q[0], range.minimum.x, range.extent.x, bits),
        DequantizeChannelComponentUVE(q[1], range.minimum.y, range.extent.y, bits),
        DequantizeChannelComponentUVE(q[2], range.minimum.z, range.extent.z, bits),
    };
}

// ---------------------------------------------------------------------------
// Source-track sampling (mirrors TrySampleAnimationClipUVE's key search semantics)
// ---------------------------------------------------------------------------

[[nodiscard]] bool TrySampleSourceTrackUVE(const std::vector<AnimationAssetSampleUVE>& samples,
                                           const double timeSeconds,
                                           AnimationAssetPoseUVE& outPose) noexcept {
    if (samples.empty()) {
        return false;
    }
    const auto upper = std::upper_bound(
        samples.begin(), samples.end(), timeSeconds,
        [](const double value, const AnimationAssetSampleUVE& sample) {
            return value < sample.timeSeconds;
        });
    if (upper == samples.begin()) {
        return TryNormalizeAnimationAssetPoseUVE(samples.front().pose, outPose);
    }
    if (upper == samples.end()) {
        return TryNormalizeAnimationAssetPoseUVE(samples.back().pose, outPose);
    }
    const AnimationAssetSampleUVE& right = *upper;
    const AnimationAssetSampleUVE& left = *(upper - 1);
    const double span = right.timeSeconds - left.timeSeconds;
    const double alpha = span <= 0.0 ? 0.0 : (timeSeconds - left.timeSeconds) / span;
    outPose = InterpolatePoseUVE(left.pose, right.pose, alpha);
    return true;
}

// ---------------------------------------------------------------------------
// Per-track compression
// ---------------------------------------------------------------------------

struct TrackReductionBudgetUVE final {
    double positionErrorMetres = 0.0;
    double rotationErrorRadians = 0.0;
    double scaleError = 0.0;
};

[[nodiscard]] double NormalizedKeyErrorUVE(const AnimationAssetPoseUVE& reference,
                                           const AnimationAssetPoseUVE& reconstructed,
                                           const TrackReductionBudgetUVE& budget) noexcept {
    double worst = 0.0;
    if (budget.positionErrorMetres > 0.0) {
        worst = std::max(worst, Vector3DistanceUVE(reference.position, reconstructed.position) /
                                    budget.positionErrorMetres);
    }
    if (budget.rotationErrorRadians > 0.0) {
        worst = std::max(worst, QuaternionAngularDistanceUVE(reference.rotation,
                                                            reconstructed.rotation) /
                                    budget.rotationErrorRadians);
    }
    if (budget.scaleError > 0.0) {
        worst = std::max(worst, Vector3DistanceUVE(reference.scale, reconstructed.scale) /
                                    budget.scaleError);
    }
    return worst;
}

/// Greedy key insertion: keep the endpoints, repeatedly split the worst-error interval at its
/// worst original key until every original key reconstructs within the reduction budget. The
/// error is measured at every original key time - the only times the source curve is defined at -
/// against the same lerp/nlerp reconstruction the runtime decoder uses.
[[nodiscard]] std::vector<std::size_t> ReduceKeysUVE(const std::vector<AnimationAssetSampleUVE>& samples,
                                                     const std::vector<std::uint32_t>& ticks,
                                                     const double durationSeconds,
                                                     const TrackReductionBudgetUVE& budget) {
    const std::size_t count = samples.size();
    std::vector<std::size_t> kept;
    if (count == 0U) {
        return kept;
    }
    kept.push_back(0U);
    if (count > 1U) {
        kept.push_back(count - 1U);
    }
    std::vector<double> keyTimes(count, 0.0);
    for (std::size_t index = 0U; index < count; ++index) {
        keyTimes[index] = TicksToTimeUVE(ticks[index], durationSeconds);
    }
    while (kept.size() < count) {
        double worstError = 0.0;
        std::size_t worstIndex = count;
        std::size_t keptCursor = 0U;
        for (std::size_t index = 0U; index < count; ++index) {
            if (keptCursor < kept.size() && kept[keptCursor] == index) {
                ++keptCursor;
                continue;
            }
            const std::size_t leftIndex = kept[keptCursor - 1U];
            const std::size_t rightIndex = kept[keptCursor];
            const double span = keyTimes[rightIndex] - keyTimes[leftIndex];
            const double alpha =
                span <= 0.0 ? 0.0 : (keyTimes[index] - keyTimes[leftIndex]) / span;
            const AnimationAssetPoseUVE reconstructed =
                InterpolatePoseUVE(samples[leftIndex].pose, samples[rightIndex].pose, alpha);
            const double error =
                NormalizedKeyErrorUVE(samples[index].pose, reconstructed, budget);
            if (error > worstError) {
                worstError = error;
                worstIndex = index;
            }
        }
        if (worstIndex == count || worstError <= 1.0) {
            break;
        }
        const auto insertAt = std::lower_bound(kept.begin(), kept.end(), worstIndex);
        kept.insert(insertAt, worstIndex);
    }
    return kept;
}

[[nodiscard]] bool CompressTrackUVE(const std::string& bone,
                                    const std::vector<AnimationAssetSampleUVE>& samples,
                                    const AnimationCompressionSettingsUVE& settings,
                                    const double durationSeconds,
                                    CompressedAnimationTrackUVE& outTrack) {
    CompressedAnimationTrackUVE track;
    track.bone = bone;
    if (samples.empty()) {
        outTrack = std::move(track);
        return true;
    }
    const std::size_t count = samples.size();
    std::vector<std::uint32_t> ticks(count, 0U);
    for (std::size_t index = 0U; index < count; ++index) {
        ticks[index] = TimeToTicksUVE(samples[index].timeSeconds, durationSeconds);
    }

    const TrackReductionBudgetUVE budget{
        settings.maximumPositionErrorMetres * kReductionBudgetShareUVE,
        settings.maximumRotationErrorRadians * kReductionBudgetShareUVE,
        settings.maximumScaleError * kReductionBudgetShareUVE,
    };
    const std::vector<std::size_t> kept = ReduceKeysUVE(samples, ticks, durationSeconds, budget);

    const ChannelRangeUVE positionRange = ComputeRangeUVE(samples, kept);
    const ChannelRangeUVE scaleRange = ComputeScaleRangeUVE(samples, kept);
    std::uint8_t positionBits = 0U;
    std::uint8_t scaleBits = 0U;
    if (!ChooseChannelBitsUVE(positionRange.extent,
                              settings.maximumPositionErrorMetres * kQuantizationBudgetShareUVE,
                              positionBits) ||
        !ChooseChannelBitsUVE(scaleRange.extent,
                              settings.maximumScaleError * kQuantizationBudgetShareUVE, scaleBits)) {
        return false;
    }

    track.positionRangeMinimum = positionRange.minimum;
    track.positionRangeExtent = positionRange.extent;
    track.scaleRangeMinimum = scaleRange.minimum;
    track.scaleRangeExtent = scaleRange.extent;
    track.positionComponentBits = positionBits;
    track.scaleComponentBits = scaleBits;

    const std::size_t stride = kTimeBytesUVE + kRotationBytesUVE + kRotationMetaBytesUVE +
                               (3U * ComponentBytesUVE(positionBits)) +
                               (3U * ComponentBytesUVE(scaleBits));
    track.keyData.reserve(stride * kept.size());
    for (const std::size_t index : kept) {
        // Worst-case record: 4 time + 6 rotation + 1 meta + 12 position + 12 scale = 35 bytes.
        std::array<std::uint8_t, 35> record{};
        std::size_t offset = 0U;
        WriteU32UVE(record.data() + offset, ticks[index]);
        offset += kTimeBytesUVE;
        const SmallestThreeEncodingUVE rotation = EncodeRotationUVE(samples[index].pose.rotation);
        for (const std::uint16_t component : rotation.components) {
            WriteU16UVE(record.data() + offset, component);
            offset += 2U;
        }
        record[offset] = rotation.meta;
        offset += kRotationMetaBytesUVE;
        std::vector<std::uint8_t> channelBytes;
        channelBytes.reserve(12U);
        AppendQuantizedVectorUVE(channelBytes, samples[index].pose.position, positionRange,
                                 positionBits);
        std::memcpy(record.data() + offset, channelBytes.data(), channelBytes.size());
        offset += channelBytes.size();
        channelBytes.clear();
        AppendQuantizedVectorUVE(channelBytes, samples[index].pose.scale, scaleRange, scaleBits);
        std::memcpy(record.data() + offset, channelBytes.data(), channelBytes.size());
        offset += channelBytes.size();
        track.keyData.insert(track.keyData.end(), record.begin(),
                             record.begin() + static_cast<std::ptrdiff_t>(offset));
    }
    outTrack = std::move(track);
    return true;
}

} // namespace

// ---------------------------------------------------------------------------
// Public surface
// ---------------------------------------------------------------------------

std::size_t CompressedAnimationTrackUVE::KeyStrideBytesUVE() const noexcept {
    const std::size_t positionBytes = ComponentBytesUVE(positionComponentBits);
    const std::size_t scaleBytes = ComponentBytesUVE(scaleComponentBits);
    if (positionBytes == 0U || scaleBytes == 0U) {
        return 0U;
    }
    return kTimeBytesUVE + kRotationBytesUVE + kRotationMetaBytesUVE + (3U * positionBytes) +
           (3U * scaleBytes);
}

std::size_t CompressedAnimationTrackUVE::KeyCountUVE() const noexcept {
    const std::size_t stride = KeyStrideBytesUVE();
    return stride == 0U ? 0U : keyData.size() / stride;
}

bool IsAnimationCompressionSettingsValidUVE(
    const AnimationCompressionSettingsUVE& settings) noexcept {
    return IsFinitePositiveUVE(settings.maximumPositionErrorMetres) &&
           settings.maximumPositionErrorMetres >=
               AnimationCompressionSettingsUVE::kMinimumPositionErrorMetresUVE &&
           settings.maximumPositionErrorMetres <=
               AnimationCompressionSettingsUVE::kMaximumPositionErrorMetresUVE &&
           IsFinitePositiveUVE(settings.maximumRotationErrorRadians) &&
           settings.maximumRotationErrorRadians >=
               AnimationCompressionSettingsUVE::kMinimumRotationErrorRadiansUVE &&
           settings.maximumRotationErrorRadians <=
               AnimationCompressionSettingsUVE::kMaximumRotationErrorRadiansUVE &&
           IsFinitePositiveUVE(settings.maximumScaleError) &&
           settings.maximumScaleError >=
               AnimationCompressionSettingsUVE::kMinimumScaleErrorUVE &&
           settings.maximumScaleError <= AnimationCompressionSettingsUVE::kMaximumScaleErrorUVE;
}

bool IsCompressedAnimationClipValidUVE(const CompressedAnimationClipUVE& clip) noexcept {
    if (clip.clipId.empty() ||
        clip.clipId.size() > kMaximumAnimationAssetIdentifierBytesUVE ||
        !IsFinitePositiveUVE(clip.durationSeconds) ||
        clip.events.size() > kMaximumAnimationAssetEventsUVE ||
        clip.rest.size() > kMaximumAnimationAssetBonesUVE ||
        clip.bones.size() > kMaximumAnimationAssetBonesUVE) {
        return false;
    }
    for (const AnimationAssetEventUVE& event : clip.events) {
        if (!std::isfinite(event.timeSeconds) || event.timeSeconds < 0.0 ||
            event.timeSeconds > clip.durationSeconds || event.eventId.empty() ||
            event.eventId.size() > kMaximumAnimationAssetIdentifierBytesUVE) {
            return false;
        }
    }
    for (const AnimationAssetRestBoneUVE& restBone : clip.rest) {
        if (restBone.bone.empty() ||
            restBone.bone.size() > kMaximumAnimationAssetIdentifierBytesUVE ||
            !IsFiniteVector3UVE(restBone.position) || !IsFiniteVector3UVE(restBone.scale) ||
            !std::isfinite(restBone.rotation.x) || !std::isfinite(restBone.rotation.y) ||
            !std::isfinite(restBone.rotation.z) || !std::isfinite(restBone.rotation.w)) {
            return false;
        }
    }
    const auto trackIsValid = [](const CompressedAnimationTrackUVE& track,
                                      const bool requireKeys) {
        const std::size_t stride = track.KeyStrideBytesUVE();
        if (stride == 0U || (track.keyData.size() % stride) != 0U) {
            return false;
        }
        const std::size_t keyCount = track.KeyCountUVE();
        if ((requireKeys && keyCount == 0U) || keyCount > kMaximumAnimationAssetSamplesUVE) {
            return false;
        }
        if (track.bone.size() > kMaximumAnimationAssetIdentifierBytesUVE ||
            !IsFiniteVector3UVE(track.positionRangeMinimum) ||
            !IsFiniteVector3UVE(track.positionRangeExtent) ||
            !IsFiniteVector3UVE(track.scaleRangeMinimum) ||
            !IsFiniteVector3UVE(track.scaleRangeExtent) ||
            track.positionRangeExtent.x < 0.0F || track.positionRangeExtent.y < 0.0F ||
            track.positionRangeExtent.z < 0.0F || track.scaleRangeExtent.x < 0.0F ||
            track.scaleRangeExtent.y < 0.0F || track.scaleRangeExtent.z < 0.0F) {
            return false;
        }
        return true;
    };
    if (!trackIsValid(clip.object, false)) {
        return false;
    }
    for (const CompressedAnimationTrackUVE& track : clip.bones) {
        if (track.bone.empty() || !trackIsValid(track, true)) {
            return false;
        }
    }
    for (std::size_t index = 0U; index < clip.bones.size(); ++index) {
        for (std::size_t other = index + 1U; other < clip.bones.size(); ++other) {
            if (clip.bones[index].bone == clip.bones[other].bone) {
                return false;
            }
        }
    }
    return true;
}

bool CompressAnimationClipAssetUVE(const AnimationClipAssetUVE& clip,
                                   const AnimationCompressionSettingsUVE& settings,
                                   CompressedAnimationClipUVE& outCompressed) {
    if (!IsAnimationClipAssetValidUVE(clip) || !IsAnimationCompressionSettingsValidUVE(settings)) {
        return false;
    }
    CompressedAnimationClipUVE compressed;
    compressed.clipId = clip.clipId;
    compressed.durationSeconds = clip.durationSeconds;
    compressed.conformed = clip.conformed;
    compressed.events = clip.events;
    compressed.rest = clip.rest;
    if (!CompressTrackUVE({}, clip.samples, settings, clip.durationSeconds, compressed.object)) {
        return false;
    }
    compressed.bones.reserve(clip.bones.size());
    for (const AnimationAssetBoneTrackUVE& source : clip.bones) {
        CompressedAnimationTrackUVE track;
        if (!CompressTrackUVE(source.bone, source.samples, settings, clip.durationSeconds, track)) {
            return false;
        }
        compressed.bones.push_back(std::move(track));
    }
    if (!IsCompressedAnimationClipValidUVE(compressed)) {
        return false;
    }
    outCompressed = std::move(compressed);
    return true;
}

bool TrySampleCompressedAnimationTrackUVE(const CompressedAnimationTrackUVE& track,
                                          const double durationSeconds, const double timeSeconds,
                                          const bool looping,
                                          AnimationAssetPoseUVE& outPose) noexcept {
    const std::size_t stride = track.KeyStrideBytesUVE();
    if (stride == 0U || !IsFinitePositiveUVE(durationSeconds) || !std::isfinite(timeSeconds) ||
        (track.keyData.size() % stride) != 0U || track.keyData.size() < stride) {
        return false;
    }
    const ChannelRangeUVE positionRange{track.positionRangeMinimum, track.positionRangeExtent};
    const ChannelRangeUVE scaleRange{track.scaleRangeMinimum, track.scaleRangeExtent};
    const std::size_t keyCount = track.KeyCountUVE();

    const auto decodeKeyTime = [&track, durationSeconds](const std::size_t index) {
        return TicksToTimeUVE(ReadU32UVE(track.keyData.data() + (index * track.KeyStrideBytesUVE())),
                              durationSeconds);
    };
    const auto decodeKeyPose = [&track, &positionRange, &scaleRange, stride](
                                   const std::size_t index, AnimationAssetPoseUVE& outKey) {
        const std::uint8_t* cursor = track.keyData.data() + (index * stride);
        cursor += kTimeBytesUVE;
        SmallestThreeEncodingUVE rotation;
        rotation.components[0] = ReadU16UVE(cursor);
        rotation.components[1] = ReadU16UVE(cursor + 2U);
        rotation.components[2] = ReadU16UVE(cursor + 4U);
        cursor += kRotationBytesUVE;
        rotation.meta = *cursor;
        cursor += kRotationMetaBytesUVE;
        outKey.position = ReadQuantizedVectorUVE(cursor, positionRange, track.positionComponentBits);
        outKey.scale = ReadQuantizedVectorUVE(cursor, scaleRange, track.scaleComponentBits);
        outKey.rotation = DecodeRotationUVE(rotation);
        AnimationAssetPoseUVE normalized{};
        if (!TryNormalizeAnimationAssetPoseUVE(outKey, normalized)) {
            return false;
        }
        outKey = normalized;
        return true;
    };

    const double sampleTime = NormalizeSampleTimeUVE(timeSeconds, durationSeconds, looping);
    std::size_t lower = 0U;
    std::size_t upper = keyCount;
    while (lower < upper) {
        const std::size_t mid = lower + ((upper - lower) / 2U);
        if (decodeKeyTime(mid) <= sampleTime) {
            lower = mid + 1U;
        } else {
            upper = mid;
        }
    }
    if (lower == 0U) {
        return decodeKeyPose(0U, outPose);
    }
    if (lower == keyCount) {
        return decodeKeyPose(keyCount - 1U, outPose);
    }
    AnimationAssetPoseUVE left{};
    AnimationAssetPoseUVE right{};
    if (!decodeKeyPose(lower - 1U, left) || !decodeKeyPose(lower, right)) {
        return false;
    }
    const double span = decodeKeyTime(lower) - decodeKeyTime(lower - 1U);
    const double alpha = span <= 0.0 ? 0.0 : (sampleTime - decodeKeyTime(lower - 1U)) / span;
    outPose = InterpolatePoseUVE(left, right, alpha);
    return true;
}

bool TryDecompressAnimationClipAssetUVE(const CompressedAnimationClipUVE& compressed,
                                        AnimationClipAssetUVE& outClip) {
    if (!IsCompressedAnimationClipValidUVE(compressed)) {
        return false;
    }
    AnimationClipAssetUVE clip;
    clip.clipId = compressed.clipId;
    clip.durationSeconds = compressed.durationSeconds;
    clip.conformed = compressed.conformed;
    clip.events = compressed.events;
    clip.rest = compressed.rest;
    const auto decodeTrack = [&compressed](const CompressedAnimationTrackUVE& track,
                                           std::vector<AnimationAssetSampleUVE>& outSamples) {
        const std::size_t keyCount = track.KeyCountUVE();
        outSamples.reserve(keyCount);
        for (std::size_t index = 0U; index < keyCount; ++index) {
            AnimationAssetPoseUVE pose{};
            if (!TrySampleCompressedAnimationTrackUVE(
                    track, compressed.durationSeconds,
                    TicksToTimeUVE(ReadU32UVE(track.keyData.data() +
                                              (index * track.KeyStrideBytesUVE())),
                                   compressed.durationSeconds),
                    false, pose)) {
                return false;
            }
            outSamples.push_back(AnimationAssetSampleUVE{
                TicksToTimeUVE(ReadU32UVE(track.keyData.data() +
                                          (index * track.KeyStrideBytesUVE())),
                               compressed.durationSeconds),
                pose});
        }
        return true;
    };
    if (compressed.object.KeyCountUVE() > 0U &&
        !decodeTrack(compressed.object, clip.samples)) {
        return false;
    }
    for (const CompressedAnimationTrackUVE& track : compressed.bones) {
        AnimationAssetBoneTrackUVE bone;
        bone.bone = track.bone;
        if (!decodeTrack(track, bone.samples)) {
            return false;
        }
        clip.bones.push_back(std::move(bone));
    }
    if (!IsAnimationClipAssetValidUVE(clip)) {
        return false;
    }
    outClip = std::move(clip);
    return true;
}

AnimationCompressionErrorReportUVE MeasureAnimationCompressionErrorUVE(
    const AnimationClipAssetUVE& original, const CompressedAnimationClipUVE& compressed,
    const std::size_t probeCount) {
    AnimationCompressionErrorReportUVE report;
    if (!IsAnimationClipAssetValidUVE(original) || !IsCompressedAnimationClipValidUVE(compressed) ||
        probeCount == 0U) {
        return report;
    }
    const std::size_t probes = std::min<std::size_t>(probeCount, 4096U);
    report.probeCount = probes;

    const auto measureTrack = [&original, &compressed, probes](
                                  const std::vector<AnimationAssetSampleUVE>& source,
                                  const CompressedAnimationTrackUVE& track,
                                  AnimationCompressionErrorReportUVE& inoutReport) {
        if (source.empty() || track.KeyCountUVE() == 0U) {
            return;
        }
        for (std::size_t probe = 0U; probe < probes; ++probe) {
            const double timeSeconds =
                (static_cast<double>(probe) / static_cast<double>(probes - 1U)) *
                original.durationSeconds;
            AnimationAssetPoseUVE sourcePose{};
            AnimationAssetPoseUVE compressedPose{};
            if (!TrySampleSourceTrackUVE(source, timeSeconds, sourcePose) ||
                !TrySampleCompressedAnimationTrackUVE(track, compressed.durationSeconds,
                                                      timeSeconds, false, compressedPose)) {
                continue;
            }
            inoutReport.maximumPositionErrorMetres =
                std::max(inoutReport.maximumPositionErrorMetres,
                         Vector3DistanceUVE(sourcePose.position, compressedPose.position));
            inoutReport.maximumRotationErrorRadians =
                std::max(inoutReport.maximumRotationErrorRadians,
                         QuaternionAngularDistanceUVE(sourcePose.rotation,
                                                      compressedPose.rotation));
            inoutReport.maximumScaleError = std::max(
                inoutReport.maximumScaleError,
                Vector3DistanceUVE(sourcePose.scale, compressedPose.scale));
        }
    };

    measureTrack(original.samples, compressed.object, report);
    if (original.bones.size() == compressed.bones.size()) {
        for (std::size_t index = 0U; index < original.bones.size(); ++index) {
            measureTrack(original.bones[index].samples, compressed.bones[index], report);
        }
    }
    return report;
}

std::size_t GetAnimationClipAssetSampleByteSizeUVE(const AnimationClipAssetUVE& clip) noexcept {
    constexpr std::size_t kSampleBytesUVE =
        sizeof(double) + (3U * sizeof(float)) + (4U * sizeof(float)) + (3U * sizeof(float));
    std::size_t total = clip.samples.size() * kSampleBytesUVE;
    for (const AnimationAssetBoneTrackUVE& track : clip.bones) {
        total += track.samples.size() * kSampleBytesUVE;
    }
    return total;
}

std::size_t GetCompressedAnimationClipByteSizeUVE(const CompressedAnimationClipUVE& clip) noexcept {
    std::size_t total = clip.object.keyData.size();
    for (const CompressedAnimationTrackUVE& track : clip.bones) {
        total += track.keyData.size();
    }
    total += clip.events.size() * (sizeof(double) + sizeof(std::string));
    total += clip.rest.size() * (sizeof(AnimationAssetRestBoneUVE));
    return total;
}

} // namespace UVE::Asset
