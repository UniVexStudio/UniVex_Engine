// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "area_overlap_system.h"

namespace UVE::Physics {

enum class AreaOverlapTransitionKind : std::uint8_t {
    Entered = 0,
    Exited,
};

struct AreaOverlapTransition final {
    AreaOverlapTransitionKind kind = AreaOverlapTransitionKind::Entered;
    AreaOverlapPair pair;

    [[nodiscard]] bool operator==(const AreaOverlapTransition&) const noexcept = default;
};

struct AreaOverlapLifecycleReport final {
    std::size_t previousActiveCount = 0U;
    std::size_t currentActiveCount = 0U;
    bool inputSnapshotTruncated = false;
    bool transitionsTruncated = false;
    std::vector<AreaOverlapTransition> transitions;

    [[nodiscard]] bool IsTruncated() const noexcept {
        return inputSnapshotTruncated || transitionsTruncated;
    }
};

/// Tracks enter/exit transitions between complete AreaOverlapSystem snapshots. The tracker
/// owns only copied generation-safe pairs and never publishes events, mutates ECS/physics state,
/// or owns the query system. Truncated snapshots retain the previous active set and emit no
/// inferred exits; Reset() explicitly discards the baseline without fabricating transitions.
class AreaOverlapLifecycleTracker final {
public:
    [[nodiscard]] AreaOverlapLifecycleReport Update(
        const AreaOverlapQueryResult& snapshot,
        std::size_t maximumTransitions = kMaximumAreaOverlapResults);

    void Reset() noexcept;

    [[nodiscard]] std::size_t GetActiveCount() const noexcept { return m_activePairs.size(); }

private:
    std::vector<AreaOverlapPair> m_activePairs;
};

} // namespace UVE::Physics
