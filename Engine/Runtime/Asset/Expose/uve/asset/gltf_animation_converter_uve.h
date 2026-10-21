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

/// The glTF channel paths a clip can carry. Translation/Rotation/Scale drive a skeleton bone;
/// Weights drives a mesh's morph-target weights - the channel glTF 2.0 defines in section 3.11
/// (Khronos Group, "glTF 2.0 Specification", https://registry.khronos.org/glTF/specs/2.0/) and the
/// one a DCC tool exports lip-sync through. The specification text is not reproduced here; the
/// reader is implemented against it.
enum class GltfAnimationPathUVE : std::uint8_t { Translation, Rotation, Scale, Weights };

/// The interpolations the converter bakes. CUBICSPLINE is rejected at parse: baking tangents to
/// dense keys would silently balloon small files past the per-track sample cap.
enum class GltfAnimationInterpolationUVE : std::uint8_t { Step, Linear };

/// One glTF channel with its target already resolved to a name: key times plus one value per key.
/// A transform channel fills `bone` and one VEC3 (translation/scale) or VEC4 unit-ish quaternion
/// (rotation) per key; a weights channel fills `node` and `morphTargetCount` SCALARs per key, laid
/// end to end in `values` exactly as the specification stores them.
struct GltfAnimationChannelSourceUVE final {
    /// The skeleton bone this channel drives. Non-empty for the TRS paths, empty for Weights.
    std::string bone;
    /// The node whose mesh this channel drives. Non-empty for Weights, empty for the TRS paths.
    /// glTF animates the mesh INSTANCE, so this is the node and not the mesh: two nodes may share
    /// one mesh, and each of them needs its own track.
    std::string node;
    GltfAnimationPathUVE path = GltfAnimationPathUVE::Translation;
    GltfAnimationInterpolationUVE interpolation = GltfAnimationInterpolationUVE::Linear;
    /// Weights only: how many morph targets one key carries. Zero for the TRS paths.
    std::size_t morphTargetCount = 0U;
    /// Float SCALAR key times in seconds: finite, >= 0, strictly increasing.
    GltfAccessorViewUVE times;
    /// Float values, one per key time: VEC3 for translation/scale, VEC4 for rotation, and
    /// `morphTargetCount` SCALARs per key for weights.
    GltfAccessorViewUVE values;
};

/// Caller-owned inputs for converting one glTF animation: the clip id plus its channels.
struct GltfAnimationSourceUVE final {
    std::string clipId;
    std::vector<GltfAnimationChannelSourceUVE> channels;
};

/// Converts one glTF animation to a clip against `skeleton`. Each bone's channels merge onto the
/// union of their key times - exact keys win, in-between times interpolate (lerp for
/// translation/scale, slerp for rotation; STEP holds the previous key), times outside a channel's
/// range hold its nearest key, and unbaked components fall back to the skeleton rest pose. Bones
/// with no channels get no track.
///
/// Weights channels become one `morphWeights` track per node, carrying the file's own keys
/// unchanged: a clip's morph track interpolates linearly and clamps outside its range, which is
/// exactly what a LINEAR glTF weights channel means, so nothing is resampled and playback cannot
/// drift from import. Only the node name is retained, so a clip plays on a mesh whose shape count
/// matches the one it was authored against.
///
/// A source with no bone channel accepts an EMPTY skeleton: a lip-sync take exported from a bare
/// head carries shapes and no skin at all, and the clip format has represented bone-free clips
/// since weight tracks landed. A source with a bone channel still requires a valid skeleton.
///
/// Returns false (leaving `outClip` untouched) for an invalid skeleton, an unknown or duplicate
/// target, a channel that names both a bone and a node, a bad key time (non-finite, negative, not
/// strictly increasing), a non-finite value or zero-length quaternion, a non-finite weight, a
/// weights channel whose value stream is not `keys * morphTargetCount` long, a STEP weights
/// channel (the clip's morph tracks have no step mode, so representing one would mean inventing
/// keys), a target name that is empty or unbounded, a union wider than the per-track sample cap,
/// or a zero duration.
[[nodiscard]] bool ConvertGltfAnimationUVE(const GltfAnimationSourceUVE& source,
                                           const SkeletonAssetUVE& skeleton, AnimationClipAssetUVE& outClip);

/// Parses the first animation of a glTF document (JSON text plus its single binary buffer):
/// channels targeting the document's skin joints resolve through the skeleton's bone names,
/// `weights` channels resolve through the node's mesh to its morph-target count, and sampler
/// inputs/outputs become key-time/value views. The clip id comes from animations[0].name,
/// defaulting to "clip" when absent or empty.
///
/// Node names are derived from the WHOLE node array in document order - `name`, else "Node",
/// uniquified with the `_N` suffix the skeleton applies to bones - so one node keeps one track name
/// in every animation of the file, which is what lets the runtime address its track. A document
/// with no `skins` is accepted when every channel is a weights channel (a lip-sync-only export);
/// it is still rejected when a channel names a bone, because there is no skeleton to resolve it
/// against.
///
/// Returns std::nullopt for malformed JSON, a file with no animation or no skin and no weights
/// channel, CUBICSPLINE or STEP interpolation on a weights channel, a transform path other than
/// TRS, a channel targeting a node outside the skin, a weights channel targeting a node without a
/// mesh, a mesh without morph targets, or a mesh whose primitives disagree about how many targets
/// they have, a value stream that is not exactly `keys * targets` long, a non-SCALAR weights
/// output, a node name above the identifier cap, a node array above the naming cap, or anything
/// above the bone cap.
[[nodiscard]] std::optional<GltfAnimationSourceUVE> ParseGltfAnimationSourceUVE(
    std::string_view json, const std::vector<std::byte>& buffer);

} // namespace UVE::Asset
