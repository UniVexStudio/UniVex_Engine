// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "render_resource_descs.h"
#include "shader_source_compile_desc.h"

namespace UVE::Render::Shader {

/// Describes a managed linked vertex+fragment program whose stages originate from separate source
/// descriptors. This preserves MaterialAsset's existing independent vertexShader/fragmentShader
/// asset GUID contract while reusing ShaderManager's async preprocessing, diagnostics, program
/// cache, and hot-reload lifecycle. The manager normalizes vertexSource.stage/fragmentSource.stage
/// to the corresponding graphics stages when the request is created; callers should still provide
/// those values for readable diagnostics. Both stages retain their own virtual path, embedded
/// fallback, defines, entry point, and dependency closure.
struct ShaderProgramStagesDesc {
    ShaderSourceCompileDesc vertexSource;
    ShaderSourceCompileDesc fragmentSource;

    std::vector<VertexAttribute> vertexLayout;
    std::uint32_t vertexStride = 0;
    PrimitiveTopology topology = PrimitiveTopology::Triangles;
    bool depthTestEnabled = true;
    bool depthWriteEnabled = true;
    PipelineBlendMode blendMode = PipelineBlendMode::Opaque;

    /// Enables program-level dependency tracking over the union of both stage closures. Individual
    /// source flags are honored only for source-only compilation requests; this program-level flag
    /// owns reload behavior for linked programs.
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
