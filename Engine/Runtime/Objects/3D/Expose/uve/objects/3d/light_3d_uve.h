// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <string_view>

#include "uve/component/entity_uve.h"
#include "uve/component/light_component_uve.h"

namespace UVE::Scene {

class IEntityManagerUVE;

/// Authoring definition for the Light3D scene object: the component set and defaults a freshly
/// created Light3D entity attaches — one directional light (the classic "sun light" the legacy
/// editor kind produced; Point and Spot stay one inspector edit away, since LightComponentUVE
/// always carries the full record and never branches on which fields are meaningful). This
/// recipe used to be hardcoded in EditorUVE's creation switch behind the legacy
/// EditorEntityKindUVE::DirectionalLight case; it now has the same per-file home every other object
/// kind has. Per Engine/Runtime/Scene/README.md's "one truth per concept" rule this holds the
/// *recipe*, not a second copy of component storage: light state itself still lives only in
/// LightComponentUVE (and is consumed by Render/LightSystemUVE).
struct Light3DObjectDefinitionUVE final {
    /// Default document-entity name for a freshly created object of this kind. "Directional Light"
    /// preserves the exact name the legacy editor entity kind has always produced.
    static constexpr std::string_view defaultName = "Directional Light";

    /// Light authored defaults: white, intensity 1, directional (LightComponentUVE's own default
    /// type — the recipe states it explicitly because the legacy recipe did).
    LightComponentUVE light = MakeDefaultLightUVE();

    [[nodiscard]] static LightComponentUVE MakeDefaultLightUVE() noexcept {
        LightComponentUVE light{};
        light.type = LightTypeUVE::Directional;
        return light;
    }
};

[[nodiscard]] bool IsLight3DObjectDefinitionValidUVE(const Light3DObjectDefinitionUVE& value) noexcept;

/// Attaches this object's components to `entity` using the definition's authored defaults. The
/// entity must be alive and must not already have any of the attached component types.
/// Every application first guarantees the Object3D baseline (Transform/WorldTransform/
/// Hierarchy/Name) through EnsureObject3DBaselineUVE - this kind is Object3D plus its recipe.
void ApplyLight3DObjectDefinitionUVE(IEntityManagerUVE& entityManager, EntityUVE entity,
                                   const Light3DObjectDefinitionUVE& value);

} // namespace UVE::Scene
