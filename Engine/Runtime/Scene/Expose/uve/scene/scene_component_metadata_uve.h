// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <string_view>
#include <typeindex>

#include "uve/object/type_metadata_uve.h"

#include "uve/strings/string_id_uve.h"

namespace UVE::Scene {

/// The value types a property can declare. A generic consumer - the inspector today, a
/// property-level serializer later - dispatches on these, so the number of cases it has to handle
/// is bounded by the number of value types the engine has (a dozen), not by the number of
/// component types it has (dozens, and growing with every feature).
///
/// Enum-typed properties always declare kPropertyTypeEnumUVE and carry their options in
/// TypeMetadataPropertyUVE::enumEntries; their accessors speak std::int64_t (see
/// MakeEnumPropertyUVE) because no generic caller can name the concrete enumeration.
inline const Strings::StringIdUVE kPropertyTypeBoolUVE{"Bool"};
inline const Strings::StringIdUVE kPropertyTypeFloatUVE{"Float"};
inline const Strings::StringIdUVE kPropertyTypeInt32UVE{"Int32"};
inline const Strings::StringIdUVE kPropertyTypeUInt32UVE{"UInt32"};
/// A byte-sized count. Its own type rather than UInt32 because the accessors are bound to the
/// member's real type: a generic consumer switching on the declared type would otherwise read four
/// bytes where the component stores one. Hitbox3D's strike count is the first user.
inline const Strings::StringIdUVE kPropertyTypeUInt8UVE{"UInt8"};
/// A 32-bit value authored as independent bits (a collision layer/mask), not as a number.
inline const Strings::StringIdUVE kPropertyTypeBitMask32UVE{"BitMask32"};
/// Custom drawer ids naming which set of layers a BitMask32 picks from, so the inspector can show
/// the project's names for them instead of bits.
inline constexpr std::string_view kLayerMaskDrawerPhysicsUVE = "layers:physics";
inline constexpr std::string_view kLayerMaskDrawerRenderUVE = "layers:render";
inline const Strings::StringIdUVE kPropertyTypeStringUVE{"String"};
inline const Strings::StringIdUVE kPropertyTypeVector2UVE{"Vector2"};
inline const Strings::StringIdUVE kPropertyTypeVector3UVE{"Vector3"};
/// A float RectUVE (UI widget footprint): drawn as Position + Size rows under one property,
/// one undo step for the whole rect.
inline const Strings::StringIdUVE kPropertyTypeRectUVE{"Rect"};
/// A Vector3 authored as a colour: same storage, a colour wheel instead of three number fields.
inline const Strings::StringIdUVE kPropertyTypeColorUVE{"Color"};
/// A linear-working-space ColorUVE authored as a colour: converted to display for the wheel,
/// back to linear on write. Light colours; the plain Color id stays for display-stored
/// Vector3 colours (primitive, environment, UI).
inline const Strings::StringIdUVE kPropertyTypeLinearColorUVE{"LinearColor"};
inline const Strings::StringIdUVE kPropertyTypeQuaternionUVE{"Quaternion"};
inline const Strings::StringIdUVE kPropertyTypeEnumUVE{"Enum"};
inline const Strings::StringIdUVE kPropertyTypeEntityUVE{"Entity"};
/// A fixed-capacity list of entity references (RayCast3D's exclusions today). Declared with
/// TypeMetadataPropertyUVE::elementCount telling a consumer how many slots the value holds; the
/// accessor reads and writes the whole array at once, so one edit is one write and one history
/// entry. Drawn by a custom drawer - a list is not one of the scalar row widgets.
inline const Strings::StringIdUVE kPropertyTypeEntityListUVE{"EntityList"};
inline const Strings::StringIdUVE kPropertyTypeAssetGuidUVE{"AssetGuid"};
/// A fixed-capacity list of asset references, declared with TypeMetadataPropertyUVE::elementCount
/// like the entity list. The accessor reads and writes the whole array at once, so one edit is one
/// write and one history entry. LODGroup3D's per-level meshes are the first user.
inline const Strings::StringIdUVE kPropertyTypeAssetGuidListUVE{"AssetGuidList"};
/// A fixed-capacity list of floats - a chain of distance thresholds today, a curve's keys later.
/// Same whole-array accessor contract as the other list types, and the same elementCount
/// declaration. LODGroup3D's thresholds are the first user.
inline const Strings::StringIdUVE kPropertyTypeFloatListUVE{"FloatList"};

/// Section sort keys. A component's own section sorts by TypeMetadataEntryUVE::order. The order
/// follows the object's class chain from most to least derived: what the concrete object brings, then
/// its abstract bases (the nearer base first), then Object3D's Transform and Visibility, then the
/// common Object section every object has.
inline constexpr std::int32_t kSectionOrderIdentityUVE = 5;
/// Everything a specific object type brings with it.
inline constexpr std::int32_t kSectionOrderTypeSpecificUVE = 100;
/// The abstract 3D bases. A base that derives from another sorts before it: SurfaceInstance3D and
/// LightEmitter3D before RenderInstance3D.
inline constexpr std::int32_t kSectionOrderObjectBaseUVE = 500;
/// Object3D's own sections.
inline constexpr std::int32_t kSectionOrderTransformUVE = 900;
inline constexpr std::int32_t kSectionOrderVisibilityUVE = 910;
/// The common Object section, last.
inline constexpr std::int32_t kSectionOrderObjectCommonUVE = 1000;

/// The process-wide metadata for every component a scene can contain, built once on first use.
///
/// This is the join the engine was missing. The ECS can already tell you which component types an
/// entity holds (IEntityManagerUVE::GetComponentTypesUVE) and hand you a pointer to each
/// (GetComponentPointerUVE), but nothing could say what those types contain. Every consumer that
/// needed to know wrote its own per-type branch, which is why adding one component meant editing a
/// dozen places. With this, a consumer resolves the type index to an entry and reads the entry.
[[nodiscard]] const Core::TypeMetadataRegistryUVE& GetSceneComponentMetadataRegistryUVE();

/// Resolves a live component's type to its metadata, or null for a type that declares none.
/// A null result is not an error: a component with no declared properties is simply not something
/// a generic consumer can present, and callers fall back to whatever they did before.
[[nodiscard]] const Core::TypeMetadataEntryUVE* FindSceneComponentMetadataUVE(
    std::type_index typeIndex) noexcept;

} // namespace UVE::Scene
