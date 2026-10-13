// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#include "uve/math/trs_uve.h"

#include <string>

namespace UVE::Math {

TrsUVE ComposeUVE(const TrsUVE& parent, const TrsUVE& child) noexcept {
    // Byte-identical expressions to the scene graph's long-standing parent/child composition
    // (SceneGraphUVE::UpdateUVE), so adopting this function cannot change a single world matrix.
    TrsUVE result;
    result.scale = parent.scale * child.scale;
    result.rotation = MultiplyUVE(parent.rotation, child.rotation);
    result.translation =
        parent.translation + RotateVectorUVE(parent.rotation, parent.scale * child.translation);
    return result;
}

Vector3UVE TransformPointUVE(const TrsUVE& trs, const Vector3UVE& point) noexcept {
    return trs.translation + RotateVectorUVE(trs.rotation, trs.scale * point);
}

Vector3UVE TransformDirectionUVE(const TrsUVE& trs, const Vector3UVE& direction) noexcept {
    return RotateVectorUVE(trs.rotation, trs.scale * direction);
}

bool TryInverseTransformPointUVE(const TrsUVE& trs, const Vector3UVE& point,
                                 Vector3UVE& outPoint) noexcept {
    if (!IsFiniteUVE(trs.translation) || !IsFiniteUVE(trs.rotation) || !IsFiniteUVE(trs.scale) ||
        !IsFiniteUVE(point)) {
        return false;
    }
    QuaternionUVE inverseRotation{};
    if (!TryInverseUVE(trs.rotation, inverseRotation)) {
        return false;
    }
    const Vector3UVE unrotated = RotateVectorUVE(inverseRotation, point - trs.translation);
    const Vector3UVE result{
        unrotated.x / trs.scale.x, unrotated.y / trs.scale.y, unrotated.z / trs.scale.z};
    if (!IsFiniteUVE(result)) {
        return false;
    }
    outPoint = result;
    return true;
}

bool TryInverseTransformDirectionUVE(const TrsUVE& trs, const Vector3UVE& direction,
                                     Vector3UVE& outDirection) noexcept {
    if (!IsFiniteUVE(trs.rotation) || !IsFiniteUVE(trs.scale) || !IsFiniteUVE(direction)) {
        return false;
    }
    QuaternionUVE inverseRotation{};
    if (!TryInverseUVE(trs.rotation, inverseRotation)) {
        return false;
    }
    const Vector3UVE unrotated = RotateVectorUVE(inverseRotation, direction);
    const Vector3UVE result{
        unrotated.x / trs.scale.x, unrotated.y / trs.scale.y, unrotated.z / trs.scale.z};
    if (!IsFiniteUVE(result)) {
        return false;
    }
    outDirection = result;
    return true;
}

bool TryInverseTransformRotationUVE(const TrsUVE& trs, const QuaternionUVE& worldDelta,
                                    QuaternionUVE& outLocalDelta) noexcept {
    if (!IsFiniteUVE(trs.rotation) || !IsFiniteUVE(worldDelta)) {
        return false;
    }
    // Byte-identical expressions to the viewport rotate-drag path's long-standing world-to-local
    // arithmetic (EditorUVE::ComputeLocalRotationForWorldAxisUVE), so adopting this function
    // cannot change a single drag: normalize the parent rotation, then conjugate the world delta
    // into the parent frame. Conjugation is scale-free - the translation and scale never enter.
    QuaternionUVE parentNormalized{};
    QuaternionUVE parentInverse{};
    if (!TryNormalizeUVE(trs.rotation, parentNormalized) ||
        !TryInverseUVE(parentNormalized, parentInverse)) {
        return false;
    }
    const QuaternionUVE result =
        MultiplyUVE(MultiplyUVE(parentInverse, worldDelta), parentNormalized);
    if (!IsFiniteUVE(result)) {
        return false;
    }
    outLocalDelta = result;
    return true;
}

std::string ToStringUVE(const TrsUVE& trs) {
    return "Trs(" + ToStringUVE(trs.translation) + ", " + ToStringUVE(trs.rotation) + ", " +
           ToStringUVE(trs.scale) + ")";
}

} // namespace UVE::Math
