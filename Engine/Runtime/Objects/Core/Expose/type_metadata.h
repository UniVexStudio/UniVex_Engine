// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cstddef>
#include <cstdint>
#include <new>
#include <string>
#include <string_view>
#include <concepts>
#include <type_traits>
#include <typeindex>
#include <typeinfo>
#include <utility>
#include <vector>

#include "string_id.h"
#include "flags.h"

namespace UVE::Core {

/// `TypeId` is the strong type id: an interned name, so reflected lookups and name-keyed
/// maps compare integers instead of strings. Type ids were raw `std::string` before the
/// `StringId` migration; this alias names the resolution at the type that owns it.
using TypeId = Strings::StringId;

enum class TypeMetadataKind : std::uint8_t {
    Component = 0,
    Resource,
    InspectorTarget,
    Other,
};

/// Declared traits a generic consumer (the inspector, the serializer) needs in order to treat a
/// property correctly without knowing its concrete type. Every one of these previously had to be
/// re-derived by a hand-written per-type branch at each consumer.
enum class TypeMetadataPropertyFlags : std::uint32_t {
    None = 0U,
    /// Never writable, even though the type exposes a setter (e.g. a derived cache).
    ReadOnly = 1U << 0U,
    /// Written by a runtime system, not by authoring - implies ReadOnly in the inspector and is
    /// excluded from serialization, because persisting it would fight the system that owns it.
    RuntimeState = 1U << 1U,
    /// Authoring-time only: shown in the inspector, never shipped in a runtime build's data.
    EditorOnly = 1U << 2U,
    /// Folded behind an "Advanced" disclosure rather than shown by default.
    Advanced = 1U << 3U,
    /// Not shown in the inspector at all (still serialized unless RuntimeState).
    Hidden = 1U << 4U,
    /// Holds a Scene::Entity that must be remapped through the serializer's local-id table.
    EntityReference = 1U << 5U,
};

/// Bounds for a numeric property, so the inspector picks a slider/step without a per-type branch.
/// `enabled` distinguishes "no range declared" from a legitimate [0, 0] range.
struct TypeMetadataNumericRange final {
    bool enabled = false;
    double minimum = 0.0;
    double maximum = 0.0;
    double step = 0.0;

    [[nodiscard]] bool operator==(const TypeMetadataNumericRange&) const = default;
};

/// One selectable value of an enum property. The inspector renders these as a collapsed dropdown;
/// the underlying value is carried explicitly so label order never has to match enumerator order.
struct TypeMetadataEnumEntry final {
    std::int64_t value = 0;
    std::string label;

    [[nodiscard]] bool operator==(const TypeMetadataEnumEntry&) const = default;
};

struct TypeMetadataProperty final {
    /// A property's identity is its name, label, type and writability; everything below is opt-in
    /// metadata a declaration adds only when it has something to say. Spelling that as a
    /// constructor rather than leaving the struct an aggregate means adding a new trait never
    /// forces every existing declaration to brace-initialize it.
    TypeMetadataProperty() = default;
    TypeMetadataProperty(std::string propertyName, std::string propertyDisplayName,
                            TypeId propertyTypeId, const bool isEditable)
        : name(std::move(propertyName)),
          displayName(std::move(propertyDisplayName)),
          typeId(std::move(propertyTypeId)),
          editable(isEditable) {}

    std::string name;
    std::string displayName;
    TypeId typeId;
    bool editable = false;

    /// Type-erased accessors bound to a specific member by MakeProperty() below - null for a
    /// property built by hand (e.g. in existing tests/data that only describe a property without
    /// needing to read/write it). `instance`/`value` are always `OwnerT*`/`ValueT*` respectively;
    /// callers use GetPropertyValue()/SetPropertyValue() rather than calling these directly
    /// to keep that contract type-safe at the call site.
    void (*getValue)(const void* instance, void* outValue) = nullptr;
    void (*setValue)(void* instance, const void* inValue) = nullptr;

