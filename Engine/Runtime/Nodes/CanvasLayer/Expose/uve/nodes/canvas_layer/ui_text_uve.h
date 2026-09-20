// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <string_view>

#include "uve/component/entity_uve.h"
#include "uve/component/ui_text_component_uve.h"

namespace UVE::Scene {

class IEntityManagerUVE;

/// Authoring definition for the UI Text scene node (CanvasLayer family): the component set and
/// defaults a freshly created UI Text entity attaches — one empty text line at the origin in
/// raw window-pixel coordinates. Until now UI Text was reachable only through the Inspector's
/// "Add Component" list; promoting it to the Scene node registry gives UI authoring the same
/// one-entry-point Add-Node list every 3D kind has. Per
/// Engine/Runtime/Scene/README.md's "one truth per concept" rule this holds the *recipe*, not a
/// second copy of component storage: the text record itself still lives only in
/// UITextComponentUVE (and is rendered by UI/UIRuntimeUVE's overlay pass).
struct UITextNodeDefinitionUVE final {
    /// Default document-entity name for a freshly created node of this kind — matches the
    /// Inspector's own "UI Text" component label exactly.
    static constexpr std::string_view defaultName = "UI Text";

    /// Text authored defaults (empty text, 16px, white, opaque); the entity's transform is
    /// attached by the creation shell.
    UITextComponentUVE text{};
};

[[nodiscard]] bool IsUITextNodeDefinitionValidUVE(const UITextNodeDefinitionUVE& value) noexcept;

/// Attaches this node's components to `entity` using the definition's authored defaults. The
/// entity must be alive and must not already have any of the attached component types.
void ApplyUITextNodeDefinitionUVE(IEntityManagerUVE& entityManager, EntityUVE entity,
                                  const UITextNodeDefinitionUVE& value);

} // namespace UVE::Scene
