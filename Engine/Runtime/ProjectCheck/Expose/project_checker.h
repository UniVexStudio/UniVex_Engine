// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace UVE::ProjectCheck {

enum class ProjectCheckSeverity {
    Warning,
    Error,
};

struct ProjectCheckDiagnostic final {
    ProjectCheckSeverity severity = ProjectCheckSeverity::Error;
    std::string code;
    std::filesystem::path path;
    std::string message;
    std::string recovery;
};

struct ProjectCheckReport final {
    std::filesystem::path projectRoot;
    std::vector<ProjectCheckDiagnostic> diagnostics;

    [[nodiscard]] bool HasErrors() const noexcept;
};

struct ProjectCheckOptions final {
    std::filesystem::path projectRoot;
    std::filesystem::path assetDatabasePath;
};

/// Read-only, deterministic project health validation. The checker never writes,
/// imports, registers assets, loads an editor scene, follows symlinks, or starts a window.
class ProjectChecker final {
public:
    [[nodiscard]] ProjectCheckReport Run(const ProjectCheckOptions& options) const;
};

[[nodiscard]] std::string RenderProjectCheckText(const ProjectCheckReport& report);
[[nodiscard]] std::string RenderProjectCheckJson(const ProjectCheckReport& report);

} // namespace UVE::ProjectCheck
