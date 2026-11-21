// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "engine_install.h"

#include <system_error>

#include "process_launcher.h"
#include "project_template.h"

namespace UVE::Pack {

EngineInstallResultUVE ResolveEngineInstallUVE(const std::filesystem::path& installRoot,
                                               const Core::VersionUVE& version) {
    std::error_code error;
    const std::filesystem::path root = std::filesystem::absolute(installRoot, error).lexically_normal();
    if (error) {
        return {"Unable to resolve the engine directory: " + error.message(), std::nullopt};
    }
    const std::filesystem::path editor =
        root / (std::string(kEditorExecutableStemUVE) + std::string(Platform::kExecutableSuffixUVE));
    if (!std::filesystem::is_regular_file(editor, error)) {
        return {"No editor was found in the engine directory " + root.string() + ".", std::nullopt};
    }
    EngineInstallUVE install;
    install.version = version;
    install.root = root;
    install.editorExecutable = editor;
    install.templatesDirectory = root / kProjectTemplatesDirectoryNameUVE;
    return {"Found engine " + version.ToStringUVE() + ".", std::move(install)};
}

std::vector<std::string> MakeEditorLaunchArgumentsUVE(const std::filesystem::path& projectFile,
                                                      const std::filesystem::path& registryFile) {
    std::vector<std::string> arguments{std::string(kEditorProjectOptionUVE), projectFile.string()};
    if (!registryFile.empty()) {
        arguments.emplace_back(kEditorProjectRegistryOptionUVE);
        arguments.push_back(registryFile.string());
    }
    return arguments;
}

} // namespace UVE::Pack
