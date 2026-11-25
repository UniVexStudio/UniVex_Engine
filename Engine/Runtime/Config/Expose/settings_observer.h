// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

#include "settings_registry.h"

namespace UVE::Config {

/// One effective setting value changing. Composite settings (colours, vectors) are reported as one
/// complete before/after event, never as separate component notifications.
struct SettingChangedEvent final {
    std::string id;
    SettingValue previousValue;
    SettingValue newValue;
};

class SettingsObserverHub;

/// Opaque handle returned by a settings observer owner. A handle can only be unsubscribed from the
/// hub that issued it.
struct SettingsObserverSubscription final {
    std::uint64_t id = 0U;
    const SettingsObserverHub* owner = nullptr;

    [[nodiscard]] bool IsValid() const noexcept { return id != 0U && owner != nullptr; }
};

using SettingsObserverCallback = std::function<void(const SettingChangedEvent&)>;

/// Shared exact-id/category-prefix observer dispatch for settings owners such as
/// SettingsDocument and Editor. The owner commits a change first, then calls
/// NotifyChanged with its effective before/after values.
///
/// Not thread-safe: the registry and hub must stay on the owner's thread. Callbacks run
/// synchronously on that thread after commit. A callback-triggered mutation dispatches its own
/// notification immediately (nested), so consumers must avoid feedback loops. Changes to
/// subscriptions during dispatch take effect at once: an observer removed before its turn is
/// skipped, while a new observer starts with the next change. Callbacks should not throw; if one
/// does, the already-committed change remains and the exception propagates to the owner.
class SettingsObserverHub final {
public:
    explicit SettingsObserverHub(const SettingsRegistry& registry) noexcept;

    SettingsObserverHub(const SettingsObserverHub&) = delete;
    SettingsObserverHub& operator=(const SettingsObserverHub&) = delete;
    SettingsObserverHub(SettingsObserverHub&&) = delete;
    SettingsObserverHub& operator=(SettingsObserverHub&&) = delete;

    /// Calls `callback` after future effective value changes to exactly `id`. Returns an invalid
    /// handle for an unknown setting or empty callback.
    [[nodiscard]] SettingsObserverSubscription SubscribeToSetting(
        std::string_view id, SettingsObserverCallback callback);
    /// Calls `callback` after changes to settings in `categoryPrefix` or any descendant category.
    /// Category paths are slash-separated and matched on segment boundaries, so "Render/Shadows"
    /// does not match "Render/ShadowsExtra". Returns an invalid handle for an empty callback or a
    /// prefix that currently matches no registered setting.
    [[nodiscard]] SettingsObserverSubscription SubscribeToCategory(
        std::string_view categoryPrefix, SettingsObserverCallback callback);
    /// Removes a subscription from this hub. Returns false for an invalid or already removed
    /// handle. A handle issued by a different hub cannot remove this hub's observer.
    [[nodiscard]] bool Unsubscribe(SettingsObserverSubscription subscription);

    /// Dispatches a committed setting change to matching observers. Equal before/after values and
    /// ids absent from the registry are ignored. Owners should call this only after applying a
    /// successful mutation, so callbacks always observe committed state.
    void NotifyChanged(std::string_view id, const SettingValue& previousValue,
                          const SettingValue& newValue);

private:
    enum class ObserverFilterKind {
        SettingId,
        CategoryPrefix,
    };

    struct Observer final {
        std::uint64_t id = 0U;
        ObserverFilterKind filterKind = ObserverFilterKind::SettingId;
        std::string filter;
        SettingsObserverCallback callback;
    };

    [[nodiscard]] SettingsObserverSubscription AddObserver(ObserverFilterKind filterKind,
                                                                  std::string filter,
                                                                  SettingsObserverCallback callback);

    const SettingsRegistry* m_registry = nullptr;
    std::vector<Observer> m_observers;
    std::uint64_t m_nextObserverId = 1U;
};

} // namespace UVE::Config
