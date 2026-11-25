// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include <iostream>
#include <string_view>

#include "project_packager.h"

namespace {

constexpr int kSuccessExitCode = 0;
constexpr int kUsageExitCode = 2;
constexpr int kOperationalFailureExitCode = 3;

void PrintUsage() {
    std::cerr << "Usage: uve_pack --project <path-to.uvproject> --runtime <path-to-uve_runtime> "
                 "--output <distributable-folder> [--cook windows|android]\n";
}

} // namespace

int main(int argc, char** argv) {
    UVE::Pack::ProjectPackOptions options{};
    for (int index = 1; index < argc; ++index) {
        const std::string_view argument = argv[index];
        if ((argument == "--project" || argument == "--runtime" || argument == "--output" || argument == "--cook") && index + 1 >= argc) {
            PrintUsage();
            return kUsageExitCode;
        }
        if (argument == "--project") {
            options.projectFile = argv[++index];
        } else if (argument == "--runtime") {
            options.runtimeExecutablePath = argv[++index];
        } else if (argument == "--output") {
            options.outputDirectory = argv[++index];
        } else if (argument == "--cook") {
            const std::string_view target = argv[++index];
            if (target == "windows") options.cookTarget = UVE::Pack::CookTarget::Windows;
            else if (target == "android") options.cookTarget = UVE::Pack::CookTarget::Android;
            else { PrintUsage(); return kUsageExitCode; }
        } else {
            PrintUsage();
            return kUsageExitCode;
        }
    }
    if (options.projectFile.empty() || options.runtimeExecutablePath.empty() || options.outputDirectory.empty()) {
        PrintUsage();
        return kUsageExitCode;
    }

    const UVE::Pack::ProjectPackResult result = UVE::Pack::ProjectPackager::Pack(options);
    if (result.IsSuccess()) {
        std::cout << result.message << '\n';
        return kSuccessExitCode;
    }
    std::cerr << "uve_pack failed: " << result.message << '\n';
    return kOperationalFailureExitCode;
}
