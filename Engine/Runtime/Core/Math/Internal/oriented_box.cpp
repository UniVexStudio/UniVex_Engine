// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "oriented_box.h"

#include <string>

#include "matrix4x4.h"

namespace UVE::Math {

Aabb OrientedBox::ToAabb() const noexcept {
    const Vector3 one{1.0F, 1.0F, 1.0F};
    const Aabb local{Vector3{-halfExtents.x, -halfExtents.y, -halfExtents.z}, halfExtents};
    return local.Transform(Matrix4x4::ComposeTrs(center, rotation, one));
}

std::string ToString(const OrientedBox& box) {
    return "OrientedBox(center=" + ToString(box.center) +
           ", halfExtents=" + ToString(box.halfExtents) +
           ", rotation=" + ToString(box.rotation) + ")";
}

} // namespace UVE::Math
