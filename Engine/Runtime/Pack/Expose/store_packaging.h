// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cstdint>
#include <filesystem>
#include <string>

namespace UVE::Pack {

enum class StorePackageTargetOsUVE : std::uint8_t {
    Windows = 0,
    Linux = 1,
    MacOS = 2,
};

struct StorePackageSpecUVE final {
    std::string appName = "UniVexApp";
    std::string appVersion = "1.0.0";
    std::string publisher = "UniVex Studios";
    std::string executableName = "uve_runtime";
    std::string codeSigningIdentity;
    std::filesystem::path stagedDirectory;
    StorePackageTargetOsUVE targetOs = StorePackageTargetOsUVE::Windows;
};

/// Generates a ready-to-compile Windows InnoSetup (`.iss`) installer script for `spec`.
[[nodiscard]] std::string GenerateWindowsInstallerScriptUVE(const StorePackageSpecUVE& spec);

/// Generates a Linux AppImage `.desktop` entry manifest for `spec`.
[[nodiscard]] std::string GenerateLinuxAppImageDesktopUVE(const StorePackageSpecUVE& spec);

/// Builds a platform-appropriate code-signing CLI command (Windows `signtool`, macOS `codesign`,
/// Linux `gpg --detach-sign`) when `codeSigningIdentity` is non-empty.
[[nodiscard]] std::string BuildCodeSigningCommandUVE(const StorePackageSpecUVE& spec,
                                                     const std::filesystem::path& artifactPath);

/// Writes OS store packaging manifests into `outDir` (`.iss` on Windows, `.desktop` + `AppRun` on Linux).
[[nodiscard]] bool EmitStorePackagingManifestsUVE(const StorePackageSpecUVE& spec,
                                                  const std::filesystem::path& outDir);

} // namespace UVE::Pack
