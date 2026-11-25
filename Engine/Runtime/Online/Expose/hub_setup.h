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
inline constexpr Core::Version kHubVersion{0U, 1U, 1U, 0U};

/// A version as people read it: "MAJOR.MINOR.PATCH", with "+build.N" only when N is not 0.
[[nodiscard]] std::string FormatDisplayVersion(const Core::Version& version);

/// A setup program is its own executable followed by the Hub payload (a gzip-compressed tar archive
/// of the installed files) and a footer: the payload's SHA-256, its size as 8 little-endian bytes,
/// then kSetupPayloadMagic. Engine/Tools/make_hub_installer.py writes the same layout.
inline constexpr std::string_view kSetupPayloadMagic = "UVXSETUP";
inline constexpr std::size_t kSetupPayloadSizeBytes = 8U;
inline constexpr std::size_t kSetupFooterBytes =
    kSha256DigestBytes + kSetupPayloadSizeBytes + kSetupPayloadMagic.size();

/// Name of the Hub executable inside an installation, without the platform suffix.
inline constexpr std::string_view kHubExecutableStem = "uve_hub";
/// The setup program copies itself into the installation under this stem, to uninstall later.
inline constexpr std::string_view kSetupExecutableStem = "uve_hub_setup";
/// Name the installation folder gets under the platform's per-user programs folder.
inline constexpr std::string_view kHubInstallFolderName = "UniVex Hub";
/// Folder, inside an installation, holding the brand icons the payload carries.
inline constexpr std::string_view kHubIconsDirectoryName = "icons";
/// The icon applications menus and file managers show for the Hub and its project files.
inline constexpr std::string_view kHubIconFileName = "univex-logo-256.png";
inline constexpr std::string_view kHubWindowsIconFileName = "univex-logo.ico";
/// Project files (.uvproject) open with the Hub under this media type.
inline constexpr std::string_view kProjectMediaType = "application/x-univex-project";
inline constexpr std::string_view kHubDisplayName = "UniVex Hub";
/// What the Hub is, as applications menus describe it.
inline constexpr std::string_view kHubDescription = "Create, open and manage UniVex projects and engine versions";
/// How file managers name a project file's type.
inline constexpr std::string_view kProjectTypeDisplayName = "UniVex Project";
/// Who installed programs and the Windows Apps & features list name as the Hub's publisher.
inline constexpr std::string_view kPublisher = "UniVex Studios";
/// Windows file association: the program identifier .uvproject files point at.
inline constexpr std::string_view kProjectProgramId = "UniVex.Project";

struct SetupPayloadLocation final {
    std::uint64_t offset = 0U;
    std::uint64_t size = 0U;
    Sha256Digest digest{};
};

/// Finds the payload appended to `setupExecutable`; nothing when it carries none.
[[nodiscard]] std::optional<SetupPayloadLocation> LocateSetupPayload(const std::filesystem::path& setupExecutable);

/// Copies the payload out to `archive` and checks it against the footer's digest.
[[nodiscard]] bool ExtractSetupPayload(const std::filesystem::path& setupExecutable,
                                          const SetupPayloadLocation& location,
                                          const std::filesystem::path& archive);

/// Where the Hub installs by default: the per-user programs folder (no administrator rights needed).
[[nodiscard]] std::optional<std::filesystem::path> DefaultHubInstallDirectory();

struct HubSetupResult final {
    bool succeeded = false;
    std::string message;
};

/// Installs or upgrades the Hub in `installDirectory` from `payloadArchive`: the files are unpacked
/// beside it first and only replace the previous installation once complete, so a failed upgrade
/// leaves the old Hub working. `setupExecutable` is copied in as the uninstaller.
[[nodiscard]] HubSetupResult InstallHub(const std::filesystem::path& payloadArchive,
                                              const std::filesystem::path& installDirectory,
                                              const std::filesystem::path& setupExecutable);

/// Makes the installed Hub appear among the user's applications and opens project files with it: on
/// Linux a desktop entry, a media type for .uvproject and their icons; on Windows a Start menu
/// shortcut, an Apps & features entry and the .uvproject file association. `entriesRoot` stands in
/// for the per-user data folder (Linux) or Start menu folder (Windows) in isolated runs, where
/// nothing system-wide is touched; empty uses the platform's own locations.
[[nodiscard]] HubSetupResult RegisterHubWithDesktop(const std::filesystem::path& installDirectory,
                                                          const std::string& version,
                                                          const std::filesystem::path& entriesRoot = {});

/// Removes what RegisterHubWithDesktop added and the installation itself. Projects, the project
/// list, settings and downloaded engines are the user's and stay.
[[nodiscard]] HubSetupResult UninstallHub(const std::filesystem::path& installDirectory,
                                                const std::filesystem::path& entriesRoot = {});

/// The Hub executable inside an installation.
[[nodiscard]] std::filesystem::path HubExecutableIn(const std::filesystem::path& installDirectory);

} // namespace UVE::Online
