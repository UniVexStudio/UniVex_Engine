// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <string_view>
#include <typeindex>

#include "uve/object/type_metadata_uve.h"

namespace UVE::Scene {

/// The value types a property can declare. A generic consumer - the inspector today, a
/// property-level serializer later - dispatches on these, so the number of cases it has to handle
/// is bounded by the number of value types the engine has (a dozen), not by the number of
/// component types it has (dozens, and growing with every feature).
///
/// Enum-typed properties always declare kPropertyTypeEnumUVE and carry their options in
/// TypeMetadataPropertyUVE::enumEntries; their accessors speak std::int64_t (see
/// MakeEnumPropertyUVE) because no generic caller can name the concrete enumeration.
inline constexpr std::string_view kPropertyTypeBoolUVE = "Bool";
inline constexpr std::string_view kPropertyTypeFloatUVE = "Float";
inline constexpr std::string_view kPropertyTypeInt32UVE = "Int32";
inline constexpr std::string_view kPropertyTypeUInt32UVE = "UInt32";
/// A 32-bit value authored as independent bits (a collision layer/mask), not as a number.
inline constexpr std::string_view kPropertyTypeBitMask32UVE = "BitMask32";
inline constexpr std::string_view kPropertyTypeStringUVE = "String";
inline constexpr std::string_view kPropertyTypeVector2UVE = "Vector2";
inline constexpr std::string_view kPropertyTypeVector3UVE = "Vector3";
/// A Vector3 authored as a colour: same storage, a colour wheel instead of three number fields.
inline constexpr std::string_view kPropertyTypeColorUVE = "Color";
inline constexpr std::string_view kPropertyTypeQuaternionUVE = "Quaternion";
inline constexpr std::string_view kPropertyTypeEnumUVE = "Enum";
inline constexpr std::string_view kPropertyTypeEntityUVE = "Entity";
inline constexpr std::string_view kPropertyTypeAssetGuidUVE = "AssetGuid";

/// Section sort keys. A component's own section sorts by TypeMetadataEntryUVE::order, which is how
/// the properties every node has in common are kept together at the bottom of the inspector
/// without the inspector itself knowing what a "common" section is.
inline constexpr std::int32_t kSectionOrderIdentityUVE = 5;
inline constexpr std::int32_t kSectionOrderTransformUVE = 10;
inline constexpr std::int32_t kSectionOrderVisibilityUVE = 20;
/// Everything a specific node type brings with it.
inline constexpr std::int32_t kSectionOrderTypeSpecificUVE = 100;
/// The common Node section, last.
/// The abstract 3D bases (BoneModifier3D, PhysicsObject3D, RenderInstance3D): below what a
/// concrete node brings, above what every node has.
inline constexpr std::int32_t kSectionOrderNodeBaseUVE = 500;
inline constexpr std::int32_t kSectionOrderNodeCommonUVE = 1000;

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
