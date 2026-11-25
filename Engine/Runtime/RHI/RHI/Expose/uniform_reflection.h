// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#pragma once

#include <cstdint>
#include <string>

#include "shader_data_type.h"

namespace UVE::Render {

/// One uniform IRenderDevice::GetPipelineUniforms() discovered by reflecting a linked
/// pipeline. `location` is a backend-internal identifier (a raw GL uniform location for
/// GlRenderDevice) that callers never need to interpret themselves — it exists purely so a
/// backend can look one up quickly from a name without leaking any GL type; a NullRenderDevice
/// pipeline never populates this (see GetPipelineUniforms()'s own doc comment).
struct UniformReflection {
    std::string name;
    ShaderDataType type = ShaderDataType::Float;
    std::int32_t location = -1;
    std::uint32_t arraySize = 1;
};

} // namespace UVE::Render
