// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace UVE::LauncherUI {

/// The folders directly inside `directory` that a person would pick from: sorted by name, without
/// hidden ones. Unreadable directories list as empty.
[[nodiscard]] std::vector<std::filesystem::path> ListPickableFoldersUVE(const std::filesystem::path& directory);

/// Opens a modal window for choosing a folder, starting in `start` (or the home folder when it is
/// not a directory). `onPicked` receives the chosen folder; cancelling calls nothing.
void ShowFolderPickerUVE(const std::string& title, const std::filesystem::path& start,
                         std::function<void(const std::filesystem::path&)> onPicked);

} // namespace UVE::LauncherUI
