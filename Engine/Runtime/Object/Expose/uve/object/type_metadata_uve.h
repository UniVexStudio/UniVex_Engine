// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <type_traits>
#include <typeindex>
#include <typeinfo>
#include <utility>
#include <vector>

namespace UVE::Core {

enum class TypeMetadataKindUVE : std::uint8_t {
    Component = 0,
    Resource,
    VisualScriptNode,
    InspectorTarget,
    Other,
};

/// Declared traits a generic consumer (the inspector, the serializer) needs in order to treat a
/// property correctly without knowing its concrete type. Every one of these previously had to be
/// re-derived by a hand-written per-type branch at each consumer.
enum class TypeMetadataPropertyFlagsUVE : std::uint32_t {
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
    /// Holds a Scene::EntityUVE that must be remapped through the serializer's local-id table.
    EntityReference = 1U << 5U,
};

[[nodiscard]] constexpr TypeMetadataPropertyFlagsUVE operator|(const TypeMetadataPropertyFlagsUVE left,
                                                               const TypeMetadataPropertyFlagsUVE right) noexcept {
    return static_cast<TypeMetadataPropertyFlagsUVE>(static_cast<std::uint32_t>(left) |
                                                     static_cast<std::uint32_t>(right));
}

[[nodiscard]] constexpr bool HasPropertyFlagUVE(const TypeMetadataPropertyFlagsUVE value,
                                                const TypeMetadataPropertyFlagsUVE flag) noexcept {
    return (static_cast<std::uint32_t>(value) & static_cast<std::uint32_t>(flag)) != 0U;
}

/// Bounds for a numeric property, so the inspector picks a slider/step without a per-type branch.
/// `enabled` distinguishes "no range declared" from a legitimate [0, 0] range.
struct TypeMetadataNumericRangeUVE final {
    bool enabled = false;
    double minimum = 0.0;
    double maximum = 0.0;
    double step = 0.0;

    [[nodiscard]] bool operator==(const TypeMetadataNumericRangeUVE&) const = default;
};

/// One selectable value of an enum property. The inspector renders these as a collapsed dropdown;
/// the underlying value is carried explicitly so label order never has to match enumerator order.
struct TypeMetadataEnumEntryUVE final {
    std::int64_t value = 0;
    std::string label;

    [[nodiscard]] bool operator==(const TypeMetadataEnumEntryUVE&) const = default;
};

struct TypeMetadataPropertyUVE final {
    /// A property's identity is its name, label, type and writability; everything below is opt-in
    /// metadata a declaration adds only when it has something to say. Spelling that as a
    /// constructor rather than leaving the struct an aggregate means adding a new trait never
    /// forces every existing declaration to brace-initialize it.
    TypeMetadataPropertyUVE() = default;
    TypeMetadataPropertyUVE(std::string propertyName, std::string propertyDisplayName,
                            std::string propertyTypeId, const bool isEditable)
        : name(std::move(propertyName)),
          displayName(std::move(propertyDisplayName)),
          typeId(std::move(propertyTypeId)),
          editable(isEditable) {}

    std::string name;
    std::string displayName;
    std::string typeId;
    bool editable = false;

    /// Type-erased accessors bound to a specific member by MakePropertyUVE() below - null for a
    /// property built by hand (e.g. in existing tests/data that only describe a property without
    /// needing to read/write it). `instance`/`value` are always `OwnerT*`/`ValueT*` respectively;
    /// callers use GetPropertyValueUVE()/SetPropertyValueUVE() rather than calling these directly
    /// to keep that contract type-safe at the call site.
    void (*getValue)(const void* instance, void* outValue) = nullptr;
    void (*setValue)(void* instance, const void* inValue) = nullptr;

    /// Inspector grouping. Empty means the owning type's own section. Properties sort by
    /// (section, order, declaration index), which is how the common Node section is kept last
    /// without the inspector knowing what a "Node section" is.
    std::string section;
    std::int32_t order = 0;
    TypeMetadataPropertyFlagsUVE flags = TypeMetadataPropertyFlagsUVE::None;
    TypeMetadataNumericRangeUVE range;
    std::vector<TypeMetadataEnumEntryUVE> enumEntries;
    std::string tooltip;
    /// Opt-in escape hatch: names a registered custom drawer for the cases a generic editor cannot
    /// serve correctly (a transform that must round-trip through euler sync, an entity picker).
    std::string customDrawerId;
    /// Conditional visibility. Null means always visible; otherwise the inspector calls it with a
    /// pointer to the owning instance, so a property can depend on a sibling field's value.
    bool (*isVisible)(const void* instance) = nullptr;

