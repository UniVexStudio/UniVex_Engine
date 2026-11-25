// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "settings_observer.h"

#include <algorithm>
#include <utility>

namespace UVE::Config {
namespace {

[[nodiscard]] bool IsWithinCategoryPrefix(const std::string_view category,
                                             const std::string_view categoryPrefix) noexcept {
    return category.starts_with(categoryPrefix) &&
           (category.size() == categoryPrefix.size() || category[categoryPrefix.size()] == '/');
}

} // namespace

SettingsObserverHub::SettingsObserverHub(const SettingsRegistry& registry) noexcept
    : m_registry(&registry) {}

SettingsObserverSubscription SettingsObserverHub::SubscribeToSetting(
    const std::string_view id, SettingsObserverCallback callback) {
    if (!callback || m_registry == nullptr || m_registry->Find(id) == nullptr) {
        return {};
    }
    return AddObserver(ObserverFilterKind::SettingId, std::string(id), std::move(callback));
}

SettingsObserverSubscription SettingsObserverHub::SubscribeToCategory(
    const std::string_view categoryPrefix, SettingsObserverCallback callback) {
    if (categoryPrefix.empty() || !callback || m_registry == nullptr) {
        return {};
    }
    const std::vector<const SettingDescriptor*> descriptors = m_registry->GetAll();
    const bool hasMatchingCategory =
        std::any_of(descriptors.begin(), descriptors.end(), [categoryPrefix](const SettingDescriptor* descriptor) {
            return IsWithinCategoryPrefix(descriptor->category, categoryPrefix);
        });
    if (!hasMatchingCategory) {
        return {};
    }
    return AddObserver(ObserverFilterKind::CategoryPrefix, std::string(categoryPrefix), std::move(callback));
}

bool SettingsObserverHub::Unsubscribe(const SettingsObserverSubscription subscription) {
    if (!subscription.IsValid() || subscription.owner != this) {
        return false;
    }
    const auto observer =
        std::find_if(m_observers.begin(), m_observers.end(), [subscription](const Observer& entry) {
            return entry.id == subscription.id;
        });
    if (observer == m_observers.end()) {
        return false;
    }
    m_observers.erase(observer);
    return true;
}

SettingsObserverSubscription SettingsObserverHub::AddObserver(
    const ObserverFilterKind filterKind, std::string filter, SettingsObserverCallback callback) {
    std::uint64_t id = m_nextObserverId == 0U ? 1U : m_nextObserverId;
    const std::uint64_t firstCandidate = id;
    while (std::any_of(m_observers.begin(), m_observers.end(), [id](const Observer& observer) {
        return observer.id == id;
    })) {
        ++id;
        if (id == 0U) {
            id = 1U;
        }
        if (id == firstCandidate) {
            return {};
        }
    }
    m_nextObserverId = id + 1U;
    if (m_nextObserverId == 0U) {
        m_nextObserverId = 1U;
    }
    m_observers.push_back(Observer{id, filterKind, std::move(filter), std::move(callback)});
    return SettingsObserverSubscription{id, this};
}

void SettingsObserverHub::NotifyChanged(const std::string_view id,
                                              const SettingValue& previousValue,
                                              const SettingValue& newValue) {
    if (previousValue == newValue || m_registry == nullptr) {
        return;
    }
    const SettingDescriptor* descriptor = m_registry->Find(id);
    if (descriptor == nullptr) {
        return;
    }

    const SettingChangedEvent event{std::string(id), previousValue, newValue};
    const std::vector<Observer> snapshot = m_observers;
    for (const Observer& observer : snapshot) {
        const bool stillSubscribed =
            std::any_of(m_observers.begin(), m_observers.end(), [&observer](const Observer& entry) {
                return entry.id == observer.id;
            });
        if (!stillSubscribed) {
            continue;
        }

        const bool matches = observer.filterKind == ObserverFilterKind::SettingId
                                 ? observer.filter == id
                                 : IsWithinCategoryPrefix(descriptor->category, observer.filter);
        if (matches) {
            observer.callback(event);
        }
    }
}

} // namespace UVE::Config