    /// Inspector sub-group inside the owning type's section, such as "Shadow". Empty means the
    /// section's top level. Consecutive properties naming the same group are drawn under one
    /// collapsible header, so declare a group's properties together and after the ungrouped ones.
    std::string section;
    std::int32_t order = 0;
    Utilities::Flags<TypeMetadataPropertyFlags> flags;
    TypeMetadataNumericRange range;
    std::vector<TypeMetadataEnumEntry> enumEntries;
    std::string tooltip;
    /// How many elements a fixed-capacity list value holds (kPropertyTypeEntityList); zero for
    /// every value that is not a list. The accessor reads and writes the whole list, so a consumer
    /// needs the length to size its buffer - the value type's own size says nothing about it.
    std::size_t elementCount = 0U;
    /// Opt-in escape hatch: names a registered custom drawer for the cases a generic editor cannot
    /// serve correctly (a transform that must round-trip through euler sync, an entity picker).
    std::string customDrawerId;
    /// Names the sibling property holding what this authored value resolves to once inheritance is
    /// applied - `mode` resolves to `resolvedModeInHierarchy`. An inspector shows the answer beside
    /// the choice ("Inherit (Running)") instead of as a second row that repeats the same dropdown.
    /// Must name another property of the same type; the registry rejects anything else.
    std::string resolvedByProperty;
    /// Conditional visibility. Null means always visible; otherwise the inspector calls it with a
    /// pointer to the owning instance, so a property can depend on a sibling field's value.
    bool (*isVisible)(const void* instance) = nullptr;
    /// Compares this one property across two instances of the owning type. Null when the value
    /// type has no equality operator.
    bool (*areEqual)(const void* leftInstance, const void* rightInstance) = nullptr;

    [[nodiscard]] bool operator==(const TypeMetadataProperty&) const = default;

    /// True when authoring must not write this property, whether declared read-only outright or
    /// because a runtime system owns it. Both consumers ask this rather than re-deriving it.
    [[nodiscard]] bool IsAuthoringWritable() const noexcept {
        return editable && setValue != nullptr &&
               !flags.Has(TypeMetadataPropertyFlags::ReadOnly) &&
               !flags.Has(TypeMetadataPropertyFlags::RuntimeState);
    }

    /// True when the property belongs in persisted scene data. Excluded: runtime-derived
    /// state (persisting it would fight the system that recomputes it every frame),
    /// editor-only authoring (never shipped in runtime data, per the flag's contract), and
    /// describe-only properties with no accessor to read/write through. The metadata-driven
    /// serializer's skip rule is exactly this predicate.
    [[nodiscard]] bool IsSerialized() const noexcept {
        return getValue != nullptr && setValue != nullptr &&
               !flags.Has(TypeMetadataPropertyFlags::RuntimeState) &&
               !flags.Has(TypeMetadataPropertyFlags::EditorOnly);
    }
};

namespace Detail {
template <typename MemberPointerT>
struct MemberPointerTraits;

template <typename OwnerT, typename ValueT>
struct MemberPointerTraits<ValueT OwnerT::*> {
    using Owner = OwnerT;
    using Value = ValueT;
};
} // namespace Detail

/// Builds a TypeMetadataProperty bound to `MemberPointer` via GetPropertyValue()/
/// SetPropertyValue() below, so reflection callers (an inspector, a serializer) can read/write
/// a registered type's property generically without knowing its concrete type ahead of time.
/// This is the genuine capability gap TypeMetadataProperty previously had - it could only
/// describe a property's existence, never actually get or set one.
template <auto MemberPointer>
[[nodiscard]] TypeMetadataProperty MakeProperty(std::string name, std::string displayName,
                                                       TypeId typeId, bool editable) {
    using Traits = Detail::MemberPointerTraits<decltype(MemberPointer)>;
    using OwnerT = typename Traits::Owner;
    using ValueT = typename Traits::Value;

    TypeMetadataProperty property{std::move(name), std::move(displayName), std::move(typeId), editable};
    property.getValue = +[](const void* instance, void* outValue) {
        *static_cast<ValueT*>(outValue) = static_cast<const OwnerT*>(instance)->*MemberPointer;
    };
    property.setValue = +[](void* instance, const void* inValue) {
        static_cast<OwnerT*>(instance)->*MemberPointer = *static_cast<const ValueT*>(inValue);
    };
    // Property-level equality is what lets a generic editor tell "the author changed this" from
    // "the widget reported the value it was given", without which every redraw would look like an
    // edit. Left null for a value type that cannot be compared, which callers treat as "unknown".
    if constexpr (requires(const ValueT& left, const ValueT& right) {
                      { left == right } -> std::convertible_to<bool>;
                  }) {
        property.areEqual = +[](const void* left, const void* right) {
            return static_cast<const OwnerT*>(left)->*MemberPointer ==
                   static_cast<const OwnerT*>(right)->*MemberPointer;
        };
    }
    return property;
}

/// Builds a property for an enum member whose accessors speak std::int64_t rather than the
/// concrete enumeration. A generic consumer cannot name LightType or ColliderShapeType, so
/// without this every enum would force exactly the per-type branch this registry exists to remove.
/// `options` carries each selectable value with its label, so the dropdown round-trips a selection
/// back to a real enumerator instead of relying on label order matching declaration order.
template <auto MemberPointer>
[[nodiscard]] TypeMetadataProperty MakeEnumProperty(std::string name, std::string displayName,
                                                          TypeId typeId, const bool editable,
                                                          std::vector<TypeMetadataEnumEntry> options) {
    using Traits = Detail::MemberPointerTraits<decltype(MemberPointer)>;
    using OwnerT = typename Traits::Owner;
    using ValueT = typename Traits::Value;
    static_assert(std::is_enum_v<ValueT>, "MakeEnumProperty requires an enumeration member.");

    TypeMetadataProperty property{std::move(name), std::move(displayName), std::move(typeId), editable};
    property.enumEntries = std::move(options);
    property.getValue = +[](const void* instance, void* outValue) {
        *static_cast<std::int64_t*>(outValue) =
            static_cast<std::int64_t>(static_cast<const OwnerT*>(instance)->*MemberPointer);
    };
    property.setValue = +[](void* instance, const void* inValue) {
        static_cast<OwnerT*>(instance)->*MemberPointer =
            static_cast<ValueT>(*static_cast<const std::int64_t*>(inValue));
    };
    // An enumeration always compares, so an enum property always knows whether it changed.
    property.areEqual = +[](const void* left, const void* right) {
        return static_cast<const OwnerT*>(left)->*MemberPointer == static_cast<const OwnerT*>(right)->*MemberPointer;
    };
    return property;
}

/// Reads a property's current value off `instance` (which must actually be a pointer to the
/// concrete owning type MakeProperty<MemberPointer> was instantiated with). Returns a
/// value-initialized ValueT and does nothing if `property` was never bound to an accessor (a
/// hand-built, describe-only property).
template <typename ValueT, typename OwnerT>
[[nodiscard]] ValueT GetPropertyValue(const TypeMetadataProperty& property, const OwnerT& instance) {
    ValueT value{};
    if (property.getValue != nullptr) {
        property.getValue(&instance, &value);
    }
    return value;
}

/// Writes `value` onto `instance` through `property`'s bound accessor. Does nothing if
/// `property` was never bound to an accessor.
template <typename ValueT, typename OwnerT>
void SetPropertyValue(const TypeMetadataProperty& property, OwnerT& instance, const ValueT& value) {
    if (property.setValue != nullptr) {
        property.setValue(&instance, &value);
    }
}

struct TypeMetadataMethod final {
    std::string name;
    std::string displayName;
    std::uint32_t flags = 0U;

