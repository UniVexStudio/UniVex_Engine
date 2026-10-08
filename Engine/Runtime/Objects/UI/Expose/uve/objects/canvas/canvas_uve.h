// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <string_view>

#include "uve/component/canvas_component_uve.h"
#include "uve/component/entity_uve.h"

namespace UVE::Scene {

class IEntityManagerUVE;

/// Authoring definition for the Canvas scene object. The record lives in CanvasComponentUVE;
/// UIRuntimeUVE reads that component for visibility and sort order.
struct CanvasObjectDefinitionUVE final {
    /// Default document-entity name for a freshly created object of this kind — matches the
    /// Inspector's own "Canvas" component label exactly, so both entry points feel identical.
    static constexpr std::string_view defaultName = "Canvas";

    /// Canvas authored defaults (visible, sort order 0); the entity's transform is attached by
    /// the creation shell.
    CanvasComponentUVE canvas{};
};

[[nodiscard]] bool IsCanvasObjectDefinitionValidUVE(const CanvasObjectDefinitionUVE& value) noexcept;

/// Attaches this object's components to `entity` using the definition's authored defaults. The
/// entity must be alive and must not already have any of the attached component types.
void ApplyCanvasObjectDefinitionUVE(IEntityManagerUVE& entityManager, EntityUVE entity,
                                  const CanvasObjectDefinitionUVE& value);

} // namespace UVE::Scene
