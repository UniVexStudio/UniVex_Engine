// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "engine_library.h"
#include "process_launcher.h"
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

/// Android runs no second program: opening a project there closes the Hub's window and leaves the
/// editor to start here, which the app (hub_main.cpp) takes once the Hub's loop has ended and runs
/// in-process from the engine's editor library. Empty on other platforms, and once taken.
[[nodiscard]] std::optional<Platform::ProcessLaunchDescUVE> TakePendingEditorLaunchUVE();

} // namespace UVE::Hub