    [[nodiscard]] bool operator==(const TypeMetadataPropertyUVE&) const = default;

    /// True when authoring must not write this property, whether declared read-only outright or
    /// because a runtime system owns it. Both consumers ask this rather than re-deriving it.
    [[nodiscard]] bool IsAuthoringWritableUVE() const noexcept {
        return editable && setValue != nullptr &&
               !HasPropertyFlagUVE(flags, TypeMetadataPropertyFlagsUVE::ReadOnly) &&
               !HasPropertyFlagUVE(flags, TypeMetadataPropertyFlagsUVE::RuntimeState);
    }

    /// True when the property belongs in persisted scene data. Runtime-derived state is excluded:
    /// persisting it would fight the system that recomputes it every frame.
    [[nodiscard]] bool IsSerializedUVE() const noexcept {
        return getValue != nullptr && setValue != nullptr &&
               !HasPropertyFlagUVE(flags, TypeMetadataPropertyFlagsUVE::RuntimeState);
    }
};

namespace Detail {
template <typename MemberPointerT>
struct MemberPointerTraitsUVE;

template <typename OwnerT, typename ValueT>
struct MemberPointerTraitsUVE<ValueT OwnerT::*> {
    using Owner = OwnerT;
    using Value = ValueT;
};
} // namespace Detail

/// Builds a TypeMetadataPropertyUVE bound to `MemberPointer` via GetPropertyValueUVE()/
/// SetPropertyValueUVE() below, so reflection callers (an inspector, a serializer) can read/write
/// a registered type's property generically without knowing its concrete type ahead of time.
/// This is the genuine capability gap TypeMetadataPropertyUVE previously had - it could only
/// describe a property's existence, never actually get or set one.
template <auto MemberPointer>
[[nodiscard]] TypeMetadataPropertyUVE MakePropertyUVE(std::string name, std::string displayName,
                                                       std::string typeId, bool editable) {
    using Traits = Detail::MemberPointerTraitsUVE<decltype(MemberPointer)>;
    using OwnerT = typename Traits::Owner;
    using ValueT = typename Traits::Value;

    TypeMetadataPropertyUVE property{std::move(name), std::move(displayName), std::move(typeId), editable};
    property.getValue = +[](const void* instance, void* outValue) {
        *static_cast<ValueT*>(outValue) = static_cast<const OwnerT*>(instance)->*MemberPointer;
    };
    property.setValue = +[](void* instance, const void* inValue) {
        static_cast<OwnerT*>(instance)->*MemberPointer = *static_cast<const ValueT*>(inValue);
    };
    return property;
}

/// Builds a property for an enum member whose accessors speak std::int64_t rather than the
/// concrete enumeration. A generic consumer cannot name LightTypeUVE or ColliderShapeTypeUVE, so
/// without this every enum would force exactly the per-type branch this registry exists to remove.
/// `options` carries each selectable value with its label, so the dropdown round-trips a selection
/// back to a real enumerator instead of relying on label order matching declaration order.
template <auto MemberPointer>
[[nodiscard]] TypeMetadataPropertyUVE MakeEnumPropertyUVE(std::string name, std::string displayName,
                                                          std::string typeId, const bool editable,
                                                          std::vector<TypeMetadataEnumEntryUVE> options) {
    using Traits = Detail::MemberPointerTraitsUVE<decltype(MemberPointer)>;
    using OwnerT = typename Traits::Owner;
    using ValueT = typename Traits::Value;
    static_assert(std::is_enum_v<ValueT>, "MakeEnumPropertyUVE requires an enumeration member.");

    TypeMetadataPropertyUVE property{std::move(name), std::move(displayName), std::move(typeId), editable};
    property.enumEntries = std::move(options);
    property.getValue = +[](const void* instance, void* outValue) {
        *static_cast<std::int64_t*>(outValue) =
            static_cast<std::int64_t>(static_cast<const OwnerT*>(instance)->*MemberPointer);
    };
    property.setValue = +[](void* instance, const void* inValue) {
        static_cast<OwnerT*>(instance)->*MemberPointer =
            static_cast<ValueT>(*static_cast<const std::int64_t*>(inValue));
    };
    return property;
}

/// Reads a property's current value off `instance` (which must actually be a pointer to the
/// concrete owning type MakePropertyUVE<MemberPointer> was instantiated with). Returns a
/// value-initialized ValueT and does nothing if `property` was never bound to an accessor (a
/// hand-built, describe-only property).
template <typename ValueT, typename OwnerT>
[[nodiscard]] ValueT GetPropertyValueUVE(const TypeMetadataPropertyUVE& property, const OwnerT& instance) {
    ValueT value{};
    if (property.getValue != nullptr) {
        property.getValue(&instance, &value);
    }
    return value;
}

/// Writes `value` onto `instance` through `property`'s bound accessor. Does nothing if
/// `property` was never bound to an accessor.
template <typename ValueT, typename OwnerT>
void SetPropertyValueUVE(const TypeMetadataPropertyUVE& property, OwnerT& instance, const ValueT& value) {
    if (property.setValue != nullptr) {
        property.setValue(&instance, &value);
    }
}

struct TypeMetadataMethodUVE final {
    std::string name;
    std::string displayName;
    std::uint32_t flags = 0U;

