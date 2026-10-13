// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <vector>

#include "uve/math/vector3_uve.h"
#include "uve/network/network_replication_uve.h"

namespace UVE::Network {

// Who relevance is judged for: a viewer is one interested party - today the far side of a
// 1:1 session, tomorrow one client of many - watching the world from a position with a radius.
using InterestViewerIdUVE = std::uint32_t;
inline constexpr InterestViewerIdUVE kInvalidInterestViewerUVE = 0U;

struct InterestStatsUVE final {
    std::uint64_t queries = 0U;
    std::uint64_t relevant = 0U;
    std::uint64_t culled = 0U;
    std::uint64_t refreshes = 0U;
    // Queries that named an unknown viewer or entity. They fail open (see below), and this
    // counts them so a misconfigured id shows up as a number instead of a mystery.
    std::uint64_t unknownIds = 0U;
    // Relevant verdicts withheld by tier/rate throttling. Culled stays distance-only, so
    // the two counters separate "too far" from "not this tick".
    std::uint64_t throttled = 0U;

    [[nodiscard]] bool operator==(const InterestStatsUVE&) const noexcept = default;
};

// Grid tuning. The cell size only moves work around, never verdicts: relevance is always the
// true distance, the grid just finds the candidates. Tiny cells are rejected (the cell index
// must fit an int even at the map's edge), and the margin below zero means no hysteresis band.
struct InterestConfigUVE final {
    float cellSize = 16.0F;
    float hysteresisMargin = 2.0F;
};

inline constexpr float kInterestDefaultCellSizeUVE = 16.0F;
inline constexpr float kInterestMinimumCellSizeUVE = 0.001F;
// Positions must be finite and inside this box (a thousand kilometres each way); wilder
// coordinates are refused at the door, so the floor-to-cell math can never overflow.
inline constexpr float kInterestMaximumCoordinateUVE = 1000000.0F;

/// Send-rate priority: every Nth send tick ships, staggered by entity id so a tier sends
/// smoothly instead of pulsing. Critical (every tick) is the default - verdicts for
/// untiered entities match the pre-tier behaviour exactly.
enum class InterestTierUVE : std::uint8_t { Critical, High, Normal, Low };

[[nodiscard]] inline constexpr std::uint8_t TierDivisorUVE(const InterestTierUVE tier) noexcept {
    switch (tier) {
        case InterestTierUVE::Critical:
            return 1U;
        case InterestTierUVE::High:
            return 2U;
        case InterestTierUVE::Normal:
            return 4U;
        case InterestTierUVE::Low:
            return 8U;
    }
    return 1U;
}

/// Spatial relevance between viewers and replicated entities: which entities a viewer needs.
/// The app pushes positions as things move, then asks - per pair for the replicator's
/// relevancy filters (`relevancy = [&interest]{ return interest.IsRelevantUVE(viewer, id); }`),
/// or in bulk for tools and future fan-out.
///
/// The verdict for one pair: within the viewer's radius means relevant, past the radius plus
/// the hysteresis margin means culled, and the band between keeps its last verdict - so an
/// entity jittering on the boundary holds still instead of flapping in and out of snapshots.
/// A pair never judged before and sitting in the band counts as culled, exactly as if the
/// band were not there. Entities flagged always-relevant skip the distance entirely.
///
/// Unknown ids fail OPEN: an unknown viewer or entity reads as relevant, matching the
/// replicator's empty-filter default. Dropping entities over a typo'd id would break
/// gameplay to save bandwidth; over-sending just costs bytes, and `unknownIds` counts every
/// such verdict so the typo still gets found.
///
/// Single-threaded and move-only like everything below the replicator. The two queries are
/// deliberately non-const: judging a pair teaches the hysteresis band its verdict.
class NetworkInterestUVE final {
public:
    explicit NetworkInterestUVE(const InterestConfigUVE& config = InterestConfigUVE{}) noexcept;

    NetworkInterestUVE(const NetworkInterestUVE&) = delete;
    NetworkInterestUVE& operator=(const NetworkInterestUVE&) = delete;
    NetworkInterestUVE(NetworkInterestUVE&&) noexcept = default;
    NetworkInterestUVE& operator=(NetworkInterestUVE&&) noexcept = default;
    ~NetworkInterestUVE() = default;

    /// Adds a viewer watching from `position` with relevance radius `radius`. Returns false
    /// (adding nothing) for the invalid id, a duplicate id, or a radius that is not finite
    /// and positive - and for a position outside the map box.
    [[nodiscard]] bool AddViewerUVE(InterestViewerIdUVE viewer,
                                    float radius,
                                    const Math::Vector3UVE& position = Math::Vector3UVE{});
    /// Drops a viewer and forgets every verdict it learned. Returns false when unknown.
    [[nodiscard]] bool RemoveViewerUVE(InterestViewerIdUVE viewer) noexcept;
    /// Adds an entity at `position`; always-relevant entities (the local player, the boss)
    /// skip the distance check entirely. Returns false for a duplicate id or a position
    /// outside the map box.
    [[nodiscard]] bool AddEntityUVE(NetworkEntityIdUVE netId,
                                    const Math::Vector3UVE& position = Math::Vector3UVE{},
                                    bool alwaysRelevant = false,
                                    InterestTierUVE tier = InterestTierUVE::Critical);
    /// Drops an entity and forgets every verdict about it. Returns false when unknown.
    [[nodiscard]] bool RemoveEntityUVE(NetworkEntityIdUVE netId) noexcept;
    /// Moves a viewer / an entity. Returns false (moving nothing) for an unknown id or a
    /// position outside the map box.
    [[nodiscard]] bool UpdateViewerUVE(InterestViewerIdUVE viewer,
                                       const Math::Vector3UVE& position) noexcept;
    [[nodiscard]] bool UpdateEntityUVE(NetworkEntityIdUVE netId,
                                       const Math::Vector3UVE& position) noexcept;
    /// Assigns the send-rate tier (policy, like viewer radii: the app owns it). False when
    /// unknown. Retiering never resets the send-tick clock: the phase carries over, so a
    /// promotion ships on its next phase instead of bursting immediately.
    [[nodiscard]] bool SetEntityTierUVE(NetworkEntityIdUVE netId, InterestTierUVE tier) noexcept;
    [[nodiscard]] std::optional<InterestTierUVE> EntityTierUVE(
        NetworkEntityIdUVE netId) const noexcept;
    /// Per-entity rate override: ships every `everyN`th send tick instead of the tier's
    /// divisor. Zero clears back to tier-driven. False when unknown.
    [[nodiscard]] bool SetEntityRateUVE(NetworkEntityIdUVE netId, std::uint8_t everyN) noexcept;
    /// The override (zero when tier-driven). Empty when unknown.
    [[nodiscard]] std::optional<std::uint8_t> EntityRateUVE(
        NetworkEntityIdUVE netId) const noexcept;

