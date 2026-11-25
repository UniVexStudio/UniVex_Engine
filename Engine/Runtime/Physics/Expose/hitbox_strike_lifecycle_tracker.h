// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "hitbox_strike.h"

namespace UVE::Physics {

enum class Hitbox3DStrikeTransitionKind : std::uint8_t {
    Entered = 0,
    Exited,
};

struct Hitbox3DStrikeTransition final {
    Hitbox3DStrikeTransitionKind kind = Hitbox3DStrikeTransitionKind::Entered;
    /// The pairing, with the last-known depth/axis/channel as evidence: for an exit, the numbers
    /// are the ones the strike had while it was live, not a recomputation after the boxes moved.
    Hitbox3DStrikePair strike;

    [[nodiscard]] bool operator==(const Hitbox3DStrikeTransition&) const noexcept = default;
};

struct Hitbox3DStrikeLifecycleReport final {
    std::size_t previousActiveCount = 0U;
    std::size_t currentActiveCount = 0U;
    bool inputSnapshotTruncated = false;
    bool transitionsTruncated = false;
    std::vector<Hitbox3DStrikeTransition> transitions;

    [[nodiscard]] bool IsTruncated() const noexcept {
        return inputSnapshotTruncated || transitionsTruncated;
    }
};

/// Tracks enter/exit transitions between consecutive Physics::SyncHitboxes3D() snapshots,
/// mirroring AreaOverlapLifecycleTracker's diffing contract exactly.
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
/// input would be the same lie as inventing a hit from one. Reset() explicitly discards the
/// baseline without fabricating transitions - the engine core calls it at teardown, where the
/// next frame's report has nothing to diff against and every live strike has already ended.
///
/// Thread-safety: not thread-safe - call only from the single thread that owns the tick.
class Hitbox3DStrikeLifecycleTracker final {
public:
    [[nodiscard]] Hitbox3DStrikeLifecycleReport Update(
        const Hitbox3DSyncReport& snapshot,
        std::size_t maximumTransitions = kMaximumHitbox3DStrikeResults);

    void Reset() noexcept;

    [[nodiscard]] std::size_t GetActiveCount() const noexcept { return m_activeStrikes.size(); }

private:
    std::vector<Hitbox3DStrikePair> m_activeStrikes;
};

} // namespace UVE::Physics
