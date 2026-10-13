// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
#pragma once
#include <cstddef>
#include <memory>
#include "uve/object/object_uve.h"

namespace UVE::Core {
/// Non-owning registry over HandleTableUVE<ObjectUVE*>. Either registry or objects may die
/// first. Clear preserves generation history; Shutdown is permanent and idempotent. Returned
/// pointers are borrowed until detach/destruction. All operations require caller synchronization.
class ObjectRegistryUVE final {
  public:
    ObjectRegistryUVE();
    ~ObjectRegistryUVE();
    ObjectRegistryUVE(const ObjectRegistryUVE &) = delete;
    ObjectRegistryUVE &operator=(const ObjectRegistryUVE &) = delete;
    ObjectRegistryUVE(ObjectRegistryUVE &&) = delete;
    ObjectRegistryUVE &operator=(ObjectRegistryUVE &&) = delete;
    /// Duplicate (including another registry) or shutdown: false, output/object unchanged.
    /// Allocation failure propagates. Registry identities never wrap; exhaustion throws.
    [[nodiscard]] bool RegisterUVE(ObjectUVE &object, ObjectHandleUVE &outHandle);
    [[nodiscard]] bool UnregisterUVE(ObjectHandleUVE handle) noexcept;
    [[nodiscard]] ObjectUVE *FindUVE(ObjectHandleUVE handle) noexcept;
    [[nodiscard]] const ObjectUVE *FindUVE(ObjectHandleUVE handle) const noexcept;
    [[nodiscard]] std::size_t GetLiveCountUVE() const noexcept;
    [[nodiscard]] bool IsShutdownUVE() const noexcept;
    void ClearUVE() noexcept;
    void ShutdownUVE() noexcept;

  private:
    std::shared_ptr<Detail::ObjectRegistryStateUVE> m_state;
};
} // namespace UVE::Core
