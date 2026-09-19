// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <string_view>

#include "uve/component/entity_uve.h"
#include "uve/component/mesh_component_uve.h"

namespace UVE::Scene {

class IEntityManagerUVE;

/// Authoring definition for the MeshInstance3D scene node: the component set and defaults a
/// freshly created MeshInstance3D entity attaches — one empty MeshComponentUVE ready for an
/// author to bind a mesh/material asset pair onto. This recipe used to be hardcoded inline in
/// EditorUVE's creation switch; it now has the same per-file home every other node kind has.
/// Per Engine/Runtime/Scene/README.md's "one truth per concept" rule this holds the *recipe*,
/// not a second copy of component storage: mesh state itself still lives only in
/// MeshComponentUVE (and is rendered by Render/MeshRendererUVE).
struct MeshInstance3DNodeDefinitionUVE final {
    /// Default document-entity name for a freshly created node of this kind. (Previously the
    /// generic "Empty" — the editor created MeshInstance3D as a bare entity plus one component.)
    static constexpr std::string_view defaultName = "MeshInstance3D";

    /// Mesh-instance authored defaults (an unbound asset pair); the entity's transform is
    /// attached by the creation shell.
    MeshComponentUVE mesh{};
};

[[nodiscard]] bool IsMeshInstance3DNodeDefinitionValidUVE(const MeshInstance3DNodeDefinitionUVE& value) noexcept;

/// Attaches this node's components to `entity` using the definition's authored defaults. The
/// entity must be alive and must not already have any of the attached component types.
void ApplyMeshInstance3DNodeDefinitionUVE(IEntityManagerUVE& entityManager, EntityUVE entity,
                                          const MeshInstance3DNodeDefinitionUVE& value);

} // namespace UVE::Scene
