// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/objects/3d/bone_attachment_3d_uve.h"

#include <cmath>
#include <cstddef>

namespace UVE::Scene {

bool IsBoneAttachment3DObjectComponentValidUVE(const BoneAttachment3DComponentUVE& value) noexcept {
    return IsBounded3DObjectStringUVE(value.boneName) && IsFinite3DObjectVectorUVE(value.localPosition) &&
           IsFinite3DObjectQuaternionUVE(value.localRotation) && IsFinite3DObjectVectorUVE(value.localScale) &&
           value.localScale.x > 0.0F && value.localScale.y > 0.0F && value.localScale.z > 0.0F;
}

ObjectWorldFrameUVE ComposeBoneAttachmentWorldFrameUVE(const ObjectWorldFrameUVE& boneFrame,
                                                             const Math::Vector3UVE& localPosition,
                                                             const Math::QuaternionUVE& localRotation,
                                                             const Math::Vector3UVE& localScale) noexcept {
    ObjectWorldFrameUVE frame{};
    const Math::Vector3UVE scaled{localPosition.x * boneFrame.scale.x, localPosition.y * boneFrame.scale.y,
                                  localPosition.z * boneFrame.scale.z};
    frame.position = boneFrame.position + Math::RotateVectorUVE(boneFrame.rotation, scaled);
    frame.rotation = Math::MultiplyUVE(boneFrame.rotation, localRotation);
    frame.scale = Math::Vector3UVE{boneFrame.scale.x * localScale.x, boneFrame.scale.y * localScale.y,
                                   boneFrame.scale.z * localScale.z};
    return frame;
}

bool TryMakeBoneAttachmentLocalTransformUVE(const ObjectWorldFrameUVE& attachmentWorld,
                                           const ObjectWorldFrameUVE& parentWorld,
                                           Math::Vector3UVE& outLocalPosition,
                                           Math::QuaternionUVE& outLocalRotation,
                                           Math::Vector3UVE& outLocalScale) noexcept {
    if (!IsFinite3DObjectVectorUVE(attachmentWorld.position) || !IsFinite3DObjectVectorUVE(attachmentWorld.scale) ||
        !IsFinite3DObjectQuaternionUVE(attachmentWorld.rotation) ||
        !IsFinite3DObjectVectorUVE(parentWorld.position) || !IsFinite3DObjectVectorUVE(parentWorld.scale) ||
        !IsFinite3DObjectQuaternionUVE(parentWorld.rotation)) {
        return false;
    }

    // A parent scale this close to zero has already flattened its children onto a plane; no local
    // transform reaches a point off that plane, and dividing by it would hand the renderer an
    // infinity. Refusing keeps the previous transform, which is what the engine does with every
    // other unusable world transform.
    constexpr float kMinimumParentScaleUVE = 1.0e-6F;
    if (std::abs(parentWorld.scale.x) < kMinimumParentScaleUVE ||
        std::abs(parentWorld.scale.y) < kMinimumParentScaleUVE ||
        std::abs(parentWorld.scale.z) < kMinimumParentScaleUVE) {
        return false;
    }

    Math::QuaternionUVE inverseParentRotation{};
    if (!Math::TryInverseUVE(parentWorld.rotation, inverseParentRotation)) {
        return false;
    }

    const Math::Vector3UVE unrotated = Math::RotateVectorUVE(inverseParentRotation,
                                                             attachmentWorld.position - parentWorld.position);
    const Math::Vector3UVE localPosition{unrotated.x / parentWorld.scale.x, unrotated.y / parentWorld.scale.y,
                                         unrotated.z / parentWorld.scale.z};
    Math::QuaternionUVE localRotation{};
    if (!Math::TryNormalizeUVE(Math::MultiplyUVE(inverseParentRotation, attachmentWorld.rotation), localRotation)) {
        return false;
    }
    const Math::Vector3UVE localScale{attachmentWorld.scale.x / parentWorld.scale.x,
                                      attachmentWorld.scale.y / parentWorld.scale.y,
                                      attachmentWorld.scale.z / parentWorld.scale.z};
    if (!IsFinite3DObjectVectorUVE(localPosition) || !IsFinite3DObjectVectorUVE(localScale)) {
        return false;
    }

    outLocalPosition = localPosition;
    outLocalRotation = localRotation;
    outLocalScale = localScale;
    return true;
}

} // namespace UVE::Scene
