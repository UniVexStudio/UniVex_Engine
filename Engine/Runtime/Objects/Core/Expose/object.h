// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
#pragma once
#include <cstdint>
#include <memory>

namespace UVE::Core {
namespace Detail {
struct ObjectRegistryState;
}
class ObjectRegistry;

/// Runtime identity, scoped to the issuing registry. Not a serialized id or a native-plugin ABI.
/// Slot generations come from the existing HandleTable; the registry tag rejects foreign ids.
struct ObjectHandle final {
    std::uint64_t registry = 0U;
    std::uint32_t index = 0U;
    std::uint32_t generation = 0U;
    [[nodiscard]] bool IsValid() const noexcept { return registry != 0U && generation != 0U; }
    [[nodiscard]] bool operator==(const ObjectHandle &) const noexcept = default;
};

/// Opt-in identity for caller-owned native objects, NOT a replacement for ECS/resource handles.
/// Registration is explicit. Destruction unregisters; registry destruction detaches without
/// deleting. The object address is stable (no copy/move). Synchronize registration, lookup,
/// borrowed-pointer use AND destruction externally; this is not a concurrent lifetime pin.
class Object {
  public:
    Object() = default;
    virtual ~Object();
    Object(const Object &) = delete;
    Object &operator=(const Object &) = delete;
    Object(Object &&) = delete;
    Object &operator=(Object &&) = delete;
    [[nodiscard]] ObjectHandle GetHandle() const noexcept { return m_handle; }

  private:
    friend class ObjectRegistry;
    ObjectHandle m_handle;
    std::weak_ptr<Detail::ObjectRegistryState> m_registry;
};
} // namespace UVE::Core