    [[nodiscard]] bool operator==(const TypeMetadataMethodUVE&) const = default;
};

struct TypeMetadataEntryUVE final {
    /// As with TypeMetadataPropertyUVE: kind/id/name/version/members are the entry's identity,
    /// and the native-type binding below is attached separately by BindTypeUVE().
    TypeMetadataEntryUVE() = default;
    TypeMetadataEntryUVE(const TypeMetadataKindUVE entryKind, std::string entryTypeId,
                         std::string entryDisplayName, const std::uint32_t entryVersion,
                         std::vector<TypeMetadataPropertyUVE> entryProperties,
                         std::vector<TypeMetadataMethodUVE> entryMethods)
        : kind(entryKind),
          typeId(std::move(entryTypeId)),
          displayName(std::move(entryDisplayName)),
          version(entryVersion),
          properties(std::move(entryProperties)),
          methods(std::move(entryMethods)) {}

    TypeMetadataKindUVE kind = TypeMetadataKindUVE::Other;
    std::string typeId;
    std::string displayName;
    std::uint32_t version = 1U;
    std::vector<TypeMetadataPropertyUVE> properties;
    std::vector<TypeMetadataMethodUVE> methods;

    /// The bridge. The ECS keys components by std::type_index; this registry keyed them by string
    /// only, so nothing could join an entity's live components to their metadata. Defaults to
    /// typeid(void) for a hand-built, describe-only entry that owns no C++ type.
    std::type_index typeIndex{typeid(void)};
    /// Default-construct / destroy one instance of the reflected type. Null for a describe-only
    /// entry. This is what makes add-by-type-id, reset-to-default (default-construct and read the
    /// field back) and generic deserialization possible without a per-type branch.
    void* (*createDefaultInstance)() = nullptr;
    void (*destroyInstance)(void* instance) = nullptr;
    /// Sort key for the type's own inspector section, so a common section can be pushed last.
    std::int32_t order = 0;

    [[nodiscard]] bool operator==(const TypeMetadataEntryUVE&) const = default;

    [[nodiscard]] bool HasFactoryUVE() const noexcept {
        return createDefaultInstance != nullptr && destroyInstance != nullptr;
    }
};

/// Binds `entry` to the concrete C++ type it describes: its std::type_index (the ECS bridge) and
/// its default-construct/destroy pair. Kept separate from MakePropertyUVE so an entry's identity
/// and its property list stay independently declarable.
template <typename TypeT>
void BindTypeUVE(TypeMetadataEntryUVE& entry) {
    entry.typeIndex = std::type_index(typeid(TypeT));
    entry.createDefaultInstance = +[]() -> void* { return new TypeT{}; };
    entry.destroyInstance = +[](void* instance) { delete static_cast<TypeT*>(instance); };
}

/// Owns one default-constructed instance produced by a registered entry's factory. Used to answer
/// "what is this property's default value" without the caller naming the concrete type.
class TypeDefaultInstanceUVE final {
public:
    TypeDefaultInstanceUVE() = default;

    explicit TypeDefaultInstanceUVE(const TypeMetadataEntryUVE& entry)
        : m_destroy(entry.destroyInstance),
          m_instance(entry.HasFactoryUVE() ? entry.createDefaultInstance() : nullptr) {}

