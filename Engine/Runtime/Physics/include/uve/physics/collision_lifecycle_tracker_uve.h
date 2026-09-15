// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "uve/physics/collision_pair_uve.h"

namespace UVE::Physics {

inline constexpr std::size_t kMaximumCollisionLifecycleResultsUVE = 4096U;

enum class CollisionTransitionKindUVE : std::uint8_t {
    Entered = 0,
    Exited,
};

struct CollisionTransitionUVE final {
    CollisionTransitionKindUVE kind = CollisionTransitionKindUVE::Entered;
    CollisionPairUVE pair;
};

struct CollisionLifecycleReportUVE final {
    std::size_t previousActiveCount = 0U;
    std::size_t currentActiveCount = 0U;
    bool inputSnapshotTruncated = false;
    bool transitionsTruncated = false;
    std::vector<CollisionTransitionUVE> transitions;

    [[nodiscard]] bool IsTruncatedUVE() const noexcept {
        return inputSnapshotTruncated || transitionsTruncated;
    }
};

/// Tracks enter/exit transitions between consecutive ICollisionSystemUVE::DetectCollisionsUVE()
/// snapshots, mirroring AreaOverlapLifecycleTrackerUVE's own diffing contract exactly - except
/// CollisionPairUVE::first/second are two symmetric physics bodies with no fixed "area vs other"
/// role (unlike AreaOverlapPairUVE), so pair identity is normalized (the lower-index/generation
/// entity always becomes `first`) before comparing snapshots; otherwise the same physical contact
/// reported as {A,B} one frame and {B,A} the next would spuriously read as an exit-then-enter.
/// `DetectCollisionsUVE()` returns a plain vector with no truncation flag of its own, so
/// `UpdateUVE()` takes an explicit `maximumPairs` cap and treats an over-cap snapshot the same way
/// AreaOverlapLifecycleTrackerUVE treats a truncated one: retain the previous baseline, infer no
/// exits.
/// Thread-safety: not thread-safe - call only from the single thread that owns the physics tick.
class CollisionLifecycleTrackerUVE final {
public:
    [[nodiscard]] CollisionLifecycleReportUVE UpdateUVE(
        const std::vector<CollisionPairUVE>& pairs, std::size_t maximumPairs = kMaximumCollisionLifecycleResultsUVE,
        std::size_t maximumTransitions = kMaximumCollisionLifecycleResultsUVE);

    void ResetUVE() noexcept;

    [[nodiscard]] std::size_t GetActiveCountUVE() const noexcept { return m_activePairs.size(); }

private:
    std::vector<CollisionPairUVE> m_activePairs;
};

} // namespace UVE::Physics
