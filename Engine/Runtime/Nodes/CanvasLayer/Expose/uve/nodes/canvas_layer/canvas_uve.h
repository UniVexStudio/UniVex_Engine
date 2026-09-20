// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <string_view>

#include "uve/component/canvas_component_uve.h"
#include "uve/component/entity_uve.h"

namespace UVE::Scene {

class IEntityManagerUVE;

/// Authoring definition for the Canvas scene node (CanvasLayer family): the component set and
/// defaults a freshly created Canvas entity attaches. Until now Canvas was reachable only through
/// the Inspector's "Add Component" list; promoting it to the Scene node registry gives UI
/// authoring the same one-entry-point Add-Node list every 3D kind has. Per
/// Engine/Runtime/Scene/README.md's "one truth per concept" rule this holds the *recipe*, not a
/// second copy of component storage: the canvas record itself still lives only in
/// CanvasComponentUVE (and is drawn by UI/UIRuntimeUVE's overlay pass).
struct CanvasNodeDefinitionUVE final {
    /// Default document-entity name for a freshly created node of this kind — matches the
    /// Inspector's own "Canvas" component label exactly, so both entry points feel identical.
    static constexpr std::string_view defaultName = "Canvas";

    /// Canvas authored defaults (visible, sort order 0); the entity's transform is attached by
    /// the creation shell.
    CanvasComponentUVE canvas{};
};

[[nodiscard]] bool IsCanvasNodeDefinitionValidUVE(const CanvasNodeDefinitionUVE& value) noexcept;

/// Attaches this node's components to `entity` using the definition's authored defaults. The
/// entity must be alive and must not already have any of the attached component types.
void ApplyCanvasNodeDefinitionUVE(IEntityManagerUVE& entityManager, EntityUVE entity,
                                  const CanvasNodeDefinitionUVE& value);

} // namespace UVE::Scene
