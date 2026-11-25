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

#include "vector3.h"
#include "network_replication.h"

namespace UVE::Network {

// Who relevance is judged for: a viewer is one interested party - today the far side of a
// 1:1 session, tomorrow one client of many - watching the world from a position with a radius.
using InterestViewerId = std::uint32_t;
inline constexpr InterestViewerId kInvalidInterestViewer = 0U;

struct InterestStats final {
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

    [[nodiscard]] bool operator==(const InterestStats&) const noexcept = default;
};

// Grid tuning. The cell size only moves work around, never verdicts: relevance is always the
// true distance, the grid just finds the candidates. Tiny cells are rejected (the cell index
// must fit an int even at the map's edge), and the margin below zero means no hysteresis band.
struct InterestConfig final {
    float cellSize = 16.0F;
    float hysteresisMargin = 2.0F;
};

inline constexpr float kInterestDefaultCellSize = 16.0F;
inline constexpr float kInterestMinimumCellSize = 0.001F;
// Positions must be finite and inside this box (a thousand kilometres each way); wilder
// coordinates are refused at the door, so the floor-to-cell math can never overflow.
inline constexpr float kInterestMaximumCoordinate = 1000000.0F;

/// Send-rate priority: every Nth send tick ships, staggered by entity id so a tier sends
/// smoothly instead of pulsing. Critical (every tick) is the default - verdicts for
/// untiered entities match the pre-tier behaviour exactly.
enum class InterestTier : std::uint8_t { Critical, High, Normal, Low };

[[nodiscard]] inline constexpr std::uint8_t TierDivisor(const InterestTier tier) noexcept {
    switch (tier) {
        case InterestTier::Critical:
            return 1U;
        case InterestTier::High:
            return 2U;
        case InterestTier::Normal:
            return 4U;
        case InterestTier::Low:
            return 8U;
    }
    return 1U;
}

/// Spatial relevance between viewers and replicated entities: which entities a viewer needs.
/// The app pushes positions as things move, then asks - per pair for the replicator's
/// relevancy filters (`relevancy = [&interest]{ return interest.IsRelevant(viewer, id); }`),
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
class NetworkInterest final {
public:
    explicit NetworkInterest(const InterestConfig& config = InterestConfig{}) noexcept;

    NetworkInterest(const NetworkInterest&) = delete;
    NetworkInterest& operator=(const NetworkInterest&) = delete;
    NetworkInterest(NetworkInterest&&) noexcept = default;
    NetworkInterest& operator=(NetworkInterest&&) noexcept = default;
    ~NetworkInterest() = default;

    /// Adds a viewer watching from `position` with relevance radius `radius`. Returns false
    /// (adding nothing) for the invalid id, a duplicate id, or a radius that is not finite
    /// and positive - and for a position outside the map box.
    [[nodiscard]] bool AddViewer(InterestViewerId viewer,
                                    float radius,
                                    const Math::Vector3& position = Math::Vector3{});
    /// Drops a viewer and forgets every verdict it learned. Returns false when unknown.
    [[nodiscard]] bool RemoveViewer(InterestViewerId viewer) noexcept;
    /// Adds an entity at `position`; always-relevant entities (the local player, the boss)
    /// skip the distance check entirely. Returns false for a duplicate id or a position
    /// outside the map box.
    [[nodiscard]] bool AddEntity(NetworkEntityId netId,
                                    const Math::Vector3& position = Math::Vector3{},
                                    bool alwaysRelevant = false,
                                    InterestTier tier = InterestTier::Critical);
    /// Drops an entity and forgets every verdict about it. Returns false when unknown.
    [[nodiscard]] bool RemoveEntity(NetworkEntityId netId) noexcept;
    /// Moves a viewer / an entity. Returns false (moving nothing) for an unknown id or a
    /// position outside the map box.
    [[nodiscard]] bool UpdateViewer(InterestViewerId viewer,
                                       const Math::Vector3& position) noexcept;
    [[nodiscard]] bool UpdateEntity(NetworkEntityId netId,
                                       const Math::Vector3& position) noexcept;
    /// Assigns the send-rate tier (policy, like viewer radii: the app owns it). False when
    /// unknown. Retiering never resets the send-tick clock: the phase carries over, so a
    /// promotion ships on its next phase instead of bursting immediately.
    [[nodiscard]] bool SetEntityTier(NetworkEntityId netId, InterestTier tier) noexcept;
    [[nodiscard]] std::optional<InterestTier> EntityTier(
        NetworkEntityId netId) const noexcept;
    /// Per-entity rate override: ships every `everyN`th send tick instead of the tier's
    /// divisor. Zero clears back to tier-driven. False when unknown.
    [[nodiscard]] bool SetEntityRate(NetworkEntityId netId, std::uint8_t everyN) noexcept;
    /// The override (zero when tier-driven). Empty when unknown.
    [[nodiscard]] std::optional<std::uint8_t> EntityRate(
        NetworkEntityId netId) const noexcept;

