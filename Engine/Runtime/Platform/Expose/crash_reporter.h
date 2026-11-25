// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <filesystem>
#include <memory>
#include <string>

namespace UVE::Platform {

enum class CrashReporterCode {
    Installed,
    AlreadyInstalled,
    Unsupported,
    Failed,
};

struct CrashReporterResult final {
    CrashReporterCode code = CrashReporterCode::Failed;
    std::filesystem::path reportPath;
    std::string message;

    [[nodiscard]] bool IsInstalled() const noexcept { return code == CrashReporterCode::Installed; }
};

/// Installs process-level unhandled-exception and fatal-signal reporting for a standalone runtime.
/// Windows writes a native minidump; POSIX writes an async-signal-safe report with signal/address
/// metadata. The report file is reserved before the runtime starts and is removed on normal
/// shutdown, so a remaining file is evidence of an abnormal exit. The optional symbol endpoint is
/// intentionally not contacted by this low-level handler; uploading requires a host-provided,
/// consent-aware integration.
class CrashReporter final {
public:
    CrashReporter();
    ~CrashReporter();

    CrashReporter(const CrashReporter&) = delete;
    CrashReporter& operator=(const CrashReporter&) = delete;
    CrashReporter(CrashReporter&&) = delete;
    CrashReporter& operator=(CrashReporter&&) = delete;

    [[nodiscard]] CrashReporterResult Install(const std::filesystem::path& dumpDirectory,
                                                     const std::string& applicationIdentifier);
    [[nodiscard]] bool IsInstalled() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace UVE::Platform
