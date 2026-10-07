// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

#include "uve/config/settings_registry_uve.h"

namespace UVE::Config {

/// One effective setting value changing. Composite settings (colours, vectors) are reported as one
/// complete before/after event, never as separate component notifications.
struct SettingChangedEventUVE final {
    std::string id;
    SettingValueUVE previousValue;
    SettingValueUVE newValue;
};

class SettingsObserverHubUVE;

/// Opaque handle returned by a settings observer owner. A handle can only be unsubscribed from the
/// hub that issued it.
struct SettingsObserverSubscriptionUVE final {
    std::uint64_t id = 0U;
    const SettingsObserverHubUVE* owner = nullptr;

    [[nodiscard]] bool IsValidUVE() const noexcept { return id != 0U && owner != nullptr; }
};

using SettingsObserverCallbackUVE = std::function<void(const SettingChangedEventUVE&)>;

/// Shared exact-id/category-prefix observer dispatch for settings owners such as
/// SettingsDocumentUVE and EditorUVE. The owner commits a change first, then calls
/// NotifyChangedUVE with its effective before/after values.
///
/// Not thread-safe: the registry and hub must stay on the owner's thread. Callbacks run
/// synchronously on that thread after commit. A callback-triggered mutation dispatches its own
/// notification immediately (nested), so consumers must avoid feedback loops. Changes to
/// subscriptions during dispatch take effect at once: an observer removed before its turn is
/// skipped, while a new observer starts with the next change. Callbacks should not throw; if one
/// does, the already-committed change remains and the exception propagates to the owner.
class SettingsObserverHubUVE final {
public:
    explicit SettingsObserverHubUVE(const SettingsRegistryUVE& registry) noexcept;

    SettingsObserverHubUVE(const SettingsObserverHubUVE&) = delete;
    SettingsObserverHubUVE& operator=(const SettingsObserverHubUVE&) = delete;
    SettingsObserverHubUVE(SettingsObserverHubUVE&&) = delete;
    SettingsObserverHubUVE& operator=(SettingsObserverHubUVE&&) = delete;

    /// Calls `callback` after future effective value changes to exactly `id`. Returns an invalid
    /// handle for an unknown setting or empty callback.
    [[nodiscard]] SettingsObserverSubscriptionUVE SubscribeToSettingUVE(
        std::string_view id, SettingsObserverCallbackUVE callback);
    /// Calls `callback` after changes to settings in `categoryPrefix` or any descendant category.
    /// Category paths are slash-separated and matched on segment boundaries, so "Render/Shadows"
    /// does not match "Render/ShadowsExtra". Returns an invalid handle for an empty callback or a
    /// prefix that currently matches no registered setting.
    [[nodiscard]] SettingsObserverSubscriptionUVE SubscribeToCategoryUVE(
        std::string_view categoryPrefix, SettingsObserverCallbackUVE callback);
    /// Removes a subscription from this hub. Returns false for an invalid or already removed
    /// handle. A handle issued by a different hub cannot remove this hub's observer.
    [[nodiscard]] bool UnsubscribeUVE(SettingsObserverSubscriptionUVE subscription);

    /// Dispatches a committed setting change to matching observers. Equal before/after values and
    /// ids absent from the registry are ignored. Owners should call this only after applying a
    /// successful mutation, so callbacks always observe committed state.
    void NotifyChangedUVE(std::string_view id, const SettingValueUVE& previousValue,
                          const SettingValueUVE& newValue);

private:
    enum class ObserverFilterKindUVE {
        SettingId,
        CategoryPrefix,
    };

    struct ObserverUVE final {
        std::uint64_t id = 0U;
        ObserverFilterKindUVE filterKind = ObserverFilterKindUVE::SettingId;
        std::string filter;
        SettingsObserverCallbackUVE callback;
    };

    [[nodiscard]] SettingsObserverSubscriptionUVE AddObserverUVE(ObserverFilterKindUVE filterKind,
                                                                  std::string filter,
                                                                  SettingsObserverCallbackUVE callback);

    const SettingsRegistryUVE* m_registry = nullptr;
    std::vector<ObserverUVE> m_observers;
    std::uint64_t m_nextObserverId = 1U;
};

} // namespace UVE::Config
