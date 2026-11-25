// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "collision_pair.h"

namespace UVE::Physics {

inline constexpr std::size_t kMaximumCollisionLifecycleResults = 4096U;

enum class CollisionTransitionKind : std::uint8_t {
    Entered = 0,
    Exited,
};

struct CollisionTransition final {
    CollisionTransitionKind kind = CollisionTransitionKind::Entered;
    CollisionPair pair;
};

struct CollisionLifecycleReport final {
    std::size_t previousActiveCount = 0U;
    std::size_t currentActiveCount = 0U;
    bool inputSnapshotTruncated = false;
    bool transitionsTruncated = false;
    std::vector<CollisionTransition> transitions;

    [[nodiscard]] bool IsTruncated() const noexcept {
        return inputSnapshotTruncated || transitionsTruncated;
    }
};

/// Tracks enter/exit transitions between consecutive ICollisionSystem::DetectCollisions()
/// snapshots, mirroring AreaOverlapLifecycleTracker's own diffing contract exactly - except
/// CollisionPair::first/second are two symmetric physics bodies with no fixed "area vs other"
/// role (unlike AreaOverlapPair), so pair identity is normalized (the lower-index/generation
/// entity always becomes `first`) before comparing snapshots; otherwise the same physical contact
/// reported as {A,B} one frame and {B,A} the next would spuriously read as an exit-then-enter.
/// `DetectCollisions()` returns a plain vector with no truncation flag of its own, so
/// `Update()` takes an explicit `maximumPairs` cap and treats an over-cap snapshot the same way
/// AreaOverlapLifecycleTracker treats a truncated one: retain the previous baseline, infer no
/// exits.
/// Thread-safety: not thread-safe - call only from the single thread that owns the physics tick.
class CollisionLifecycleTracker final {
public:
    [[nodiscard]] CollisionLifecycleReport Update(
        const std::vector<CollisionPair>& pairs, std::size_t maximumPairs = kMaximumCollisionLifecycleResults,
        std::size_t maximumTransitions = kMaximumCollisionLifecycleResults);

    void Reset() noexcept;

    [[nodiscard]] std::size_t GetActiveCount() const noexcept { return m_activePairs.size(); }

private:
    std::vector<CollisionPair> m_activePairs;
};

} // namespace UVE::Physics
