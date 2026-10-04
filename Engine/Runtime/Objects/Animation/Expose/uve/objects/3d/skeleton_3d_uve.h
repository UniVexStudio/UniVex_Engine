// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "uve/component/entity_uve.h"
#include "uve/math/vector3_uve.h"
#include "uve/math/quaternion_uve.h"
#include "uve/objects/3d/object_3d_common_uve.h"

namespace UVE::Scene {

class IEntityManagerUVE;

inline constexpr std::size_t kMaximumSkeletonBonesUVE = 256U;

/// The sentinel bone index: "no bone named or resolved". Kept as a named constant because the
/// components built on a skeleton carry two of these (the authored request and the runtime answer)
/// and a bare `UINT32_MAX` in either place reads as an accident.
inline constexpr std::uint32_t kInvalidSkeletonBoneIndexUVE = std::numeric_limits<std::uint32_t>::max();

struct SkeletonBoneUVE final {
    std::string name;
    std::int32_t parentIndex = -1;
    Math::Vector3UVE localPosition{};
    Math::QuaternionUVE localRotation{};
    Math::Vector3UVE localScale{1.0F, 1.0F, 1.0F};

    [[nodiscard]] bool operator==(const SkeletonBoneUVE& other) const noexcept {
        return name == other.name && parentIndex == other.parentIndex &&
               localPosition.x == other.localPosition.x && localPosition.y == other.localPosition.y &&
               localPosition.z == other.localPosition.z && localRotation.x == other.localRotation.x &&
               localRotation.y == other.localRotation.y && localRotation.z == other.localRotation.z &&
               localRotation.w == other.localRotation.w && localScale.x == other.localScale.x &&
               localScale.y == other.localScale.y && localScale.z == other.localScale.z;
    }
};

/// Skeleton3D: a bone hierarchy in its rest pose. A new one is empty - bones are authored in a DCC
/// tool (a Blender armature, say) and arrive with the model they were exported with; this object
/// never invents them. `skeletonAssetPath` names that model, content-relative.
/// One bone's pose right now: local to its parent bone, like SkeletonBoneUVE's rest pose.
struct SkeletonBonePoseUVE final {
    Math::Vector3UVE position{};
    Math::QuaternionUVE rotation{};
    Math::Vector3UVE scale{1.0F, 1.0F, 1.0F};
};

struct Skeleton3DComponentUVE final {
    std::string skeletonAssetPath;
    std::vector<SkeletonBoneUVE> bones;
    bool enabled = true;

    // ---- Runtime state, written by animation; never saved --------------------------------------
    /// One entry per bone while something animates the skeleton; empty means the rest pose.
    std::vector<SkeletonBonePoseUVE> pose;

    /// Authored data only: a playing skeleton equals its saved self.
    [[nodiscard]] bool operator==(const Skeleton3DComponentUVE& other) const {
        return skeletonAssetPath == other.skeletonAssetPath && bones == other.bones && enabled == other.enabled;
    }
};

/// The pose a renderer should draw: `pose` when it covers every bone, else the rest pose.
[[nodiscard]] std::vector<SkeletonBonePoseUVE> GetSkeletonCurrentPoseUVE(const Skeleton3DComponentUVE& skeleton);

[[nodiscard]] bool IsSkeleton3DObjectComponentValidUVE(const Skeleton3DComponentUVE& value) noexcept;

/// A world-space TRS frame: the same {position, rotation, scale} a TransformComponentUVE carries,
/// but composed all the way down a chain rather than authored on one object.
///
/// It is named for the shape, not for the owner, because several of them meet in one resolution -
/// the skeleton's, the bone's, and the object's - and they are interchangeable values. The scene
/// graph composes local transforms into exactly these, TryResolveSkeletonBoneWorldFrameUVE()
/// composes a bone chain into one, and TryMakeBoneAttachmentLocalTransformUVE() inverts that
/// arithmetic to go back the other way.
struct ObjectWorldFrameUVE final {
    Math::Vector3UVE position{};
    Math::QuaternionUVE rotation{};
    Math::Vector3UVE scale{1.0F, 1.0F, 1.0F};
};

/// Finds a bone by name, writing its index to `outIndex`. Exact, case-sensitive match; the first
/// bone with that name wins (duplicate names in one skeleton are an asset defect, and picking the
/// first is the only answer that does not depend on iteration order).
[[nodiscard]] bool TryFindSkeletonBoneIndexUVE(const Skeleton3DComponentUVE& skeleton,
                                               std::string_view boneName,
                                               std::uint32_t& outIndex) noexcept;

/// The world frame of one bone, given the skeleton object's own world frame.
///
/// Walks the bone's parent chain from the root down - the bone chain's own order, not the
/// skeleton's storage order, because a bone's local transform is relative to its PARENT BONE - and
/// composes each step: the posed local transform when the runtime pose covers that bone, else the
/// rest pose the skeleton was imported with. A chain that loops, leaves the bone array, or is
/// deeper than the bone limit is refused rather than walked forever.
/// The skeleton's own world frame is passed as a frame rather than as three loose values on
/// purpose: an empty `{}` for a scale would silently mean zero and collapse every bone to a point,
/// while an empty ObjectWorldFrameUVE means the identity - the frame a skeleton at the origin is in.
[[nodiscard]] bool TryResolveSkeletonBoneWorldFrameUVE(const Skeleton3DComponentUVE& skeleton,
                                                       std::uint32_t boneIndex,
                                                       const ObjectWorldFrameUVE& skeletonWorldFrame,
                                                       ObjectWorldFrameUVE& outFrame) noexcept;

/// Applies only an explicit authored/imported skeleton asset payload. This function deliberately
/// rejects an empty asset or empty hierarchy so retarget metadata cannot hydrate a Skeleton3D object.
[[nodiscard]] inline bool TryBindExplicitSkeleton3DAssetUVE(
    Skeleton3DComponentUVE& target, std::string assetPath, std::vector<SkeletonBoneUVE> bones) {
    Skeleton3DComponentUVE candidate;
    candidate.skeletonAssetPath = std::move(assetPath);
    candidate.bones = std::move(bones);
    candidate.enabled = target.enabled;
    if (candidate.skeletonAssetPath.empty() || candidate.bones.empty() ||
        !IsSkeleton3DObjectComponentValidUVE(candidate)) {
        return false;
    }
    target = std::move(candidate);
    return true;
}

struct Skeleton3DObjectDefinitionUVE final {
    static constexpr std::string_view defaultName = "Skeleton3D";
    Skeleton3DComponentUVE skeleton{};
};

/// The Object3D recipe under this object's name, then the skeleton component.
void ApplySkeleton3DObjectDefinitionUVE(IEntityManagerUVE& entityManager, EntityUVE entity,
                                      const Skeleton3DObjectDefinitionUVE& value);

} // namespace UVE::Scene
