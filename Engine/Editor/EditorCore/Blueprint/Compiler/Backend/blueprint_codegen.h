// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <string>
#include <string_view>

#include "blueprint_compiler.h"

namespace UVE::Blueprint {

/// C++23 source for `program`, to be compiled into a release build. The file registers itself
/// under the program's fingerprint, so once it is linked in every object running that exact program
/// runs this code instead of the interpreter - with the same results, errors and `wait` behaviour.
/// `origin` (usually the script's path) only goes into a comment.
[[nodiscard]] std::string GenerateUVScriptNativeCpp(const Program& program, std::string_view origin);

} // namespace UVE::Blueprint
