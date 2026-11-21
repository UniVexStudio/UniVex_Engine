// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "engine_library.h"
#include "project_registry.h"

namespace UVE::Hub {

struct ProjectOpenResultUVE final {
    bool launched = false;
    std::string message;
};

/// Opens `projectFile` in the engine the user chose for it (from `registryFile`), else the engine
/// matching the project, and records it as opened. `registryOverride` is handed to the editor when
/// the Hub itself runs on an isolated registry. Used by the Hub window and by opening a project file
/// directly (double-clicking a .uvproject).
[[nodiscard]] ProjectOpenResultUVE OpenProjectInEngineUVE(const std::filesystem::path& projectFile,
                                                          const std::vector<Online::InstalledEngineUVE>& engines,
                                                          const std::filesystem::path& registryFile,
                                                          const std::filesystem::path& registryOverride);

} // namespace UVE::Hub
