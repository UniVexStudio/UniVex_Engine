// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <string_view>

#include "uve/component/entity_uve.h"
#include "uve/component/ui_button_component_uve.h"

namespace UVE::Scene {

class IEntityManagerUVE;

/// Authoring definition for the UI Button scene node (CanvasLayer family): the component set and
/// defaults a freshly created UI Button entity attaches — one hit-testable 120x32 button with
/// the established normal/hover/pressed palette. Until now UI Button was reachable only through
/// the Inspector's "Add Component" list; promoting it to the Scene node registry gives UI
/// authoring the same one-entry-point Add-Node list every 3D kind has. Per
/// Engine/Runtime/Scene/README.md's "one truth per concept" rule this holds the *recipe*, not a
/// second copy of component storage: the button record itself still lives only in
/// UIButtonComponentUVE (and is hit-tested/ticked by UI/UIRuntimeUVE every frame).
struct UIButtonNodeDefinitionUVE final {
    /// Default document-entity name for a freshly created node of this kind — matches the
    /// Inspector's own "UI Button" component label exactly.
    static constexpr std::string_view defaultName = "UI Button";

    /// Button authored defaults (120x32 px, standard palette); the entity's transform is
    /// attached by the creation shell.
    UIButtonComponentUVE button{};
};

[[nodiscard]] bool IsUIButtonNodeDefinitionValidUVE(const UIButtonNodeDefinitionUVE& value) noexcept;

/// Attaches this node's components to `entity` using the definition's authored defaults. The
/// entity must be alive and must not already have any of the attached component types.
void ApplyUIButtonNodeDefinitionUVE(IEntityManagerUVE& entityManager, EntityUVE entity,
                                    const UIButtonNodeDefinitionUVE& value);

} // namespace UVE::Scene
