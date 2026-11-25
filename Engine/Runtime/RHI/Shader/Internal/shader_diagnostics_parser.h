// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "shader_compile_diagnostics.h"

namespace UVE::Render::Shader::Detail {

/// Parses a raw glGetShaderInfoLog()/glGetProgramInfoLog() string into structured diagnostics,
/// tolerating the two common driver line-number formats: Mesa-style (`"0:12: error: ..."` or
/// `"0:12(5): error: ..."`) and NVIDIA-style (`"ERROR: 0:12: ..."`). The leading integer (the
/// GL-native `#line` "source string number" PreprocessShaderSource() emits) is resolved back
/// to a real virtual path via `fileIndexTable` (index -> path, as produced by
/// PreprocessResult::fileIndexTable) — an out-of-range or missing index leaves `filePath`
/// empty rather than erroring. A line that doesn't match either format still produces one
/// ShaderCompileError (with `lineNumber == 0` and the raw text in `message`) — never dropped,
/// never throws.
[[nodiscard]] std::vector<ShaderCompileError> ParseGlInfoLog(std::string_view rawLog,
                                                                    const std::vector<std::string>& fileIndexTable);

} // namespace UVE::Render::Shader::Detail