    TypeDefaultInstanceUVE(const TypeDefaultInstanceUVE&) = delete;
    TypeDefaultInstanceUVE& operator=(const TypeDefaultInstanceUVE&) = delete;

    TypeDefaultInstanceUVE(TypeDefaultInstanceUVE&& other) noexcept
        : m_destroy(std::exchange(other.m_destroy, nullptr)),
          m_instance(std::exchange(other.m_instance, nullptr)) {}

    TypeDefaultInstanceUVE& operator=(TypeDefaultInstanceUVE&& other) noexcept {
        if (this != &other) {
            ResetUVE();
            m_destroy = std::exchange(other.m_destroy, nullptr);
            m_instance = std::exchange(other.m_instance, nullptr);
        }
        return *this;
    }

    ~TypeDefaultInstanceUVE() { ResetUVE(); }

    [[nodiscard]] const void* GetUVE() const noexcept { return m_instance; }
    [[nodiscard]] bool IsValidUVE() const noexcept { return m_instance != nullptr; }

private:
    void ResetUVE() noexcept {
        if (m_instance != nullptr && m_destroy != nullptr) {
            m_destroy(m_instance);
        }
        m_instance = nullptr;
    }

    void (*m_destroy)(void*) = nullptr;
    void* m_instance = nullptr;
};

struct TypeMetadataSnapshotUVE final {
    std::uint64_t generation = 0U;
    bool entriesTruncated = false;
    std::vector<TypeMetadataEntryUVE> entries;

    [[nodiscard]] bool operator==(const TypeMetadataSnapshotUVE&) const = default;
};

enum class TypeMetadataRegistrationCodeUVE : std::uint8_t {
    Registered = 0,
    InvalidEntry,
    DuplicateType,
    CapacityExceeded,
};

struct TypeMetadataRegistrationResultUVE final {
    TypeMetadataRegistrationCodeUVE code = TypeMetadataRegistrationCodeUVE::InvalidEntry;
    std::string message;

    [[nodiscard]] bool IsRegisteredUVE() const noexcept {
        return code == TypeMetadataRegistrationCodeUVE::Registered;
    }
};

class TypeMetadataRegistryUVE final {
public:
    static constexpr std::size_t kMaximumTypesUVE = 256U;
    static constexpr std::size_t kMaximumMembersPerTypeUVE = 128U;
    static constexpr std::size_t kMaximumIdentifierBytesUVE = 128U;
    static constexpr std::size_t kMaximumDisplayNameBytesUVE = 256U;

    TypeMetadataRegistryUVE() = default;
    /// Copying stays deleted: a registry is authoritative, and a silent duplicate would let two
    /// consumers disagree about what is registered. Moving is allowed, so a registry can be built
    /// completely and then handed over - which is how a populated one reaches a function-local
    /// static without being assembled in place.
    TypeMetadataRegistryUVE(const TypeMetadataRegistryUVE&) = delete;
    TypeMetadataRegistryUVE& operator=(const TypeMetadataRegistryUVE&) = delete;
    TypeMetadataRegistryUVE(TypeMetadataRegistryUVE&&) noexcept = default;
    TypeMetadataRegistryUVE& operator=(TypeMetadataRegistryUVE&&) noexcept = default;

    [[nodiscard]] TypeMetadataRegistrationResultUVE RegisterTypeUVE(TypeMetadataEntryUVE entry);
    [[nodiscard]] const TypeMetadataEntryUVE* FindTypeUVE(std::string_view typeId) const noexcept;
    /// The ECS-side lookup: resolves a live component's std::type_index to its metadata. Entries
    /// left at typeid(void) (describe-only) are never matched, so they cannot collide with each
    /// other on the void index.
    [[nodiscard]] const TypeMetadataEntryUVE* FindTypeByIndexUVE(std::type_index typeIndex) const noexcept;
    [[nodiscard]] TypeMetadataSnapshotUVE GetSnapshotUVE() const;
    [[nodiscard]] std::size_t GetTypeCountUVE() const noexcept;
    [[nodiscard]] std::uint64_t GetGenerationUVE() const noexcept;

private:
    std::vector<TypeMetadataEntryUVE> m_entries;
    std::uint64_t m_generation = 0U;
};

} // namespace UVE::Core
