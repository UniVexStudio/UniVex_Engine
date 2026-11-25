// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "animation_clip_compression.h"

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

constexpr std::uint32_t kTimeTicksMax = 0xFFFFFFFFu;
constexpr double kTimeTicksMaxAsDouble = 4294967295.0;

// Smallest-three quaternion storage: the three smaller components each live in
// [-1/sqrt(2), 1/sqrt(2)], biased and quantized to 16 bits over that range.
constexpr float kSmallestThreeHalfRange = 0.7071067811865476F;
constexpr float kSmallestThreeEncodeScale =
    65535.0F / (2.0F * kSmallestThreeHalfRange);

// Budget split: keyframe reduction and quantization each target 40% of the total tolerance,
// leaving 20% as numerical margin (float interpolation, time rounding, nlerp normalization).
constexpr double kReductionBudgetShare = 0.4;
constexpr double kQuantizationBudgetShare = 0.4;

// The codec's own floors live on `AnimationCompressionSettings` (header) so callers can see
// them; settings validation rejects tolerances below them explicitly rather than silently
// producing a clip that misses its stated bound.

constexpr std::size_t kTimeBytes = 4U;
constexpr std::size_t kRotationBytes = 6U;
constexpr std::size_t kRotationMetaBytes = 1U;

// ---------------------------------------------------------------------------
// Small helpers
// ---------------------------------------------------------------------------

[[nodiscard]] bool IsFinitePositive(const double value) noexcept {
    return std::isfinite(value) && value > 0.0;
}

