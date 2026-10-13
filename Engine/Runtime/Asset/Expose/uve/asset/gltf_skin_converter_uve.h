// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "uve/asset/gltf_mesh_converter_uve.h"
#include "uve/asset/gltf_skeleton_uve.h"
#include "uve/asset/mesh_asset_uve.h"
#include "uve/asset/skeleton_asset_uve.h"

namespace UVE::Asset {

/// Caller-owned inputs for converting one glTF skin: the joint hierarchy plus the accessors the
/// skin and its skinned primitive reference. The hierarchy arrives parents-first (as produced by
/// ParseGltfSkeletonUVE), but JOINTS_0 and the inverse bind matrices index the skin's OWN joint
/// order, so `skinJointNodes` carries that order (document node indices) and the converter maps
/// through GltfJointUVE::sourceNodeIndex. `vertexCount` comes from the primitive's POSITION
/// accessor, so a JOINTS_0/WEIGHTS_0 count mismatch fails here instead of at mesh merge.
struct GltfSkinSourceUVE final {
    /// Becomes the skeleton's id; must satisfy the skeleton validator (non-empty, bounded).
    std::string skeletonId;
    GltfSkeletonUVE skeleton;
    /// skins[0].joints[] order — one document node index per skeleton joint.
    std::vector<std::size_t> skinJointNodes;
    /// Float MAT4, one matrix per skin joint in skin order. Absent when the skin omits
    /// inverseBindMatrices, which the converter reads as identity per the glTF 2.0 spec.
    std::optional<GltfAccessorViewUVE> inverseBindMatrices;
    /// UBYTE/USHORT/UINT VEC4 joint indices in skin order, one element per vertex.
    GltfAccessorViewUVE joints0;
    /// Float VEC4 blend weights, one element per vertex.
    GltfAccessorViewUVE weights0;
    std::uint64_t vertexCount = 0U;
};

/// One converted skin: the standalone skeleton, the mesh's joints in the same order (carrying
/// the inverse bind matrices), and the per-vertex influences with joint indices already mapped
/// to skeleton order and weights renormalized to sum to one.
struct GltfSkinResultUVE final {
    SkeletonAssetUVE skeleton;
    std::vector<MeshJointUVE> joints;
    std::vector<MeshSkinningInfluenceUVE> influences;
};

/// Converts one glTF skin to a standalone skeleton plus mesh skinning data. Returns false
/// (leaving `outSkin` untouched) when the hierarchy fails skeleton validation, the skin-order
/// node list disagrees with the hierarchy, an accessor span is invalid, a joint index is out of
/// range, a weight is non-finite or negative, or a vertex's weights sum to zero. Weights are
/// renormalized to sum to one — the importer-side correction IsSkinningInfluenceNormalizedUVE()
/// documents — and inverse bind matrices are transposed from glTF column-major to the engine's
/// row-major Matrix4x4UVE.
[[nodiscard]] bool ConvertGltfSkinUVE(const GltfSkinSourceUVE& source, GltfSkinResultUVE& outSkin);

/// Parses the first skin of a glTF document (JSON text plus its single binary buffer): the
/// hierarchy via ParseGltfSkeletonUVE, inverse bind matrices from skins[0], and JOINTS_0/
/// WEIGHTS_0 plus the vertex count from the document's single mesh primitive, with the same
/// strictness as the mesh importer (exactly one mesh with exactly one primitive, POSITION
/// required). The skeleton id comes from skins[0].name, defaulting to "skin" when absent or
/// empty. Returns std::nullopt for malformed JSON, a file with no skin, an accessor count or
/// type mismatch, or anything above the skeleton joint cap.
[[nodiscard]] std::optional<GltfSkinSourceUVE> ParseGltfSkinSourceUVE(
    std::string_view json, const std::vector<std::byte>& buffer);

} // namespace UVE::Asset
