// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace UVE::Platform {

/// File-name suffix of an executable on the platform this build targets.
#if defined(_WIN32)
inline constexpr std::string_view kExecutableSuffixUVE = ".exe";
#else
inline constexpr std::string_view kExecutableSuffixUVE = "";
#endif

struct ProcessLaunchDescUVE final {
    /// The program to run. A bare name is looked up on the search path.
    std::filesystem::path executable;
    std::vector<std::string> arguments;
    /// Empty keeps the caller's working directory.
    std::filesystem::path workingDirectory;
};

struct ProcessLaunchResultUVE final {
    bool launched = false;
    std::string message;
};

/// Starts a program that outlives the caller: it is not waited for and keeps running when the
/// caller exits. Arguments are passed as-is, never through a shell.
[[nodiscard]] ProcessLaunchResultUVE LaunchDetachedProcessUVE(const ProcessLaunchDescUVE& desc);

/// The running program's own executable file, or empty when the platform cannot say.
[[nodiscard]] std::filesystem::path CurrentExecutablePathUVE();

/// Shows `directory` in the platform's file manager.
[[nodiscard]] ProcessLaunchResultUVE RevealInFileManagerUVE(const std::filesystem::path& directory);

} // namespace UVE::Platform
