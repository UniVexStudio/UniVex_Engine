// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cstddef>

#include "uve/component/entity_uve.h"

namespace UVE::Scene {

class IEntityManagerUVE;
class ISceneGraphUVE;

/// What one run of the bone-attachment pass did. Counts rather than a log: a caller that only wants
/// the work done ignores them, and a test can pin them without reaching into the scene.
struct BoneAttachmentPassReportUVE final {
    /// Ticking attachments the pass looked at. An entity whose resolved process mode says it does not
    /// run is not looked at - and so is not counted - because nothing else about the scene moves for
    /// it either.
    std::size_t considered = 0U;
    /// Attachments that landed on a bone this pass and had their transform written.
    std::size_t bound = 0U;
    /// Looked-at attachments that did not: switched off, pointing at no skeleton, naming no bone,
    /// naming a bone the skeleton no longer has, or parented to something whose world frame cannot
    /// be inverted. Each of those keeps the transform it had - it is never moved somewhere
    /// arbitrary - and records the sentinel in `resolvedBoneIndex`.
    std::size_t unbound = 0U;
};

/// Puts every ticking BoneAttachment3D of a scene on its bone.
///
/// The skeleton is the entity the component's reference names, the bone is chosen by index when that
/// names a real bone and by exact name otherwise, the bone's world frame is composed down the bone
/// chain - this frame's pose where the runtime has one, else the rest pose the skeleton was imported
/// with - starting from the skeleton object's own world frame, and the object's LOCAL transform is
/// written so that the scene graph's own propagation lands it exactly there. That is what makes a
/// weapon ride a hand and a camera ride a head through the ordinary transform path, with no special
/// case anywhere else in the engine.
///
/// It reads the authored local transforms up each chain rather than the cached world transforms, so a
/// parent that moved earlier in the same frame still counts, and it is meant to be called after the
/// animation step that posed the skeleton and before SceneGraphUVE::UpdateUVE() propagates world
/// transforms - then an attachment is on the bone in the same frame the pose arrives.
///
/// Attachments resolve parents-first, so one that rides another (a scope on a rifle that is itself in
/// a hand) is composed after the one it hangs off. Entities keep the scene's fixed-step ordering:
/// physicsPriority first, then hierarchy depth, stably, so a scene that sets no priorities resolves
/// in the order its data was authored.
///
/// Writes nothing for an attachment that cannot resolve: the object keeps the transform it was
/// authored with, `bound` is cleared, and `resolvedBoneIndex` stays the sentinel.
[[nodiscard]] BoneAttachmentPassReportUVE SyncBoneAttachment3DObjectsUVE(IEntityManagerUVE& entityManager,
                                                                         const ISceneGraphUVE& sceneGraph);

} // namespace UVE::Scene
