// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <string_view>
#include <vector>

#include "uve/component/entity_uve.h"
#include "uve/component/light_emitter_component_uve.h"
#include "uve/math/vector3_uve.h"

namespace UVE::Scene {

class IEntityManagerUVE;

/// A light from infinitely far away that falls on everything along the object's -Z: the sun, the
/// moon. Its colour, energy and shadow switch come from LightEmitter3D; what is its own is how far
/// its shadow reaches and how the shadow's cascades are shared out over that distance.
struct DirectionalLight3DComponentUVE final {
    /// Shadows are drawn up to this far from the camera, in metres; 0 follows the camera's far plane.
    float shadowMaxDistance = 100.0F;
    /// How the cascades are spread over that distance: 0 evenly, 1 packed near the camera.
    /// A negative value inherits EngineConfigUVE::shadowCascadeSplitLambda / the project shadow default.
    float shadowSplitBlend = -1.0F;
    /// Width, in metres, over which shadows fade to fully lit at the end of the final cascade. 0 disables it.
    float shadowDistanceFadeRange = 10.0F;

    /// The sun disc the sky draws where this light's direction points - moon, sun, or neither.
    ///
    /// WHY THE DISC LIVES ON THE LIGHT AND NOT ON THE SKY. A disc is the visible body of a light
    /// source. Two directional lights in one scene are two bodies in the sky, and a body must sit
    /// where its own light points and be as bright as its own emitter says. Putting the disc on the
    /// sky would make it belong to the background instead, so a moonlit night and a noon over the
    /// same sky could not differ, and turning a light off would leave its disc behind.
    ///
    /// 3.8 degrees reproduces the disc the sky shader drew before this was authorable, so an
    /// existing scene is unchanged. The real sun is 0.53; setting that gives a physically sized
    /// disc, at the cost of it being small on screen. 0 hides the disc entirely.
    float discAngularDiameterDegrees = 3.8F;
    /// Peak brightness of the disc, multiplying what its colour and energy already give it.
    float discIntensity = 1.0F;
    /// How much dimmer the disc's rim is than its centre, 0 being a flat disc. The real sun is
    /// around 0.6 at visible wavelengths; a moon is close to 0.85.
    float discLimbDarkening = 0.0F;

    [[nodiscard]] bool operator==(const DirectionalLight3DComponentUVE&) const = default;
};

[[nodiscard]] bool IsDirectionalLight3DComponentValidUVE(const DirectionalLight3DComponentUVE& value) noexcept;

struct DirectionalLight3DObjectDefinitionUVE final {
    static constexpr std::string_view defaultName = "DirectionalLight3D";
    DirectionalLight3DComponentUVE light{};
    /// A sun casts shadows unless told otherwise.
    LightEmitterComponentUVE emitter = [] {
        LightEmitterComponentUVE value{};
        value.shadowEnabled = true;
        return value;
    }();
};

/// LightEmitter3D's recipe (RenderInstance3D, Object3D, Object), then this object's own component.
/// Components already on the entity keep their values.
void ApplyDirectionalLight3DObjectDefinitionUVE(IEntityManagerUVE& entityManager, EntityUVE entity,
                                              const DirectionalLight3DObjectDefinitionUVE& value);

struct LightDirectionGizmoUVE final {
    Math::Vector3UVE origin{};
    Math::Vector3UVE direction{0.0F, 0.0F, -1.0F};
    Math::Vector3UVE color{1.0F, 1.0F, 1.0F};
};

void CollectLightDirectionGizmosUVE(IEntityManagerUVE& entityManager, std::vector<LightDirectionGizmoUVE>& out);

} // namespace UVE::Scene
