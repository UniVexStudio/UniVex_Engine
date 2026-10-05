// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cstdint>
#include <string>
#include <string_view>

#include "uve/component/entity_uve.h"
#include "uve/math/quaternion_uve.h"
#include "uve/math/vector3_uve.h"
#include "uve/objects/3d/object_3d_common_uve.h"
#include "uve/objects/3d/skeleton_3d_uve.h"

namespace UVE::Scene {

/// BoneAttachment3D: an object that rides a bone of a skeleton instead of the hierarchy.
///
/// The component is authored data plus the answer to one question - which bone am I on - and the
/// engine resolves that answer every frame onto the object's own transform, so everything
/// downstream (a weapon in a hand, a camera on a head, an effect on a foot) works through the
/// ordinary transform path with no special cases in any other system.
struct BoneAttachment3DComponentUVE final {
    /// The skeleton this attachment hangs off, as a real entity reference.
    ///
    /// Serialized as the file-local id under the `skeletonLocalId` key this component has always
    /// used, and remapped back to an entity on load - exactly like AnimationDriverComponentUVE's
    /// target. A reference to something that is not a skeleton, or to nothing at all, leaves the
    /// object where it was authored rather than moving it somewhere meaningless.
    EntityUVE skeleton = kInvalidEntityUVE;

    /// Which bone, by index, in the skeleton's `bones`. Takes precedence over `boneName` when it
    /// names a bone that exists; the sentinel means "ask the name instead".
    std::uint32_t boneIndex = kInvalidSkeletonBoneIndexUVE;

    /// The bone's name, for a reference that survives the skeleton being re-exported with its bones
    /// in a different order. Case-sensitive, exact - the names come from the asset, not a human.
    std::string boneName;

    /// Where the object sits in the BONE's space. Position, then rotation, then scale, applied in
    /// the bone's frame: an attachment authored at (0, 0.1, 0) on a hand is 10 cm along that hand's
    /// own up axis, not along the world's.
    Math::Vector3UVE localPosition{};
    Math::QuaternionUVE localRotation{};
    Math::Vector3UVE localScale{1.0F, 1.0F, 1.0F};
    bool enabled = true;

    // ---- Runtime state, written by the resolver; never saved ------------------------------------
    /// The bone that was actually bound this frame: `boneIndex` when it named a real bone, else the
    /// index `boneName` resolved to, else the sentinel. This is what makes a name-based attachment
    /// observable - an author who renames a bone in the source file can see the attachment stop
    /// binding instead of silently riding a different bone.
    std::uint32_t resolvedBoneIndex = kInvalidSkeletonBoneIndexUVE;

    /// True while the object is following a bone this frame. False is the authored state: the
    /// object keeps the transform it was saved with.
    bool bound = false;

    /// Authored data only: an attachment that has not been ticked equals its saved self.
    [[nodiscard]] bool operator==(const BoneAttachment3DComponentUVE& other) const {
        return skeleton == other.skeleton && boneIndex == other.boneIndex && boneName == other.boneName &&
               localPosition.x == other.localPosition.x && localPosition.y == other.localPosition.y &&
               localPosition.z == other.localPosition.z && localRotation.x == other.localRotation.x &&
               localRotation.y == other.localRotation.y && localRotation.z == other.localRotation.z &&
               localRotation.w == other.localRotation.w && localScale.x == other.localScale.x &&
               localScale.y == other.localScale.y && localScale.z == other.localScale.z &&
               enabled == other.enabled;
    }
};

[[nodiscard]] bool IsBoneAttachment3DObjectComponentValidUVE(const BoneAttachment3DComponentUVE& value) noexcept;

/// Whether an attachment has enough to bind: enabled, pointed at a skeleton, and naming a bone by
/// index or by name. A default attachment is valid scene data and stays inert until this is true.
[[nodiscard]] inline bool IsBoneAttachment3DObjectComponentResolvableUVE(
    const BoneAttachment3DComponentUVE& value) noexcept {
    const bool hasSkeletonReference = value.skeleton != kInvalidEntityUVE;
    const bool hasBoneReference = value.boneIndex != kInvalidSkeletonBoneIndexUVE || !value.boneName.empty();
    return value.enabled && hasSkeletonReference && hasBoneReference;
}

/// Composes an attachment's authored local transform onto a bone's world frame: the world frame the
/// attachment object should take this frame.
[[nodiscard]] ObjectWorldFrameUVE ComposeBoneAttachmentWorldFrameUVE(
    const ObjectWorldFrameUVE& boneFrame, const Math::Vector3UVE& localPosition,
    const Math::QuaternionUVE& localRotation, const Math::Vector3UVE& localScale) noexcept;

/// The authored LOCAL transform that puts an object's world frame exactly on `attachmentWorld`,
/// relative to `parentWorld` - the inverse of the composition SceneGraphUVE::UpdateUVE() performs:
///
///     worldScale    = parentWorld.scale * localScale
///     worldRotation = parentWorld.rotation * localRotation
///     worldPosition = parentWorld.position + Rotate(parentWorld.rotation, parentWorld.scale * localPosition)
///
/// An attachment is an ordinary object in the hierarchy, so the world transform it needs is not
/// what it may store: the propagation pass recomposes every dirty object from its parent, and a
/// child written in world units would be moved twice by that parent. Writing the local inverse is
/// what makes the attachment follow a bone while still being a normal child - it can be parented to
/// anything, moved by a Character3D, or have children of its own.
///
/// Returns false, leaving the outputs untouched, when the inputs are not finite or the parent's
/// scale or rotation cannot be inverted (a zero-scale parent flattens its children onto a plane and
/// there is no local transform that reaches a point off it). Callers keep the previous transform in
/// that case, which is the recoverable outcome.
[[nodiscard]] bool TryMakeBoneAttachmentLocalTransformUVE(
    const ObjectWorldFrameUVE& attachmentWorld, const ObjectWorldFrameUVE& parentWorld,
    Math::Vector3UVE& outLocalPosition, Math::QuaternionUVE& outLocalRotation,
    Math::Vector3UVE& outLocalScale) noexcept;

} // namespace UVE::Scene
