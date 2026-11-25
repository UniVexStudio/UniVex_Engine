// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "project_checker.h"

#include <algorithm>
#include <exception>
#include <optional>
#include <string_view>
#include <system_error>

#include "asset_database.h"
#include "asset_type_registry.h"
#include "uve_file_envelope.h"

namespace UVE::ProjectCheck {
namespace {

[[nodiscard]] const char* SeverityLabel(const ProjectCheckSeverity severity) noexcept {
    return severity == ProjectCheckSeverity::Error ? "error" : "warning";
}

[[nodiscard]] std::string EscapeJson(const std::string_view value) {
    std::string output;
    output.reserve(value.size() + 8U);
    for (const char character : value) {
        switch (character) {
            case '\\': output += "\\\\"; break;
            case '"': output += "\\\""; break;
            case '\n': output += "\\n"; break;
            case '\r': output += "\\r"; break;
            case '\t': output += "\\t"; break;
            default: output += character; break;
        }
    }
    return output;
}

[[nodiscard]] std::optional<Asset::AssetKind> ExpectedKind(const std::filesystem::path& path) {
    return Asset::ExpectedAssetKindForPath(path);
}

void Add(ProjectCheckReport& report, const ProjectCheckSeverity severity, std::string code,
            std::filesystem::path path, std::string message, std::string recovery) {
    report.diagnostics.push_back(ProjectCheckDiagnostic{
        severity, std::move(code), std::move(path), std::move(message), std::move(recovery)});
}

void Sort(ProjectCheckReport& report) {
    std::sort(report.diagnostics.begin(), report.diagnostics.end(), [](const ProjectCheckDiagnostic& lhs,
                                                                         const ProjectCheckDiagnostic& rhs) {
        if (lhs.path.generic_string() != rhs.path.generic_string()) return lhs.path.generic_string() < rhs.path.generic_string();
        if (lhs.code != rhs.code) return lhs.code < rhs.code;
        return lhs.message < rhs.message;
    });
}

} // namespace

bool ProjectCheckReport::HasErrors() const noexcept {
    return std::any_of(diagnostics.begin(), diagnostics.end(), [](const ProjectCheckDiagnostic& diagnostic) {
        return diagnostic.severity == ProjectCheckSeverity::Error;
    });
}

ProjectCheckReport ProjectChecker::Run(const ProjectCheckOptions& options) const {
    ProjectCheckReport report{};
    report.projectRoot = options.projectRoot.lexically_normal();
    std::error_code error;
    if (report.projectRoot.empty() || !std::filesystem::is_directory(report.projectRoot, error) || error) {
        Add(report, ProjectCheckSeverity::Error, "project.root.invalid", report.projectRoot,
               "Project root is missing, inaccessible, or not a directory.", "Provide an existing readable project root.");
        return report;
    }

    const std::filesystem::path databasePath = options.assetDatabasePath.empty()
        ? report.projectRoot / ".uvassetdb"
        : (options.assetDatabasePath.is_absolute() ? options.assetDatabasePath
                                                   : report.projectRoot / options.assetDatabasePath);
    const std::filesystem::path normalizedDatabasePath = databasePath.lexically_normal();
    Asset::AssetDatabase database;
    if (std::filesystem::exists(normalizedDatabasePath, error) && !database.Load(normalizedDatabasePath)) {
        Add(report, ProjectCheckSeverity::Error, "registry.load.failed", normalizedDatabasePath,
               "Asset registry could not be parsed.", "Restore valid .uvassetdb JSON from source control.");
    } else if (!std::filesystem::exists(normalizedDatabasePath, error)) {
        Add(report, ProjectCheckSeverity::Warning, "registry.missing", normalizedDatabasePath,
               "Asset registry is absent; project content remains inspectable.",
               "Create or restore the registry when registered asset identity is required.");
    }

    std::vector<std::filesystem::path> registeredPaths;
    for (const Asset::AssetRecord& record : database.GetRegisteredAssets()) {
        const std::filesystem::path path = record.path.is_absolute() ? record.path.lexically_normal()
                                                                       : (report.projectRoot / record.path).lexically_normal();
        registeredPaths.push_back(path);
        std::error_code statusError;
        const std::filesystem::file_status status = std::filesystem::symlink_status(path, statusError);
        if (statusError || !std::filesystem::is_regular_file(status)) {
            Add(report, ProjectCheckSeverity::Error, "registry.path.missing", record.path,
                   "Registered asset does not resolve to an ordinary readable file.",
                   "Restore the file or fix the registry record explicitly.");
        }
    }

    std::filesystem::recursive_directory_iterator iterator(
        report.projectRoot, std::filesystem::directory_options::skip_permission_denied, error);
    const std::filesystem::recursive_directory_iterator end;
    if (error) {
        Add(report, ProjectCheckSeverity::Error, "project.enumeration.failed", report.projectRoot,
               "Project content cannot be enumerated.", "Fix root permissions or path accessibility.");
        return report;
    }
    for (; iterator != end; iterator.increment(error)) {
        if (error) {
            Add(report, ProjectCheckSeverity::Error, "project.entry.unreadable", report.projectRoot,
                   "A project directory entry could not be enumerated.", "Fix permissions and rerun the checker.");
            error.clear();
            continue;
        }
        const std::filesystem::directory_entry entry = *iterator;
        std::error_code statusError;
        const std::filesystem::file_status status = entry.symlink_status(statusError);
        if (statusError || std::filesystem::is_symlink(status) || !std::filesystem::is_regular_file(status)) continue;
        const std::filesystem::path path = entry.path().lexically_normal();
        try {
            const std::optional<Asset::AssetKind> expected = ExpectedKind(path);
            if (!expected.has_value()) continue;
            const auto decoded = Asset::ReadUveFile(path);
            if (!decoded.has_value()) {
                Add(report, ProjectCheckSeverity::Error, "envelope.decode.failed", path,
                       "Universal asset envelope is malformed or unsupported.",
                       "Restore or re-export this asset from a trusted source.");
            } else if (decoded->first.assetType != *expected) {
                Add(report, ProjectCheckSeverity::Error, "envelope.kind.mismatch", path,
                       "Envelope asset kind does not match the file extension.",
                       "Use the correct extension or re-export with the correct asset kind.");
            }
            if (std::find(registeredPaths.begin(), registeredPaths.end(), path) == registeredPaths.end()) {
                Add(report, ProjectCheckSeverity::Warning, "registry.file.unregistered", path,
                       "Supported asset file is not represented in the current registry snapshot.",
                       "Register the asset through the project asset workflow if GUID identity is needed.");
            }
        } catch (const std::exception&) {
            Add(report, ProjectCheckSeverity::Error, "validation.exception", path,
                   "An unexpected validation exception was isolated to this file.",
                   "Inspect or restore this file; other project files were still checked.");
        } catch (...) {
            Add(report, ProjectCheckSeverity::Error, "validation.exception", path,
                   "An unknown validation failure was isolated to this file.",
                   "Inspect or restore this file; other project files were still checked.");
        }
    }
    Sort(report);
    return report;
}

std::string RenderProjectCheckText(const ProjectCheckReport& report) {
    std::string output = "Project health: " + report.projectRoot.generic_string() + "\n";
    if (report.diagnostics.empty()) return output + "OK: no diagnostics.\n";
    for (const ProjectCheckDiagnostic& diagnostic : report.diagnostics) {
        output += std::string{SeverityLabel(diagnostic.severity)} + " [" + diagnostic.code + "] " +
                  diagnostic.path.generic_string() + ": " + diagnostic.message + " Recovery: " + diagnostic.recovery + "\n";
    }
    return output;
}

std::string RenderProjectCheckJson(const ProjectCheckReport& report) {
    std::string output = "{\"projectRoot\":\"" + EscapeJson(report.projectRoot.generic_string()) +
                         "\",\"hasErrors\":" + (report.HasErrors() ? "true" : "false") + ",\"diagnostics\":[";
    for (std::size_t index = 0U; index < report.diagnostics.size(); ++index) {
        const ProjectCheckDiagnostic& diagnostic = report.diagnostics[index];
        if (index != 0U) output += ',';
        output += "{\"severity\":\"" + std::string{SeverityLabel(diagnostic.severity)} + "\",\"code\":\"" +
                  EscapeJson(diagnostic.code) + "\",\"path\":\"" + EscapeJson(diagnostic.path.generic_string()) +
                  "\",\"message\":\"" + EscapeJson(diagnostic.message) + "\",\"recovery\":\"" +
                  EscapeJson(diagnostic.recovery) + "\"}";
    }
    return output + "]}\n";
}

} // namespace UVE::ProjectCheck
