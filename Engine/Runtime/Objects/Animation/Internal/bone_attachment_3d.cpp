// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "bone_attachment_3d.h"

#include "trs.h"

#include <cmath>
#include <cstddef>

namespace UVE::Scene {

bool IsBoneAttachment3DObjectComponentValid(const BoneAttachment3DComponent& value) noexcept {
    return IsBounded3DObjectString(value.boneName) && IsFinite3DObjectVector(value.localPosition) &&
           IsFinite3DObjectQuaternion(value.localRotation) && IsFinite3DObjectVector(value.localScale) &&
           value.localScale.x > 0.0F && value.localScale.y > 0.0F && value.localScale.z > 0.0F;
}

ObjectWorldFrame ComposeBoneAttachmentWorldFrame(const ObjectWorldFrame& boneFrame,
                                                             const Math::Vector3& localPosition,
                                                             const Math::Quaternion& localRotation,
                                                             const Math::Vector3& localScale) noexcept {
    const Math::Trs composed = Math::Compose(
        Math::Trs{boneFrame.position, boneFrame.rotation, boneFrame.scale},
        Math::Trs{localPosition, localRotation, localScale});
    ObjectWorldFrame frame{};
    frame.position = composed.translation;
    frame.rotation = composed.rotation;
    frame.scale = composed.scale;
    return frame;
}

bool TryMakeBoneAttachmentLocalTransform(const ObjectWorldFrame& attachmentWorld,
                                           const ObjectWorldFrame& parentWorld,
                                           Math::Vector3& outLocalPosition,
                                           Math::Quaternion& outLocalRotation,
                                           Math::Vector3& outLocalScale) noexcept {
    if (!IsFinite3DObjectVector(attachmentWorld.position) || !IsFinite3DObjectVector(attachmentWorld.scale) ||
        !IsFinite3DObjectQuaternion(attachmentWorld.rotation) ||
        !IsFinite3DObjectVector(parentWorld.position) || !IsFinite3DObjectVector(parentWorld.scale) ||
        !IsFinite3DObjectQuaternion(parentWorld.rotation)) {
        return false;
    }

    // A parent scale this close to zero has already flattened its children onto a plane; no local
    // transform reaches a point off that plane, and dividing by it would hand the renderer an
    // infinity. Refusing keeps the previous transform, which is what the engine does with every
    // other unusable world transform.
    constexpr float kMinimumParentScale = 1.0e-6F;
    if (std::abs(parentWorld.scale.x) < kMinimumParentScale ||
        std::abs(parentWorld.scale.y) < kMinimumParentScale ||
        std::abs(parentWorld.scale.z) < kMinimumParentScale) {
        return false;
    }

    Math::Quaternion inverseParentRotation{};
    if (!Math::TryInverse(parentWorld.rotation, inverseParentRotation)) {
        return false;
    }

    const Math::Trs parentTrs{parentWorld.position, parentWorld.rotation, parentWorld.scale};
    Math::Vector3 localPosition{};
    if (!Math::TryInverseTransformPoint(parentTrs, attachmentWorld.position, localPosition)) {
        return false;
    }
    Math::Quaternion localRotation{};
    if (!Math::TryNormalize(Math::Multiply(inverseParentRotation, attachmentWorld.rotation), localRotation)) {
        return false;
    }
    const Math::Vector3 localScale{attachmentWorld.scale.x / parentWorld.scale.x,
                                      attachmentWorld.scale.y / parentWorld.scale.y,
                                      attachmentWorld.scale.z / parentWorld.scale.z};
    if (!IsFinite3DObjectVector(localPosition) || !IsFinite3DObjectVector(localScale)) {
        return false;
    }

    outLocalPosition = localPosition;
    outLocalRotation = localRotation;
    outLocalScale = localScale;
    return true;
}

} // namespace UVE::Scene
