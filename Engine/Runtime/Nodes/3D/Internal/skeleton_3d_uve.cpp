// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/nodes/3d/skeleton_3d_uve.h"

namespace UVE::Scene {

bool IsSkeleton3DNodeComponentValidUVE(const Skeleton3DNodeComponentUVE& value) noexcept {
    if (!IsBounded3DNodeStringUVE(value.skeletonAssetPath) || value.bones.size() > kMaximumSkeletonBonesUVE) {
        return false;
    }
    for (std::size_t index = 0U; index < value.bones.size(); ++index) {
        const SkeletonBoneUVE& bone = value.bones[index];
        if (!IsBounded3DNodeStringUVE(bone.name, false) || !IsFinite3DNodeVectorUVE(bone.localPosition) ||
            !IsFinite3DNodeQuaternionUVE(bone.localRotation) || !IsFinite3DNodeVectorUVE(bone.localScale) ||
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

} // namespace UVE::Scene
