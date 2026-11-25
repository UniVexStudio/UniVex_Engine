// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
#include "hot_reload.h"
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <limits>
#include <mutex>
#include <set>
#include <stdexcept>
#include <system_error>
#include <unordered_map>
#include <utility>
#include <vector>
#include "native_file_watcher.h"
#include "asset_reloaded_event.h"
#include "logging_macros.h"
namespace UVE::Asset {
namespace {
bool WantsNative(HotReloadPreference preference) {
    if (preference != HotReloadPreference::Automatic && preference != HotReloadPreference::Polling) {
        throw std::invalid_argument("HotReload: invalid watcher preference");
    }
    return preference == HotReloadPreference::Automatic;
}
} // namespace
struct HotReload::Impl {
    struct Snapshot {
        std::filesystem::path path, target;
        std::filesystem::file_time_type writeTime{};
        std::uintmax_t size = 0U;
        bool observed = false, exists = false, pending = false;
    };
    Impl(Events::IEventSystem &eventBus, double interval, HotReloadPreference preference)
        : events(eventBus), pollInterval(interval), watcher(WantsNative(preference)) {}
    Events::IEventSystem &events;
    double pollInterval, accumulated = 0.0;
    mutable std::mutex mutex;
    std::unordered_map<AssetGuid, Snapshot> snapshots;
    Detail::NativeFileWatcher watcher;
    std::uint64_t overflowCount = 0U;
};
HotReload::HotReload(Events::IEventSystem &events, double interval)
    : HotReload(events, interval, HotReloadPreference::Automatic) {}
HotReload::HotReload(Events::IEventSystem &events, double interval,
                           HotReloadPreference preference)
    : m_impl(std::make_unique<Impl>(events, interval, preference)) {}
HotReload::~HotReload() = default;
HotReloadBackend HotReload::GetBackend() const {
    const std::lock_guard lock(m_impl->mutex);
    return m_impl->watcher.IsAvailable() ? HotReloadBackend::Inotify : HotReloadBackend::Polling;
}
std::uint64_t HotReload::GetNativeOverflowCount() const {
    const std::lock_guard lock(m_impl->mutex);
    return m_impl->overflowCount;
}
void HotReload::Track(AssetGuid guid) {
    const std::lock_guard lock(m_impl->mutex);
    m_impl->snapshots.try_emplace(guid); // completion must not erase a pending change or real epoch timestamp
}
void HotReload::Untrack(AssetGuid guid) {
    const std::lock_guard lock(m_impl->mutex);
    m_impl->snapshots.erase(guid);
}
void HotReload::Poll(IAssetManager &manager, IAssetDatabase &database, double delta) {
    if (!std::isfinite(delta) || delta < 0.0)
        return;
    m_impl->accumulated += delta;
    if (m_impl->accumulated < m_impl->pollInterval)
        return;
    m_impl->accumulated = 0.0;
    std::vector<AssetGuid> reloads;
    {
        const std::lock_guard lock(m_impl->mutex);
        struct Resolved {
            AssetGuid guid;
            std::filesystem::path path, target;
        };
        std::vector<Resolved> resolved;
        std::set<std::filesystem::path> directories;
        for (const auto &[guid, snapshot] : m_impl->snapshots) {
            static_cast<void>(snapshot);
            auto path = database.Resolve(guid);
            std::filesystem::path target;
            if (!path.empty()) {
                std::error_code error;
                auto absolute = std::filesystem::absolute(path, error);
                if (!error)
                    path = absolute.lexically_normal();
                target = std::filesystem::weakly_canonical(path, error);
                if (error)
                    target = path;
                if (!path.parent_path().empty())
                    directories.insert(path.parent_path());
                if (!target.parent_path().empty())
                    directories.insert(target.parent_path());
            }
            resolved.push_back({guid, std::move(path), std::move(target)});
        }
        m_impl->watcher.SetDirectories(directories);
        const auto changes = m_impl->watcher.Poll();
        if (changes.overflowed && m_impl->overflowCount != std::numeric_limits<std::uint64_t>::max())
            ++m_impl->overflowCount;
        for (const auto &entry : resolved) {
            auto &snapshot = m_impl->snapshots.at(entry.guid);
            std::error_code error;
            auto timestamp = std::filesystem::file_time_type{};
            std::uintmax_t size = 0U;
            bool exists = false;
            if (entry.path.empty()) {
                UVE_WARNING("HotReload: tracked asset GUID {} no longer resolves to a path - skipping",
                            entry.guid.value);
            } else {
                timestamp = std::filesystem::last_write_time(entry.path, error);
                if (!error)
                    size = std::filesystem::file_size(entry.path, error);
                exists = !error;
                if (!exists)
                    UVE_WARNING("HotReload: tracked asset GUID {} at {} is missing/unreadable - skipping",
                                entry.guid.value, entry.path.string());
            }
            if (snapshot.observed && exists) {
                snapshot.pending = snapshot.pending || !snapshot.exists || entry.path != snapshot.path ||
                                   entry.target != snapshot.target || timestamp != snapshot.writeTime ||
                                   size != snapshot.size || changes.rescanRequired ||
                                   changes.paths.contains(entry.path) || changes.paths.contains(entry.target);
                if (snapshot.pending)
                    reloads.push_back(entry.guid);
            }
            snapshot.observed = true;
            snapshot.exists = exists;
            snapshot.path = entry.path;
            snapshot.target = entry.target;
            snapshot.writeTime = timestamp;
            snapshot.size = size;
        }
    }
    std::sort(reloads.begin(), reloads.end(), [](auto a, auto b) { return a.value < b.value; });
    for (const auto guid : reloads) {
        // No manager lock is acquired while holding ours; completion/GC can Track/Untrack.
        if (manager.GetLoadState(guid) == AssetLoadState::Loading)
            continue;
        {
            const std::lock_guard lock(m_impl->mutex);
            const auto entry = m_impl->snapshots.find(guid);
            if (entry == m_impl->snapshots.end() || !entry->second.pending || !entry->second.exists)
                continue;
            entry->second.pending = false;
        }
        manager.Reload(guid, database);
        m_impl->events.QueueEvent(AssetReloadedEvent{guid});
    }
}
} // namespace UVE::Asset
