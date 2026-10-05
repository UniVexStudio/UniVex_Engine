// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/objects/3d/skeleton_3d_uve.h"

#include "uve/entity/i_entity_manager_uve.h"
#include "uve/objects/3d/abstract_objects_3d_uve.h"

namespace UVE::Scene {

std::vector<SkeletonBonePoseUVE> GetSkeletonCurrentPoseUVE(const Skeleton3DComponentUVE& skeleton) {
    if (skeleton.pose.size() == skeleton.bones.size()) {
        return skeleton.pose;
    }
    std::vector<SkeletonBonePoseUVE> rest;
    rest.reserve(skeleton.bones.size());
    for (const SkeletonBoneUVE& bone : skeleton.bones) {
        rest.push_back(SkeletonBonePoseUVE{bone.localPosition, bone.localRotation, bone.localScale});
    }
    return rest;
}

bool IsSkeleton3DObjectComponentValidUVE(const Skeleton3DComponentUVE& value) noexcept {
    if (!IsBounded3DObjectStringUVE(value.skeletonAssetPath) || value.bones.size() > kMaximumSkeletonBonesUVE) {
        return false;
    }
    for (std::size_t index = 0U; index < value.bones.size(); ++index) {
        const SkeletonBoneUVE& bone = value.bones[index];
        if (!IsBounded3DObjectStringUVE(bone.name, false) || !IsFinite3DObjectVectorUVE(bone.localPosition) ||
            !IsFinite3DObjectQuaternionUVE(bone.localRotation) || !IsFinite3DObjectVectorUVE(bone.localScale) ||
            bone.localScale.x <= 0.0F || bone.localScale.y <= 0.0F || bone.localScale.z <= 0.0F ||
            (bone.parentIndex >= 0 && static_cast<std::size_t>(bone.parentIndex) >= index)) {
            return false;
        }
        for (std::size_t previous = 0U; previous < index; ++previous) {
            if (value.bones[previous].name == bone.name) {
                return false;
            }
        }
    }
    return true;
}

void ApplySkeleton3DObjectDefinitionUVE(IEntityManagerUVE& entityManager, const EntityUVE entity,
                                      const Skeleton3DObjectDefinitionUVE& value) {
    ApplyObject3DRecipeUVE(entityManager, entity, Skeleton3DObjectDefinitionUVE::defaultName);
    if (entityManager.IsAliveUVE(entity) && !entityManager.HasComponentUVE<Skeleton3DComponentUVE>(entity)) {
        entityManager.AddComponentUVE<Skeleton3DComponentUVE>(entity, value.skeleton);
    }
}

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
        const SkeletonBoneUVE& bone = skeleton.bones[current];
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

} // namespace UVE::Scene
