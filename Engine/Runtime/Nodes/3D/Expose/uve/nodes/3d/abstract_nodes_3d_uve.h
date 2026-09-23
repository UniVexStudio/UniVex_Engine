// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <string_view>

#include "uve/component/entity_uve.h"

namespace UVE::Scene {

class IEntityManagerUVE;

// The three abstract kinds between Node3D and the concrete 3D nodes. None of them is ever created
// on its own - none appears in the Add-Node library - they exist so their children share one
// definition of what they have in common:
//
//   Node3D
//   +- BoneModifier3D    adjusts a posed skeleton          (BoneModifierComponentUVE)
//   +- PhysicsObject3D   takes part in collision           (PhysicsObjectComponentUVE)
//   +- RenderInstance3D  is drawn                          (RenderInstanceComponentUVE)
//
// A child's recipe calls its base's Apply first and then attaches its own components, so a child
// is exactly "its base plus its own", the same way every base is "Node3D plus its own".

struct BoneModifier3DNodeDefinitionUVE final {
    static constexpr std::string_view typeName = "BoneModifier3D";
};

struct PhysicsObject3DNodeDefinitionUVE final {
    static constexpr std::string_view typeName = "PhysicsObject3D";
};

struct RenderInstance3DNodeDefinitionUVE final {
    static constexpr std::string_view typeName = "RenderInstance3D";
};

/// Each applies the Node3D recipe (transform baseline, Visibility, the common Node section) and
/// attaches its base component where missing. Existing components and their authored values are
/// left alone, and a destroyed entity is refused quietly. `nameFallback` is the name given to an
/// entity that has none - the concrete child's own name, since the base is never created itself.
void ApplyBoneModifier3DBaseUVE(IEntityManagerUVE& entityManager, EntityUVE entity, std::string_view nameFallback);
void ApplyPhysicsObject3DBaseUVE(IEntityManagerUVE& entityManager, EntityUVE entity, std::string_view nameFallback);
void ApplyRenderInstance3DBaseUVE(IEntityManagerUVE& entityManager, EntityUVE entity, std::string_view nameFallback);

} // namespace UVE::Scene
