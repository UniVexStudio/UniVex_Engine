// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace prism {
class Splitter;
}

namespace UVE::Editor {

struct SavedWindowBounds final {
    int width = 0;
    int height = 0;
    int x = 0;
    int y = 0;
    bool hasPosition = false;
    bool maximized = false;
    bool pageFloating = false;
    float pageX = 0.0F;
    float pageY = 0.0F;
    float pageW = 0.0F;
    float pageH = 0.0F;
    bool hasPageBounds = false;
};

/// Atomic, immediately-flushed persistent layout store for all editor windows, splitters, and panels.
/// Defaults apply only on first-ever open or after ResetAll(); survives sudden process termination on Android.
class EditorLayoutStore final {
public:
    [[nodiscard]] static EditorLayoutStore& Instance();

    /// Override storage file path (for unit tests) and load from it.
    void SetStoragePathForTest(std::filesystem::path path);

    [[nodiscard]] bool HasSplitter(std::string_view key) const;
    [[nodiscard]] std::vector<float> GetSplitterSizes(std::string_view key) const;
    void SetSplitterSizes(std::string_view key, const std::vector<float>& sizes);

    /// Restores saved pane sizes if present and saves immediately whenever the user drags a handle.
    void BindSplitter(std::string key, prism::Splitter& splitter);

    [[nodiscard]] bool GetBool(std::string_view key, bool fallback) const;
    void SetBool(std::string_view key, bool value);

    [[nodiscard]] int GetInt(std::string_view key, int fallback) const;
    void SetInt(std::string_view key, int value);

    [[nodiscard]] std::optional<SavedWindowBounds> GetWindowBounds(std::string_view key) const;
    void SetWindowBounds(std::string_view key, const SavedWindowBounds& bounds);

    /// Clears all remembered panel/window layouts so defaults apply again.
    void ResetAll();

private:
    EditorLayoutStore() = default;
    void EnsureLoaded() const;
    void LoadFromDisk() const;
    void SaveToDisk() const;
    [[nodiscard]] std::filesystem::path ResolvePath() const;

    mutable bool m_loaded = false;
    mutable std::filesystem::path m_overridePath;
    mutable std::unordered_map<std::string, std::vector<float>> m_splitters;
    mutable std::unordered_map<std::string, bool> m_bools;
    mutable std::unordered_map<std::string, int> m_ints;
    mutable std::unordered_map<std::string, SavedWindowBounds> m_windows;
};

} // namespace UVE::Editor
