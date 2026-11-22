// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>

#include "engine_version.h"
#include "sha256.h"

namespace UVE::Online {

/// The Hub's own version, separate from any engine's. `uve_hub --version` prints it, and a release
/// tag hub-v<version> must match it before an installer is published.
inline constexpr Core::VersionUVE kHubVersionUVE{0U, 1U, 1U, 0U};

/// A version as people read it: "MAJOR.MINOR.PATCH", with "+build.N" only when N is not 0.
[[nodiscard]] std::string FormatDisplayVersionUVE(const Core::VersionUVE& version);

/// A setup program is its own executable followed by the Hub payload (a gzip-compressed tar archive
/// of the installed files) and a footer: the payload's SHA-256, its size as 8 little-endian bytes,
/// then kSetupPayloadMagicUVE. Engine/Tools/make_hub_installer.py writes the same layout.
inline constexpr std::string_view kSetupPayloadMagicUVE = "UVXSETUP";
inline constexpr std::size_t kSetupPayloadSizeBytesUVE = 8U;
inline constexpr std::size_t kSetupFooterBytesUVE =
    kSha256DigestBytesUVE + kSetupPayloadSizeBytesUVE + kSetupPayloadMagicUVE.size();

/// Name of the Hub executable inside an installation, without the platform suffix.
inline constexpr std::string_view kHubExecutableStemUVE = "uve_hub";
/// The setup program copies itself into the installation under this stem, to uninstall later.
inline constexpr std::string_view kSetupExecutableStemUVE = "uve_hub_setup";
/// Name the installation folder gets under the platform's per-user programs folder.
inline constexpr std::string_view kHubInstallFolderNameUVE = "UniVex Hub";
/// Folder, inside an installation, holding the brand icons the payload carries.
inline constexpr std::string_view kHubIconsDirectoryNameUVE = "icons";
/// The icon applications menus and file managers show for the Hub and its project files.
inline constexpr std::string_view kHubIconFileNameUVE = "univex-logo-256.png";
inline constexpr std::string_view kHubWindowsIconFileNameUVE = "univex-logo.ico";
/// Project files (.uvproject) open with the Hub under this media type.
inline constexpr std::string_view kProjectMediaTypeUVE = "application/x-univex-project";
inline constexpr std::string_view kHubDisplayNameUVE = "UniVex Hub";
/// What the Hub is, as applications menus describe it.
inline constexpr std::string_view kHubDescriptionUVE = "Create, open and manage UniVex projects and engine versions";
/// How file managers name a project file's type.
inline constexpr std::string_view kProjectTypeDisplayNameUVE = "UniVex Project";
/// Who installed programs and the Windows Apps & features list name as the Hub's publisher.
inline constexpr std::string_view kPublisherUVE = "UniVex Studios";
/// Windows file association: the program identifier .uvproject files point at.
inline constexpr std::string_view kProjectProgramIdUVE = "UniVex.Project";

struct SetupPayloadLocationUVE final {
    std::uint64_t offset = 0U;
    std::uint64_t size = 0U;
    Sha256DigestUVE digest{};
};

/// Finds the payload appended to `setupExecutable`; nothing when it carries none.
[[nodiscard]] std::optional<SetupPayloadLocationUVE> LocateSetupPayloadUVE(const std::filesystem::path& setupExecutable);

/// Copies the payload out to `archive` and checks it against the footer's digest.
[[nodiscard]] bool ExtractSetupPayloadUVE(const std::filesystem::path& setupExecutable,
                                          const SetupPayloadLocationUVE& location,
                                          const std::filesystem::path& archive);

/// Where the Hub installs by default: the per-user programs folder (no administrator rights needed).
[[nodiscard]] std::optional<std::filesystem::path> DefaultHubInstallDirectoryUVE();

struct HubSetupResultUVE final {
    bool succeeded = false;
    std::string message;
};

/// Installs or upgrades the Hub in `installDirectory` from `payloadArchive`: the files are unpacked
/// beside it first and only replace the previous installation once complete, so a failed upgrade
/// leaves the old Hub working. `setupExecutable` is copied in as the uninstaller.
[[nodiscard]] HubSetupResultUVE InstallHubUVE(const std::filesystem::path& payloadArchive,
                                              const std::filesystem::path& installDirectory,
                                              const std::filesystem::path& setupExecutable);

/// Makes the installed Hub appear among the user's applications and opens project files with it: on
/// Linux a desktop entry, a media type for .uvproject and their icons; on Windows a Start menu
/// shortcut, an Apps & features entry and the .uvproject file association. `entriesRoot` stands in
/// for the per-user data folder (Linux) or Start menu folder (Windows) in isolated runs, where
/// nothing system-wide is touched; empty uses the platform's own locations.
[[nodiscard]] HubSetupResultUVE RegisterHubWithDesktopUVE(const std::filesystem::path& installDirectory,
                                                          const std::string& version,
                                                          const std::filesystem::path& entriesRoot = {});

/// Removes what RegisterHubWithDesktopUVE added and the installation itself. Projects, the project
/// list, settings and downloaded engines are the user's and stay.
[[nodiscard]] HubSetupResultUVE UninstallHubUVE(const std::filesystem::path& installDirectory,
                                                const std::filesystem::path& entriesRoot = {});

/// The Hub executable inside an installation.
[[nodiscard]] std::filesystem::path HubExecutableInUVE(const std::filesystem::path& installDirectory);

} // namespace UVE::Online