[[nodiscard]] bool IsFiniteVector3(const Math::Vector3& value) noexcept {
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

[[nodiscard]] std::size_t ComponentBytes(const std::uint8_t bits) noexcept {
    return bits == 16U ? 2U : (bits == 32U ? 4U : 0U);
}

[[nodiscard]] double NormalizeSampleTime(const double timeSeconds,
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

[[nodiscard]] double QuaternionDot(const Math::Quaternion& lhs,
                                      const Math::Quaternion& rhs) noexcept {
    return static_cast<double>(lhs.x) * static_cast<double>(rhs.x) +
           static_cast<double>(lhs.y) * static_cast<double>(rhs.y) +
           static_cast<double>(lhs.z) * static_cast<double>(rhs.z) +
           static_cast<double>(lhs.w) * static_cast<double>(rhs.w);
}

/// Angular distance in radians between two unit quaternions. q and -q are the same rotation, so
/// the absolute dot product decides the angle.
[[nodiscard]] double QuaternionAngularDistance(const Math::Quaternion& lhs,
                                                  const Math::Quaternion& rhs) noexcept {
    const double dot = std::clamp(std::abs(QuaternionDot(lhs, rhs)), 0.0, 1.0);
    return 2.0 * std::acos(dot);
}

[[nodiscard]] double Vector3Distance(const Math::Vector3& lhs,
                                        const Math::Vector3& rhs) noexcept {
    const double dx = static_cast<double>(lhs.x) - static_cast<double>(rhs.x);
    const double dy = static_cast<double>(lhs.y) - static_cast<double>(rhs.y);
    const double dz = static_cast<double>(lhs.z) - static_cast<double>(rhs.z);
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

/// Mirrors the uncompressed clip sampler's pose interpolation exactly (lerp position/scale,
/// nlerp + normalize rotation, no shortest-path sign flip, left-key fallback on a degenerate
/// normalize) so decompressed sampling is semantically identical to `InterpolatePose`.
[[nodiscard]] bool IsFiniteAnimationAssetPose(const AnimationAssetPose& pose) noexcept {
    return Math::IsFinite(pose.position) && Math::IsFinite(pose.scale) &&
           Math::IsFinite(pose.rotation);
}

[[nodiscard]] bool TryNormalizeAnimationAssetPose(const AnimationAssetPose& pose,
                                                     AnimationAssetPose& outNormalized) noexcept {
    if (!IsFiniteAnimationAssetPose(pose)) {
        return false;
    }
    Math::Quaternion normalizedRotation{};
    if (!Math::TryNormalize(pose.rotation, normalizedRotation)) {
        return false;
    }
    outNormalized = pose;
    outNormalized.rotation = normalizedRotation;
    return true;
}

[[nodiscard]] AnimationAssetPose InterpolatePose(const AnimationAssetPose& left,
                                                       const AnimationAssetPose& right,
                                                       const double alpha) noexcept {
    const float factor = static_cast<float>(std::clamp(alpha, 0.0, 1.0));
    AnimationAssetPose result;
    result.position = left.position * (1.0F - factor) + right.position * factor;
    result.scale = left.scale * (1.0F - factor) + right.scale * factor;
    result.rotation = Math::Quaternion{
        left.rotation.x * (1.0F - factor) + right.rotation.x * factor,
        left.rotation.y * (1.0F - factor) + right.rotation.y * factor,
        left.rotation.z * (1.0F - factor) + right.rotation.z * factor,
        left.rotation.w * (1.0F - factor) + right.rotation.w * factor,
    };
    AnimationAssetPose normalized;
    if (!TryNormalizeAnimationAssetPose(result, normalized)) {
        return left;
    }
    return normalized;
}

// ---------------------------------------------------------------------------
// Time quantization
// ---------------------------------------------------------------------------

[[nodiscard]] std::uint32_t TimeToTicks(const double timeSeconds,
                                           const double durationSeconds) noexcept {
    if (durationSeconds <= 0.0 || timeSeconds <= 0.0) {
        return 0U;
    }
    if (timeSeconds >= durationSeconds) {
        return kTimeTicksMax;
    }
    const double scaled = (timeSeconds / durationSeconds) * kTimeTicksMaxAsDouble;
    return static_cast<std::uint32_t>(std::llround(std::clamp(scaled, 0.0, kTimeTicksMaxAsDouble)));
}

[[nodiscard]] double TicksToTime(const std::uint32_t ticks,
                                    const double durationSeconds) noexcept {
    return (static_cast<double>(ticks) / kTimeTicksMaxAsDouble) * durationSeconds;
}

// ---------------------------------------------------------------------------
// Byte-stream record access (little-endian, memcpy-based: no alignment assumptions)
// ---------------------------------------------------------------------------

void WriteU16(std::uint8_t* const destination, const std::uint16_t value) noexcept {
    std::memcpy(destination, &value, sizeof(value));
}

void WriteU32(std::uint8_t* const destination, const std::uint32_t value) noexcept {
    std::memcpy(destination, &value, sizeof(value));
}

[[nodiscard]] std::uint16_t ReadU16(const std::uint8_t* const source) noexcept {
    std::uint16_t value = 0U;
    std::memcpy(&value, source, sizeof(value));
    return value;
}

[[nodiscard]] std::uint32_t ReadU32(const std::uint8_t* const source) noexcept {
    std::uint32_t value = 0U;
    std::memcpy(&value, source, sizeof(value));
    return value;
}

// ---------------------------------------------------------------------------
// Smallest-three quaternion quantization
// ---------------------------------------------------------------------------

struct SmallestThreeEncoding final {
    std::uint16_t components[3] = {0U, 0U, 0U};
    std::uint8_t meta = 0U; // bits 0-1: largest index, bit 2: largest sign (1 = negative)
};

[[nodiscard]] SmallestThreeEncoding EncodeRotation(const Math::Quaternion& input) noexcept {
    Math::Quaternion unit{};
    const Math::Quaternion source = TryNormalize(input, unit) ? unit : Math::Quaternion{0.0F, 0.0F, 0.0F, 1.0F};
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
    SmallestThreeEncoding encoding;
    encoding.meta = static_cast<std::uint8_t>(
        (largestIndex & 0x3U) | ((values[largestIndex] < 0.0F) ? 0x4U : 0x0U));
    std::size_t outputIndex = 0U;
    for (std::size_t index = 0U; index < 4U; ++index) {
        if (index == largestIndex) {
            continue;
        }
        const float biased =
            (values[index] + kSmallestThreeHalfRange) * kSmallestThreeEncodeScale;
        const float clamped = std::clamp(biased, 0.0F, 65535.0F);
        encoding.components[outputIndex] = static_cast<std::uint16_t>(std::lround(clamped));
        ++outputIndex;
    }
    return encoding;
}

[[nodiscard]] Math::Quaternion DecodeRotation(const SmallestThreeEncoding& encoding) noexcept {
    const float decoded[3] = {
        (static_cast<float>(encoding.components[0]) / kSmallestThreeEncodeScale) -
            kSmallestThreeHalfRange,
        (static_cast<float>(encoding.components[1]) / kSmallestThreeEncodeScale) -
            kSmallestThreeHalfRange,
        (static_cast<float>(encoding.components[2]) / kSmallestThreeEncodeScale) -
            kSmallestThreeHalfRange,
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
    return Math::Quaternion{values[0], values[1], values[2], values[3]};
}

// ---------------------------------------------------------------------------
// Channel quantization (positions and scales)
// ---------------------------------------------------------------------------

struct ChannelRange final {
    Math::Vector3 minimum{0.0F, 0.0F, 0.0F};
    Math::Vector3 extent{0.0F, 0.0F, 0.0F};
};

[[nodiscard]] ChannelRange ComputeRange(const std::vector<AnimationAssetSample>& samples,
                                              const std::vector<std::size_t>& kept) noexcept {
    Math::Vector3 minimum{std::numeric_limits<float>::max(), std::numeric_limits<float>::max(),
                             std::numeric_limits<float>::max()};
    Math::Vector3 maximum{std::numeric_limits<float>::lowest(),
                             std::numeric_limits<float>::lowest(),
                             std::numeric_limits<float>::lowest()};
    for (const std::size_t index : kept) {
        const Math::Vector3& value = samples[index].pose.position;
        minimum.x = std::min(minimum.x, value.x);
        minimum.y = std::min(minimum.y, value.y);
        minimum.z = std::min(minimum.z, value.z);
        maximum.x = std::max(maximum.x, value.x);
        maximum.y = std::max(maximum.y, value.y);
        maximum.z = std::max(maximum.z, value.z);
    }
    return ChannelRange{minimum, maximum - minimum};
}

[[nodiscard]] ChannelRange ComputeScaleRange(const std::vector<AnimationAssetSample>& samples,
                                                   const std::vector<std::size_t>& kept) noexcept {
    Math::Vector3 minimum{std::numeric_limits<float>::max(), std::numeric_limits<float>::max(),
                             std::numeric_limits<float>::max()};
    Math::Vector3 maximum{std::numeric_limits<float>::lowest(),
                             std::numeric_limits<float>::lowest(),
                             std::numeric_limits<float>::lowest()};
    for (const std::size_t index : kept) {
        const Math::Vector3& value = samples[index].pose.scale;
        minimum.x = std::min(minimum.x, value.x);
        minimum.y = std::min(minimum.y, value.y);
        minimum.z = std::min(minimum.z, value.z);
        maximum.x = std::max(maximum.x, value.x);
        maximum.y = std::max(maximum.y, value.y);
        maximum.z = std::max(maximum.z, value.z);
    }
    return ChannelRange{minimum, maximum - minimum};
}

/// Quantization half-step error budget for a 3D channel over `extent` at `bits`. Uses the widest
/// axis conservatively and scales by sqrt(3) for the euclidean worst case.
[[nodiscard]] bool ChannelQuantizationMeetsBudget(const Math::Vector3& extent,
                                                     const std::uint32_t bits,
                                                     const double budget) noexcept {
    const double levels = (bits == 16U) ? 65535.0 : 4294967295.0;
    const double widest = std::max({static_cast<double>(extent.x), static_cast<double>(extent.y),
                                    static_cast<double>(extent.z)});
    const double worstCase = (widest / levels) * 0.5 * 1.7320508075688772;
    return worstCase <= budget;
}

[[nodiscard]] bool ChooseChannelBits(const Math::Vector3& extent, const double budget,
                                        std::uint8_t& outBits) noexcept {
    if (ChannelQuantizationMeetsBudget(extent, 16U, budget)) {
        outBits = 16U;
        return true;
    }
    if (ChannelQuantizationMeetsBudget(extent, 32U, budget)) {
        outBits = 32U;
        return true;
    }
    return false;
}

[[nodiscard]] std::uint32_t QuantizeChannelComponent(const float value, const float minimum,
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

[[nodiscard]] float DequantizeChannelComponent(const std::uint32_t quantized, const float minimum,
                                                  const float extent,
                                                  const std::uint8_t bits) noexcept {
    if (extent <= 0.0F) {
        return minimum;
    }
    const double levels = (bits == 16U) ? 65535.0 : 4294967295.0;
    return minimum + static_cast<float>((static_cast<double>(quantized) / levels) *
                                        static_cast<double>(extent));
}

void AppendQuantizedVector(std::vector<std::uint8_t>& stream, const Math::Vector3& value,
                              const ChannelRange& range, const std::uint8_t bits) noexcept {
    const std::uint32_t q[3] = {
        QuantizeChannelComponent(value.x, range.minimum.x, range.extent.x, bits),
        QuantizeChannelComponent(value.y, range.minimum.y, range.extent.y, bits),
        QuantizeChannelComponent(value.z, range.minimum.z, range.extent.z, bits),
    };
    for (const std::uint32_t component : q) {
        if (bits == 16U) {
            std::array<std::uint8_t, 2> bytes{};
            WriteU16(bytes.data(), static_cast<std::uint16_t>(component));
            stream.insert(stream.end(), bytes.begin(), bytes.end());
        } else {
            std::array<std::uint8_t, 4> bytes{};
            WriteU32(bytes.data(), component);
            stream.insert(stream.end(), bytes.begin(), bytes.end());
        }
    }
}

[[nodiscard]] Math::Vector3 ReadQuantizedVector(const std::uint8_t*& cursor,
                                                      const ChannelRange& range,
                                                      const std::uint8_t bits) noexcept {
    std::uint32_t q[3] = {0U, 0U, 0U};
    for (std::size_t index = 0U; index < 3U; ++index) {
        if (bits == 16U) {
            q[index] = ReadU16(cursor);
            cursor += 2U;
        } else {
            q[index] = ReadU32(cursor);
            cursor += 4U;
        }
    }
    return Math::Vector3{
        DequantizeChannelComponent(q[0], range.minimum.x, range.extent.x, bits),
        DequantizeChannelComponent(q[1], range.minimum.y, range.extent.y, bits),
        DequantizeChannelComponent(q[2], range.minimum.z, range.extent.z, bits),
    };
}

// ---------------------------------------------------------------------------
// Source-track sampling (mirrors TrySampleAnimationClip's key search semantics)
// ---------------------------------------------------------------------------

[[nodiscard]] bool TrySampleSourceTrack(const std::vector<AnimationAssetSample>& samples,
                                           const double timeSeconds,
                                           AnimationAssetPose& outPose) noexcept {
    if (samples.empty()) {
        return false;
    }
    const auto upper = std::upper_bound(
        samples.begin(), samples.end(), timeSeconds,
        [](const double value, const AnimationAssetSample& sample) {
            return value < sample.timeSeconds;
        });
    if (upper == samples.begin()) {
        return TryNormalizeAnimationAssetPose(samples.front().pose, outPose);
    }
    if (upper == samples.end()) {
        return TryNormalizeAnimationAssetPose(samples.back().pose, outPose);
    }
    const AnimationAssetSample& right = *upper;
    const AnimationAssetSample& left = *(upper - 1);
    const double span = right.timeSeconds - left.timeSeconds;
    const double alpha = span <= 0.0 ? 0.0 : (timeSeconds - left.timeSeconds) / span;
    outPose = InterpolatePose(left.pose, right.pose, alpha);
    return true;
}

// ---------------------------------------------------------------------------
// Per-track compression
// ---------------------------------------------------------------------------

struct TrackReductionBudget final {
    double positionErrorMetres = 0.0;
    double rotationErrorRadians = 0.0;
    double scaleError = 0.0;
};

[[nodiscard]] double NormalizedKeyError(const AnimationAssetPose& reference,
                                           const AnimationAssetPose& reconstructed,
                                           const TrackReductionBudget& budget) noexcept {
    double worst = 0.0;
    if (budget.positionErrorMetres > 0.0) {
        worst = std::max(worst, Vector3Distance(reference.position, reconstructed.position) /
                                    budget.positionErrorMetres);
    }
    if (budget.rotationErrorRadians > 0.0) {
        worst = std::max(worst, QuaternionAngularDistance(reference.rotation,
                                                            reconstructed.rotation) /
                                    budget.rotationErrorRadians);
    }
    if (budget.scaleError > 0.0) {
        worst = std::max(worst, Vector3Distance(reference.scale, reconstructed.scale) /
                                    budget.scaleError);
    }
    return worst;
}

/// Greedy key insertion: keep the endpoints, repeatedly split the worst-error interval at its
/// worst original key until every original key reconstructs within the reduction budget. The
/// error is measured at every original key time - the only times the source curve is defined at -
/// against the same lerp/nlerp reconstruction the runtime decoder uses.
[[nodiscard]] std::vector<std::size_t> ReduceKeys(const std::vector<AnimationAssetSample>& samples,
                                                     const std::vector<std::uint32_t>& ticks,
                                                     const double durationSeconds,
                                                     const TrackReductionBudget& budget) {
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
        keyTimes[index] = TicksToTime(ticks[index], durationSeconds);
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
            const AnimationAssetPose reconstructed =
                InterpolatePose(samples[leftIndex].pose, samples[rightIndex].pose, alpha);
            const double error =
                NormalizedKeyError(samples[index].pose, reconstructed, budget);
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

[[nodiscard]] bool CompressTrack(const std::string& bone,
                                    const std::vector<AnimationAssetSample>& samples,
                                    const AnimationCompressionSettings& settings,
                                    const double durationSeconds,
                                    CompressedAnimationTrack& outTrack) {
    CompressedAnimationTrack track;
    track.bone = bone;
    if (samples.empty()) {
        outTrack = std::move(track);
        return true;
    }
    const std::size_t count = samples.size();
    std::vector<std::uint32_t> ticks(count, 0U);
    for (std::size_t index = 0U; index < count; ++index) {
        ticks[index] = TimeToTicks(samples[index].timeSeconds, durationSeconds);
    }

    const TrackReductionBudget budget{
        settings.maximumPositionErrorMetres * kReductionBudgetShare,
        settings.maximumRotationErrorRadians * kReductionBudgetShare,
        settings.maximumScaleError * kReductionBudgetShare,
    };
    const std::vector<std::size_t> kept = ReduceKeys(samples, ticks, durationSeconds, budget);

    const ChannelRange positionRange = ComputeRange(samples, kept);
    const ChannelRange scaleRange = ComputeScaleRange(samples, kept);
    std::uint8_t positionBits = 0U;
    std::uint8_t scaleBits = 0U;
    if (!ChooseChannelBits(positionRange.extent,
                              settings.maximumPositionErrorMetres * kQuantizationBudgetShare,
                              positionBits) ||
        !ChooseChannelBits(scaleRange.extent,
                              settings.maximumScaleError * kQuantizationBudgetShare, scaleBits)) {
        return false;
    }

    track.positionRangeMinimum = positionRange.minimum;
    track.positionRangeExtent = positionRange.extent;
    track.scaleRangeMinimum = scaleRange.minimum;
    track.scaleRangeExtent = scaleRange.extent;
    track.positionComponentBits = positionBits;
    track.scaleComponentBits = scaleBits;

    const std::size_t stride = kTimeBytes + kRotationBytes + kRotationMetaBytes +
                               (3U * ComponentBytes(positionBits)) +
                               (3U * ComponentBytes(scaleBits));
    track.keyData.reserve(stride * kept.size());
    for (const std::size_t index : kept) {
        // Worst-case record: 4 time + 6 rotation + 1 meta + 12 position + 12 scale = 35 bytes.
        std::array<std::uint8_t, 35> record{};
        std::size_t offset = 0U;
        WriteU32(record.data() + offset, ticks[index]);
        offset += kTimeBytes;
        const SmallestThreeEncoding rotation = EncodeRotation(samples[index].pose.rotation);
        for (const std::uint16_t component : rotation.components) {
            WriteU16(record.data() + offset, component);
            offset += 2U;
        }
        record[offset] = rotation.meta;
        offset += kRotationMetaBytes;
        std::vector<std::uint8_t> channelBytes;
        channelBytes.reserve(12U);
        AppendQuantizedVector(channelBytes, samples[index].pose.position, positionRange,
                                 positionBits);
        std::memcpy(record.data() + offset, channelBytes.data(), channelBytes.size());
        offset += channelBytes.size();
        channelBytes.clear();
        AppendQuantizedVector(channelBytes, samples[index].pose.scale, scaleRange, scaleBits);
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

std::size_t CompressedAnimationTrack::KeyStrideBytes() const noexcept {
    const std::size_t positionBytes = ComponentBytes(positionComponentBits);
    const std::size_t scaleBytes = ComponentBytes(scaleComponentBits);
    if (positionBytes == 0U || scaleBytes == 0U) {
        return 0U;
    }
    return kTimeBytes + kRotationBytes + kRotationMetaBytes + (3U * positionBytes) +
           (3U * scaleBytes);
}

std::size_t CompressedAnimationTrack::KeyCount() const noexcept {
    const std::size_t stride = KeyStrideBytes();
    return stride == 0U ? 0U : keyData.size() / stride;
}

bool IsAnimationCompressionSettingsValid(
    const AnimationCompressionSettings& settings) noexcept {
    return IsFinitePositive(settings.maximumPositionErrorMetres) &&
           settings.maximumPositionErrorMetres >=
               AnimationCompressionSettings::kMinimumPositionErrorMetres &&
           settings.maximumPositionErrorMetres <=
               AnimationCompressionSettings::kMaximumPositionErrorMetres &&
           IsFinitePositive(settings.maximumRotationErrorRadians) &&
           settings.maximumRotationErrorRadians >=
               AnimationCompressionSettings::kMinimumRotationErrorRadians &&
           settings.maximumRotationErrorRadians <=
               AnimationCompressionSettings::kMaximumRotationErrorRadians &&
           IsFinitePositive(settings.maximumScaleError) &&
           settings.maximumScaleError >=
               AnimationCompressionSettings::kMinimumScaleError &&
           settings.maximumScaleError <= AnimationCompressionSettings::kMaximumScaleError;
}

bool IsCompressedAnimationClipValid(const CompressedAnimationClip& clip) noexcept {
    if (clip.clipId.empty() ||
        clip.clipId.size() > kMaximumAnimationAssetIdentifierBytes ||
        !IsFinitePositive(clip.durationSeconds) ||
        clip.events.size() > kMaximumAnimationAssetEvents ||
        clip.rest.size() > kMaximumAnimationAssetBones ||
        clip.bones.size() > kMaximumAnimationAssetBones) {
        return false;
    }
    for (const AnimationAssetEvent& event : clip.events) {
        if (!std::isfinite(event.timeSeconds) || event.timeSeconds < 0.0 ||
            event.timeSeconds > clip.durationSeconds || event.eventId.empty() ||
            event.eventId.size() > kMaximumAnimationAssetIdentifierBytes) {
            return false;
        }
    }
    for (const AnimationAssetRestBone& restBone : clip.rest) {
        if (restBone.bone.empty() ||
            restBone.bone.size() > kMaximumAnimationAssetIdentifierBytes ||
            !IsFiniteVector3(restBone.position) || !IsFiniteVector3(restBone.scale) ||
            !std::isfinite(restBone.rotation.x) || !std::isfinite(restBone.rotation.y) ||
            !std::isfinite(restBone.rotation.z) || !std::isfinite(restBone.rotation.w)) {
            return false;
        }
    }
    const auto trackIsValid = [](const CompressedAnimationTrack& track,
                                      const bool requireKeys) {
        const std::size_t stride = track.KeyStrideBytes();
        if (stride == 0U || (track.keyData.size() % stride) != 0U) {
            return false;
        }
        const std::size_t keyCount = track.KeyCount();
        if ((requireKeys && keyCount == 0U) || keyCount > kMaximumAnimationAssetSamples) {
            return false;
        }
        if (track.bone.size() > kMaximumAnimationAssetIdentifierBytes ||
            !IsFiniteVector3(track.positionRangeMinimum) ||
            !IsFiniteVector3(track.positionRangeExtent) ||
            !IsFiniteVector3(track.scaleRangeMinimum) ||
            !IsFiniteVector3(track.scaleRangeExtent) ||
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
    for (const CompressedAnimationTrack& track : clip.bones) {
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

bool CompressAnimationClipAsset(const AnimationClipAsset& clip,
                                   const AnimationCompressionSettings& settings,
                                   CompressedAnimationClip& outCompressed) {
    if (!IsAnimationClipAssetValid(clip) || !IsAnimationCompressionSettingsValid(settings)) {
        return false;
    }
    CompressedAnimationClip compressed;
    compressed.clipId = clip.clipId;
    compressed.durationSeconds = clip.durationSeconds;
    compressed.conformed = clip.conformed;
    compressed.events = clip.events;
    compressed.rest = clip.rest;
    if (!CompressTrack({}, clip.samples, settings, clip.durationSeconds, compressed.object)) {
        return false;
    }
    compressed.bones.reserve(clip.bones.size());
    for (const AnimationAssetBoneTrack& source : clip.bones) {
        CompressedAnimationTrack track;
        if (!CompressTrack(source.bone, source.samples, settings, clip.durationSeconds, track)) {
            return false;
        }
        compressed.bones.push_back(std::move(track));
    }
    if (!IsCompressedAnimationClipValid(compressed)) {
        return false;
    }
    outCompressed = std::move(compressed);
    return true;
}

bool TrySampleCompressedAnimationTrack(const CompressedAnimationTrack& track,
                                          const double durationSeconds, const double timeSeconds,
                                          const bool looping,
                                          AnimationAssetPose& outPose) noexcept {
    const std::size_t stride = track.KeyStrideBytes();
    if (stride == 0U || !IsFinitePositive(durationSeconds) || !std::isfinite(timeSeconds) ||
        (track.keyData.size() % stride) != 0U || track.keyData.size() < stride) {
        return false;
    }
    const ChannelRange positionRange{track.positionRangeMinimum, track.positionRangeExtent};
    const ChannelRange scaleRange{track.scaleRangeMinimum, track.scaleRangeExtent};
    const std::size_t keyCount = track.KeyCount();

    const auto decodeKeyTime = [&track, durationSeconds](const std::size_t index) {
        return TicksToTime(ReadU32(track.keyData.data() + (index * track.KeyStrideBytes())),
                              durationSeconds);
    };
    const auto decodeKeyPose = [&track, &positionRange, &scaleRange, stride](
                                   const std::size_t index, AnimationAssetPose& outKey) {
        const std::uint8_t* cursor = track.keyData.data() + (index * stride);
        cursor += kTimeBytes;
        SmallestThreeEncoding rotation;
        rotation.components[0] = ReadU16(cursor);
        rotation.components[1] = ReadU16(cursor + 2U);
        rotation.components[2] = ReadU16(cursor + 4U);
        cursor += kRotationBytes;
        rotation.meta = *cursor;
        cursor += kRotationMetaBytes;
        outKey.position = ReadQuantizedVector(cursor, positionRange, track.positionComponentBits);
        outKey.scale = ReadQuantizedVector(cursor, scaleRange, track.scaleComponentBits);
        outKey.rotation = DecodeRotation(rotation);
        AnimationAssetPose normalized{};
        if (!TryNormalizeAnimationAssetPose(outKey, normalized)) {
            return false;
        }
        outKey = normalized;
        return true;
    };

    const double sampleTime = NormalizeSampleTime(timeSeconds, durationSeconds, looping);
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
    AnimationAssetPose left{};
    AnimationAssetPose right{};
    if (!decodeKeyPose(lower - 1U, left) || !decodeKeyPose(lower, right)) {
        return false;
    }
    const double span = decodeKeyTime(lower) - decodeKeyTime(lower - 1U);
    const double alpha = span <= 0.0 ? 0.0 : (sampleTime - decodeKeyTime(lower - 1U)) / span;
    outPose = InterpolatePose(left, right, alpha);
    return true;
}

bool TryDecompressAnimationClipAsset(const CompressedAnimationClip& compressed,
                                        AnimationClipAsset& outClip) {
    if (!IsCompressedAnimationClipValid(compressed)) {
        return false;
    }
    AnimationClipAsset clip;
    clip.clipId = compressed.clipId;
    clip.durationSeconds = compressed.durationSeconds;
    clip.conformed = compressed.conformed;
    clip.events = compressed.events;
    clip.rest = compressed.rest;
    const auto decodeTrack = [&compressed](const CompressedAnimationTrack& track,
                                           std::vector<AnimationAssetSample>& outSamples) {
        const std::size_t keyCount = track.KeyCount();
        outSamples.reserve(keyCount);
        for (std::size_t index = 0U; index < keyCount; ++index) {
            AnimationAssetPose pose{};
            if (!TrySampleCompressedAnimationTrack(
                    track, compressed.durationSeconds,
                    TicksToTime(ReadU32(track.keyData.data() +
                                              (index * track.KeyStrideBytes())),
                                   compressed.durationSeconds),
                    false, pose)) {
                return false;
            }
            outSamples.push_back(AnimationAssetSample{
                TicksToTime(ReadU32(track.keyData.data() +
                                          (index * track.KeyStrideBytes())),
                               compressed.durationSeconds),
                pose});
        }
        return true;
    };
    if (compressed.object.KeyCount() > 0U &&
        !decodeTrack(compressed.object, clip.samples)) {
        return false;
    }
    for (const CompressedAnimationTrack& track : compressed.bones) {
        AnimationAssetBoneTrack bone;
        bone.bone = track.bone;
        if (!decodeTrack(track, bone.samples)) {
            return false;
        }
        clip.bones.push_back(std::move(bone));
    }
    if (!IsAnimationClipAssetValid(clip)) {
        return false;
    }
    outClip = std::move(clip);
    return true;
}

AnimationCompressionErrorReport MeasureAnimationCompressionError(
    const AnimationClipAsset& original, const CompressedAnimationClip& compressed,
    const std::size_t probeCount) {
    AnimationCompressionErrorReport report;
    if (!IsAnimationClipAssetValid(original) || !IsCompressedAnimationClipValid(compressed) ||
        probeCount == 0U) {
        return report;
    }
    const std::size_t probes = std::min<std::size_t>(probeCount, 4096U);
    report.probeCount = probes;

    const auto measureTrack = [&original, &compressed, probes](
                                  const std::vector<AnimationAssetSample>& source,
                                  const CompressedAnimationTrack& track,
                                  AnimationCompressionErrorReport& inoutReport) {
        if (source.empty() || track.KeyCount() == 0U) {
            return;
        }
        for (std::size_t probe = 0U; probe < probes; ++probe) {
            const double timeSeconds =
                (static_cast<double>(probe) / static_cast<double>(probes - 1U)) *
                original.durationSeconds;
            AnimationAssetPose sourcePose{};
            AnimationAssetPose compressedPose{};
            if (!TrySampleSourceTrack(source, timeSeconds, sourcePose) ||
                !TrySampleCompressedAnimationTrack(track, compressed.durationSeconds,
                                                      timeSeconds, false, compressedPose)) {
                continue;
            }
            inoutReport.maximumPositionErrorMetres =
                std::max(inoutReport.maximumPositionErrorMetres,
                         Vector3Distance(sourcePose.position, compressedPose.position));
            inoutReport.maximumRotationErrorRadians =
                std::max(inoutReport.maximumRotationErrorRadians,
                         QuaternionAngularDistance(sourcePose.rotation,
                                                      compressedPose.rotation));
            inoutReport.maximumScaleError = std::max(
                inoutReport.maximumScaleError,
                Vector3Distance(sourcePose.scale, compressedPose.scale));
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

std::size_t GetAnimationClipAssetSampleByteSize(const AnimationClipAsset& clip) noexcept {
    constexpr std::size_t kSampleBytes =
        sizeof(double) + (3U * sizeof(float)) + (4U * sizeof(float)) + (3U * sizeof(float));
    std::size_t total = clip.samples.size() * kSampleBytes;
    for (const AnimationAssetBoneTrack& track : clip.bones) {
        total += track.samples.size() * kSampleBytes;
    }
    return total;
}

std::size_t GetCompressedAnimationClipByteSize(const CompressedAnimationClip& clip) noexcept {
    std::size_t total = clip.object.keyData.size();
    for (const CompressedAnimationTrack& track : clip.bones) {
        total += track.keyData.size();
    }
    total += clip.events.size() * (sizeof(double) + sizeof(std::string));
    total += clip.rest.size() * (sizeof(AnimationAssetRestBone));
    return total;
}

} // namespace UVE::Asset
