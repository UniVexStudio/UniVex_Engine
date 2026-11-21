// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace UVE::Asset {

enum class MaterialPinTypeUVE : std::uint8_t {
    Float = 0,
    Vec2 = 1,
    Vec3 = 2,
    Vec4 = 3,
    Texture2D = 4,
};

enum class MaterialGraphNodeKindUVE : std::uint8_t {
    PbrOutput = 0,
    ConstantFloat = 1,
    ConstantVec3 = 2,
    Color = 3,
    UvCoord = 4,
    Time = 5,
    TextureSample = 6,
    Add = 7,
    Subtract = 8,
    Multiply = 9,
    Lerp = 10,
    Clamp = 11,
    Pow = 12,
    Sin = 13,
    Fresnel = 14,
    SimplexNoise = 15,
};

struct MaterialGraphNodeUVE final {
    std::uint32_t id = 0U;
    MaterialGraphNodeKindUVE kind = MaterialGraphNodeKindUVE::ConstantFloat;
    std::string label;
    float posX = 0.0F;
    float posY = 0.0F;
    std::array<float, 4> value{1.0F, 1.0F, 1.0F, 1.0F};
    std::string textureSlotName;
};

struct MaterialGraphLinkUVE final {
    std::uint32_t fromNodeId = 0U;
    std::uint32_t fromOutputIndex = 0U;
    std::uint32_t toNodeId = 0U;
    std::uint32_t toInputIndex = 0U;
};

/// Visual Material / Shader Graph asset (`.uvmatgraph`) storing nodes and typed links
/// compiled into a PBR surface shader.
struct MaterialGraphAssetUVE final {
    std::string name = "NewMaterialGraph";
    std::vector<MaterialGraphNodeUVE> nodes;
    std::vector<MaterialGraphLinkUVE> links;
};

struct MaterialGraphCompileResultUVE final {
    bool ok = false;
    std::string glslSource;
    std::vector<std::string> errors;
};

/// Creates a default PBR Material Graph with a Color constant wired into PbrOutput BaseColor.
[[nodiscard]] MaterialGraphAssetUVE CreateDefaultPbrMaterialGraphUVE();

/// Returns a display name for `kind`.
[[nodiscard]] const char* MaterialGraphNodeKindNameUVE(MaterialGraphNodeKindUVE kind) noexcept;

/// Compiles `graph` via cycle-checked topological sort into a complete GLSL PBR surface shader.
[[nodiscard]] MaterialGraphCompileResultUVE CompileMaterialGraphToGlslUVE(
    const MaterialGraphAssetUVE& graph);

/// Serializes / deserializes `.uvmatgraph` in a human-readable, VCS-diffable text format.
[[nodiscard]] bool SaveMaterialGraphAssetUVE(const MaterialGraphAssetUVE& graph,
                                             const std::filesystem::path& path);
[[nodiscard]] bool LoadMaterialGraphAssetUVE(const std::filesystem::path& path,
                                             MaterialGraphAssetUVE& outGraph);

} // namespace UVE::Asset
