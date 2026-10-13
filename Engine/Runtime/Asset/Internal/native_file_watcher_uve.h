// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
#pragma once
#include <filesystem>
#include <map>
#include <set>
#include <vector>
namespace UVE::Asset::Detail {
struct NativeFileChangesUVE final {
    std::set<std::filesystem::path> paths;
    bool rescanRequired = false;
    bool overflowed = false;
};
/// Non-recursive, owner-thread notification supplement. Linux inotify, unavailable elsewhere.
/// No callbacks/worker; failed directories remain covered by the caller's polling snapshots.
class NativeFileWatcherUVE final {
  public:
    explicit NativeFileWatcherUVE(bool enabled);
    ~NativeFileWatcherUVE();
    NativeFileWatcherUVE(const NativeFileWatcherUVE &) = delete;
    NativeFileWatcherUVE &operator=(const NativeFileWatcherUVE &) = delete;
    [[nodiscard]] bool IsAvailableUVE() const noexcept { return m_descriptor >= 0; }
    void SetDirectoriesUVE(const std::set<std::filesystem::path> &directories);
    [[nodiscard]] NativeFileChangesUVE PollUVE();

  private:
    int m_descriptor = -1;
    std::map<std::filesystem::path, int> m_watches;
    std::map<int, std::vector<std::filesystem::path>> m_directoriesByWatch;
    bool m_rescanRequired = false;
};
} // namespace UVE::Asset::Detail
