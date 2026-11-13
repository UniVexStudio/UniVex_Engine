// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
#include "animation_optimizer.h"

#include "animation_clip_compression.h"
#include "quaternion.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace UVE::Asset {
namespace {

struct LocalPoseUVE final {
    Math::Vector3UVE position{};
    Math::QuaternionUVE rotation{};
    Math::Vector3UVE scale{1.0F, 1.0F, 1.0F};
};

[[nodiscard]] Math::Vector3UVE LerpUVE(const Math::Vector3UVE a, const Math::Vector3UVE b, const float t) {
    return Math::Vector3UVE{a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t};
}

[[nodiscard]] Math::QuaternionUVE NlerpUVE(const Math::QuaternionUVE a, Math::QuaternionUVE b, const float t) {
    if (a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w < 0.0F) {
        b = Math::QuaternionUVE{-b.x, -b.y, -b.z, -b.w};
    }
    Math::QuaternionUVE q{a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t, a.w + (b.w - a.w) * t};
    const float length = std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
    if (length > 1.0e-8F) {
        q = Math::QuaternionUVE{q.x / length, q.y / length, q.z / length, q.w / length};
    }
    return q;
}

/// A track's pose at `time`: clamped at both ends, linear between samples.
[[nodiscard]] bool SampleUVE(const std::vector<AnimationAssetSampleUVE>& samples, const double time, LocalPoseUVE& out) {
    if (samples.empty()) {
        return false;
    }
    const auto after = std::lower_bound(samples.begin(), samples.end(), time,
                                        [](const AnimationAssetSampleUVE& s, const double t) { return s.timeSeconds < t; });
    if (after == samples.begin() || after == samples.end()) {
        const AnimationAssetPoseUVE& pose = after == samples.end() ? samples.back().pose : after->pose;
        out = LocalPoseUVE{pose.position, pose.rotation, pose.scale};
        return true;
    }
    const auto before = after - 1;
    const double span = after->timeSeconds - before->timeSeconds;
    const float t = span > 1.0e-12 ? static_cast<float>((time - before->timeSeconds) / span) : 0.0F;
    out.position = LerpUVE(before->pose.position, after->pose.position, t);
    out.rotation = NlerpUVE(before->pose.rotation, after->pose.rotation, t);
    out.scale = LerpUVE(before->pose.scale, after->pose.scale, t);
    return true;
}

/// Every joint's position in the skeleton's world at `time` (an object clip: its one track).
void WorldAtUVE(const AnimationClipAssetUVE& clip, const std::unordered_map<std::string, std::size_t>& trackOf, const double time,
                std::vector<Math::Vector3UVE>& outPositions) {
    if (clip.rest.empty()) {
        LocalPoseUVE pose;
        outPositions.assign(1U, SampleUVE(clip.samples, time, pose) ? pose.position : Math::Vector3UVE{});
        return;
    }
    const std::size_t count = clip.rest.size();
    std::vector<Math::QuaternionUVE> rotation(count);
    std::vector<Math::Vector3UVE> scale(count);
    outPositions.assign(count, Math::Vector3UVE{});
    for (std::size_t bone = 0U; bone < count; ++bone) {
        const AnimationAssetRestBoneUVE& rest = clip.rest[bone];
        LocalPoseUVE local{rest.position, rest.rotation, rest.scale};
        if (const auto found = trackOf.find(rest.bone); found != trackOf.end()) {
            static_cast<void>(SampleUVE(clip.bones[found->second].samples, time, local));
        }
        const std::int32_t parent = rest.parent;
        if (parent >= 0 && static_cast<std::size_t>(parent) < bone) {
            const auto p = static_cast<std::size_t>(parent);
            const Math::Vector3UVE scaled{local.position.x * scale[p].x, local.position.y * scale[p].y, local.position.z * scale[p].z};
            outPositions[bone] = outPositions[p] + Math::RotateVectorUVE(rotation[p], scaled);
            rotation[bone] = Math::MultiplyUVE(rotation[p], local.rotation);
            scale[bone] = Math::Vector3UVE{scale[p].x * local.scale.x, scale[p].y * local.scale.y, scale[p].z * local.scale.z};
        } else {
            outPositions[bone] = local.position;
            rotation[bone] = local.rotation;
            scale[bone] = local.scale;
        }
    }
}

[[nodiscard]] std::unordered_map<std::string, std::size_t> TracksByBoneUVE(const AnimationClipAssetUVE& clip) {
    std::unordered_map<std::string, std::size_t> out;
    for (std::size_t track = 0U; track < clip.bones.size(); ++track) {
        out.emplace(clip.bones[track].bone, track);
    }
    return out;
}

} // namespace

std::size_t CountAnimationClipSamplesUVE(const AnimationClipAssetUVE& clip) noexcept {
    std::size_t count = clip.samples.size();
    for (const AnimationAssetBoneTrackUVE& track : clip.bones) {
        count += track.samples.size();
    }
    return count;
}

