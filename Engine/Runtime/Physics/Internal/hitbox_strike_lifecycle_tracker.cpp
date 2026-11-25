// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "hitbox_strike_lifecycle_tracker.h"

#include <algorithm>
#include <cstddef>
#include <utility>

namespace UVE::Physics {
namespace {

/// Deterministic order for a pair: hitbox first, then hurtbox, each by (index, generation) - never
/// by pointer or pool order, so two runs over the same scene emit transitions in the same order.
[[nodiscard]] bool StrikeLess(const Hitbox3DStrikePair& lhs,
                                 const Hitbox3DStrikePair& rhs) noexcept {
    if (lhs.hitbox.index != rhs.hitbox.index) {
        return lhs.hitbox.index < rhs.hitbox.index;
    }
    if (lhs.hitbox.generation != rhs.hitbox.generation) {
        return lhs.hitbox.generation < rhs.hitbox.generation;
    }
    if (lhs.hurtbox.index != rhs.hurtbox.index) {
        return lhs.hurtbox.index < rhs.hurtbox.index;
    }
    return lhs.hurtbox.generation < rhs.hurtbox.generation;
}

/// Whether two records are the same strike. Deliberately NOT operator==: two records for the same
/// pairing at different depths are the same strike, and that is the question identity asks here.
[[nodiscard]] bool StrikeIdentityEqual(const Hitbox3DStrikePair& lhs,
                                          const Hitbox3DStrikePair& rhs) noexcept {
    return lhs.hitbox == rhs.hitbox && lhs.hurtbox == rhs.hurtbox;
}

} // namespace

Hitbox3DStrikeLifecycleReport Hitbox3DStrikeLifecycleTracker::Update(
    const Hitbox3DSyncReport& snapshot, const std::size_t maximumTransitions) {
    Hitbox3DStrikeLifecycleReport report;
    report.previousActiveCount = m_activeStrikes.size();
    report.inputSnapshotTruncated =
        snapshot.IsTruncated() || snapshot.strikes.size() > kMaximumHitbox3DStrikeResults;
    if (report.inputSnapshotTruncated) {
        report.currentActiveCount = m_activeStrikes.size();
        return report;
    }

    std::vector<Hitbox3DStrikePair> currentStrikes = snapshot.strikes;
    std::sort(currentStrikes.begin(), currentStrikes.end(), StrikeLess);
    currentStrikes.erase(std::unique(currentStrikes.begin(), currentStrikes.end(), StrikeIdentityEqual),
                         currentStrikes.end());

    report.currentActiveCount = currentStrikes.size();
    const std::size_t transitionCap = std::min(maximumTransitions, kMaximumHitbox3DStrikeResults);
    report.transitions.reserve(std::min(transitionCap, m_activeStrikes.size() + currentStrikes.size()));

    // Exits first, then enters, so a consumer reading the report in order releases the old strike
    // before it is told about the new one - the order a script or a state machine expects. The
    // whole report is re-sorted by pair below, which is the order that is actually contracted.
    for (const Hitbox3DStrikePair& previous : m_activeStrikes) {
        if (!std::binary_search(currentStrikes.begin(), currentStrikes.end(), previous, StrikeLess)) {
            if (report.transitions.size() >= transitionCap) {
                report.transitionsTruncated = true;
                continue;
            }
            report.transitions.push_back({Hitbox3DStrikeTransitionKind::Exited, previous});
        }
    }
    for (const Hitbox3DStrikePair& current : currentStrikes) {
        if (!std::binary_search(m_activeStrikes.begin(), m_activeStrikes.end(), current, StrikeLess)) {
            if (report.transitions.size() >= transitionCap) {
                report.transitionsTruncated = true;
                continue;
            }
            report.transitions.push_back({Hitbox3DStrikeTransitionKind::Entered, current});
        }
    }

    std::sort(report.transitions.begin(), report.transitions.end(),
              [](const Hitbox3DStrikeTransition& lhs, const Hitbox3DStrikeTransition& rhs) {
                  if (StrikeLess(lhs.strike, rhs.strike)) {
                      return true;
                  }
                  if (StrikeLess(rhs.strike, lhs.strike)) {
                      return false;
                  }
                  return lhs.kind < rhs.kind;
              });
    m_activeStrikes = std::move(currentStrikes);
    return report;
}

void Hitbox3DStrikeLifecycleTracker::Reset() noexcept { m_activeStrikes.clear(); }

} // namespace UVE::Physics
