// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
#pragma once
#include <cstdint>
#include <memory>
#include "i_hot_reload.h"
#include "i_event_system.h"
namespace UVE::Asset {
enum class HotReloadPreference : std::uint8_t { Automatic = 0, Polling };
enum class HotReloadBackend : std::uint8_t { Polling = 0, Inotify };
/// Explicitly tracked GUIDs only, not a second project scanner/importer. Automatic supplements
/// portable path/existence/mtime/size snapshots with Linux inotify; other platforms, failed native
/// initialization and unwatchable directories keep polling. Both modes retain the existing
/// interval as their debounce/dispatch boundary. No event thread or immediate callbacks are added.
/// First observation is a baseline; duplicate Track is idempotent. Deletion keeps last-good data;
/// recreation/relocation schedules reload. Changes while Loading remain pending. Native queue
/// overflow/directory replacement forces a rescan. Requests are coalesced and sorted by GUID.
/// Polling cannot detect content-only changes with identical path/size/mtime between polls.
/// AssetReloadedEvent still means triggered, not succeeded. Track/Untrack/queries are synchronized;
/// Poll and lifetime belong to the engine thread. Manager/event callbacks occur outside the lock.
/// Untrack does not cancel an already-dispatched load; AssetManager may Track again on completion.
class HotReload final : public IHotReload {
  public:
    explicit HotReload(Events::IEventSystem &eventSystem, double pollIntervalSeconds = 1.0);
    HotReload(Events::IEventSystem &eventSystem, double pollIntervalSeconds,
                 HotReloadPreference preference);
    ~HotReload() override;
    HotReload(const HotReload &) = delete;
    HotReload &operator=(const HotReload &) = delete;
    /// Actual notification backend initialization, not an all-directory watch guarantee.
    [[nodiscard]] HotReloadBackend GetBackend() const;
    [[nodiscard]] std::uint64_t GetNativeOverflowCount() const;
    void Track(AssetGuid guid) override;
    void Untrack(AssetGuid guid) override;
    /// Negative/nonfinite deltas do not mutate timing or snapshots.
    void Poll(IAssetManager &assetManager, IAssetDatabase &assetDatabase,
                 double deltaTimeSeconds) override;

  private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};
} // namespace UVE::Asset