    [[nodiscard]] bool operator==(const TypeMetadataMethod&) const = default;
};

struct TypeMetadataEntry final {
    /// As with TypeMetadataProperty: kind/id/name/version/members are the entry's identity,
    /// and the native-type binding below is attached separately by BindType().
    TypeMetadataEntry() = default;
    TypeMetadataEntry(const TypeMetadataKind entryKind, TypeId entryTypeId,
                         std::string entryDisplayName, const std::uint32_t entryVersion,
                         std::vector<TypeMetadataProperty> entryProperties,
                         std::vector<TypeMetadataMethod> entryMethods)
        : kind(entryKind),
          typeId(std::move(entryTypeId)),
          displayName(std::move(entryDisplayName)),
          version(entryVersion),
          properties(std::move(entryProperties)),
          methods(std::move(entryMethods)) {}

    TypeMetadataKind kind = TypeMetadataKind::Other;
    TypeId typeId;
    std::string displayName;
    std::uint32_t version = 1U;
    std::vector<TypeMetadataProperty> properties;
    std::vector<TypeMetadataMethod> methods;

    /// The bridge. The ECS keys components by std::type_index; this registry keyed them by string
    /// only, so nothing could join an entity's live components to their metadata. Defaults to
    /// typeid(void) for a hand-built, describe-only entry that owns no C++ type.
    std::type_index typeIndex{typeid(void)};
    /// The reflected type's C++ name ("CanvasComponent"), joining the registry's string
    /// type id to the names the serializer's component table and scene files use. Empty for a
    /// hand-built, describe-only entry that owns no C++ type; every declared component entry
    /// sets it, and the scene serializer auto-discovers generic registrations from it.
    std::string cppName;
    /// Default-construct / destroy one instance of the reflected type. Null for a describe-only
    /// entry. This is what makes add-by-type-id, reset-to-default (default-construct and read the
    /// field back) and generic deserialization possible without a per-type branch.
    void* (*createDefaultInstance)() = nullptr;
    void (*destroyInstance)(void* instance) = nullptr;
    /// Copies an existing instance, and overwrites one in place. Together with the pointer the ECS
    /// already hands out for a live component, these are what let an undo record a component's
    /// prior value and put it back without naming the component's type.
    void* (*cloneInstance)(const void* source) = nullptr;
    void (*assignInstance)(void* destination, const void* source) = nullptr;
    /// Archetype-slot construction for the reflected type: its size/alignment plus the
    /// construct-default/move-construct/destroy trio `IEntityManager::AddComponentErased`
    /// needs. A Core-native mirror of `Scene::ComponentTypeInfo` (which Core must not
    /// include); consumers adapt these into one at the attach call site. Zero/null for a
    /// describe-only entry. Set together with the heap factory by `BindType()`.
    std::size_t instanceSize = 0;
    std::size_t instanceAlignment = 0;
    void (*constructDefaultInPlace)(void* destination) = nullptr;
    void (*moveConstructInPlace)(void* destination, void* source) = nullptr;
    void (*destroyInPlace)(void* target) = nullptr;
    /// Sort key for the type's own inspector section, so a common section can be pushed last.
    std::int32_t order = 0;
    /// Inspector presentation: draw this type's section inside a host type's section whenever an
    /// entity carries both - Thread Group belongs under Process, the way a sub-group reads. Hosts
    /// are listed in order of preference and the first one the entity carries draws it, so one
    /// type can belong to different objects (a collider sits in a primitive mesh's section on a
    /// BoxMesh3D and in PhysicsObject3D's on a body). On an entity with none of its hosts it stands
    /// on its own, so nothing ever becomes unreachable. A host may also keep the type's rows in its
    /// own shape rather than draw the section - Character3D authors a collider's Layer and Mask as
    /// the body's own rows, because a body's collision identity is not a separate part of it.
    std::vector<TypeId> nestedUnderTypeIds;
    /// Inspector presentation: draw the properties as rows in place, with no collapsible header,
    /// for a type that is conceptually one property of the object rather than a feature of it - a
    /// object has a script and has metadata, it does not have a "Script section".
    bool presentedInline = false;
    /// Inspector presentation: the section title for one instance, when it depends on the value -
    /// one component shared by several object kinds titles its section with the kind it is on.
    /// Null uses displayName.
    const char* (*sectionTitle)(const void* instance) = nullptr;
    /// The type's own whole-value rule (a mesh reference pair, a non-degenerate size). A generic
    /// editor checks it after writing one property and undoes a write that breaks it, so an edit
    /// can never leave a component that its systems assert against. Null means any value is valid.
    bool (*isInstanceValid)(const void* instance) = nullptr;

