// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/physics/collision_lifecycle_tracker_uve.h"

#include <algorithm>
#include <cstddef>
#include <utility>

namespace UVE::Physics {

namespace {

/// Reorders a pair so the lower-index (or, on an index tie, lower-generation) entity is always
/// `first` - CollisionPairUVE has no fixed role the way AreaOverlapPairUVE's area/other does, so
/// without this the same physical contact could be reported as {A,B} one frame and {B,A} the
/// next, which the diff below would otherwise misread as an exit-then-enter. `separationAxis` is
/// negated on a swap since it is documented to point from `first` toward `second`.
[[nodiscard]] CollisionPairUVE NormalizePairUVE(const CollisionPairUVE& pair) noexcept {
    const bool firstIsSmaller = pair.first.index < pair.second.index ||
                                 (pair.first.index == pair.second.index && pair.first.generation <= pair.second.generation);
    if (firstIsSmaller) {
        return pair;
    }
    CollisionPairUVE swapped = pair;
    swapped.first = pair.second;
    swapped.second = pair.first;
    swapped.separationAxis = -pair.separationAxis;
    return swapped;
}

[[nodiscard]] bool PairLessUVE(const CollisionPairUVE& lhs, const CollisionPairUVE& rhs) noexcept {
    if (lhs.first.index != rhs.first.index) {
        return lhs.first.index < rhs.first.index;
    }
    if (lhs.first.generation != rhs.first.generation) {
        return lhs.first.generation < rhs.first.generation;
    }
    if (lhs.second.index != rhs.second.index) {
        return lhs.second.index < rhs.second.index;
    }
    return lhs.second.generation < rhs.second.generation;
}

[[nodiscard]] bool PairIdentityEqualUVE(const CollisionPairUVE& lhs, const CollisionPairUVE& rhs) noexcept {
    return lhs.first == rhs.first && lhs.second == rhs.second;
}

} // namespace

CollisionLifecycleReportUVE CollisionLifecycleTrackerUVE::UpdateUVE(const std::vector<CollisionPairUVE>& pairs,
                                                                     const std::size_t maximumPairs,
                                                                     const std::size_t maximumTransitions) {
    CollisionLifecycleReportUVE report;
    report.previousActiveCount = m_activePairs.size();
    report.inputSnapshotTruncated = pairs.size() > maximumPairs;
    if (report.inputSnapshotTruncated) {
        report.currentActiveCount = m_activePairs.size();
        return report;
    }

    std::vector<CollisionPairUVE> currentPairs;
    currentPairs.reserve(pairs.size());
    for (const CollisionPairUVE& pair : pairs) {
        currentPairs.push_back(NormalizePairUVE(pair));
    }
    std::sort(currentPairs.begin(), currentPairs.end(), PairLessUVE);
    currentPairs.erase(std::unique(currentPairs.begin(), currentPairs.end(), PairIdentityEqualUVE), currentPairs.end());

    report.currentActiveCount = currentPairs.size();
    const std::size_t transitionCap = std::min(maximumTransitions, kMaximumCollisionLifecycleResultsUVE);
    report.transitions.reserve(std::min(transitionCap, m_activePairs.size() + currentPairs.size()));

    for (const CollisionPairUVE& previous : m_activePairs) {
        if (!std::binary_search(currentPairs.begin(), currentPairs.end(), previous, PairLessUVE)) {
            if (report.transitions.size() >= transitionCap) {
                report.transitionsTruncated = true;
                continue;
            }
            report.transitions.push_back({CollisionTransitionKindUVE::Exited, previous});
        }
    }
    for (const CollisionPairUVE& current : currentPairs) {
        if (!std::binary_search(m_activePairs.begin(), m_activePairs.end(), current, PairLessUVE)) {
            if (report.transitions.size() >= transitionCap) {
                report.transitionsTruncated = true;
                continue;
            }
            report.transitions.push_back({CollisionTransitionKindUVE::Entered, current});
        }
    }

    std::sort(report.transitions.begin(), report.transitions.end(),
              [](const CollisionTransitionUVE& lhs, const CollisionTransitionUVE& rhs) {
                  if (PairLessUVE(lhs.pair, rhs.pair)) {
                      return true;
                  }
                  if (PairLessUVE(rhs.pair, lhs.pair)) {
                      return false;
                  }
                  return lhs.kind < rhs.kind;
              });
    m_activePairs = std::move(currentPairs);
    return report;
}

void CollisionLifecycleTrackerUVE::ResetUVE() noexcept { m_activePairs.clear(); }

} // namespace UVE::Physics
