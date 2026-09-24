// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

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

/// True when the FBX in `source` skins a mesh to bones, which is what makes it a rigged Model in
/// the editor rather than a plain Mesh. False for a static model or bytes that do not parse.
[[nodiscard]] bool FbxSourceHasSkinUVE(std::span<const std::byte> source);

} // namespace UVE::Asset
