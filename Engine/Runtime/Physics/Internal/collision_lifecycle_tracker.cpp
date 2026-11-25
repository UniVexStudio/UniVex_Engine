// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "collision_lifecycle_tracker.h"

#include <algorithm>
#include <cstddef>
#include <utility>

namespace UVE::Physics {

namespace {

/// Reorders a pair so the lower-index (or, on an index tie, lower-generation) entity is always
/// `first` - CollisionPair has no fixed role the way AreaOverlapPair's area/other does, so
/// without this the same physical contact could be reported as {A,B} one frame and {B,A} the
/// next, which the diff below would otherwise misread as an exit-then-enter. `separationAxis` is
/// negated on a swap since it is documented to point from `first` toward `second`.
[[nodiscard]] CollisionPair NormalizePair(const CollisionPair& pair) noexcept {
    const bool firstIsSmaller = pair.first.index < pair.second.index ||
                                 (pair.first.index == pair.second.index && pair.first.generation <= pair.second.generation);
    if (firstIsSmaller) {
        return pair;
    }
    CollisionPair swapped = pair;
    swapped.first = pair.second;
    swapped.second = pair.first;
    swapped.separationAxis = -pair.separationAxis;
    return swapped;
}

[[nodiscard]] bool PairLess(const CollisionPair& lhs, const CollisionPair& rhs) noexcept {
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

[[nodiscard]] bool PairIdentityEqual(const CollisionPair& lhs, const CollisionPair& rhs) noexcept {
    return lhs.first == rhs.first && lhs.second == rhs.second;
}

} // namespace

CollisionLifecycleReport CollisionLifecycleTracker::Update(const std::vector<CollisionPair>& pairs,
                                                                     const std::size_t maximumPairs,
                                                                     const std::size_t maximumTransitions) {
    CollisionLifecycleReport report;
    report.previousActiveCount = m_activePairs.size();
    report.inputSnapshotTruncated = pairs.size() > maximumPairs;
    if (report.inputSnapshotTruncated) {
        report.currentActiveCount = m_activePairs.size();
        return report;
    }

    std::vector<CollisionPair> currentPairs;
    currentPairs.reserve(pairs.size());
    for (const CollisionPair& pair : pairs) {
        currentPairs.push_back(NormalizePair(pair));
    }
    std::sort(currentPairs.begin(), currentPairs.end(), PairLess);
    currentPairs.erase(std::unique(currentPairs.begin(), currentPairs.end(), PairIdentityEqual), currentPairs.end());

    report.currentActiveCount = currentPairs.size();
    const std::size_t transitionCap = std::min(maximumTransitions, kMaximumCollisionLifecycleResults);
    report.transitions.reserve(std::min(transitionCap, m_activePairs.size() + currentPairs.size()));

    for (const CollisionPair& previous : m_activePairs) {
        if (!std::binary_search(currentPairs.begin(), currentPairs.end(), previous, PairLess)) {
            if (report.transitions.size() >= transitionCap) {
                report.transitionsTruncated = true;
                continue;
            }
            report.transitions.push_back({CollisionTransitionKind::Exited, previous});
        }
    }
    for (const CollisionPair& current : currentPairs) {
        if (!std::binary_search(m_activePairs.begin(), m_activePairs.end(), current, PairLess)) {
            if (report.transitions.size() >= transitionCap) {
                report.transitionsTruncated = true;
                continue;
            }
            report.transitions.push_back({CollisionTransitionKind::Entered, current});
        }
    }

    std::sort(report.transitions.begin(), report.transitions.end(),
              [](const CollisionTransition& lhs, const CollisionTransition& rhs) {
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

void CollisionLifecycleTracker::Reset() noexcept { m_activePairs.clear(); }

} // namespace UVE::Physics
