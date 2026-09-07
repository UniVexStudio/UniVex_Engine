// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace UVE::Core {

enum class TypeMetadataKindUVE : std::uint8_t {
    Component = 0,
    Resource,
    VisualScriptNode,
    InspectorTarget,
    Other,
};

struct TypeMetadataPropertyUVE final {
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

    [[nodiscard]] bool operator==(const TypeMetadataPropertyUVE&) const = default;
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

    TypeMetadataPropertyUVE property;
    property.name = std::move(name);
    property.displayName = std::move(displayName);
    property.typeId = std::move(typeId);
    property.editable = editable;
    property.getValue = +[](const void* instance, void* outValue) {
        *static_cast<ValueT*>(outValue) = static_cast<const OwnerT*>(instance)->*MemberPointer;
    };
    property.setValue = +[](void* instance, const void* inValue) {
        static_cast<OwnerT*>(instance)->*MemberPointer = *static_cast<const ValueT*>(inValue);
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
    TypeMetadataKindUVE kind = TypeMetadataKindUVE::Other;
    std::string typeId;
    std::string displayName;
    std::uint32_t version = 1U;
    std::vector<TypeMetadataPropertyUVE> properties;
    std::vector<TypeMetadataMethodUVE> methods;

    [[nodiscard]] bool operator==(const TypeMetadataEntryUVE&) const = default;
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
    TypeMetadataRegistryUVE(const TypeMetadataRegistryUVE&) = delete;
    TypeMetadataRegistryUVE& operator=(const TypeMetadataRegistryUVE&) = delete;

    [[nodiscard]] TypeMetadataRegistrationResultUVE RegisterTypeUVE(TypeMetadataEntryUVE entry);
    [[nodiscard]] const TypeMetadataEntryUVE* FindTypeUVE(std::string_view typeId) const noexcept;
    [[nodiscard]] TypeMetadataSnapshotUVE GetSnapshotUVE() const;
    [[nodiscard]] std::size_t GetTypeCountUVE() const noexcept;
    [[nodiscard]] std::uint64_t GetGenerationUVE() const noexcept;

private:
    std::vector<TypeMetadataEntryUVE> m_entries;
    std::uint64_t m_generation = 0U;
};

} // namespace UVE::Core
