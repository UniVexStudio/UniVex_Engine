// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#include "trs.h"

#include <string>

namespace UVE::Math {

Trs Compose(const Trs& parent, const Trs& child) noexcept {
    // Byte-identical expressions to the scene graph's long-standing parent/child composition
    // (SceneGraph::Update), so adopting this function cannot change a single world matrix.
    Trs result;
    result.scale = parent.scale * child.scale;
    result.rotation = Multiply(parent.rotation, child.rotation);
    result.translation =
        parent.translation + RotateVector(parent.rotation, parent.scale * child.translation);
    return result;
}

Vector3 TransformPoint(const Trs& trs, const Vector3& point) noexcept {
    return trs.translation + RotateVector(trs.rotation, trs.scale * point);
}

Vector3 TransformDirection(const Trs& trs, const Vector3& direction) noexcept {
    return RotateVector(trs.rotation, trs.scale * direction);
}

bool TryInverseTransformPoint(const Trs& trs, const Vector3& point,
                                 Vector3& outPoint) noexcept {
    if (!IsFinite(trs.translation) || !IsFinite(trs.rotation) || !IsFinite(trs.scale) ||
        !IsFinite(point)) {
        return false;
    }
    Quaternion inverseRotation{};
    if (!TryInverse(trs.rotation, inverseRotation)) {
        return false;
    }
    const Vector3 unrotated = RotateVector(inverseRotation, point - trs.translation);
    const Vector3 result{
        unrotated.x / trs.scale.x, unrotated.y / trs.scale.y, unrotated.z / trs.scale.z};
    if (!IsFinite(result)) {
        return false;
    }
    outPoint = result;
    return true;
}

bool TryInverseTransformDirection(const Trs& trs, const Vector3& direction,
                                     Vector3& outDirection) noexcept {
    if (!IsFinite(trs.rotation) || !IsFinite(trs.scale) || !IsFinite(direction)) {
        return false;
    }
    Quaternion inverseRotation{};
    if (!TryInverse(trs.rotation, inverseRotation)) {
        return false;
    }
    const Vector3 unrotated = RotateVector(inverseRotation, direction);
    const Vector3 result{
        unrotated.x / trs.scale.x, unrotated.y / trs.scale.y, unrotated.z / trs.scale.z};
    if (!IsFinite(result)) {
        return false;
    }
    outDirection = result;
    return true;
}

bool TryInverseTransformRotation(const Trs& trs, const Quaternion& worldDelta,
                                    Quaternion& outLocalDelta) noexcept {
    if (!IsFinite(trs.rotation) || !IsFinite(worldDelta)) {
        return false;
    }
    // Byte-identical expressions to the viewport rotate-drag path's long-standing world-to-local
    // arithmetic (Editor::ComputeLocalRotationForWorldAxis), so adopting this function
    // cannot change a single drag: normalize the parent rotation, then conjugate the world delta
    // into the parent frame. Conjugation is scale-free - the translation and scale never enter.
    Quaternion parentNormalized{};
    Quaternion parentInverse{};
    if (!TryNormalize(trs.rotation, parentNormalized) ||
        !TryInverse(parentNormalized, parentInverse)) {
        return false;
    }
    const Quaternion result =
        Multiply(Multiply(parentInverse, worldDelta), parentNormalized);
    if (!IsFinite(result)) {
        return false;
    }
    outLocalDelta = result;
    return true;
}

std::string ToString(const Trs& trs) {
    return "Trs(" + ToString(trs.translation) + ", " + ToString(trs.rotation) + ", " +
           ToString(trs.scale) + ")";
}

} // namespace UVE::Math
