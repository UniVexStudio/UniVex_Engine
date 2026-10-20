// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cstddef>
#include <span>

#include "uve/component/entity_uve.h"

namespace UVE::Scene {

class IEntityManagerUVE;
class ISceneGraphUVE;

/// What one run of the full-body IK pass did. Counts rather than a log: a caller that only wants the
/// work done ignores them, and a test can pin them without reaching into the scene.
struct FullBodyIKPassReportUVE final {
    /// Ticking solves the pass looked at. An entity whose resolved process mode says it does not run
    /// is not looked at - and so is not counted - because nothing else about the scene moves for it
    /// either.
    std::size_t considered = 0U;
    /// Solves that were run and had their joints written into their skeleton's pose.
    std::size_t solved = 0U;
    /// Looked-at solves that were not run: switched off, missing a bone or its reference, carrying
    /// an effector that resolves to nothing, whose skeleton was not posed by the pass this ran in,
    /// or whose target object has gone. Each leaves the pose exactly as the animation wrote it,
    /// which is the whole reason a refusal is safe to make.
    std::size_t refused = 0U;
};

/// Solves every ticking FullBodyIK3D whose skeleton was posed by the step this runs in.
///
/// `posedSkeletons` is the gate that keeps a modifier honest. A solve's result is rotations blended
/// over the pose the animation wrote, so applying it twice would blend the same solve twice and the
/// body would creep toward its targets over several frames instead of reaching them in one. A
/// skeleton that no driver wrote this pass is therefore skipped: its pose is from an earlier pass
/// and has already had its modifiers applied.
///
/// The solve runs AFTER the two-bone pass: a two-bone chain has already planted what it was asked
/// to, and the whole-body solve then reconciles those placements with every other pull in one pose.
/// The pose is materialized whole before it is written - one bone's rotation changed in a pose that
/// does not carry the other bones would drop every animation track that is not this solve - and the
/// write is a blend of the solved locals over the posed ones through the modifier's `influence`.
/// Where the root was translated, the root bone's local POSITION carries it, converted through the
/// parent bone's world frame exactly as its rotation is.
///
/// Runs after the drivers that pose skeletons and before the pass that puts attachments on bones, so
/// a weapon in a hand follows the hand the IK just moved, in the same frame.
[[nodiscard]] FullBodyIKPassReportUVE SyncFullBodyIK3DObjectsUVE(IEntityManagerUVE& entityManager,
                                                                 const ISceneGraphUVE& sceneGraph,
                                                                 std::span<const EntityUVE> posedSkeletons);

} // namespace UVE::Scene