    /// The verdict for one pair, learned for hysteresis: relevant, culled, or fail-open true
    /// for unknown ids. Counts one query (plus one unknown id when it fails open).
    [[nodiscard]] bool IsRelevant(InterestViewerId viewer, NetworkEntityId netId);
    /// Every entity relevant to `viewer`, sorted by id: the same verdicts as IsRelevant,
    /// learned in bulk. An unknown viewer gets every entity (fail-open); an empty world
    /// gives nothing. Counts one refresh (plus one unknown id when it fails open).
    [[nodiscard]] std::vector<NetworkEntityId> RelevantEntities(
        InterestViewerId viewer);

    [[nodiscard]] InterestStats Stats() const noexcept { return m_stats; }
    [[nodiscard]] std::size_t ViewerCount() const noexcept { return m_viewers.size(); }
    [[nodiscard]] std::size_t EntityCount() const noexcept { return m_entities.size(); }
    /// Every known viewer, sorted: the fan-out loop's roster. Empty when none.
    [[nodiscard]] std::vector<InterestViewerId> Viewers() const;
    /// The pushed positions behind the verdicts (the fan-out ranks by these). Empty when
    /// unknown.
    [[nodiscard]] std::optional<Math::Vector3> ViewerPosition(
        InterestViewerId viewer) const noexcept;
    [[nodiscard]] std::optional<Math::Vector3> EntityPosition(
        NetworkEntityId netId) const noexcept;
    [[nodiscard]] float CellSize() const noexcept { return m_cellSize; }
    [[nodiscard]] float HysteresisMargin() const noexcept { return m_hysteresisMargin; }

private:
    struct ViewerRecord final {
        float radius = 0.0F;
        Math::Vector3 position{};
    };
    struct EntityRecord final {
        Math::Vector3 position{};
        bool alwaysRelevant = false;
        InterestTier tier = InterestTier::Critical;
        // Per-entity rate override; zero means tier-driven.
        std::uint8_t rateOverride = 0U;
    };
    using CellKey = std::tuple<int, int, int>;

    [[nodiscard]] CellKey CellOf(const Math::Vector3& position) const noexcept;
    [[nodiscard]] bool CellOverlaps(const CellKey& cell, const Math::Vector3& center,
                                       float radius) const noexcept;
    void SweepViewerPairs(InterestViewerId viewer) noexcept;
    void SweepEntityPairs(NetworkEntityId netId) noexcept;
    [[nodiscard]] bool Judge(InterestViewerId viewer, NetworkEntityId netId,
                                float distanceSquared, float radius) noexcept;
    /// The send-tick gate: advances the pair's clock and answers whether this tick ships
    /// ((ticks + net id) falls on the divisor's phase). True for unknown entities - the
    /// callers only ask about known pairs, and fail-open must never throttle.
    bool Throttle(InterestViewerId viewer, NetworkEntityId netId) noexcept;

    float m_cellSize = kInterestDefaultCellSize;
    float m_hysteresisMargin = 2.0F;
    std::unordered_map<InterestViewerId, ViewerRecord> m_viewers;
    std::unordered_map<NetworkEntityId, EntityRecord> m_entities;
    std::unordered_map<NetworkEntityId, CellKey> m_entityCells;
    std::map<CellKey, std::vector<NetworkEntityId>> m_cells;
    // Pairs currently judged relevant: the hysteresis band's memory. Only consulted for pairs
    // sitting in the band; every other verdict recomputes from current positions.
    std::set<std::pair<InterestViewerId, NetworkEntityId>> m_relevantPairs;
    // Per-pair send-tick clocks: every query (relevant, culled, or always) advances the
    // clock, since the replicator asks once per entity per send tick. Swept with the pair.
    std::map<std::pair<InterestViewerId, NetworkEntityId>, std::uint64_t> m_sendTicks;
    InterestStats m_stats;
};

} // namespace UVE::Network
