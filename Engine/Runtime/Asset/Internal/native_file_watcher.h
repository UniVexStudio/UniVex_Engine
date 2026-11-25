// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
#pragma once
#include <filesystem>
#include <map>
#include <set>
#include <vector>
namespace UVE::Asset::Detail {
struct NativeFileChanges final {
    std::set<std::filesystem::path> paths;
    bool rescanRequired = false;
    bool overflowed = false;
};
/// Non-recursive, owner-thread notification supplement. Linux inotify, unavailable elsewhere.
/// No callbacks/worker; failed directories remain covered by the caller's polling snapshots.
class NativeFileWatcher final {
  public:
    explicit NativeFileWatcher(bool enabled);
    ~NativeFileWatcher();
    NativeFileWatcher(const NativeFileWatcher &) = delete;
    NativeFileWatcher &operator=(const NativeFileWatcher &) = delete;
    [[nodiscard]] bool IsAvailable() const noexcept { return m_descriptor >= 0; }
    void SetDirectories(const std::set<std::filesystem::path> &directories);
    [[nodiscard]] NativeFileChanges Poll();

  private:
    int m_descriptor = -1;
    std::map<std::filesystem::path, int> m_watches;
    std::map<int, std::vector<std::filesystem::path>> m_directoriesByWatch;
    bool m_rescanRequired = false;
};
} // namespace UVE::Asset::Detail