AnimationOptimizeResultUVE OptimizeAnimationClipUVE(AnimationClipAssetUVE& clip, const AnimationOptimizeSettingsUVE& settings) {
    AnimationOptimizeResultUVE result;
    result.samplesBefore = CountAnimationClipSamplesUVE(clip);
    result.samplesAfter = result.samplesBefore;
    if (!IsAnimationClipAssetValidUVE(clip) || clip.durationSeconds <= 0.0 || result.samplesBefore < 4U) {
        return result;
    }
    // Each joint's budget: tighter at the root, looser down the chain.
    const std::size_t joints = clip.rest.empty() ? 1U : clip.rest.size();
    std::vector<double> budget(joints, settings.rootErrorMetres);
    for (std::size_t bone = 0U; bone < clip.rest.size(); ++bone) {
        std::size_t depth = 0U;
        for (std::int32_t at = clip.rest[bone].parent; at >= 0 && depth <= clip.rest.size(); at = clip.rest[static_cast<std::size_t>(at)].parent) {
            ++depth;
        }
        budget[bone] = std::min(settings.maximumErrorMetres, settings.rootErrorMetres * (1.0 + settings.depthGrowth * static_cast<double>(depth)));
    }
    // The original, at every probe.
    const std::size_t probes = std::max<std::size_t>(8U, settings.probes);
    const auto originalTracks = TracksByBoneUVE(clip);
    std::vector<std::vector<Math::Vector3UVE>> reference(probes);
    for (std::size_t p = 0U; p < probes; ++p) {
        WorldAtUVE(clip, originalTracks, clip.durationSeconds * static_cast<double>(p) / static_cast<double>(probes - 1U), reference[p]);
    }
    // A candidate at `strength` (a multiple of the codec's finest tolerances): its worst ratio to budget.
    const auto attempt = [&](const double strength, AnimationClipAssetUVE& out, double& worstRatio, double& worstMetres) {
        AnimationCompressionSettingsUVE codec;
        codec.maximumPositionErrorMetres = std::clamp(0.0005 * strength, AnimationCompressionSettingsUVE::kMinimumPositionErrorMetresUVE, 0.5);
        codec.maximumRotationErrorRadians = std::clamp(0.001 * strength, AnimationCompressionSettingsUVE::kMinimumRotationErrorRadiansUVE, 0.5);
        codec.maximumScaleError = std::clamp(0.0005 * strength, AnimationCompressionSettingsUVE::kMinimumScaleErrorUVE, 0.5);
        CompressedAnimationClipUVE compressed;
        AnimationClipAssetUVE rebuilt;
        if (!CompressAnimationClipAssetUVE(clip, codec, compressed) || !TryDecompressAnimationClipAssetUVE(compressed, rebuilt)) {
            return false;
        }
        // What the codec does not carry stays the original's.
        rebuilt.morphWeights = clip.morphWeights;
        rebuilt.clipId = clip.clipId;
        const auto tracks = TracksByBoneUVE(rebuilt);
        worstRatio = 0.0;
        worstMetres = 0.0;
        std::vector<Math::Vector3UVE> at;
        for (std::size_t p = 0U; p < probes; ++p) {
            WorldAtUVE(rebuilt, tracks, clip.durationSeconds * static_cast<double>(p) / static_cast<double>(probes - 1U), at);
            for (std::size_t j = 0U; j < at.size() && j < reference[p].size(); ++j) {
                const Math::Vector3UVE d = at[j] - reference[p][j];
                const double error = std::sqrt(static_cast<double>(d.x * d.x + d.y * d.y + d.z * d.z));
                worstMetres = std::max(worstMetres, error);
                worstRatio = std::max(worstRatio, error / std::max(budget[std::min(j, budget.size() - 1U)], 1.0e-9));
            }
        }
        out = std::move(rebuilt);
        return true;
    };
    // The strongest reduction within budget: a doubling search, then a bisection between the last
    // pass and the first failure.
    double good = 0.0;
    AnimationClipAssetUVE best;
    double bestRatio = 0.0;
    double bestMetres = 0.0;
    double low = 0.0;
    double high = 0.0;
    for (double strength = 0.25; strength <= 256.0; strength *= 2.0) {
        AnimationClipAssetUVE candidate;
        double ratio = 0.0;
        double metres = 0.0;
        if (!attempt(strength, candidate, ratio, metres)) {
            break;
        }
        if (ratio > 1.0) {
            high = strength;
            break;
        }
        good = strength;
        low = strength;
        best = std::move(candidate);
        bestRatio = ratio;
        bestMetres = metres;
    }
    if (high > 0.0) {
        for (int step = 0; step < 5; ++step) {
            const double mid = (low + high) * 0.5;
            AnimationClipAssetUVE candidate;
            double ratio = 0.0;
            double metres = 0.0;
            if (!attempt(mid, candidate, ratio, metres)) {
                break;
            }
            if (ratio <= 1.0) {
                low = mid;
                good = mid;
                best = std::move(candidate);
                bestRatio = ratio;
                bestMetres = metres;
            } else {
                high = mid;
            }
        }
    }
    if (good <= 0.0 || !IsAnimationClipAssetValidUVE(best)) {
        return result;
    }
    const std::size_t after = CountAnimationClipSamplesUVE(best);
    if (after >= result.samplesBefore) {
        return result;
    }
    result.changed = true;
    result.samplesAfter = after;
    result.worstBudgetRatio = bestRatio;
    result.worstErrorMetres = bestMetres;
    clip = std::move(best);
    return result;
}

} // namespace UVE::Asset
