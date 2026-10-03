// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/objects/3d/bone_attachment_3d_uve.h"

namespace UVE::Scene {

bool IsBoneAttachment3DObjectComponentValidUVE(const BoneAttachment3DComponentUVE& value) noexcept {
    return IsBounded3DObjectStringUVE(value.boneName) && IsFinite3DObjectVectorUVE(value.localPosition) &&
           IsFinite3DObjectQuaternionUVE(value.localRotation) && IsFinite3DObjectVectorUVE(value.localScale) &&
           value.localScale.x > 0.0F && value.localScale.y > 0.0F && value.localScale.z > 0.0F;
}

} // namespace UVE::Scene
