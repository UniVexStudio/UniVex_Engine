// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <filesystem>
#include <memory>
#include <string>

namespace UVE::Platform {

enum class CrashReporterCodeUVE {
    Installed,
    AlreadyInstalled,
    Unsupported,
    Failed,
};

struct CrashReporterResultUVE final {
    CrashReporterCodeUVE code = CrashReporterCodeUVE::Failed;
    std::filesystem::path reportPath;
    std::string message;

    [[nodiscard]] bool IsInstalledUVE() const noexcept { return code == CrashReporterCodeUVE::Installed; }
};

/// Installs process-level unhandled-exception and fatal-signal reporting for a standalone runtime.
/// Windows writes a native minidump; POSIX writes an async-signal-safe report with signal/address
/// metadata. The report file is reserved before the runtime starts and is removed on normal
/// shutdown, so a remaining file is evidence of an abnormal exit. The optional symbol endpoint is
/// intentionally not contacted by this low-level handler; uploading requires a host-provided,
/// consent-aware integration.
class CrashReporterUVE final {
public:
    CrashReporterUVE();
    ~CrashReporterUVE();

    CrashReporterUVE(const CrashReporterUVE&) = delete;
    CrashReporterUVE& operator=(const CrashReporterUVE&) = delete;
    CrashReporterUVE(CrashReporterUVE&&) = delete;
    CrashReporterUVE& operator=(CrashReporterUVE&&) = delete;

    [[nodiscard]] CrashReporterResultUVE InstallUVE(const std::filesystem::path& dumpDirectory,
                                                     const std::string& applicationIdentifier);
    [[nodiscard]] bool IsInstalledUVE() const noexcept;

private:
    struct ImplUVE;
    std::unique_ptr<ImplUVE> m_impl;
};

} // namespace UVE::Platform
