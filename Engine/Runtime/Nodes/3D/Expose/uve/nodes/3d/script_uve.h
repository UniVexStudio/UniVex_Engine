// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <string_view>

#include "uve/component/entity_uve.h"
#include "uve/component/script_component_uve.h"

namespace UVE::Scene {

class IEntityManagerUVE;

/// Authoring definition for the Script scene node: the component set and defaults a freshly
/// created Script entity attaches. This recipe used to be hardcoded inline in EditorUVE's
/// creation switch; it now has the same per-file home every other node kind has. Per
/// Engine/Runtime/Scene/README.md's "one truth per concept" rule this holds the *recipe*, not a
/// second copy of component storage: script state itself still lives only in
/// ScriptComponentUVE (and is ticked by Scripting/ScriptRuntimeUVE).
struct ScriptNodeDefinitionUVE final {
    /// Default document-entity name for a freshly created node of this kind. (Previously the
    /// generic "Empty" — the editor created Script as a bare entity plus one component.)
    static constexpr std::string_view defaultName = "Script";

    /// Script authored defaults (an unbound script asset); the entity's transform is attached by
    /// the creation shell.
    ScriptComponentUVE script{};
};

[[nodiscard]] bool IsScriptNodeDefinitionValidUVE(const ScriptNodeDefinitionUVE& value) noexcept;

/// Attaches this node's components to `entity` using the definition's authored defaults. The
/// entity must be alive and must not already have any of the attached component types.
void ApplyScriptNodeDefinitionUVE(IEntityManagerUVE& entityManager, EntityUVE entity,
                                  const ScriptNodeDefinitionUVE& value);

} // namespace UVE::Scene
