// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/config/settings_observer_uve.h"

#include <algorithm>
#include <utility>

namespace UVE::Config {
namespace {

[[nodiscard]] bool IsWithinCategoryPrefixUVE(const std::string_view category,
                                             const std::string_view categoryPrefix) noexcept {
    return category.starts_with(categoryPrefix) &&
           (category.size() == categoryPrefix.size() || category[categoryPrefix.size()] == '/');
}

} // namespace

SettingsObserverHubUVE::SettingsObserverHubUVE(const SettingsRegistryUVE& registry) noexcept
    : m_registry(&registry) {}

SettingsObserverSubscriptionUVE SettingsObserverHubUVE::SubscribeToSettingUVE(
    const std::string_view id, SettingsObserverCallbackUVE callback) {
    if (!callback || m_registry == nullptr || m_registry->FindUVE(id) == nullptr) {
        return {};
    }
    return AddObserverUVE(ObserverFilterKindUVE::SettingId, std::string(id), std::move(callback));
}

SettingsObserverSubscriptionUVE SettingsObserverHubUVE::SubscribeToCategoryUVE(
    const std::string_view categoryPrefix, SettingsObserverCallbackUVE callback) {
    if (categoryPrefix.empty() || !callback || m_registry == nullptr) {
        return {};
    }
    const std::vector<const SettingDescriptorUVE*> descriptors = m_registry->GetAllUVE();
    const bool hasMatchingCategory =
        std::any_of(descriptors.begin(), descriptors.end(), [categoryPrefix](const SettingDescriptorUVE* descriptor) {
            return IsWithinCategoryPrefixUVE(descriptor->category, categoryPrefix);
        });
    if (!hasMatchingCategory) {
        return {};
    }
    return AddObserverUVE(ObserverFilterKindUVE::CategoryPrefix, std::string(categoryPrefix), std::move(callback));
}

bool SettingsObserverHubUVE::UnsubscribeUVE(const SettingsObserverSubscriptionUVE subscription) {
    if (!subscription.IsValidUVE() || subscription.owner != this) {
        return false;
    }
    const auto observer =
        std::find_if(m_observers.begin(), m_observers.end(), [subscription](const ObserverUVE& entry) {
            return entry.id == subscription.id;
        });
    if (observer == m_observers.end()) {
        return false;
    }
    m_observers.erase(observer);
    return true;
}

SettingsObserverSubscriptionUVE SettingsObserverHubUVE::AddObserverUVE(
    const ObserverFilterKindUVE filterKind, std::string filter, SettingsObserverCallbackUVE callback) {
    std::uint64_t id = m_nextObserverId == 0U ? 1U : m_nextObserverId;
    const std::uint64_t firstCandidate = id;
    while (std::any_of(m_observers.begin(), m_observers.end(), [id](const ObserverUVE& observer) {
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
    m_observers.push_back(ObserverUVE{id, filterKind, std::move(filter), std::move(callback)});
    return SettingsObserverSubscriptionUVE{id, this};
}

void SettingsObserverHubUVE::NotifyChangedUVE(const std::string_view id,
                                              const SettingValueUVE& previousValue,
                                              const SettingValueUVE& newValue) {
    if (previousValue == newValue || m_registry == nullptr) {
        return;
    }
    const SettingDescriptorUVE* descriptor = m_registry->FindUVE(id);
    if (descriptor == nullptr) {
        return;
    }

    const SettingChangedEventUVE event{std::string(id), previousValue, newValue};
    const std::vector<ObserverUVE> snapshot = m_observers;
    for (const ObserverUVE& observer : snapshot) {
        const bool stillSubscribed =
            std::any_of(m_observers.begin(), m_observers.end(), [&observer](const ObserverUVE& entry) {
                return entry.id == observer.id;
            });
        if (!stillSubscribed) {
            continue;
        }

        const bool matches = observer.filterKind == ObserverFilterKindUVE::SettingId
                                 ? observer.filter == id
                                 : IsWithinCategoryPrefixUVE(descriptor->category, observer.filter);
        if (matches) {
            observer.callback(event);
        }
    }
}

} // namespace UVE::Config
