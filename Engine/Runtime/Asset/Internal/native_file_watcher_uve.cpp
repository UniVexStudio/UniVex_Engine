// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
#include "native_file_watcher_uve.h"
#include <array>
#include <cerrno>
#include <cstddef>
#include <cstring>
#include <string>
#include <utility>
#if defined(__linux__)
#include <sys/inotify.h>
#include <unistd.h>
#endif
namespace UVE::Asset::Detail {
NativeFileWatcherUVE::NativeFileWatcherUVE(bool enabled) {
#if defined(__linux__)
    if (enabled)
        m_descriptor = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
#else
    static_cast<void>(enabled);
#endif
}
NativeFileWatcherUVE::~NativeFileWatcherUVE() {
#if defined(__linux__)
    if (m_descriptor >= 0)
        static_cast<void>(close(m_descriptor));
#endif
}
void NativeFileWatcherUVE::SetDirectoriesUVE(const std::set<std::filesystem::path> &directories) {
#if defined(__linux__)
    if (!IsAvailableUVE())
        return;
    std::map<std::filesystem::path, int> next;
    for (const auto &directory : directories) {
        // Reassert: a directory may have been atomically replaced at the same lexical path.
        const int watch =
            inotify_add_watch(m_descriptor, directory.c_str(),
                              IN_CREATE | IN_MODIFY | IN_CLOSE_WRITE | IN_ATTRIB | IN_DELETE | IN_MOVED_FROM |
                                  IN_MOVED_TO | IN_DELETE_SELF | IN_MOVE_SELF | IN_ONLYDIR);
        if (watch >= 0)
            next.emplace(directory, watch);
    }
    std::map<int, std::vector<std::filesystem::path>> reverse;
    for (const auto &[directory, watch] : next)
        reverse[watch].push_back(directory);
    for (const auto &[directory, watch] : m_watches) {
        const auto replacement = next.find(directory);
        if (directories.contains(directory) && (replacement == next.end() || replacement->second != watch)) {
            m_rescanRequired = true; // old invalidation events may refer to an id no longer in next
        }
        // Multiple lexical aliases can share an inode/watch; do not remove a retained alias.
        if (!reverse.contains(watch))
            static_cast<void>(inotify_rm_watch(m_descriptor, watch));
    }
    m_watches = std::move(next);
    m_directoriesByWatch = std::move(reverse);
#else
    static_cast<void>(directories);
#endif
}
NativeFileChangesUVE NativeFileWatcherUVE::PollUVE() {
    NativeFileChangesUVE changes;
    changes.rescanRequired = std::exchange(m_rescanRequired, false);
#if defined(__linux__)
    if (!IsAvailableUVE())
        return changes;
    std::array<std::byte, 16U * 1024U> bytes{};
    for (std::size_t readIndex = 0; readIndex < 64U; ++readIndex) {
        const auto count = read(m_descriptor, bytes.data(), bytes.size());
        if (count < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK)
                return changes;
            if (errno == EINTR)
                continue;
        }
        if (count <= 0) {
            changes.rescanRequired = true;
            static_cast<void>(close(m_descriptor));
            m_descriptor = -1;
            return changes; // actual polling fallback after native failure
        }
        const auto size = static_cast<std::size_t>(count);
        std::size_t offset = 0U;
        while (offset < size) {
            inotify_event event{};
            if (size - offset < sizeof(event)) {
                changes.rescanRequired = true;
                return changes;
            }
            std::memcpy(&event, bytes.data() + offset, sizeof(event)); // byte storage has no event alignment
            offset += sizeof(event);
            if (event.len > size - offset) {
                changes.rescanRequired = true;
                return changes;
            }
            if ((event.mask & IN_Q_OVERFLOW) != 0U) {
                changes.overflowed = true;
                changes.rescanRequired = true;
            }
            const auto watched = m_directoriesByWatch.find(event.wd);
            if (watched != m_directoriesByWatch.end()) {
                if ((event.mask & (IN_DELETE_SELF | IN_MOVE_SELF | IN_IGNORED | IN_UNMOUNT)) != 0U)
                    changes.rescanRequired = true;
                if (event.len != 0U) {
                    const auto *name = reinterpret_cast<const char *>(bytes.data() + offset);
                    const auto *end = static_cast<const char *>(std::memchr(name, '\0', event.len));
                    if (end == nullptr) {
                        changes.rescanRequired = true;
                        return changes;
                    }
                    for (const auto &directory : watched->second)
                        changes.paths.insert((directory / std::string(name, end)).lexically_normal());
                }
            }
            offset += event.len;
        }
    }
    changes.rescanRequired = true; // sustained writers cannot monopolize the engine tick
#endif
    return changes;
}
} // namespace UVE::Asset::Detail
