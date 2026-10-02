// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/nodes/3d/bone_attachment_3d_uve.h"

namespace UVE::Scene {

bool IsBoneAttachment3DNodeComponentValidUVE(const BoneAttachment3DNodeComponentUVE& value) noexcept {
    return IsBounded3DNodeStringUVE(value.boneName) && IsFinite3DNodeVectorUVE(value.localPosition) &&
           IsFinite3DNodeQuaternionUVE(value.localRotation) && IsFinite3DNodeVectorUVE(value.localScale) &&
           value.localScale.x > 0.0F && value.localScale.y > 0.0F && value.localScale.z > 0.0F;
}

} // namespace UVE::Scene
