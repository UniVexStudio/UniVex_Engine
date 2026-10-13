// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include "uve/component/entity_uve.h"
#include "uve/entity/i_entity_manager_uve.h"

namespace UVE::Scene {

/// Positions every followable camera from its target: a camera with CameraFollowComponentUVE,
/// TransformComponentUVE, WorldTransformComponentUVE, and HierarchyComponentUVE whose target is a
/// live entity with a world transform gets localPosition = target world position + offset, and
/// its world transform marked dirty - the same two writes SceneGraphUVE::SetLocalTransformUVE()
/// performs, done directly because uve_gameplay must not depend on uve_scene. A candidate pose
/// that fails IsTransformComponentValidUVE() (a NaN target position, a non-finite offset that
/// slipped past validation) is refused: the camera holds its last pose.
///
/// Skipped, untouched, and holding: invalid targets, dead or transformless targets, cameras
/// missing any of the four components, and parented cameras (rig-driven; the spring arm owns
/// those). Writes are independent per camera, so iteration order is irrelevant and the pass
/// sorts nothing.
void UpdateCameraFollowUVE(IEntityManagerUVE& entityManager);

/// The possession hook for Player-kind Possessed transitions: every live CameraFollowComponentUVE
/// with followPossessedPawn retargets to `pawn`. AI-kind possessions never retarget, and a
/// controller that is dead, transformless, or not Player-kind retargets nothing. Called inline
/// where the engine queues the lifecycle events - the queued PawnPossessedEventUVE is for game
/// code; the engine's own follow cameras consume the same transition directly, with no
/// subscription round-trip.
void RetargetPossessionFollowersUVE(IEntityManagerUVE& entityManager, EntityUVE controller, EntityUVE pawn);

/// The possession hook for Player-kind Unpossessed transitions: every live flagged follower whose
/// target is `pawn` parks (target back to invalid) and holds its last pose. Followers tracking
/// any other target keep it. Same Player-kind gate as the retarget hook.
void ReleasePossessionFollowersUVE(IEntityManagerUVE& entityManager, EntityUVE controller, EntityUVE pawn);

} // namespace UVE::Scene
