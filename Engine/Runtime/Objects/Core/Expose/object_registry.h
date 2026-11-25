// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
#pragma once
#include <cstddef>
#include <memory>
#include "object.h"

namespace UVE::Core {
/// Non-owning registry over HandleTable<Object*>. Either registry or objects may die
/// first. Clear preserves generation history; Shutdown is permanent and idempotent. Returned
/// pointers are borrowed until detach/destruction. All operations require caller synchronization.
class ObjectRegistry final {
  public:
    ObjectRegistry();
    ~ObjectRegistry();
    ObjectRegistry(const ObjectRegistry &) = delete;
    ObjectRegistry &operator=(const ObjectRegistry &) = delete;
    ObjectRegistry(ObjectRegistry &&) = delete;
    ObjectRegistry &operator=(ObjectRegistry &&) = delete;
    /// Duplicate (including another registry) or shutdown: false, output/object unchanged.
    /// Allocation failure propagates. Registry identities never wrap; exhaustion throws.
    [[nodiscard]] bool Register(Object &object, ObjectHandle &outHandle);
    [[nodiscard]] bool Unregister(ObjectHandle handle) noexcept;
    [[nodiscard]] Object *Find(ObjectHandle handle) noexcept;
    [[nodiscard]] const Object *Find(ObjectHandle handle) const noexcept;
    [[nodiscard]] std::size_t GetLiveCount() const noexcept;
    [[nodiscard]] bool IsShutdown() const noexcept;
    void Clear() noexcept;
    void Shutdown() noexcept;

  private:
    std::shared_ptr<Detail::ObjectRegistryState> m_state;
};
} // namespace UVE::Core
