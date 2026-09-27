// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <string>
#include <string_view>

#include "uve/uvscript/uvscript_compiler_uve.h"

namespace UVE::UVScript {

/// C++23 source for `program`, to be compiled into a release build. The file registers itself
/// under the program's fingerprint, so once it is linked in every node running that exact program
/// runs this code instead of the interpreter - with the same results, errors and `wait` behaviour.
/// `origin` (usually the script's path) only goes into a comment.
[[nodiscard]] std::string GenerateUVScriptNativeCppUVE(const ProgramUVE& program, std::string_view origin);

} // namespace UVE::UVScript
