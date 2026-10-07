// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <filesystem>
#include <string>
#include <string_view>

#include "uve/platform/editor_project_package_uve.h"

namespace UVE::Platform {

/// Stable target key used by project application identifiers and icon sets.
[[nodiscard]] std::string_view GetApplicationTargetUVE() noexcept;

struct ApplicationUserDataDirectoryResultUVE final {
    bool success = false;
    std::filesystem::path directory;
    std::string message;
};

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
