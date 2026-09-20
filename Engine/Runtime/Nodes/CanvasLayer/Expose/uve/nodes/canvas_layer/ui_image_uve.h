// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <string_view>

#include "uve/component/entity_uve.h"
#include "uve/component/ui_image_component_uve.h"

namespace UVE::Scene {

class IEntityManagerUVE;

/// Authoring definition for the UI Image scene node (CanvasLayer family): the component set and
/// defaults a freshly created UI Image entity attaches — one untextured white 64x64 quad (an
/// unset texture guid is a valid authored state: it renders as a flat tint quad). Until now
/// UI Image was reachable only through the Inspector's "Add Component" list; promoting it to
/// the Scene node registry gives UI authoring the same one-entry-point Add-Node list every 3D
/// kind has. Per Engine/Runtime/Scene/README.md's "one truth per concept" rule this holds the
/// *recipe*, not a second copy of component storage: the image record itself still lives only
/// in UIImageComponentUVE (and is rendered by UI/UIRuntimeUVE's overlay pass).
struct UIImageNodeDefinitionUVE final {
    /// Default document-entity name for a freshly created node of this kind — matches the
    /// Inspector's own "UI Image" component label exactly.
    static constexpr std::string_view defaultName = "UI Image";

    /// Image authored defaults (no texture bound, 64x64 px, white tint, opaque); the entity's
    /// transform is attached by the creation shell.
    UIImageComponentUVE image{};
};

[[nodiscard]] bool IsUIImageNodeDefinitionValidUVE(const UIImageNodeDefinitionUVE& value) noexcept;

/// Attaches this node's components to `entity` using the definition's authored defaults. The
/// entity must be alive and must not already have any of the attached component types.
void ApplyUIImageNodeDefinitionUVE(IEntityManagerUVE& entityManager, EntityUVE entity,
                                   const UIImageNodeDefinitionUVE& value);

} // namespace UVE::Scene