    [[nodiscard]] bool operator==(const TypeMetadataEntry&) const = default;

    [[nodiscard]] bool HasFactory() const noexcept {
        return createDefaultInstance != nullptr && destroyInstance != nullptr &&
               cloneInstance != nullptr && assignInstance != nullptr;
    }
};

/// Binds `entry` to the concrete C++ type it describes: its std::type_index (the ECS bridge),
/// its heap factory (default-construct/destroy/clone/assign), and its archetype-slot
/// construction trio (size/alignment plus in-place construct/move/destroy). Kept separate from
/// MakeProperty so an entry's identity and its property list stay independently declarable.
template <typename TypeT>
void BindType(TypeMetadataEntry& entry) {
    entry.typeIndex = std::type_index(typeid(TypeT));
    entry.createDefaultInstance = +[]() -> void* { return new TypeT{}; };
    entry.destroyInstance = +[](void* instance) { delete static_cast<TypeT*>(instance); };
    entry.cloneInstance = +[](const void* source) -> void* {
        return new TypeT{*static_cast<const TypeT*>(source)};
    };
    entry.assignInstance = +[](void* destination, const void* source) {
        *static_cast<TypeT*>(destination) = *static_cast<const TypeT*>(source);
    };
    entry.instanceSize = sizeof(TypeT);
    entry.instanceAlignment = alignof(TypeT);
    entry.constructDefaultInPlace = +[](void* destination) { ::new (destination) TypeT(); };
    entry.moveConstructInPlace = +[](void* destination, void* source) {
        TypeT* const sourceObject = static_cast<TypeT*>(source);
        ::new (destination) TypeT(std::move(*sourceObject));
        sourceObject->~TypeT();
    };
    entry.destroyInPlace = +[](void* target) { static_cast<TypeT*>(target)->~TypeT(); };
}

/// Owns one instance of a reflected type without the owner naming that type. Used to answer "what
/// is this property's default value" (MakeDefault) and to hold a component's prior value for an
/// undo entry (Clone).
class TypeInstance final {
public:
    TypeInstance() = default;

