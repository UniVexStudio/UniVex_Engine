// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

#include "uve/asset/material_asset_uve.h"

namespace UVE::Asset {

inline constexpr std::size_t kMaximumMtlMaterialSourceBytesUVE = 64U * 1024U;

/// Converts one bounded MTL material declaration into a failure-atomic MaterialAssetUVE. The v1
/// path accepts one `newmtl` block, validated Kd/Ka/Ks/Ke/Tf vector properties, validated Ns/Ni/d/Tr/
/// illum scalar properties, and strict map directives as syntax-only evidence. It maps Kd to albedo,
/// Ks to metallic, d/Tr to opacity policy, and Ke to emissive color. It does not fabricate texture or
/// shader GUIDs, load external files, preserve unsupported map options, or own importer/GPU state.
[[nodiscard]] bool ConvertMtlMaterialUVE(std::string_view source, MaterialAssetUVE& outMaterial);

/// One material of a multi-material `.mtl` library: the converted `MaterialAssetUVE` plus the
/// texture files its `map_*` directives name. Those names stay file names - resolving them into
/// GUIDs is the importer's job, because only it knows which directory the model came from.
struct MtlLibraryMaterialUVE final {
    std::string name;
    MaterialAssetUVE material;
    std::string albedoTextureFile;   ///< map_Kd
    std::string normalTextureFile;   ///< map_Bump / bump / norm
    std::string metallicTextureFile; ///< map_Ks
    std::string emissiveTextureFile; ///< map_Ke
    std::string occlusionTextureFile;///< map_Ka / map_ao
};

struct MtlLibraryUVE final {
    std::vector<MtlLibraryMaterialUVE> materials;
    /// Set when the source declared more `newmtl` blocks than kMaximumMtlLibraryMaterialsUVE. The
    /// reported materials are still published; the rest are left out and the importer says so.
    bool truncated = false;
    /// `newmtl` blocks whose own directives did not convert. They are dropped from `materials`,
    /// never silently rewritten: the importer reports the count so a half-understood library is
    /// visible instead of looking complete.
    std::size_t skippedMaterials = 0U;
};

/// Largest `.mtl` library the importer will read. A scene library with dozens of materials still
/// fits comfortably; an unbounded read would not.
inline constexpr std::size_t kMaximumMtlLibrarySourceBytesUVE = 1024U * 1024U;
/// Most materials one library may declare. Matches the per-material cap's intent: bounded work per
/// import, reported rather than truncated silently.
inline constexpr std::size_t kMaximumMtlLibraryMaterialsUVE = 256U;

/// Converts a whole `.mtl` library: every `newmtl` block goes through the single-material converter
/// unchanged, and the texture-map directives are recorded as authored file names. Returns false only
/// for a source that is empty, oversized or NUL-containing; individual blocks that fail are counted
/// in `skippedMaterials`. It performs no filesystem I/O and fabricates no GUIDs.
[[nodiscard]] bool ConvertMtlLibraryUVE(std::string_view source, MtlLibraryUVE& outLibrary);

} // namespace UVE::Asset
