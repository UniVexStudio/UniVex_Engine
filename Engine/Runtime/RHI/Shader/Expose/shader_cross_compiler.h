// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cstdint>
#include <string>
#include <string_view>

#include "render_resource_descs.h"

namespace UVE::Render::Shader {

enum class ShaderTargetDialect : std::uint8_t {
    OpenGL430 = 0,
    OpenGLES300 = 1,
    VulkanGlsl450 = 2,
};

struct ShaderCrossCompileResult final {
    bool ok = false;
    std::string source;
    std::string errorMessage;
};

/// Translates a single-source UniVex GLSL shader (supporting `#ifdef VERTEX_SHADER` /
/// `#ifdef FRAGMENT_SHADER` / compute kernels) into a concrete backend dialect header,
/// precision qualifiers, and stage macro preamble ready for OpenGL 4.3, OpenGL ES 3.0, or
/// Vulkan GLSL 450 -> SPIR-V compilation.
[[nodiscard]] ShaderCrossCompileResult TranspileShaderSourceForDialect(
    std::string_view singleSource, ShaderStage stage, ShaderTargetDialect dialect);

} // namespace UVE::Render::Shader
