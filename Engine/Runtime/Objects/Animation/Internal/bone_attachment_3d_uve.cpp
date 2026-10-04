// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/objects/3d/bone_attachment_3d_uve.h"

#include <cmath>
#include <cstddef>
#include <vector>

#include "uve/objects/3d/skeleton_3d_uve.h"

namespace UVE::Scene {

namespace {

/// One bone's local frame for this frame: its pose when the runtime has one, else its rest pose.
/// Reading `pose` here rather than through GetSkeletonCurrentPoseUVE() keeps this allocation-free -
/// that helper builds a whole copy of the pose for callers that need every bone, and this needs one.
void GetBoneLocalTRSUVE(const Skeleton3DComponentUVE& skeleton, const std::size_t boneIndex,
                        Math::Vector3UVE& outPosition, Math::QuaternionUVE& outRotation,
                        Math::Vector3UVE& outScale) noexcept {
    const SkeletonBoneUVE& rest = skeleton.bones[boneIndex];
    if (boneIndex < skeleton.pose.size()) {
        outPosition = skeleton.pose[boneIndex].position;
        outRotation = skeleton.pose[boneIndex].rotation;
        outScale = skeleton.pose[boneIndex].scale;
        return;
    }
    outPosition = rest.localPosition;
    outRotation = rest.localRotation;
    outScale = rest.localScale;
}

} // namespace

bool IsBoneAttachment3DObjectComponentValidUVE(const BoneAttachment3DComponentUVE& value) noexcept {
    return IsBounded3DObjectStringUVE(value.boneName) && IsFinite3DObjectVectorUVE(value.localPosition) &&
           IsFinite3DObjectQuaternionUVE(value.localRotation) && IsFinite3DObjectVectorUVE(value.localScale) &&
           value.localScale.x > 0.0F && value.localScale.y > 0.0F && value.localScale.z > 0.0F;
}

bool TryFindSkeletonBoneIndexUVE(const Skeleton3DComponentUVE& skeleton, const std::string_view boneName,
                                 std::uint32_t& outIndex) noexcept {
    if (boneName.empty()) {
        return false;
    }
    for (std::size_t index = 0U; index < skeleton.bones.size(); ++index) {
        if (skeleton.bones[index].name == boneName) {
            outIndex = static_cast<std::uint32_t>(index);
            return true;
        }
    }
    return false;
}

bool TryResolveSkeletonBoneWorldFrameUVE(const Skeleton3DComponentUVE& skeleton, const std::uint32_t boneIndex,
                                         const ObjectWorldFrameUVE& skeletonWorldFrame,
                                         ObjectWorldFrameUVE& outFrame) noexcept {
    outFrame = ObjectWorldFrameUVE{};
    if (skeleton.bones.empty() || boneIndex >= skeleton.bones.size()) {
        return false;
    }
    if (!IsFinite3DObjectVectorUVE(skeletonWorldFrame.position) ||
        !IsFinite3DObjectVectorUVE(skeletonWorldFrame.scale)) {
        return false;
    }

    // Collected root-first, then composed downwards: a bone's local transform is relative to its
    // parent BONE, so the chain has to be walked from the root no matter which bone was asked for.
    std::vector<std::uint32_t> chain;
    chain.reserve(skeleton.bones.size());
    std::uint32_t current = boneIndex;
    for (std::size_t depth = 0U; depth < skeleton.bones.size() + 1U; ++depth) {
        chain.push_back(current);
        const Scene::SkeletonBoneUVE& bone = skeleton.bones[current];
        if (bone.parentIndex < 0) {
            break;
        }
        const auto parent = static_cast<std::uint32_t>(bone.parentIndex);
        if (parent >= skeleton.bones.size()) {
            // A parent index outside the array is a malformed skeleton; refusing here is what keeps
            // a corrupt asset from turning into an out-of-bounds read every frame.
            return false;
        }
        if (chain.size() > skeleton.bones.size()) {
            return false;
        }
        current = parent;
    }
    outFrame = skeletonWorldFrame;
    for (std::size_t index = chain.size(); index > 0U; --index) {
        const std::uint32_t bone = chain[index - 1U];
        Math::Vector3UVE localPosition{};
        Math::QuaternionUVE localRotation{};
        Math::Vector3UVE localScale{};
        GetBoneLocalTRSUVE(skeleton, bone, localPosition, localRotation, localScale);

        // Scale, then rotate, then translate - the order every TRS compose in this engine uses, so a
        // bone's own scale stretches the bones under it rather than the space it sits in.
        const Math::Vector3UVE scaled{localPosition.x * outFrame.scale.x, localPosition.y * outFrame.scale.y,
                                      localPosition.z * outFrame.scale.z};
        outFrame.position = outFrame.position + Math::RotateVectorUVE(outFrame.rotation, scaled);
        outFrame.rotation = Math::MultiplyUVE(outFrame.rotation, localRotation);
        outFrame.scale = Math::Vector3UVE{outFrame.scale.x * localScale.x, outFrame.scale.y * localScale.y,
                                          outFrame.scale.z * localScale.z};
    }
    return true;
}

ObjectWorldFrameUVE ComposeBoneAttachmentWorldFrameUVE(const ObjectWorldFrameUVE& boneFrame,
                                                             const Math::Vector3UVE& localPosition,
                                                             const Math::QuaternionUVE& localRotation,
                                                             const Math::Vector3UVE& localScale) noexcept {
    ObjectWorldFrameUVE frame{};
    const Math::Vector3UVE scaled{localPosition.x * boneFrame.scale.x, localPosition.y * boneFrame.scale.y,
                                  localPosition.z * boneFrame.scale.z};
    frame.position = boneFrame.position + Math::RotateVectorUVE(boneFrame.rotation, scaled);
    frame.rotation = Math::MultiplyUVE(boneFrame.rotation, localRotation);
    frame.scale = Math::Vector3UVE{boneFrame.scale.x * localScale.x, boneFrame.scale.y * localScale.y,
                                   boneFrame.scale.z * localScale.z};
    return frame;
}

bool TryMakeBoneAttachmentLocalTransformUVE(const ObjectWorldFrameUVE& attachmentWorld,
                                           const ObjectWorldFrameUVE& parentWorld,
                                           Math::Vector3UVE& outLocalPosition,
                                           Math::QuaternionUVE& outLocalRotation,
                                           Math::Vector3UVE& outLocalScale) noexcept {
    if (!IsFinite3DObjectVectorUVE(attachmentWorld.position) || !IsFinite3DObjectVectorUVE(attachmentWorld.scale) ||
        !IsFinite3DObjectQuaternionUVE(attachmentWorld.rotation) ||
        !IsFinite3DObjectVectorUVE(parentWorld.position) || !IsFinite3DObjectVectorUVE(parentWorld.scale) ||
        !IsFinite3DObjectQuaternionUVE(parentWorld.rotation)) {
        return false;
    }

    // A parent scale this close to zero has already flattened its children onto a plane; no local
    // transform reaches a point off that plane, and dividing by it would hand the renderer an
    // infinity. Refusing keeps the previous transform, which is what the engine does with every
    // other unusable world transform.
    constexpr float kMinimumParentScaleUVE = 1.0e-6F;
    if (std::abs(parentWorld.scale.x) < kMinimumParentScaleUVE ||
        std::abs(parentWorld.scale.y) < kMinimumParentScaleUVE ||
        std::abs(parentWorld.scale.z) < kMinimumParentScaleUVE) {
        return false;
    }

    Math::QuaternionUVE inverseParentRotation{};
    if (!Math::TryInverseUVE(parentWorld.rotation, inverseParentRotation)) {
        return false;
    }

    const Math::Vector3UVE unrotated = Math::RotateVectorUVE(inverseParentRotation,
                                                             attachmentWorld.position - parentWorld.position);
    const Math::Vector3UVE localPosition{unrotated.x / parentWorld.scale.x, unrotated.y / parentWorld.scale.y,
                                         unrotated.z / parentWorld.scale.z};
    Math::QuaternionUVE localRotation{};
    if (!Math::TryNormalizeUVE(Math::MultiplyUVE(inverseParentRotation, attachmentWorld.rotation), localRotation)) {
        return false;
    }
    const Math::Vector3UVE localScale{attachmentWorld.scale.x / parentWorld.scale.x,
                                      attachmentWorld.scale.y / parentWorld.scale.y,
                                      attachmentWorld.scale.z / parentWorld.scale.z};
    if (!IsFinite3DObjectVectorUVE(localPosition) || !IsFinite3DObjectVectorUVE(localScale)) {
        return false;
    }

    outLocalPosition = localPosition;
    outLocalRotation = localRotation;
    outLocalScale = localScale;
    return true;
}

} // namespace UVE::Scene
