// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include <algorithm>
#include <iostream>
#include <string>
#include <string_view>

#include "project_checker.h"

namespace {

constexpr int kSuccessExitCode = 0;
constexpr int kHealthErrorExitCode = 1;
constexpr int kUsageExitCode = 2;
constexpr int kOperationalFailureExitCode = 3;

void PrintUsage() {
    std::cerr << "Usage: uve_project_check --project-root <path> [--asset-db <path>] [--format text|json]\n";
}

} // namespace

int main(int argc, char** argv) {
    UVE::ProjectCheck::ProjectCheckOptions options{};
    std::string format = "text";
    for (int index = 1; index < argc; ++index) {
        const std::string_view argument = argv[index];
        if ((argument == "--project-root" || argument == "--asset-db" || argument == "--format") && index + 1 >= argc) {
            PrintUsage();
            return kUsageExitCode;
        }
        if (argument == "--project-root") {
            options.projectRoot = argv[++index];
        } else if (argument == "--asset-db") {
            options.assetDatabasePath = argv[++index];
        } else if (argument == "--format") {
            format = argv[++index];
        } else {
            PrintUsage();
            return kUsageExitCode;
        }
    }
    if (options.projectRoot.empty() || (format != "text" && format != "json")) {
        PrintUsage();
        return kUsageExitCode;
    }
    try {
        const UVE::ProjectCheck::ProjectCheckReport report = UVE::ProjectCheck::ProjectChecker{}.Run(options);
        if (format == "json") {
            std::cout << UVE::ProjectCheck::RenderProjectCheckJson(report);
        } else {
            std::cout << UVE::ProjectCheck::RenderProjectCheckText(report);
        }
        const bool invalidRoot = std::any_of(report.diagnostics.begin(), report.diagnostics.end(),
                                             [](const UVE::ProjectCheck::ProjectCheckDiagnostic& diagnostic) {
                                                 return diagnostic.code == "project.root.invalid";
                                             });
        if (invalidRoot) {
            return kOperationalFailureExitCode;
        }
        return report.HasErrors() ? kHealthErrorExitCode : kSuccessExitCode;
    } catch (const std::exception& exception) {
        std::cerr << "uve_project_check operational failure: " << exception.what() << '\n';
    } catch (...) {
        std::cerr << "uve_project_check operational failure: unknown exception\n";
    }
    return kOperationalFailureExitCode;
}
