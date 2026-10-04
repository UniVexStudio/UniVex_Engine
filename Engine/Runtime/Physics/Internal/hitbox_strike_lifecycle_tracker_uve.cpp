// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/physics/hitbox_strike_lifecycle_tracker_uve.h"

#include <algorithm>
#include <cstddef>
#include <utility>

namespace UVE::Physics {
namespace {

/// Deterministic order for a pair: hitbox first, then hurtbox, each by (index, generation) - never
/// by pointer or pool order, so two runs over the same scene emit transitions in the same order.
[[nodiscard]] bool StrikeLessUVE(const Hitbox3DStrikePairUVE& lhs,
                                 const Hitbox3DStrikePairUVE& rhs) noexcept {
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
[[nodiscard]] bool StrikeIdentityEqualUVE(const Hitbox3DStrikePairUVE& lhs,
                                          const Hitbox3DStrikePairUVE& rhs) noexcept {
    return lhs.hitbox == rhs.hitbox && lhs.hurtbox == rhs.hurtbox;
}

} // namespace

Hitbox3DStrikeLifecycleReportUVE Hitbox3DStrikeLifecycleTrackerUVE::UpdateUVE(
    const Hitbox3DSyncReportUVE& snapshot, const std::size_t maximumTransitions) {
    Hitbox3DStrikeLifecycleReportUVE report;
    report.previousActiveCount = m_activeStrikes.size();
    report.inputSnapshotTruncated =
        snapshot.IsTruncatedUVE() || snapshot.strikes.size() > kMaximumHitbox3DStrikeResultsUVE;
    if (report.inputSnapshotTruncated) {
        report.currentActiveCount = m_activeStrikes.size();
        return report;
    }

    std::vector<Hitbox3DStrikePairUVE> currentStrikes = snapshot.strikes;
    std::sort(currentStrikes.begin(), currentStrikes.end(), StrikeLessUVE);
    currentStrikes.erase(std::unique(currentStrikes.begin(), currentStrikes.end(), StrikeIdentityEqualUVE),
                         currentStrikes.end());

    report.currentActiveCount = currentStrikes.size();
    const std::size_t transitionCap = std::min(maximumTransitions, kMaximumHitbox3DStrikeResultsUVE);
    report.transitions.reserve(std::min(transitionCap, m_activeStrikes.size() + currentStrikes.size()));

    // Exits first, then enters, so a consumer reading the report in order releases the old strike
    // before it is told about the new one - the order a script or a state machine expects. The
    // whole report is re-sorted by pair below, which is the order that is actually contracted.
    for (const Hitbox3DStrikePairUVE& previous : m_activeStrikes) {
        if (!std::binary_search(currentStrikes.begin(), currentStrikes.end(), previous, StrikeLessUVE)) {
            if (report.transitions.size() >= transitionCap) {
                report.transitionsTruncated = true;
                continue;
            }
            report.transitions.push_back({Hitbox3DStrikeTransitionKindUVE::Exited, previous});
        }
    }
    for (const Hitbox3DStrikePairUVE& current : currentStrikes) {
        if (!std::binary_search(m_activeStrikes.begin(), m_activeStrikes.end(), current, StrikeLessUVE)) {
            if (report.transitions.size() >= transitionCap) {
                report.transitionsTruncated = true;
                continue;
            }
            report.transitions.push_back({Hitbox3DStrikeTransitionKindUVE::Entered, current});
        }
    }

    std::sort(report.transitions.begin(), report.transitions.end(),
              [](const Hitbox3DStrikeTransitionUVE& lhs, const Hitbox3DStrikeTransitionUVE& rhs) {
                  if (StrikeLessUVE(lhs.strike, rhs.strike)) {
                      return true;
                  }
                  if (StrikeLessUVE(rhs.strike, lhs.strike)) {
                      return false;
                  }
                  return lhs.kind < rhs.kind;
              });
    m_activeStrikes = std::move(currentStrikes);
    return report;
}

void Hitbox3DStrikeLifecycleTrackerUVE::ResetUVE() noexcept { m_activeStrikes.clear(); }

} // namespace UVE::Physics
