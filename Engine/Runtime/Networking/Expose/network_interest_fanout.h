// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once
//
// Scope: the interest fan-out - one interest core serving many clients. The core judges
// (viewer, net id) pairs; the fan-out turns each viewer's relevant set into a RANKED send
// selection (tier first, distance within a tier, id order beneath that) and cuts it to
// the viewer's per-tick budget, so an overloaded server sheds far Low entities before
// near Critical ones:
//
//   pump:     fanout.RefreshAll() (or Refresh per viewer), then each viewer's
//             replicator.Update(). The replicators' relevancy filters were bound once
//             to fanout.IsSelected and read the fresh selection every send tick.
//   budgets:  SetViewerBudget caps how many entities ship per refresh; zero (the
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

#include "network_interest.h"

namespace UVE::Network {

/// Cumulative fan-out counters: refreshes run, entities kept, entities shed by budget.
struct FanoutStats final {
    std::uint64_t refreshes = 0U;
    std::uint64_t selected = 0U;
    std::uint64_t budgetDropped = 0U;

    [[nodiscard]] bool operator==(const FanoutStats&) const noexcept = default;
};

class NetworkInterestFanout final {
public:
    explicit NetworkInterestFanout(NetworkInterest& interest) noexcept
        : m_interest(&interest) {}

    /// Caps `viewer`'s selection at `maxEntities` per refresh; zero lifts the cap (and
    /// forgets the viewer when the core never knew it).
    void SetViewerBudget(InterestViewerId viewer, std::uint32_t maxEntities) noexcept;
    /// The cap (zero when unset: unbudgeted).
    [[nodiscard]] std::uint32_t ViewerBudget(InterestViewerId viewer) const noexcept;
    /// Recomputes `viewer`'s selection from the core's current verdicts (throttle
    /// included: off-phase entities are not selected) and answers how many selected.
    /// Counts a refresh even for unknown viewers (their fail-open set, budget-cut when
    /// one is set).
    [[nodiscard]] std::size_t Refresh(InterestViewerId viewer);
    /// Refreshes every viewer the fan-out knows (core viewers plus budgeted ones), in
    /// id order, and answers the total selected.
    [[nodiscard]] std::size_t RefreshAll();
    /// Whether `netId` is in `viewer`'s current selection: the bound relevancy check.
    /// True for unknown viewers, unknown ids, and never-refreshed viewers - fail open.
    [[nodiscard]] bool IsSelected(InterestViewerId viewer,
                                     NetworkEntityId netId) const;
    /// Whatever the last refresh chose, ranked. Empty when the viewer was never
    /// refreshed or is unknown - the fail-open query is IsSelected, not this.
    [[nodiscard]] std::vector<NetworkEntityId> SelectedEntities(
        InterestViewerId viewer) const;
    /// How many refreshed, selected, and shed by budget since construction.
    [[nodiscard]] FanoutStats Stats() const noexcept { return m_stats; }

private:
    NetworkInterest* m_interest;
    std::unordered_map<InterestViewerId, std::uint32_t> m_budgets;
    std::map<InterestViewerId, std::vector<NetworkEntityId>> m_selections;
    FanoutStats m_stats;
};

} // namespace UVE::Network
