// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#pragma once

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "render_resource_descs.h"

namespace UVE::Render::Shader {

/// Describes a linked vertex+fragment program to create via IShaderManager::CreateProgram().
/// Both stages come from the *same* resolved source (see BuiltIn's single-file-per-program
/// convention) — ShaderManager compiles it twice, injecting VERTEX_SHADER/FRAGMENT_SHADER
/// respectively (see Detail::ApplyPreprocessor()) so each stage's `#ifdef` block is the only
/// one active. `virtualFilePath`/`embeddedFallbackSourceCode` follow the same automatic-fallback
/// contract as ShaderSourceCompileDesc.
struct ShaderProgramDesc {
    std::string virtualFilePath;
    std::string embeddedFallbackSourceCode;
    std::vector<std::pair<std::string, std::string>> extraDefines;
    std::string entryPointName = "main";

    std::vector<VertexAttribute> vertexLayout;
    std::uint32_t vertexStride = 0;
    PrimitiveTopology topology = PrimitiveTopology::Triangles;
    bool depthTestEnabled = true;
    bool depthWriteEnabled = true;
    PipelineBlendMode blendMode = PipelineBlendMode::Opaque;

    bool hotReloadEnabled = true;
    std::string debugName;

    // Appended rasterizer tail mirroring PipelineDesc (Tier 2.1): same fields, same defaults,
    // threaded through ShaderManager into both the compiled and binary-cache pipeline paths.
    CullMode cullMode = CullMode::None;
    FrontFace frontFace = FrontFace::CounterClockwise;
    FillMode fillMode = FillMode::Fill;
    bool depthBiasEnabled = false;
    float depthBiasConstantFactor = 0.0F;
    float depthBiasSlopeFactor = 0.0F;
    DepthCompare depthCompare = DepthCompare::Less;
};

} // namespace UVE::Render::Shader
