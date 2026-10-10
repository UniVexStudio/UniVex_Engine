// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <string_view>

#include "uve/component/entity_uve.h"

namespace UVE::Scene {

class IEntityManagerUVE;

/// The Object itself - the base every scene object stands on, and the thing the Object IS
/// (the registry shows that kind as "Object").
///
/// An Object is deliberately not spatial: it has no Transform, no WorldTransform and no
/// Visibility, because those describe a *place* in the level and an Object is not a place, it is
/// the thing a place belongs to. What it has is identity (a Name and its place in the hierarchy)
/// and the Object section every kind shares - Process, Thread Group, Physics Interpolation, Auto
/// Translate, Editor Description, Script and Metadata. That section is why scripts and metadata
/// hang off an object of any kind, and it is why this file lives here in Objects/Core rather than
/// beside the 3D objects: every Object3D in the level is a child of the Object, not a variant of it.
///
/// The two functions below are the seam between the two halves. EnsureObjectBaselineUVE is the
/// whole recipe of a pure Object, and EnsureCommonObjectSectionUVE is the part Object3D shares
/// with it - Object3D's own baseline (EnsureObject3DBaselineUVE, in the 3D library) keeps the
/// transform components instead of removing them and then calls straight into this one.

/// The recipe of a pure Object - an object with no transform, such as the Object root or an
/// AnimationSequencer: in the hierarchy, named, carrying the common Object section and nothing
/// spatial. Any Transform/WorldTransform/Visibility present is removed; an existing transform is
/// first folded into the children so nothing moves on screen. Idempotent, and refuses a dead
/// entity. `nameFallback` is used only when the entity has no name yet.
void EnsureObjectBaselineUVE(IEntityManagerUVE& entityManager, EntityUVE entity, std::string_view nameFallback);

/// The Object section every object has in common - Process, Thread Group, Physics Interpolation, Auto
/// Translate, Editor Description, Script and Metadata - attached with defaults where missing.
/// Every default is Inherit or empty, so attaching them changes nothing about how the scene runs;
/// it makes them reachable in an Inspector that offers no Add Component. Shared by the Object
/// root and Object3D as well as the other kinds, so all of them carry exactly the same section.
void EnsureCommonObjectSectionUVE(IEntityManagerUVE& entityManager, EntityUVE entity);

// -------------------------------------------------------------------------------------------------
// The Object kind - the document's structural root
//
// The root of a document is not a special kind of object; it IS the Object. It was called
// `SceneRoot` until 2026-10-08, and the rename covers the whole identity: the marker component, the
// definition, the registry kind (`SceneObjectKindUVE::Object`), the display name and the type id.
// The kind's enumerator value and the older spellings stay readable -
// `FindSceneObjectDescriptorUVE` still resolves the "object" type id and the serializer still
// resolves the "ObjectComponentUVE" component name - so every document written before the
// rename loads unchanged, and writing always uses the new names.
// -------------------------------------------------------------------------------------------------

/// Marker component identifying the document's one Object entity. Structural only by design: the
/// root is a pure Object - a name, the hierarchy link and the common Object section (Process,
/// Thread Group, Physics Interpolation, Auto Translate, Editor Description, Script, Metadata),
/// with no transform of its own - and it exists so every document has exactly one
/// top-of-hierarchy anchor, which every Object3D in the level hangs from. Scene-wide settings
/// (environment, navigation, streaming) are deliberately NOT on it yet - they get their own
/// authored homes when the systems that consume them exist, not before.
struct ObjectComponentUVE final {};

[[nodiscard]] constexpr bool IsObjectComponentValidUVE(const ObjectComponentUVE&) noexcept {
    return true; // a pure marker: no authored state to invalidate
}

/// Authoring definition for the Object. The recipe is the marker component (plus the creation
/// shell's Name, folded into the pure Object baseline above); `defaultName` is what a fresh
/// document's root is called, and it names what the object IS: the base Object. The kind is
/// `libraryCreatable = false`: the root is created by the document lifecycle (new document,
/// load-time migration), never through the Add-Object library.
struct ObjectDefinitionUVE final {
    static constexpr std::string_view defaultName = "Object";
};

[[nodiscard]] constexpr bool IsObjectDefinitionValidUVE(const ObjectDefinitionUVE&) noexcept {
    return true;
}

/// Attaches the Object's recipe to `entity`: the pure Object baseline - in the hierarchy, named,
/// carrying the common Object section and no transform of its own (EnsureObjectBaselineUVE) - plus
/// the marker. It runs on every new document and every load, so a root saved when roots still
/// carried a transform is migrated too. The entity must be alive; existing components keep their
/// values.
void ApplyObjectDefinitionUVE(IEntityManagerUVE& entityManager, EntityUVE entity, const ObjectDefinitionUVE& value);

} // namespace UVE::Scene
