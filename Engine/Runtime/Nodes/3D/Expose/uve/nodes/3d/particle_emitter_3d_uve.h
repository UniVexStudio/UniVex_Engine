// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <string_view>

#include "uve/component/entity_uve.h"
#include "uve/component/particle_emitter_component_uve.h"

namespace UVE::Scene {

class IEntityManagerUVE;

/// Authoring definition for the ParticleEmitter3D scene node: the component set and defaults a
/// freshly created ParticleEmitter3D entity attaches. This recipe used to be hardcoded inline in
/// EditorUVE's creation switch; it now has the same per-file home every other node kind has.
/// Per Engine/Runtime/Scene/README.md's "one truth per concept" rule this holds the *recipe*,
/// not a second copy of component storage: emitter state itself still lives only in
/// ParticleEmitterComponentUVE (and is ticked by Scene/ParticleRuntimeUVE).
struct ParticleEmitter3DNodeDefinitionUVE final {
    /// Default document-entity name for a freshly created node of this kind. (Previously the
    /// generic "Empty" — the editor created ParticleEmitter3D as a bare entity plus one
    /// component.)
    static constexpr std::string_view defaultName = "ParticleEmitter3D";

    /// Emitter authored defaults; the entity's transform is attached by the creation shell.
    ParticleEmitterComponentUVE emitter{};
};

[[nodiscard]] bool IsParticleEmitter3DNodeDefinitionValidUVE(const ParticleEmitter3DNodeDefinitionUVE& value) noexcept;

/// Attaches this node's components to `entity` using the definition's authored defaults. The
/// entity must be alive and must not already have any of the attached component types.
void ApplyParticleEmitter3DNodeDefinitionUVE(IEntityManagerUVE& entityManager, EntityUVE entity,
                                             const ParticleEmitter3DNodeDefinitionUVE& value);

} // namespace UVE::Scene
