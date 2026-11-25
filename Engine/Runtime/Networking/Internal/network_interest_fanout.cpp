// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "network_interest_fanout.h"

#include <algorithm>
#include <set>

namespace UVE::Network {

void NetworkInterestFanout::SetViewerBudget(const InterestViewerId viewer,
                                                  const std::uint32_t maxEntities) noexcept {
    if (maxEntities == 0U) {
        m_budgets.erase(viewer);
        return;
    }
    m_budgets[viewer] = maxEntities;
}

std::uint32_t NetworkInterestFanout::ViewerBudget(
    const InterestViewerId viewer) const noexcept {
    const auto found = m_budgets.find(viewer);
    return found == m_budgets.end() ? 0U : found->second;
}

std::size_t NetworkInterestFanout::Refresh(const InterestViewerId viewer) {
    // The bulk verdicts carry the throttle: off-phase entities never reach the ranking.
    std::vector<NetworkEntityId> ranked = m_interest->RelevantEntities(viewer);
    const std::optional<Math::Vector3> anchor = m_interest->ViewerPosition(viewer);
    if (anchor.has_value()) {
        const auto rankOf = [this, &anchor](const NetworkEntityId netId) {
            const InterestTier tier =
                m_interest->EntityTier(netId).value_or(InterestTier::Critical);
            const Math::Vector3 position =
                m_interest->EntityPosition(netId).value_or(Math::Vector3{});
            const float dx = position.x - anchor->x;
            const float dy = position.y - anchor->y;
            const float dz = position.z - anchor->z;
            return std::tuple{tier, dx * dx + dy * dy + dz * dz, netId};
        };
        std::sort(ranked.begin(), ranked.end(), [&](const NetworkEntityId left,
                                                   const NetworkEntityId right) {
            return rankOf(left) < rankOf(right);
        });
    }
    const std::uint32_t budget = ViewerBudget(viewer);
    if (budget != 0U && ranked.size() > budget) {
        m_stats.budgetDropped += ranked.size() - budget;
        ranked.resize(budget);
    }
    m_stats.selected += ranked.size();
    ++m_stats.refreshes;
    m_selections[viewer] = ranked;
    return ranked.size();
}

std::size_t NetworkInterestFanout::RefreshAll() {
    std::set<InterestViewerId> roster;
    for (const InterestViewerId viewer : m_interest->Viewers()) {
        roster.insert(viewer);
    }
    for (const auto& [viewer, _] : m_budgets) {
        roster.insert(viewer);
    }
    std::size_t total = 0U;
    for (const InterestViewerId viewer : roster) {
        total += Refresh(viewer);
    }
    return total;
}

bool NetworkInterestFanout::IsSelected(const InterestViewerId viewer,
                                             const NetworkEntityId netId) const {
    const auto selection = m_selections.find(viewer);
    if (selection == m_selections.end()) {
        return true;
    }
    const std::vector<NetworkEntityId>& ids = selection->second;
    return std::find(ids.begin(), ids.end(), netId) != ids.end();
}

std::vector<NetworkEntityId> NetworkInterestFanout::SelectedEntities(
    const InterestViewerId viewer) const {
    const auto selection = m_selections.find(viewer);
    if (selection == m_selections.end()) {
        return {};
    }
    return selection->second;
}

} // namespace UVE::Network
