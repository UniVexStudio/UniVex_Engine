// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#pragma once

#include <cstdint>
#include <filesystem>
#include <string>

namespace UVE::Asset {

/// Which programmable stage a ShaderAsset belongs to. Deliberately a separate type from
/// `Render::ShaderStage` (engine/render, Increment 10) — see `Asset::TextureAssetFormat`'s doc
/// comment for why: avoiding a dependency cycle, since engine/render already depends on
/// engine/asset. `Compute` is reserved for the future `ComputeSystem` (Part 7.2) — unused by
/// anything built so far.
enum class ShaderStageKind : std::uint8_t { Vertex, Fragment, Compute };

/// The CPU-side, engine-native representation of a `.uvshader` asset (Part 2's file-format
/// table): shader source text, stored and validated as-is. `ShaderManager`'s real "compile
/// GLSL/HLSL/Metal at runtime" step (Part 7.2) is blocked in this environment — no glslang/
/// shaderc/DXC is available — so this asset only carries source text; compilation is future work
/// once such a library exists.
struct ShaderAsset {
    ShaderStageKind stage = ShaderStageKind::Vertex;
    std::string sourceCode;
    std::string entryPointName = "main";
};

/// Loads `path` as a `.uve*` envelope with `AssetKind::Shader`, filling `outShader`. Returns
/// false (logging the reason) if the file is missing/malformed, isn't actually a Shader asset, its
/// `stage` is not one of the known `ShaderStageKind` values, or its `sourceCode` is empty (a
/// shader asset with no source is meaningless). The reserved `Compute` enum value remains a valid
/// typed asset value even though runtime compilation is future work.
[[nodiscard]] bool LoadShaderAsset(const std::filesystem::path& path, ShaderAsset& outShader);

/// Writes `shader` to `path` as a `.uve*` envelope with `AssetKind::Shader`. Returns false
/// (logging the reason) for an unknown `ShaderStageKind` value or if the file can't be written.
[[nodiscard]] bool SaveShaderAsset(const ShaderAsset& shader, const std::filesystem::path& path);

} // namespace UVE::Asset
