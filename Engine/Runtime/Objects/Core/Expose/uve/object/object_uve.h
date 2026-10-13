// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
#pragma once
#include <cstdint>
#include <memory>

namespace UVE::Core {
namespace Detail {
struct ObjectRegistryStateUVE;
}
class ObjectRegistryUVE;

/// Runtime identity, scoped to the issuing registry. Not a serialized id or a native-plugin ABI.
/// Slot generations come from the existing HandleTableUVE; the registry tag rejects foreign ids.
struct ObjectHandleUVE final {
    std::uint64_t registry = 0U;
    std::uint32_t index = 0U;
    std::uint32_t generation = 0U;
    [[nodiscard]] bool IsValidUVE() const noexcept { return registry != 0U && generation != 0U; }
    [[nodiscard]] bool operator==(const ObjectHandleUVE &) const noexcept = default;
};

/// Opt-in identity for caller-owned native objects, NOT a replacement for ECS/resource handles.
/// Registration is explicit. Destruction unregisters; registry destruction detaches without
/// deleting. The object address is stable (no copy/move). Synchronize registration, lookup,
/// borrowed-pointer use AND destruction externally; this is not a concurrent lifetime pin.
class ObjectUVE {
  public:
    ObjectUVE() = default;
    virtual ~ObjectUVE();
    ObjectUVE(const ObjectUVE &) = delete;
    ObjectUVE &operator=(const ObjectUVE &) = delete;
    ObjectUVE(ObjectUVE &&) = delete;
    ObjectUVE &operator=(ObjectUVE &&) = delete;
    [[nodiscard]] ObjectHandleUVE GetHandleUVE() const noexcept { return m_handle; }

  private:
    friend class ObjectRegistryUVE;
    ObjectHandleUVE m_handle;
    std::weak_ptr<Detail::ObjectRegistryStateUVE> m_registry;
};
} // namespace UVE::Core