    /// The verdict for one pair, learned for hysteresis: relevant, culled, or fail-open true
    /// for unknown ids. Counts one query (plus one unknown id when it fails open).
    [[nodiscard]] bool IsRelevantUVE(InterestViewerIdUVE viewer, NetworkEntityIdUVE netId);
    /// Every entity relevant to `viewer`, sorted by id: the same verdicts as IsRelevantUVE,
    /// learned in bulk. An unknown viewer gets every entity (fail-open); an empty world
    /// gives nothing. Counts one refresh (plus one unknown id when it fails open).
    [[nodiscard]] std::vector<NetworkEntityIdUVE> RelevantEntitiesUVE(
        InterestViewerIdUVE viewer);

    [[nodiscard]] InterestStatsUVE StatsUVE() const noexcept { return m_stats; }
    [[nodiscard]] std::size_t ViewerCountUVE() const noexcept { return m_viewers.size(); }
    [[nodiscard]] std::size_t EntityCountUVE() const noexcept { return m_entities.size(); }
    /// Every known viewer, sorted: the fan-out loop's roster. Empty when none.
    [[nodiscard]] std::vector<InterestViewerIdUVE> ViewersUVE() const;
    /// The pushed positions behind the verdicts (the fan-out ranks by these). Empty when
    /// unknown.
    [[nodiscard]] std::optional<Math::Vector3UVE> ViewerPositionUVE(
        InterestViewerIdUVE viewer) const noexcept;
    [[nodiscard]] std::optional<Math::Vector3UVE> EntityPositionUVE(
        NetworkEntityIdUVE netId) const noexcept;
    [[nodiscard]] float CellSizeUVE() const noexcept { return m_cellSize; }
    [[nodiscard]] float HysteresisMarginUVE() const noexcept { return m_hysteresisMargin; }

private:
    struct ViewerRecordUVE final {
        float radius = 0.0F;
        Math::Vector3UVE position{};
    };
    struct EntityRecordUVE final {
        Math::Vector3UVE position{};
        bool alwaysRelevant = false;
        InterestTierUVE tier = InterestTierUVE::Critical;
        // Per-entity rate override; zero means tier-driven.
        std::uint8_t rateOverride = 0U;
    };
    using CellKeyUVE = std::tuple<int, int, int>;

    [[nodiscard]] CellKeyUVE CellOfUVE(const Math::Vector3UVE& position) const noexcept;
    [[nodiscard]] bool CellOverlapsUVE(const CellKeyUVE& cell, const Math::Vector3UVE& center,
                                       float radius) const noexcept;
    void SweepViewerPairsUVE(InterestViewerIdUVE viewer) noexcept;
    void SweepEntityPairsUVE(NetworkEntityIdUVE netId) noexcept;
    [[nodiscard]] bool JudgeUVE(InterestViewerIdUVE viewer, NetworkEntityIdUVE netId,
                                float distanceSquared, float radius) noexcept;
    /// The send-tick gate: advances the pair's clock and answers whether this tick ships
    /// ((ticks + net id) falls on the divisor's phase). True for unknown entities - the
    /// callers only ask about known pairs, and fail-open must never throttle.
    bool ThrottleUVE(InterestViewerIdUVE viewer, NetworkEntityIdUVE netId) noexcept;

    float m_cellSize = kInterestDefaultCellSizeUVE;
    float m_hysteresisMargin = 2.0F;
    std::unordered_map<InterestViewerIdUVE, ViewerRecordUVE> m_viewers;
    std::unordered_map<NetworkEntityIdUVE, EntityRecordUVE> m_entities;
    std::unordered_map<NetworkEntityIdUVE, CellKeyUVE> m_entityCells;
    std::map<CellKeyUVE, std::vector<NetworkEntityIdUVE>> m_cells;
    // Pairs currently judged relevant: the hysteresis band's memory. Only consulted for pairs
    // sitting in the band; every other verdict recomputes from current positions.
    std::set<std::pair<InterestViewerIdUVE, NetworkEntityIdUVE>> m_relevantPairs;
    // Per-pair send-tick clocks: every query (relevant, culled, or always) advances the
    // clock, since the replicator asks once per entity per send tick. Swept with the pair.
    std::map<std::pair<InterestViewerIdUVE, NetworkEntityIdUVE>, std::uint64_t> m_sendTicks;
    InterestStatsUVE m_stats;
};

} // namespace UVE::Network
