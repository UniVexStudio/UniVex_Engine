// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "area_overlap_lifecycle_tracker.h"

#include <algorithm>
#include <cstddef>
#include <utility>

namespace UVE::Physics {

namespace {

[[nodiscard]] bool PairLess(const AreaOverlapPair& lhs, const AreaOverlapPair& rhs) noexcept {
    if (lhs.area.index != rhs.area.index) {
        return lhs.area.index < rhs.area.index;
    }
    if (lhs.area.generation != rhs.area.generation) {
        return lhs.area.generation < rhs.area.generation;
    }
    if (lhs.other.index != rhs.other.index) {
        return lhs.other.index < rhs.other.index;
    }
    return lhs.other.generation < rhs.other.generation;
}

[[nodiscard]] bool PairIdentityEqual(const AreaOverlapPair& lhs,
                                        const AreaOverlapPair& rhs) noexcept {
    return lhs.area == rhs.area && lhs.other == rhs.other;
}

} // namespace

AreaOverlapLifecycleReport AreaOverlapLifecycleTracker::Update(
    const AreaOverlapQueryResult& snapshot, const std::size_t maximumTransitions) {
    AreaOverlapLifecycleReport report;
    report.previousActiveCount = m_activePairs.size();
    report.inputSnapshotTruncated = snapshot.truncated || snapshot.overlaps.size() > kMaximumAreaOverlapResults;
    if (report.inputSnapshotTruncated) {
        report.currentActiveCount = m_activePairs.size();
        return report;
    }

    std::vector<AreaOverlapPair> currentPairs = snapshot.overlaps;
    std::sort(currentPairs.begin(), currentPairs.end(), PairLess);
    currentPairs.erase(std::unique(currentPairs.begin(), currentPairs.end(), PairIdentityEqual), currentPairs.end());

    report.currentActiveCount = currentPairs.size();
    const std::size_t transitionCap = std::min(maximumTransitions, kMaximumAreaOverlapResults);
    report.transitions.reserve(std::min(transitionCap, m_activePairs.size() + currentPairs.size()));

    for (const AreaOverlapPair& previous : m_activePairs) {
        if (!std::binary_search(currentPairs.begin(), currentPairs.end(), previous, PairLess)) {
            if (report.transitions.size() >= transitionCap) {
                report.transitionsTruncated = true;
                continue;
            }
            report.transitions.push_back({AreaOverlapTransitionKind::Exited, previous});
        }
    }
    for (const AreaOverlapPair& current : currentPairs) {
        if (!std::binary_search(m_activePairs.begin(), m_activePairs.end(), current, PairLess)) {
            if (report.transitions.size() >= transitionCap) {
                report.transitionsTruncated = true;
                continue;
            }
            report.transitions.push_back({AreaOverlapTransitionKind::Entered, current});
        }
    }

    std::sort(report.transitions.begin(), report.transitions.end(), [](const AreaOverlapTransition& lhs,
                                                                        const AreaOverlapTransition& rhs) {
        if (PairLess(lhs.pair, rhs.pair)) {
            return true;
        }
        if (PairLess(rhs.pair, lhs.pair)) {
            return false;
        }
        return lhs.kind < rhs.kind;
    });
    m_activePairs = std::move(currentPairs);
    return report;
}

void AreaOverlapLifecycleTracker::Reset() noexcept { m_activePairs.clear(); }

} // namespace UVE::Physics
