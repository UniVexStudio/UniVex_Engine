// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "skeleton_3d.h"

#include <algorithm>
#include <cctype>

#include "i_entity_manager.h"
#include "abstract_objects_3d.h"

namespace UVE::Scene {

std::vector<SkeletonBonePose> GetSkeletonCurrentPose(const Skeleton3DComponent& skeleton) {
    if (skeleton.pose.size() == skeleton.bones.size()) {
        return skeleton.pose;
    }
    std::vector<SkeletonBonePose> rest;
    rest.reserve(skeleton.bones.size());
    for (const SkeletonBone& bone : skeleton.bones) {
        rest.push_back(SkeletonBonePose{bone.localPosition, bone.localRotation, bone.localScale});
    }
    return rest;
}

bool IsSkeleton3DObjectComponentValid(const Skeleton3DComponent& value) noexcept {
    if (!IsBounded3DObjectString(value.skeletonAssetPath) || value.bones.size() > kMaximumSkeletonBones) {
        return false;
    }
    for (std::size_t index = 0U; index < value.bones.size(); ++index) {
        const SkeletonBone& bone = value.bones[index];
        if (!IsBounded3DObjectString(bone.name, false) || !IsFinite3DObjectVector(bone.localPosition) ||
            !IsFinite3DObjectQuaternion(bone.localRotation) || !IsFinite3DObjectVector(bone.localScale) ||
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

void ApplySkeleton3DObjectDefinition(IEntityManager& entityManager, const Entity entity,
                                      const Skeleton3DObjectDefinition& value) {
    ApplyObject3DRecipe(entityManager, entity, Skeleton3DObjectDefinition::defaultName);
    if (entityManager.IsAlive(entity) && !entityManager.HasComponent<Skeleton3DComponent>(entity)) {
        entityManager.AddComponent<Skeleton3DComponent>(entity, value.skeleton);
    }
}

namespace {

/// One bone's local frame for this frame: its pose when the runtime has one, else its rest pose.
/// Reading `pose` here rather than through GetSkeletonCurrentPose() keeps this allocation-free -
/// that helper builds a whole copy of the pose for callers that need every bone, and this needs one.
void GetBoneLocalTRS(const Skeleton3DComponent& skeleton, const std::size_t boneIndex,
                        Math::Vector3& outPosition, Math::Quaternion& outRotation,
                        Math::Vector3& outScale) noexcept {
    const SkeletonBone& rest = skeleton.bones[boneIndex];
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

bool TryFindSkeletonBoneIndex(const Skeleton3DComponent& skeleton, const std::string_view boneName,
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
[[nodiscard]] std::string_view UnqualifiedBoneName(const std::string_view name) noexcept {
    const std::size_t separator = name.find_last_of(":|");
    return separator == std::string_view::npos ? name : name.substr(separator + 1U);
}

[[nodiscard]] bool EqualsIgnoringCase(const std::string_view left, const std::string_view right) noexcept {
    if (left.size() != right.size()) {
        return false;
    }
    return std::equal(left.begin(), left.end(), right.begin(), [](const char a, const char b) {
        return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b));
    });
}

} // namespace

void SkeletonBoneLookup::Build(const Skeleton3DComponent& skeleton) {
    m_skeleton = &skeleton;
    m_boneOfName.clear();
    m_boneOfName.reserve(skeleton.bones.size());
    for (std::size_t index = 0U; index < skeleton.bones.size(); ++index) {
        // try_emplace, not emplace: the first bone of a duplicated name wins, which is the same
        // answer TryFindSkeletonBoneIndex() gives and the only one that does not depend on
        // iteration order.
        m_boneOfName.try_emplace(skeleton.bones[index].name, static_cast<std::uint32_t>(index));
    }
}

bool SkeletonBoneLookup::TryFind(const std::string_view jointName,
                                       std::uint32_t& outBoneIndex) const noexcept {
    if (m_skeleton == nullptr || jointName.empty() || m_skeleton->bones.empty()) {
        return false;
    }
    if (const auto exact = m_boneOfName.find(jointName); exact != m_boneOfName.end()) {
        outBoneIndex = exact->second;
        return true;
    }
    const std::string_view unqualified = UnqualifiedBoneName(jointName);
    if (unqualified.empty()) {
        return false;
    }
    for (std::size_t index = 0U; index < m_skeleton->bones.size(); ++index) {
        if (EqualsIgnoringCase(UnqualifiedBoneName(m_skeleton->bones[index].name), unqualified)) {
            outBoneIndex = static_cast<std::uint32_t>(index);
            return true;
        }
    }
    return false;
}

bool TryBuildSkinFromMeshJoints(const Asset::MeshAsset& mesh, const Skeleton3DComponent& skeleton,
                                   SkinComponent& outSkin) {
    if (mesh.joints.empty() || skeleton.bones.empty()) {
        return false;
    }
    SkeletonBoneLookup lookup;
    lookup.Build(skeleton);

    SkinComponent candidate;
    candidate.boneBinds.reserve(mesh.joints.size());
    std::size_t boundJointCount = 0U;
    for (const Asset::MeshJoint& joint : mesh.joints) {
        SkinBoneBind bind;
        bind.boneName = joint.name;
        bind.inverseBindPose = joint.inverseBindMatrix;
        std::uint32_t boneIndex = 0U;
        if (lookup.TryFind(joint.name, boneIndex)) {
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

bool TryResolveSkeletonBoneWorldFrame(const Skeleton3DComponent& skeleton, const std::uint32_t boneIndex,
                                         const ObjectWorldFrame& skeletonWorldFrame,
                                         ObjectWorldFrame& outFrame) noexcept {
    outFrame = ObjectWorldFrame{};
    if (skeleton.bones.empty() || boneIndex >= skeleton.bones.size()) {
        return false;
    }
    if (!IsFinite3DObjectVector(skeletonWorldFrame.position) ||
        !IsFinite3DObjectVector(skeletonWorldFrame.scale)) {
        return false;
    }

    // Collected root-first, then composed downwards: a bone's local transform is relative to its
    // parent BONE, so the chain has to be walked from the root no matter which bone was asked for.
    std::vector<std::uint32_t> chain;
    chain.reserve(skeleton.bones.size());
    std::uint32_t current = boneIndex;
    for (std::size_t depth = 0U; depth < skeleton.bones.size() + 1U; ++depth) {
        chain.push_back(current);
        const SkeletonBone& bone = skeleton.bones[current];
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
        Math::Vector3 localPosition{};
        Math::Quaternion localRotation{};
        Math::Vector3 localScale{};
        GetBoneLocalTRS(skeleton, bone, localPosition, localRotation, localScale);

        // Scale, then rotate, then translate - the order every TRS compose in this engine uses, so a
        // bone's own scale stretches the bones under it rather than the space it sits in.
        const Math::Vector3 scaled{localPosition.x * outFrame.scale.x, localPosition.y * outFrame.scale.y,
                                      localPosition.z * outFrame.scale.z};
        outFrame.position = outFrame.position + Math::RotateVector(outFrame.rotation, scaled);
        outFrame.rotation = Math::Multiply(outFrame.rotation, localRotation);
        outFrame.scale = Math::Vector3{outFrame.scale.x * localScale.x, outFrame.scale.y * localScale.y,
                                          outFrame.scale.z * localScale.z};
    }
    return true;
}

} // namespace UVE::Scene
