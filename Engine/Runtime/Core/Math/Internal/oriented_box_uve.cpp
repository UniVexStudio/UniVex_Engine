// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/math/oriented_box_uve.h"

#include <string>

#include "uve/math/matrix4x4_uve.h"

namespace UVE::Math {

AabbUVE OrientedBoxUVE::ToAabbUVE() const noexcept {
    const Vector3UVE one{1.0F, 1.0F, 1.0F};
    const AabbUVE local{Vector3UVE{-halfExtents.x, -halfExtents.y, -halfExtents.z}, halfExtents};
    return local.TransformUVE(Matrix4x4UVE::ComposeTrsUVE(center, rotation, one));
}

std::string ToStringUVE(const OrientedBoxUVE& box) {
    return "OrientedBoxUVE(center=" + ToStringUVE(box.center) +
           ", halfExtents=" + ToStringUVE(box.halfExtents) +
           ", rotation=" + ToStringUVE(box.rotation) + ")";
}

} // namespace UVE::Math
