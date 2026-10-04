// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cstddef>
#include <span>

#include "uve/component/entity_uve.h"

namespace UVE::Scene {

class IEntityManagerUVE;
class ISceneGraphUVE;

/// What one run of the two-bone IK pass did. Counts rather than a log: a caller that only wants the
/// work done ignores them, and a test can pin them without reaching into the scene.
struct TwoBoneIKPassReportUVE final {
    /// Ticking chains the pass looked at. An entity whose resolved process mode says it does not run
    /// is not looked at - and so is not counted - because nothing else about the scene moves for it
    /// either.
    std::size_t considered = 0U;
    /// Chains that were solved and had both bone rotations written into their skeleton's pose.
    std::size_t solved = 0U;
    /// Looked-at chains that were not solved: switched off, missing a bone or its reference, whose
    /// three bones are not a root-middle-end chain of the skeleton, whose skeleton was not posed by
    /// the pass this ran in, or whose target object has gone. Each leaves the pose exactly as the
    /// animation wrote it, which is the whole reason a refusal is safe to make.
    std::size_t refused = 0U;
};

/// Solves every ticking TwoBoneIK3D whose skeleton was posed by the step this runs in.
///
/// `posedSkeletons` is the gate that keeps a modifier honest. A chain's result is a rotation blended
/// over the pose the animation wrote, so applying it twice would blend the same solve twice and a
/// limb would creep toward the target over several frames instead of reaching it in one. A skeleton
/// that no driver wrote this pass is therefore skipped: its pose is from an earlier pass and has
/// already had its modifiers applied.
///
/// Chains resolve their skeleton by reference, their three bones by index when it names a real bone
/// and by exact name otherwise, and their target and pole the same way. The pose is materialized
/// whole before it is written - one bone's rotation changed in a pose that does not carry the other
/// bones would drop every animation track that is not this chain - and the write is a blend of the
/// solved rotations over the posed ones through the modifier's `influence`.
///
/// Runs after the drivers that pose skeletons and before the pass that puts attachments on bones, so
/// a weapon in a hand follows the hand the IK just moved, in the same frame.
[[nodiscard]] TwoBoneIKPassReportUVE SyncTwoBoneIK3DObjectsUVE(IEntityManagerUVE& entityManager,
                                                              const ISceneGraphUVE& sceneGraph,
                                                              std::span<const EntityUVE> posedSkeletons);

} // namespace UVE::Scene
