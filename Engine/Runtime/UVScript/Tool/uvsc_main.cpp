// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

// uvsc: compiles a .uvs script to C++23 for a release build.
//
//     uvsc --host <node.uvhost> --out <script.uvs.cpp> <script.uvs>
//
// The host file describes what the node offers (see DescribedHostUVE). Problems are printed as
// `file:line:column: message` and the exit code is non-zero; nothing is written then.

#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>

#include "uve/uvscript/uvscript_codegen_uve.h"
#include "uve/uvscript/uvscript_host_description_uve.h"

namespace {

[[nodiscard]] std::optional<std::string> ReadFileUVE(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return std::nullopt;
    }
    std::ostringstream text;
    text << file.rdbuf();
    return text.str();
}

int UsageUVE() {
    std::cerr << "usage: uvsc --host <node.uvhost> --out <script.uvs.cpp> <script.uvs>\n";
    return 2;
}

} // namespace

int main(const int argc, char** const argv) {
    std::filesystem::path hostPath;
    std::filesystem::path outPath;
    std::filesystem::path scriptPath;
    for (int i = 1; i < argc; ++i) {
        const std::string_view argument = argv[i];
        if (argument == "--host" && i + 1 < argc) {
            hostPath = argv[++i];
        } else if (argument == "--out" && i + 1 < argc) {
            outPath = argv[++i];
        } else if (!argument.starts_with("--") && scriptPath.empty()) {
            scriptPath = argument;
        } else {
            return UsageUVE();
        }
    }
    if (hostPath.empty() || outPath.empty() || scriptPath.empty()) {
        return UsageUVE();
    }

    const std::optional<std::string> hostText = ReadFileUVE(hostPath);
    const std::optional<std::string> source = ReadFileUVE(scriptPath);
    if (!hostText.has_value() || !source.has_value()) {
        std::cerr << (hostText.has_value() ? scriptPath : hostPath).string() << ": cannot be read\n";
        return 1;
    }
    std::string hostError;
    const std::optional<UVE::UVScript::DescribedHostUVE> host = UVE::UVScript::DescribedHostUVE::ParseUVE(*hostText, hostError);
    if (!host.has_value()) {
        std::cerr << hostPath.string() << ": " << hostError << "\n";
        return 1;
    }
    const UVE::UVScript::CompileResultUVE compiled = UVE::UVScript::CompileUVScriptSourceUVE(*source, *host);
    if (!compiled.IsSuccessUVE()) {
        for (const UVE::UVScript::DiagnosticUVE& diagnostic : compiled.diagnostics) {
            std::cerr << scriptPath.string() << ":" << diagnostic.at.line << ":" << diagnostic.at.column << ": "
                      << diagnostic.message << "\n";
        }
        return 1;
    }
    const std::string cpp = UVE::UVScript::GenerateUVScriptNativeCppUVE(*compiled.program, scriptPath.filename().string());
    // Written only when it changed, so an unchanged script does not trigger a recompile.
    if (ReadFileUVE(outPath) == cpp) {
        return 0;
    }
    std::filesystem::create_directories(outPath.parent_path().empty() ? std::filesystem::path{"."} : outPath.parent_path());
    std::ofstream out(outPath, std::ios::binary | std::ios::trunc);
    out << cpp;
    if (!out) {
        std::cerr << outPath.string() << ": cannot be written\n";
        return 1;
    }
    return 0;
}
