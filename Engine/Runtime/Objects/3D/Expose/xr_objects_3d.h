// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "entity.h"
#include "quaternion.h"
#include "vector3.h"
#include "object_3d_common.h"

namespace UVE::Scene {

class IEntityManager;

/// AR/VR tracking-space origin (`worldScale`, `currentOrigin`).
struct XROrigin3DComponent final {
    float worldScale = 1.0F;
    bool currentOrigin = true;

    [[nodiscard]] bool operator==(const XROrigin3DComponent&) const = default;
};

struct XROrigin3DObjectDefinition final {
    static constexpr std::string_view defaultName = "XROrigin3D";
    XROrigin3DComponent origin{};
};

void ApplyXROrigin3DObjectDefinition(IEntityManager& entityManager, Entity entity,
                                        const XROrigin3DObjectDefinition& value);

enum class XRTrackedHand : std::uint8_t {
    Head = 0,
    LeftHand = 1,
    RightHand = 2,
};

/// AR/VR tracked pose component used by `XRCamera3D` and `XRController3D`.
struct XRTracker3DComponent final {
    XRTrackedHand trackerRole = XRTrackedHand::Head;
    Math::Vector3 trackedLocalPosition{0.0F, 1.6F, 0.0F};
    Math::Quaternion trackedLocalRotation{};
    float triggerAxis = 0.0F;
    float gripAxis = 0.0F;
    bool trackingActive = true;

    [[nodiscard]] bool operator==(const XRTracker3DComponent&) const = default;
};

struct XRCamera3DObjectDefinition final {
    static constexpr std::string_view defaultName = "XRCamera3D";
    XRTracker3DComponent tracker{XRTrackedHand::Head, Math::Vector3{0.0F, 1.6F, 0.0F}};
};

struct XRController3DObjectDefinition final {
    static constexpr std::string_view defaultName = "XRController3D";
    XRTracker3DComponent tracker{XRTrackedHand::LeftHand,
                                    Math::Vector3{-0.25F, 1.1F, -0.4F}};
};

void ApplyXRCamera3DObjectDefinition(IEntityManager& entityManager, Entity entity,
                                        const XRCamera3DObjectDefinition& value);
void ApplyXRController3DObjectDefinition(IEntityManager& entityManager, Entity entity,
                                            const XRController3DObjectDefinition& value);

/// AR/VR facial expression blend-weight modifier (`targetMeshEntity`, `blendWeights`).
struct XRFaceModifier3DComponent final {
    Entity targetMeshEntity = kInvalidEntity;
    std::vector<float> blendWeights{};
    bool enabled = true;

    [[nodiscard]] bool operator==(const XRFaceModifier3DComponent&) const = default;
};

struct XRFaceModifier3DObjectDefinition final {
    static constexpr std::string_view defaultName = "XRFaceModifier3D";
    XRFaceModifier3DComponent faceModifier{};
};

void ApplyXRFaceModifier3DObjectDefinition(IEntityManager& entityManager, Entity entity,
                                              const XRFaceModifier3DObjectDefinition& value);

/// Synchronizes all active `XRTracker3DComponent` entities into their `TransformComponent`
/// (scaled by the active `XROrigin3DComponent::worldScale`) and clamps
/// `XRFaceModifier3DComponent` blend weights to `[0, 1]`.
std::size_t UpdateXRObjects3D(IEntityManager& entityManager);

} // namespace UVE::Scene
