// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include "uve/component/entity_uve.h"
#include "uve/objects/3d/skeleton_3d_uve.h"

namespace UVE::Scene {

class IEntityManagerUVE;

/// The world frame the scene graph will compose for `entity`: every local transform up its parent
/// chain, outermost first, stopping where the graph stops - at `topLevel`, at a parent with no
/// transform of its own, at the root, or at a malformed chain.
///
/// It reads the LOCAL transforms rather than the cached world transform on purpose: the passes that
/// use it (putting an attachment on a bone, solving a limb onto a target) run BEFORE
/// SceneGraphUVE::UpdateUVE() propagates world transforms, so that the thing they write is right in
/// the same frame rather than a frame late. A parent that moved earlier this frame is therefore only
/// visible through its local transform, which is exactly what this composes.
///
/// One walk, in one place: the two passes have to agree on what "the parent's world frame" means down
/// to the last multiplication, or an attachment and the IK that moves the bone it rides would
/// disagree by a scale factor nobody could find.
[[nodiscard]] ObjectWorldFrameUVE ComposeSceneObjectWorldFrameUVE(IEntityManagerUVE& entityManager,
                                                                  EntityUVE entity);

} // namespace UVE::Scene
