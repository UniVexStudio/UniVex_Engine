// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "shader_cross_compiler.h"

#include <string>
#include <string_view>

namespace UVE::Render::Shader {
namespace {

[[nodiscard]] std::string_view VersionHeaderForDialect(const ShaderTargetDialect dialect) noexcept {
    switch (dialect) {
        case ShaderTargetDialect::OpenGL430:
            return "#version 430 core\n";
        case ShaderTargetDialect::OpenGLES300:
            return "#version 300 es\nprecision highp float;\nprecision highp int;\n";
        case ShaderTargetDialect::VulkanGlsl450:
            return "#version 450\n#define UVE_VULKAN 1\n";
    }
    return "#version 430 core\n";
}

[[nodiscard]] std::string_view StageMacroForStage(const ShaderStage stage) noexcept {
    switch (stage) {
        case ShaderStage::Vertex:
            return "#define VERTEX_SHADER 1\n";
        case ShaderStage::Fragment:
            return "#define FRAGMENT_SHADER 1\n";
        case ShaderStage::Compute:
            return "#define COMPUTE_SHADER 1\n";
        case ShaderStage::Geometry:
            return "#define GEOMETRY_SHADER 1\n";
    }
    return "";
}

} // namespace

ShaderCrossCompileResult TranspileShaderSourceForDialect(const std::string_view singleSource,
                                                               const ShaderStage stage,
                                                               const ShaderTargetDialect dialect) {
    ShaderCrossCompileResult out{};
    if (singleSource.empty()) {
        out.errorMessage = "Shader source is empty";
        return out;
    }

    // Strip any existing leading #version line so we emit the exact target dialect header first.
    std::string_view body = singleSource;
    std::size_t lineStart = 0U;
    while (lineStart < body.size()) {
        const std::size_t lineEnd = body.find('\n', lineStart);
        const std::string_view line =
            lineEnd == std::string_view::npos ? body.substr(lineStart) : body.substr(lineStart, lineEnd - lineStart);
        const std::size_t firstNonWs = line.find_first_not_of(" \t\r");
        if (firstNonWs == std::string_view::npos) {
            lineStart = (lineEnd == std::string_view::npos) ? body.size() : (lineEnd + 1U);
            continue;
        }
        const std::string_view trimmed = line.substr(firstNonWs);
        if (trimmed.rfind("#version", 0U) == 0U) {
            const std::size_t afterVersion =
                (lineEnd == std::string_view::npos) ? body.size() : (lineEnd + 1U);
            std::string stripped;
            stripped.reserve(body.size());
            stripped.append(body.substr(0U, lineStart));
            stripped.append(body.substr(afterVersion));
            out.source.reserve(stripped.size() + 128U);
            out.source.append(VersionHeaderForDialect(dialect));
            out.source.append(StageMacroForStage(stage));
            out.source.append(stripped);
            out.ok = true;
            return out;
        }
        if (trimmed.rfind("//", 0U) == 0U) {
            lineStart = (lineEnd == std::string_view::npos) ? body.size() : (lineEnd + 1U);
            continue;
        }
        break;
    }

    out.source.reserve(body.size() + 128U);
    out.source.append(VersionHeaderForDialect(dialect));
    out.source.append(StageMacroForStage(stage));
    out.source.append(body);
    out.ok = true;
    return out;
}

} // namespace UVE::Render::Shader
