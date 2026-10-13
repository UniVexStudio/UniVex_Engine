// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once
//
// Scope: the interest fan-out - one interest core serving many clients. The core judges
// (viewer, net id) pairs; the fan-out turns each viewer's relevant set into a RANKED send
// selection (tier first, distance within a tier, id order beneath that) and cuts it to
// the viewer's per-tick budget, so an overloaded server sheds far Low entities before
// near Critical ones:
//
//   pump:     fanout.RefreshAllUVE() (or RefreshUVE per viewer), then each viewer's
//             replicator.UpdateUVE(). The replicators' relevancy filters were bound once
//             to fanout.IsSelectedUVE and read the fresh selection every send tick.
//   budgets:  SetViewerBudgetUVE caps how many entities ship per refresh; zero (the
//             default) means unbudgeted. Explicit budgets even cut fail-open sets.
//   fail-open: unknown viewers, unknown ids, and never-refreshed viewers all select -
//             the fan-out over-sends exactly like the core.
//
// Lifetimes: borrows the interest core (it must outlive the fan-out).
//

#include <cstddef>
#include <cstdint>
#include <map>
#include <unordered_map>
#include <vector>

#include "uve/network/network_interest_uve.h"

namespace UVE::Network {

/// Cumulative fan-out counters: refreshes run, entities kept, entities shed by budget.
struct FanoutStatsUVE final {
    std::uint64_t refreshes = 0U;
    std::uint64_t selected = 0U;
    std::uint64_t budgetDropped = 0U;

    [[nodiscard]] bool operator==(const FanoutStatsUVE&) const noexcept = default;
};

class NetworkInterestFanoutUVE final {
public:
    explicit NetworkInterestFanoutUVE(NetworkInterestUVE& interest) noexcept
        : m_interest(&interest) {}

    /// Caps `viewer`'s selection at `maxEntities` per refresh; zero lifts the cap (and
    /// forgets the viewer when the core never knew it).
    void SetViewerBudgetUVE(InterestViewerIdUVE viewer, std::uint32_t maxEntities) noexcept;
    /// The cap (zero when unset: unbudgeted).
    [[nodiscard]] std::uint32_t ViewerBudgetUVE(InterestViewerIdUVE viewer) const noexcept;
    /// Recomputes `viewer`'s selection from the core's current verdicts (throttle
    /// included: off-phase entities are not selected) and answers how many selected.
    /// Counts a refresh even for unknown viewers (their fail-open set, budget-cut when
    /// one is set).
    [[nodiscard]] std::size_t RefreshUVE(InterestViewerIdUVE viewer);
    /// Refreshes every viewer the fan-out knows (core viewers plus budgeted ones), in
    /// id order, and answers the total selected.
    [[nodiscard]] std::size_t RefreshAllUVE();
    /// Whether `netId` is in `viewer`'s current selection: the bound relevancy check.
    /// True for unknown viewers, unknown ids, and never-refreshed viewers - fail open.
    [[nodiscard]] bool IsSelectedUVE(InterestViewerIdUVE viewer,
                                     NetworkEntityIdUVE netId) const;
    /// Whatever the last refresh chose, ranked. Empty when the viewer was never
    /// refreshed or is unknown - the fail-open query is IsSelectedUVE, not this.
    [[nodiscard]] std::vector<NetworkEntityIdUVE> SelectedEntitiesUVE(
        InterestViewerIdUVE viewer) const;
    /// How many refreshed, selected, and shed by budget since construction.
    [[nodiscard]] FanoutStatsUVE StatsUVE() const noexcept { return m_stats; }

private:
    NetworkInterestUVE* m_interest;
    std::unordered_map<InterestViewerIdUVE, std::uint32_t> m_budgets;
    std::map<InterestViewerIdUVE, std::vector<NetworkEntityIdUVE>> m_selections;
    FanoutStatsUVE m_stats;
};

} // namespace UVE::Network
