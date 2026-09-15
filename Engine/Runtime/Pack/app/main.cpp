// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include <iostream>
#include <string_view>

#include "uve/pack/project_packager_uve.h"

namespace {

constexpr int kSuccessExitCodeUVE = 0;
constexpr int kUsageExitCodeUVE = 2;
constexpr int kOperationalFailureExitCodeUVE = 3;

void PrintUsageUVE() {
    std::cerr << "Usage: uve_pack --project <path-to.uveditor> --runtime <path-to-uve_runtime> "
                 "--output <distributable-folder>\n";
}

} // namespace

int main(int argc, char** argv) {
    UVE::Pack::ProjectPackOptionsUVE options{};
    for (int index = 1; index < argc; ++index) {
        const std::string_view argument = argv[index];
        if ((argument == "--project" || argument == "--runtime" || argument == "--output") && index + 1 >= argc) {
            PrintUsageUVE();
            return kUsageExitCodeUVE;
        }
        if (argument == "--project") {
            options.projectFile = argv[++index];
        } else if (argument == "--runtime") {
            options.runtimeExecutablePath = argv[++index];
        } else if (argument == "--output") {
            options.outputDirectory = argv[++index];
        } else {
            PrintUsageUVE();
            return kUsageExitCodeUVE;
        }
    }
    if (options.projectFile.empty() || options.runtimeExecutablePath.empty() || options.outputDirectory.empty()) {
        PrintUsageUVE();
        return kUsageExitCodeUVE;
    }

    const UVE::Pack::ProjectPackResultUVE result = UVE::Pack::ProjectPackagerUVE::PackUVE(options);
    if (result.IsSuccessUVE()) {
        std::cout << result.message << '\n';
        return kSuccessExitCodeUVE;
    }
    std::cerr << "uve_pack failed: " << result.message << '\n';
    return kOperationalFailureExitCodeUVE;
}
