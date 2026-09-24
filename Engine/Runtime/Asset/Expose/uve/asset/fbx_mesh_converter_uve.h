// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

#include "uve/asset/gltf_skeleton_uve.h"
#include "uve/asset/mesh_asset_uve.h"

namespace UVE::Asset {

inline constexpr std::size_t kMaximumFbxMeshSourceBytesUVE = 256U * 1024U * 1024U;
inline constexpr std::uint32_t kMaximumFbxMeshVerticesUVE = 1'000'000U;

/// Converts an FBX file (binary or ASCII, any version the parser reads) into one failure-atomic
/// static MeshAssetUVE.
///
/// Every mesh instance in the scene is merged, each placed where its node puts it, so a model
/// authored as several parts imports as the whole it looks like. The result is in the engine's
/// space: metres, +Y up, right-handed - an FBX from a centimetre, Z-up tool comes in at its real
/// size and standing up. Faces are triangulated, missing normals are generated, the first UV set
/// is kept, identical corners are shared and tangents are derived.
///
/// A skinned mesh imports in its bind pose, without its skin, the same as the glTF path: skinning,
/// materials, textures, cameras, lights and animation are not converted. Embedded and external
/// files are never read. Returns false and leaves `outMesh` untouched when the bytes do not parse,
/// hold no triangles, exceed kMaximumFbxMeshVerticesUVE or produce a non-finite value.
[[nodiscard]] bool ConvertFbxMeshUVE(std::span<const std::byte> source, MeshAssetUVE& outMesh);

/// What an FBX holds, read without its geometry or animation curves - enough to tell a model from
/// an animation-only file (a skeleton and its curves, the common shape of exported motion) before
/// anything is imported.
struct FbxSourceSummaryUVE final {
    std::size_t meshCount = 0U;
    std::size_t boneCount = 0U;
    std::size_t animationCount = 0U;
    /// The longest animation's length; 0 when there are none.
    double longestAnimationSeconds = 0.0;
    /// A mesh is skinned to bones, which is what makes a model a rigged Model rather than a Mesh.
    bool hasSkin = false;

    /// No mesh to show, but motion to play: the file is an animation, not a model.
    [[nodiscard]] bool IsAnimationOnlyUVE() const noexcept { return meshCount == 0U && animationCount > 0U; }
};

/// Summarizes the FBX in `source`; empty when the bytes do not parse as FBX.
[[nodiscard]] std::optional<FbxSourceSummaryUVE> DescribeFbxSourceUVE(std::span<const std::byte> source);

/// Reads an FBX's bones - names, hierarchy and rest pose - into the same joint list a glTF skin
/// gives (GltfSkeletonUVE is the engine's skeleton-source shape, whatever file it came from).
///
/// Every bone node counts, skinned or not, so an animation-only file (bones and takes, no mesh)
/// has a skeleton too. Poses are in the engine's space, metres and +Y up; parents precede their
/// children; a bone whose nearest bone ancestor is none is a root. Names are made unique the way
/// the glTF reader does it. Returns std::nullopt when the bytes do not parse, hold no bones, or
/// hold more than `maximumJoints`. skinCount is the number of skin deformers.
[[nodiscard]] std::optional<GltfSkeletonUVE> ReadFbxSkeletonUVE(std::span<const std::byte> source,
                                                                std::size_t maximumJoints);

} // namespace UVE::Asset
