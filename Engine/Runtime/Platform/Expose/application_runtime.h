// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <filesystem>
#include <string>
#include <string_view>

#include "editor_project_package.h"

namespace UVE::Platform {

/// Stable target key used by project application identifiers and icon sets.
[[nodiscard]] std::string_view GetApplicationTargetUVE() noexcept;

/// Target key identifying engine packages (.uvpackage) runnable on this device.
/// Desktop platforms match GetApplicationTargetUVE() ("linux", "windows", "macos");
/// Android includes the native ABI ("android-arm64-v8a", "android-x86_64").
[[nodiscard]] std::string_view GetEnginePackageTargetUVE() noexcept;

/// True when an engine package manifest targeting `manifestTarget` can run on this device.
/// Accepts exact GetEnginePackageTargetUVE() matches, plus legacy "android" manifests on arm64-v8a.
[[nodiscard]] bool IsCompatibleEnginePackageTargetUVE(std::string_view manifestTarget) noexcept;

/// On Windows GUI-subsystem executables (-mwindows), attaches standard streams to an existing
/// parent console when stdout/stderr are not already redirected to a pipe or file. Never allocates
/// a new console window (no-op on double-click and on non-Windows platforms).
void AttachParentConsoleForCliUVE() noexcept;

struct ApplicationUserDataDirectoryResultUVE final {
    bool success = false;
    std::filesystem::path directory;
    std::string message;
};

/// The platform's per-user data root (for example XDG_DATA_HOME on Linux, LOCALAPPDATA on Windows,
/// Application Support on macOS). Not created; callers create their own directory beneath it.
[[nodiscard]] ApplicationUserDataDirectoryResultUVE ResolvePerUserDataRootUVE();

/// The current user's home directory (USERPROFILE on Windows, HOME elsewhere). Not created.
[[nodiscard]] ApplicationUserDataDirectoryResultUVE ResolveUserHomeDirectoryUVE();

/// The user's visible Documents directory for default project storage:
/// - Windows: FOLDERID_Documents (OneDrive/localized aware) or USERPROFILE/Documents
/// - Linux/macOS: XDG_DOCUMENTS_DIR or HOME/Documents
/// - Android: /storage/emulated/0/Documents when writable (All Files Access), else app-visible
///   external Documents (Android/data/<pkg>/files/Documents) instead of private /data/user/0.
[[nodiscard]] ApplicationUserDataDirectoryResultUVE ResolveUserDocumentsDirectoryUVE();

/// The user's Downloads directory (for folder picker quick-jump shortcuts). Not created.
[[nodiscard]] ApplicationUserDataDirectoryResultUVE ResolveUserDownloadsDirectoryUVE();

/// Public shared storage root on Android (/storage/emulated/0) or user home on Desktop.
[[nodiscard]] ApplicationUserDataDirectoryResultUVE ResolvePublicStorageRootUVE();

/// Resolves and creates `directoryName` under the per-user data root, for data a tool keeps on
/// behalf of the user rather than of one project.
[[nodiscard]] ApplicationUserDataDirectoryResultUVE ResolveToolUserDataDirectoryUVE(std::string_view directoryName);

/// Resolves and creates the application user-data directory. Portable projects use a sibling
/// directory under `projectRoot`; regular projects use the platform's per-user data directory.
/// An empty manifest directory name falls back to product short name, then a sanitized project ID.
[[nodiscard]] ApplicationUserDataDirectoryResultUVE ResolveApplicationUserDataDirectoryUVE(
    const EditorProjectPackageUVE& package, const std::filesystem::path& projectRoot);

enum class SingleInstanceLockCodeUVE {
    Acquired,
    AlreadyRunning,
    Unsupported,
    Failed,
};

struct SingleInstanceLockResultUVE final {
    SingleInstanceLockCodeUVE code = SingleInstanceLockCodeUVE::Failed;
    std::string message;

    [[nodiscard]] bool IsAcquiredUVE() const noexcept { return code == SingleInstanceLockCodeUVE::Acquired; }
};

/// Process-lifetime, non-blocking single-instance lock keyed by the selected target application ID.
/// POSIX uses an advisory flock in the user-data directory; Windows uses a per-session named mutex.
/// The lock is released automatically when the owning process or this object exits.
class SingleInstanceLockUVE final {
public:
    SingleInstanceLockUVE() = default;
    ~SingleInstanceLockUVE();

    SingleInstanceLockUVE(const SingleInstanceLockUVE&) = delete;
    SingleInstanceLockUVE& operator=(const SingleInstanceLockUVE&) = delete;

    [[nodiscard]] SingleInstanceLockResultUVE AcquireUVE(std::string_view applicationIdentifier,
                                                         const std::filesystem::path& userDataDirectory);
    [[nodiscard]] bool IsAcquiredUVE() const noexcept;

private:
#if defined(_WIN32)
    void* m_nativeHandle = nullptr;
#elif !defined(__EMSCRIPTEN__)
    int m_fileDescriptor = -1;
#endif
};

} // namespace UVE::Platform
