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
    float shadowSplitBlend = 0.6F;

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
