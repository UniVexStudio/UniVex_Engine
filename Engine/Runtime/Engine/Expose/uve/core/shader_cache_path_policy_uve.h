// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <filesystem>

namespace UVE::Core {

/// The shader-program binary cache accepts a relative directory only. Reject rooted paths and
/// explicit parent traversal so project settings cannot directly point the cache outside the process
/// working directory. Cache creation remains best-effort; this is lexical containment, not a sandbox
/// against symlinks created inside the working tree.
[[nodiscard]] inline bool IsSafeShaderCachePathUVE(const std::filesystem::path& path) {
    if (path.empty() || path.is_absolute() || path.has_root_name() || path.has_root_directory()) {
        return false;
    }
    if (path.native().find(std::filesystem::path::value_type{}) != std::filesystem::path::string_type::npos) {
        return false;
    }
    for (const std::filesystem::path& component : path) {
        if (component == std::filesystem::path("..")) {
            return false;
        }
    }
    return true;
}

} // namespace UVE::Core
