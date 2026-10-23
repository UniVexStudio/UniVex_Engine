// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/objects/3d/skeleton_3d_uve.h"

#include <algorithm>
#include <cctype>

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

namespace {

/// The name without its namespace: everything after the last `:` or `|`. Exporters put one of those
/// two between a namespace and a name, and a mesh and its skeleton routinely disagree about keeping
/// it - so this is the spelling both sides can be compared in.
[[nodiscard]] std::string_view UnqualifiedBoneNameUVE(const std::string_view name) noexcept {
    const std::size_t separator = name.find_last_of(":|");
    return separator == std::string_view::npos ? name : name.substr(separator + 1U);
}

[[nodiscard]] bool EqualsIgnoringCaseUVE(const std::string_view left, const std::string_view right) noexcept {
    if (left.size() != right.size()) {
        return false;
    }
    return std::equal(left.begin(), left.end(), right.begin(), [](const char a, const char b) {
        return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b));
    });
}

} // namespace

void SkeletonBoneLookupUVE::BuildUVE(const Skeleton3DComponentUVE& skeleton) {
    m_skeleton = &skeleton;
    m_boneOfName.clear();
    m_boneOfName.reserve(skeleton.bones.size());
    for (std::size_t index = 0U; index < skeleton.bones.size(); ++index) {
        // try_emplace, not emplace: the first bone of a duplicated name wins, which is the same
        // answer TryFindSkeletonBoneIndexUVE() gives and the only one that does not depend on
        // iteration order.
        m_boneOfName.try_emplace(skeleton.bones[index].name, static_cast<std::uint32_t>(index));
    }
}

bool SkeletonBoneLookupUVE::TryFindUVE(const std::string_view jointName,
                                       std::uint32_t& outBoneIndex) const noexcept {
    if (m_skeleton == nullptr || jointName.empty() || m_skeleton->bones.empty()) {
        return false;
    }
    if (const auto exact = m_boneOfName.find(jointName); exact != m_boneOfName.end()) {
        outBoneIndex = exact->second;
        return true;
    }
    const std::string_view unqualified = UnqualifiedBoneNameUVE(jointName);
    if (unqualified.empty()) {
        return false;
    }
    for (std::size_t index = 0U; index < m_skeleton->bones.size(); ++index) {
        if (EqualsIgnoringCaseUVE(UnqualifiedBoneNameUVE(m_skeleton->bones[index].name), unqualified)) {
            outBoneIndex = static_cast<std::uint32_t>(index);
            return true;
        }
    }
    return false;
}

bool TryBuildSkinFromMeshJointsUVE(const Asset::MeshAssetUVE& mesh, const Skeleton3DComponentUVE& skeleton,
                                   SkinComponentUVE& outSkin) {
    if (mesh.joints.empty() || skeleton.bones.empty()) {
        return false;
    }
    SkeletonBoneLookupUVE lookup;
    lookup.BuildUVE(skeleton);

    SkinComponentUVE candidate;
    candidate.boneBinds.reserve(mesh.joints.size());
    std::size_t boundJointCount = 0U;
    for (const Asset::MeshJointUVE& joint : mesh.joints) {
        SkinBoneBindUVE bind;
        bind.boneName = joint.name;
        bind.inverseBindPose = joint.inverseBindMatrix;
        std::uint32_t boneIndex = 0U;
        if (lookup.TryFindUVE(joint.name, boneIndex)) {
            bind.boneIndex = static_cast<std::int32_t>(boneIndex);
            ++boundJointCount;
        }
        candidate.boneBinds.push_back(std::move(bind));
    }
    if (boundJointCount == 0U) {
        // Not one name answered. Recording a Skin whose every entry is unbound would say the same
        // thing an absent Skin says, and would make an author hunting a naming mismatch look at a
        // binding that looks configured and draws nothing.
        return false;
    }
    candidate.bindCount = static_cast<std::uint32_t>(candidate.boneBinds.size());
    outSkin = std::move(candidate);
    return true;
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