    /// A default-constructed instance, or an invalid holder when `entry` declares no factory.
    [[nodiscard]] static TypeInstance MakeDefault(const TypeMetadataEntry& entry) {
        return entry.HasFactory() ? TypeInstance{entry.createDefaultInstance(), entry.destroyInstance}
                                     : TypeInstance{};
    }

    /// A copy of `source`, which must point at an instance of `entry`'s type. An invalid holder
    /// when `entry` declares no factory or `source` is null.
    [[nodiscard]] static TypeInstance Clone(const TypeMetadataEntry& entry, const void* source) {
        return (entry.HasFactory() && source != nullptr)
                   ? TypeInstance{entry.cloneInstance(source), entry.destroyInstance}
                   : TypeInstance{};
    }

    TypeInstance(const TypeInstance&) = delete;
    TypeInstance& operator=(const TypeInstance&) = delete;

    TypeInstance(TypeInstance&& other) noexcept
        : m_destroy(std::exchange(other.m_destroy, nullptr)),
          m_instance(std::exchange(other.m_instance, nullptr)) {}

    TypeInstance& operator=(TypeInstance&& other) noexcept {
        if (this != &other) {
            Reset();
            m_destroy = std::exchange(other.m_destroy, nullptr);
            m_instance = std::exchange(other.m_instance, nullptr);
        }
        return *this;
    }

    ~TypeInstance() { Reset(); }

    [[nodiscard]] const void* Get() const noexcept { return m_instance; }
    [[nodiscard]] void* GetMutable() noexcept { return m_instance; }
    [[nodiscard]] bool IsValid() const noexcept { return m_instance != nullptr; }

private:
    TypeInstance(void* instance, void (*destroy)(void*)) noexcept
        : m_destroy(destroy), m_instance(instance) {}

    void Reset() noexcept {
        if (m_instance != nullptr && m_destroy != nullptr) {
            m_destroy(m_instance);
        }
        m_instance = nullptr;
    }

    void (*m_destroy)(void*) = nullptr;
    void* m_instance = nullptr;
};

struct TypeMetadataSnapshot final {
    std::uint64_t generation = 0U;
    bool entriesTruncated = false;
    std::vector<TypeMetadataEntry> entries;

    [[nodiscard]] bool operator==(const TypeMetadataSnapshot&) const = default;
};

enum class TypeMetadataRegistrationCode : std::uint8_t {
    Registered = 0,
    InvalidEntry,
    DuplicateType,
    CapacityExceeded,
};

struct TypeMetadataRegistrationResult final {
    TypeMetadataRegistrationCode code = TypeMetadataRegistrationCode::InvalidEntry;
    std::string message;

    [[nodiscard]] bool IsRegistered() const noexcept {
        return code == TypeMetadataRegistrationCode::Registered;
    }
};

class TypeMetadataRegistry final {
public:
    static constexpr std::size_t kMaximumTypes = 4096U;
    static constexpr std::size_t kMaximumMembersPerType = 128U;
    static constexpr std::size_t kMaximumIdentifierBytes = 128U;
    static constexpr std::size_t kMaximumDisplayNameBytes = 256U;

    TypeMetadataRegistry() = default;
    /// Copying stays deleted: a registry is authoritative, and a silent duplicate would let two
    /// consumers disagree about what is registered. Moving is allowed, so a registry can be built
    /// completely and then handed over - which is how a populated one reaches a function-local
    /// static without being assembled in place.
    TypeMetadataRegistry(const TypeMetadataRegistry&) = delete;
    TypeMetadataRegistry& operator=(const TypeMetadataRegistry&) = delete;
    TypeMetadataRegistry(TypeMetadataRegistry&&) noexcept = default;
    TypeMetadataRegistry& operator=(TypeMetadataRegistry&&) noexcept = default;

    [[nodiscard]] TypeMetadataRegistrationResult RegisterType(TypeMetadataEntry entry);
    [[nodiscard]] const TypeMetadataEntry* FindType(TypeId typeId) const noexcept;
    /// The ECS-side lookup: resolves a live component's std::type_index to its metadata. Entries
    /// left at typeid(void) (describe-only) are never matched, so they cannot collide with each
    /// other on the void index.
    [[nodiscard]] const TypeMetadataEntry* FindTypeByIndex(std::type_index typeIndex) const noexcept;
    [[nodiscard]] TypeMetadataSnapshot GetSnapshot() const;
    [[nodiscard]] std::size_t GetTypeCount() const noexcept;
    [[nodiscard]] std::uint64_t GetGeneration() const noexcept;

private:
    std::vector<TypeMetadataEntry> m_entries;
    std::uint64_t m_generation = 0U;
};

} // namespace UVE::Core
