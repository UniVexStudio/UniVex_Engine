// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <string_view>

#include "uve/component/entity_uve.h"

namespace UVE::Scene {

class IEntityManagerUVE;

/// Authoring definition for the Object3D scene object: the transform-only base of every scene
/// hierarchy, and the object you reach for when you need a pivot or a grouping parent with no
/// appearance of its own.
///
/// Its recipe is the baseline every 3D scene object stands on - a TransformComponentUVE, a
/// WorldTransformComponentUVE, a HierarchyComponentUVE and a name. Every other kind is this
/// plus its own components, so BoxMesh3D is Object3D plus a primitive mesh and a collider,
/// Camera3D is Object3D plus a camera.
///
/// The capabilities people expect of it live on those shared components rather than here, which
/// is why this file stays small while the object gets richer: position/rotation/scale and Top Level
/// on TransformComponentUVE, Visible and Visibility Parent on VisibilityComponentUVE, notes on
/// EditorDescriptionComponentUVE. Putting them here instead would give them to ONE kind; putting
/// them on the components gives them to all 43.
///
/// NAMING. This kind used to be spelled `Empty` everywhere. The rename covers the enumerator,
/// the file and the on-disk type id, and the old spellings keep working at the two seams that
/// cross a persistence boundary: FindSceneObjectDescriptorUVE still resolves the legacy "empty"
/// type id to this kind, and the hierarchy icon table still maps the legacy "empty" name, so a
/// saved scene or layout written before the rename loads identically after it.
struct Object3DObjectDefinitionUVE final {
    /// Shown in the Add-Object library and used as a freshly created object's entity name.
    static constexpr std::string_view defaultName = "Object3D";
};

[[nodiscard]] bool IsObject3DObjectDefinitionValidUVE(const Object3DObjectDefinitionUVE& value) noexcept;

/// Attaches this object's components to `entity`: the transform baseline, Visibility, and the common
/// Object section (EnsureCommonObjectSectionUVE). Applying it guarantees the baseline: any of Transform/WorldTransform/Hierarchy/Name that is
/// missing gets attached with sane defaults, and anything already present - its authored values
/// included - is left alone. On the standard creation path the shell has attached all four
/// before this runs, so the guarantee costs nothing there; it exists for every other path
/// (deserialization, programmatic creation, repair) that can reach a Object3D without the shell.
void ApplyObject3DObjectDefinitionUVE(IEntityManagerUVE& entityManager, EntityUVE entity,
                                  const Object3DObjectDefinitionUVE& value);

/// The baseline part of the Object3D recipe, factored out so the other kind whose recipe rests on
/// the same baseline - the scene root - shares one guarantee rather than a second copy of it.
/// `nameFallback` is used only when the entity has no name yet; existing components are never
/// overwritten, and a destroyed entity is refused quietly, matching the scene-root apply.
void EnsureObject3DBaselineUVE(IEntityManagerUVE& entityManager, EntityUVE entity,
                             std::string_view nameFallback);

/// The recipe of a pure Object - a object with no transform, such as the scene root or an
/// AnimationSequencer: in the hierarchy, named, carrying the common Object section and nothing spatial.
/// Any Transform/WorldTransform/Visibility present is removed; an existing transform is first
/// folded into the children so nothing moves on screen. Idempotent, and refuses a dead entity.
void EnsureObjectBaselineUVE(IEntityManagerUVE& entityManager, EntityUVE entity, std::string_view nameFallback);

/// The Object section every object has in common - Process, Thread Group, Physics Interpolation, Auto
/// Translate, Editor Description, Script and Metadata - attached with defaults where missing.
/// Every default is Inherit or empty, so attaching them changes nothing about how the scene runs;
/// it makes them reachable in an Inspector that offers no Add Component. Shared by the scene root
/// and Object3D so both carry exactly the same section.
void EnsureCommonObjectSectionUVE(IEntityManagerUVE& entityManager, EntityUVE entity);

} // namespace UVE::Scene
