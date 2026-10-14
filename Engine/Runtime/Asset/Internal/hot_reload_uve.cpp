// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
#include "uve/asset/hot_reload_uve.h"
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
#include "native_file_watcher_uve.h"
#include "uve/asset/asset_reloaded_event_uve.h"
#include "uve/logging/logging_macros_uve.h"
namespace UVE::Asset {
namespace {
bool WantsNativeUVE(HotReloadPreferenceUVE preference) {
    if (preference != HotReloadPreferenceUVE::Automatic && preference != HotReloadPreferenceUVE::Polling) {
        throw std::invalid_argument("HotReloadUVE: invalid watcher preference");
    }
    return preference == HotReloadPreferenceUVE::Automatic;
}
} // namespace
struct HotReloadUVE::ImplUVE {
    struct SnapshotUVE {
        std::filesystem::path path, target;
        std::filesystem::file_time_type writeTime{};
        std::uintmax_t size = 0U;
        bool observed = false, exists = false, pending = false;
    };
    ImplUVE(Events::IEventSystemUVE &eventBus, double interval, HotReloadPreferenceUVE preference)
        : events(eventBus), pollInterval(interval), watcher(WantsNativeUVE(preference)) {}
    Events::IEventSystemUVE &events;
    double pollInterval, accumulated = 0.0;
    mutable std::mutex mutex;
    std::unordered_map<AssetGuidUVE, SnapshotUVE> snapshots;
    Detail::NativeFileWatcherUVE watcher;
    std::uint64_t overflowCount = 0U;
};
HotReloadUVE::HotReloadUVE(Events::IEventSystemUVE &events, double interval)
    : HotReloadUVE(events, interval, HotReloadPreferenceUVE::Automatic) {}
HotReloadUVE::HotReloadUVE(Events::IEventSystemUVE &events, double interval,
                           HotReloadPreferenceUVE preference)
    : m_impl(std::make_unique<ImplUVE>(events, interval, preference)) {}
HotReloadUVE::~HotReloadUVE() = default;
HotReloadBackendUVE HotReloadUVE::GetBackendUVE() const {
    const std::lock_guard lock(m_impl->mutex);
    return m_impl->watcher.IsAvailableUVE() ? HotReloadBackendUVE::Inotify : HotReloadBackendUVE::Polling;
}
std::uint64_t HotReloadUVE::GetNativeOverflowCountUVE() const {
    const std::lock_guard lock(m_impl->mutex);
    return m_impl->overflowCount;
}
void HotReloadUVE::TrackUVE(AssetGuidUVE guid) {
    const std::lock_guard lock(m_impl->mutex);
    m_impl->snapshots.try_emplace(guid); // completion must not erase a pending change or real epoch timestamp
}
void HotReloadUVE::UntrackUVE(AssetGuidUVE guid) {
    const std::lock_guard lock(m_impl->mutex);
    m_impl->snapshots.erase(guid);
}
void HotReloadUVE::PollUVE(IAssetManagerUVE &manager, IAssetDatabaseUVE &database, double delta) {
    if (!std::isfinite(delta) || delta < 0.0)
        return;
    m_impl->accumulated += delta;
    if (m_impl->accumulated < m_impl->pollInterval)
        return;
    m_impl->accumulated = 0.0;
    std::vector<AssetGuidUVE> reloads;
    {
        const std::lock_guard lock(m_impl->mutex);
        struct ResolvedUVE {
            AssetGuidUVE guid;
            std::filesystem::path path, target;
        };
        std::vector<ResolvedUVE> resolved;
        std::set<std::filesystem::path> directories;
        for (const auto &[guid, snapshot] : m_impl->snapshots) {
            static_cast<void>(snapshot);
            auto path = database.ResolveUVE(guid);
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
        m_impl->watcher.SetDirectoriesUVE(directories);
        const auto changes = m_impl->watcher.PollUVE();
        if (changes.overflowed && m_impl->overflowCount != std::numeric_limits<std::uint64_t>::max())
            ++m_impl->overflowCount;
        for (const auto &entry : resolved) {
            auto &snapshot = m_impl->snapshots.at(entry.guid);
            std::error_code error;
            auto timestamp = std::filesystem::file_time_type{};
            std::uintmax_t size = 0U;
            bool exists = false;
            if (entry.path.empty()) {
                UVE_WARNING("HotReloadUVE: tracked asset GUID {} no longer resolves to a path - skipping",
                            entry.guid.value);
            } else {
                timestamp = std::filesystem::last_write_time(entry.path, error);
                if (!error)
                    size = std::filesystem::file_size(entry.path, error);
                exists = !error;
                if (!exists)
                    UVE_WARNING("HotReloadUVE: tracked asset GUID {} at {} is missing/unreadable - skipping",
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
        if (manager.GetLoadStateUVE(guid) == AssetLoadStateUVE::Loading)
            continue;
        {
            const std::lock_guard lock(m_impl->mutex);
            const auto entry = m_impl->snapshots.find(guid);
            if (entry == m_impl->snapshots.end() || !entry->second.pending || !entry->second.exists)
                continue;
            entry->second.pending = false;
        }
        manager.ReloadUVE(guid, database);
        m_impl->events.QueueEvent(AssetReloadedEventUVE{guid});
    }
}
} // namespace UVE::Asset
