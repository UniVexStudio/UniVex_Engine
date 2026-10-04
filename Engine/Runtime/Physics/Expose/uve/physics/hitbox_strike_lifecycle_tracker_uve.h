// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "uve/physics/hitbox_strike_uve.h"

namespace UVE::Physics {

enum class Hitbox3DStrikeTransitionKindUVE : std::uint8_t {
    Entered = 0,
    Exited,
};

struct Hitbox3DStrikeTransitionUVE final {
    Hitbox3DStrikeTransitionKindUVE kind = Hitbox3DStrikeTransitionKindUVE::Entered;
    /// The pairing, with the last-known depth/axis/channel as evidence: for an exit, the numbers
    /// are the ones the strike had while it was live, not a recomputation after the boxes moved.
    Hitbox3DStrikePairUVE strike;

    [[nodiscard]] bool operator==(const Hitbox3DStrikeTransitionUVE&) const noexcept = default;
};

struct Hitbox3DStrikeLifecycleReportUVE final {
    std::size_t previousActiveCount = 0U;
    std::size_t currentActiveCount = 0U;
    bool inputSnapshotTruncated = false;
    bool transitionsTruncated = false;
    std::vector<Hitbox3DStrikeTransitionUVE> transitions;

    [[nodiscard]] bool IsTruncatedUVE() const noexcept {
        return inputSnapshotTruncated || transitionsTruncated;
    }
};

/// Tracks enter/exit transitions between consecutive Physics::SyncHitboxes3DUVE() snapshots,
/// mirroring AreaOverlapLifecycleTrackerUVE's diffing contract exactly.
///
/// Why a tracker at all: a strike that lasts a hundred frames is still ONE strike as far as a
/// consequence is concerned. The per-hitbox list is the state ("I am touching these right now"), and
/// the transition is the event ("this hit started", "this hit ended") - so a damage application, a
/// hit reaction or an i-frame grant can be written against an edge instead of against a per-frame
/// overlap that has to be de-duplicated by hand at every call site.
///
/// Identity is the (hitbox, hurtbox) pair only. A channel, depth or axis that changes mid-strike is
/// the same strike getting deeper, not a new one; the transition carries the latest numbers as
/// evidence, so a consumer that wants the deepest penetration it has seen keeps a max of its own.
///
/// A truncated snapshot retains the previous baseline and infers NO exits - the pairs beyond the
/// bound are not gone, they are simply not in the report, and inventing an exit from an incomplete
/// input would be the same lie as inventing a hit from one. ResetUVE() explicitly discards the
/// baseline without fabricating transitions - the engine core calls it at teardown, where the
/// next frame's report has nothing to diff against and every live strike has already ended.
///
/// Thread-safety: not thread-safe - call only from the single thread that owns the tick.
class Hitbox3DStrikeLifecycleTrackerUVE final {
public:
    [[nodiscard]] Hitbox3DStrikeLifecycleReportUVE UpdateUVE(
        const Hitbox3DSyncReportUVE& snapshot,
        std::size_t maximumTransitions = kMaximumHitbox3DStrikeResultsUVE);

    void ResetUVE() noexcept;

    [[nodiscard]] std::size_t GetActiveCountUVE() const noexcept { return m_activeStrikes.size(); }

private:
    std::vector<Hitbox3DStrikePairUVE> m_activeStrikes;
};

} // namespace UVE::Physics
