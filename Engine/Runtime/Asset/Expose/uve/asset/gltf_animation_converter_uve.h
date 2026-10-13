// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "uve/asset/animation_clip_asset_uve.h"
#include "uve/asset/gltf_mesh_converter_uve.h"
#include "uve/asset/skeleton_asset_uve.h"

namespace UVE::Asset {

/// The glTF channel paths a skeletal clip can carry. Morph-target ("weights") channels are
/// rejected at parse — the engine's bone tracks have no morph target to receive them.
enum class GltfAnimationPathUVE : std::uint8_t { Translation, Rotation, Scale };

/// The interpolations the converter bakes. CUBICSPLINE is rejected at parse: baking tangents to
/// dense keys would silently balloon small files past the per-track sample cap.
enum class GltfAnimationInterpolationUVE : std::uint8_t { Step, Linear };

/// One glTF channel with its target already resolved to a skeleton bone name: key times plus one
/// value per key (VEC3 for translation/scale, VEC4 unit-ish quaternion for rotation).
struct GltfAnimationChannelSourceUVE final {
    std::string bone;
    GltfAnimationPathUVE path = GltfAnimationPathUVE::Translation;
    GltfAnimationInterpolationUVE interpolation = GltfAnimationInterpolationUVE::Linear;
    /// Float SCALAR key times in seconds: finite, >= 0, strictly increasing.
    GltfAccessorViewUVE times;
    /// Float values, one per key time.
    GltfAccessorViewUVE values;
};

/// Caller-owned inputs for converting one glTF animation: the clip id plus its channels.
struct GltfAnimationSourceUVE final {
    std::string clipId;
    std::vector<GltfAnimationChannelSourceUVE> channels;
};

/// Converts one glTF animation to a skeletal clip against `skeleton`. Each bone's channels merge
/// onto the union of their key times — exact keys win, in-between times interpolate (lerp for
/// translation/scale, slerp for rotation; STEP holds the previous key), times outside a
/// channel's range hold its nearest key, and unbaked components fall back to the skeleton rest
/// pose. Bones with no channels get no track. Returns false (leaving `outClip` untouched) for an
/// invalid skeleton, an unknown or duplicate (bone, path) target, a bad key time (non-finite,
/// negative, not strictly increasing), a non-finite value or zero-length quaternion, a union
/// wider than the per-track sample cap, or a zero duration.
[[nodiscard]] bool ConvertGltfAnimationUVE(const GltfAnimationSourceUVE& source,
                                           const SkeletonAssetUVE& skeleton, AnimationClipAssetUVE& outClip);

/// Parses the first animation of a glTF document (JSON text plus its single binary buffer):
/// channels targeting the document's skin joints resolve through the skeleton's bone names, and
/// sampler inputs/outputs become key-time/value views. The clip id comes from
/// animations[0].name, defaulting to "clip" when absent or empty. Returns std::nullopt for
/// malformed JSON, a file with no animation or no skin, CUBICSPLINE interpolation, a
/// non-TRS path, a channel targeting a node outside the skin, or anything above the bone cap.
[[nodiscard]] std::optional<GltfAnimationSourceUVE> ParseGltfAnimationSourceUVE(
    std::string_view json, const std::vector<std::byte>& buffer);

} // namespace UVE::Asset
