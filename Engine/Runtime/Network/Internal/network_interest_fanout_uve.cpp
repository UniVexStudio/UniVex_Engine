// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/network/network_interest_fanout_uve.h"

#include <algorithm>
#include <set>

namespace UVE::Network {

void NetworkInterestFanoutUVE::SetViewerBudgetUVE(const InterestViewerIdUVE viewer,
                                                  const std::uint32_t maxEntities) noexcept {
    if (maxEntities == 0U) {
        m_budgets.erase(viewer);
        return;
    }
    m_budgets[viewer] = maxEntities;
}

std::uint32_t NetworkInterestFanoutUVE::ViewerBudgetUVE(
    const InterestViewerIdUVE viewer) const noexcept {
    const auto found = m_budgets.find(viewer);
    return found == m_budgets.end() ? 0U : found->second;
}

std::size_t NetworkInterestFanoutUVE::RefreshUVE(const InterestViewerIdUVE viewer) {
    // The bulk verdicts carry the throttle: off-phase entities never reach the ranking.
    std::vector<NetworkEntityIdUVE> ranked = m_interest->RelevantEntitiesUVE(viewer);
    const std::optional<Math::Vector3UVE> anchor = m_interest->ViewerPositionUVE(viewer);
    if (anchor.has_value()) {
        const auto rankOf = [this, &anchor](const NetworkEntityIdUVE netId) {
            const InterestTierUVE tier =
                m_interest->EntityTierUVE(netId).value_or(InterestTierUVE::Critical);
            const Math::Vector3UVE position =
                m_interest->EntityPositionUVE(netId).value_or(Math::Vector3UVE{});
            const float dx = position.x - anchor->x;
            const float dy = position.y - anchor->y;
            const float dz = position.z - anchor->z;
            return std::tuple{tier, dx * dx + dy * dy + dz * dz, netId};
        };
        std::sort(ranked.begin(), ranked.end(), [&](const NetworkEntityIdUVE left,
                                                   const NetworkEntityIdUVE right) {
            return rankOf(left) < rankOf(right);
        });
    }
    const std::uint32_t budget = ViewerBudgetUVE(viewer);
    if (budget != 0U && ranked.size() > budget) {
        m_stats.budgetDropped += ranked.size() - budget;
        ranked.resize(budget);
    }
    m_stats.selected += ranked.size();
    ++m_stats.refreshes;
    m_selections[viewer] = ranked;
    return ranked.size();
}

std::size_t NetworkInterestFanoutUVE::RefreshAllUVE() {
    std::set<InterestViewerIdUVE> roster;
    for (const InterestViewerIdUVE viewer : m_interest->ViewersUVE()) {
        roster.insert(viewer);
    }
    for (const auto& [viewer, _] : m_budgets) {
        roster.insert(viewer);
    }
    std::size_t total = 0U;
    for (const InterestViewerIdUVE viewer : roster) {
        total += RefreshUVE(viewer);
    }
    return total;
}

bool NetworkInterestFanoutUVE::IsSelectedUVE(const InterestViewerIdUVE viewer,
                                             const NetworkEntityIdUVE netId) const {
    const auto selection = m_selections.find(viewer);
    if (selection == m_selections.end()) {
        return true;
    }
    const std::vector<NetworkEntityIdUVE>& ids = selection->second;
    return std::find(ids.begin(), ids.end(), netId) != ids.end();
}

std::vector<NetworkEntityIdUVE> NetworkInterestFanoutUVE::SelectedEntitiesUVE(
    const InterestViewerIdUVE viewer) const {
    const auto selection = m_selections.find(viewer);
    if (selection == m_selections.end()) {
        return {};
    }
    return selection->second;
}

} // namespace UVE::Network
