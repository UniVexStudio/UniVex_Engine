// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "uve/component/entity_uve.h"
#include "uve/math/quaternion_uve.h"
#include "uve/math/vector3_uve.h"
#include "uve/objects/3d/object_3d_common_uve.h"

namespace UVE::Scene {

class IEntityManagerUVE;

/// AR/VR tracking-space origin (`worldScale`, `currentOrigin`).
struct XROrigin3DComponentUVE final {
    float worldScale = 1.0F;
    bool currentOrigin = true;

    [[nodiscard]] bool operator==(const XROrigin3DComponentUVE&) const = default;
};

struct XROrigin3DObjectDefinitionUVE final {
    static constexpr std::string_view defaultName = "XROrigin3D";
    XROrigin3DComponentUVE origin{};
};

void ApplyXROrigin3DObjectDefinitionUVE(IEntityManagerUVE& entityManager, EntityUVE entity,
                                        const XROrigin3DObjectDefinitionUVE& value);

enum class XRTrackedHandUVE : std::uint8_t {
    Head = 0,
    LeftHand = 1,
    RightHand = 2,
};

/// AR/VR tracked pose component used by `XRCamera3D` and `XRController3D`.
struct XRTracker3DComponentUVE final {
    XRTrackedHandUVE trackerRole = XRTrackedHandUVE::Head;
    Math::Vector3UVE trackedLocalPosition{0.0F, 1.6F, 0.0F};
    Math::QuaternionUVE trackedLocalRotation{};
    float triggerAxis = 0.0F;
    float gripAxis = 0.0F;
    bool trackingActive = true;

    [[nodiscard]] bool operator==(const XRTracker3DComponentUVE&) const = default;
};

struct XRCamera3DObjectDefinitionUVE final {
    static constexpr std::string_view defaultName = "XRCamera3D";
    XRTracker3DComponentUVE tracker{XRTrackedHandUVE::Head, Math::Vector3UVE{0.0F, 1.6F, 0.0F}};
};

struct XRController3DObjectDefinitionUVE final {
    static constexpr std::string_view defaultName = "XRController3D";
    XRTracker3DComponentUVE tracker{XRTrackedHandUVE::LeftHand,
                                    Math::Vector3UVE{-0.25F, 1.1F, -0.4F}};
};

void ApplyXRCamera3DObjectDefinitionUVE(IEntityManagerUVE& entityManager, EntityUVE entity,
                                        const XRCamera3DObjectDefinitionUVE& value);
void ApplyXRController3DObjectDefinitionUVE(IEntityManagerUVE& entityManager, EntityUVE entity,
                                            const XRController3DObjectDefinitionUVE& value);

/// AR/VR facial expression blend-weight modifier (`targetMeshEntity`, `blendWeights`).
struct XRFaceModifier3DComponentUVE final {
    EntityUVE targetMeshEntity = kInvalidEntityUVE;
    std::vector<float> blendWeights{};
    bool enabled = true;

    [[nodiscard]] bool operator==(const XRFaceModifier3DComponentUVE&) const = default;
};

struct XRFaceModifier3DObjectDefinitionUVE final {
    static constexpr std::string_view defaultName = "XRFaceModifier3D";
    XRFaceModifier3DComponentUVE faceModifier{};
};

void ApplyXRFaceModifier3DObjectDefinitionUVE(IEntityManagerUVE& entityManager, EntityUVE entity,
                                              const XRFaceModifier3DObjectDefinitionUVE& value);

/// Synchronizes all active `XRTracker3DComponentUVE` entities into their `TransformComponentUVE`
/// (scaled by the active `XROrigin3DComponentUVE::worldScale`) and clamps
/// `XRFaceModifier3DComponentUVE` blend weights to `[0, 1]`.
std::size_t UpdateXRObjects3DUVE(IEntityManagerUVE& entityManager);

} // namespace UVE::Scene
