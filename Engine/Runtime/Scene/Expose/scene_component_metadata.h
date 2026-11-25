// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <string_view>
#include <typeindex>

#include "type_metadata.h"

#include "string_id.h"

namespace UVE::Scene {

/// The value types a property can declare. A generic consumer - the inspector today, a
/// property-level serializer later - dispatches on these, so the number of cases it has to handle
/// is bounded by the number of value types the engine has (a dozen), not by the number of
/// component types it has (dozens, and growing with every feature).
///
/// Enum-typed properties always declare kPropertyTypeEnum and carry their options in
/// TypeMetadataProperty::enumEntries; their accessors speak std::int64_t (see
/// MakeEnumProperty) because no generic caller can name the concrete enumeration.
inline const Strings::StringId kPropertyTypeBool{"Bool"};
inline const Strings::StringId kPropertyTypeFloat{"Float"};
inline const Strings::StringId kPropertyTypeInt32{"Int32"};
inline const Strings::StringId kPropertyTypeUInt32{"UInt32"};
/// A byte-sized count. Its own type rather than UInt32 because the accessors are bound to the
/// member's real type: a generic consumer switching on the declared type would otherwise read four
/// bytes where the component stores one. Hitbox3D's strike count is the first user.
inline const Strings::StringId kPropertyTypeUInt8{"UInt8"};
/// A 32-bit value authored as independent bits (a collision layer/mask), not as a number.
inline const Strings::StringId kPropertyTypeBitMask32{"BitMask32"};
/// Custom drawer ids naming which set of layers a BitMask32 picks from, so the inspector can show
/// the project's names for them instead of bits.
inline constexpr std::string_view kLayerMaskDrawerPhysics = "layers:physics";
inline constexpr std::string_view kLayerMaskDrawerRender = "layers:render";
inline const Strings::StringId kPropertyTypeString{"String"};
inline const Strings::StringId kPropertyTypeVector2{"Vector2"};
inline const Strings::StringId kPropertyTypeVector3{"Vector3"};
/// A float Rect (UI widget footprint): drawn as Position + Size rows under one property,
/// one undo step for the whole rect.
inline const Strings::StringId kPropertyTypeRect{"Rect"};
/// A Vector3 authored as a colour: same storage, a colour wheel instead of three number fields.
inline const Strings::StringId kPropertyTypeColor{"Color"};
/// A linear-working-space Color authored as a colour: converted to display for the wheel,
/// back to linear on write. Light colours; the plain Color id stays for display-stored
/// Vector3 colours (primitive, environment, UI).
inline const Strings::StringId kPropertyTypeLinearColor{"LinearColor"};
inline const Strings::StringId kPropertyTypeQuaternion{"Quaternion"};
inline const Strings::StringId kPropertyTypeEnum{"Enum"};
inline const Strings::StringId kPropertyTypeEntity{"Entity"};
/// A fixed-capacity list of entity references (RayCast3D's exclusions today). Declared with
/// TypeMetadataProperty::elementCount telling a consumer how many slots the value holds; the
/// accessor reads and writes the whole array at once, so one edit is one write and one history
/// entry. Drawn by a custom drawer - a list is not one of the scalar row widgets.
inline const Strings::StringId kPropertyTypeEntityList{"EntityList"};
inline const Strings::StringId kPropertyTypeAssetGuid{"AssetGuid"};
/// A fixed-capacity list of asset references, declared with TypeMetadataProperty::elementCount
/// like the entity list. The accessor reads and writes the whole array at once, so one edit is one
/// write and one history entry. LODGroup3D's per-level meshes are the first user.
inline const Strings::StringId kPropertyTypeAssetGuidList{"AssetGuidList"};
/// A fixed-capacity list of floats - a chain of distance thresholds today, a curve's keys later.
/// Same whole-array accessor contract as the other list types, and the same elementCount
/// declaration. LODGroup3D's thresholds are the first user.
inline const Strings::StringId kPropertyTypeFloatList{"FloatList"};
/// A variable-length list of strings, read and written whole like the other list types but with no
/// elementCount: the value itself carries its length. A path per surface is the first user, so the
/// list is indexed by the thing it describes rather than being a bag of names.
inline const Strings::StringId kPropertyTypeStringList{"StringList"};

/// Section sort keys. A component's own section sorts by TypeMetadataEntry::order. The order
/// follows the object's class chain from most to least derived: what the concrete object brings, then
/// its abstract bases (the nearer base first), then Object3D's Transform and Visibility, then the
/// common Object section every object has.
inline constexpr std::int32_t kSectionOrderIdentity = 5;
/// Everything a specific object type brings with it.
inline constexpr std::int32_t kSectionOrderTypeSpecific = 100;
/// The abstract 3D bases. A base that derives from another sorts before it: SurfaceInstance3D and
/// LightEmitter3D before RenderInstance3D.
inline constexpr std::int32_t kSectionOrderObjectBase = 500;
/// Object3D's own sections.
inline constexpr std::int32_t kSectionOrderTransform = 900;
inline constexpr std::int32_t kSectionOrderVisibility = 910;
/// The common Object section, last.
inline constexpr std::int32_t kSectionOrderObjectCommon = 1000;

/// The process-wide metadata for every component a scene can contain, built once on first use.
///
/// This is the join the engine was missing. The ECS can already tell you which component types an
/// entity holds (IEntityManager::GetComponentTypes) and hand you a pointer to each
/// (GetComponentPointer), but nothing could say what those types contain. Every consumer that
/// needed to know wrote its own per-type branch, which is why adding one component meant editing a
/// dozen places. With this, a consumer resolves the type index to an entry and reads the entry.
[[nodiscard]] const Core::TypeMetadataRegistry& GetSceneComponentMetadataRegistry();

/// Resolves a live component's type to its metadata, or null for a type that declares none.
/// A null result is not an error: a component with no declared properties is simply not something
/// a generic consumer can present, and callers fall back to whatever they did before.
[[nodiscard]] const Core::TypeMetadataEntry* FindSceneComponentMetadata(
    std::type_index typeIndex) noexcept;

} // namespace UVE::Scene
