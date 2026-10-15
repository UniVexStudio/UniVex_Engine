// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
#include "uve/object/object_registry_uve.h"
#include <atomic>
#include <limits>
#include <stdexcept>
#include "uve/containers/handle_table_uve.h"

namespace UVE::Core {
namespace Detail {
struct ObjectRegistryStateUVE final {
    explicit ObjectRegistryStateUVE(const std::uint64_t identity) : id(identity) {}
    const std::uint64_t id;
    bool shutdown = false;
    Containers::HandleTableUVE<ObjectUVE *> objects;
};
} // namespace Detail
namespace {
std::uint64_t NextRegistryIdUVE() {
    static std::atomic<std::uint64_t> next{1U};
    auto candidate = next.load(std::memory_order_relaxed);
    while (candidate != 0U) {
        const auto successor = candidate == std::numeric_limits<std::uint64_t>::max() ? 0U : candidate + 1U;
        if (next.compare_exchange_weak(candidate, successor, std::memory_order_relaxed))
            return candidate;
    }
    throw std::overflow_error("ObjectRegistryUVE identity space exhausted");
}
} // namespace
ObjectUVE::~ObjectUVE() {
    if (const auto state = m_registry.lock())
        state->objects.ReleaseUVE({m_handle.index, m_handle.generation});
}
ObjectRegistryUVE::ObjectRegistryUVE()
    : m_state(std::make_shared<Detail::ObjectRegistryStateUVE>(NextRegistryIdUVE())) {}
ObjectRegistryUVE::~ObjectRegistryUVE() { ShutdownUVE(); }
bool ObjectRegistryUVE::RegisterUVE(ObjectUVE &object, ObjectHandleUVE &outHandle) {
    if (m_state->shutdown || object.m_handle.IsValidUVE())
        return false;
    const auto slot = m_state->objects.AcquireUVE(&object);
    object.m_registry = m_state;
    object.m_handle = {m_state->id, slot.index, slot.generation};
    outHandle = object.m_handle;
    return true;
}
bool ObjectRegistryUVE::UnregisterUVE(const ObjectHandleUVE handle) noexcept {
    auto *object = FindUVE(handle);
    if (object == nullptr)
        return false;
    object->m_handle = {};
    object->m_registry.reset();
    return m_state->objects.ReleaseUVE({handle.index, handle.generation});
}
ObjectUVE *ObjectRegistryUVE::FindUVE(const ObjectHandleUVE handle) noexcept {
    if (handle.registry != m_state->id)
        return nullptr;
    auto *slot = m_state->objects.FindUVE({handle.index, handle.generation});
    return slot == nullptr ? nullptr : *slot;
}
const ObjectUVE *ObjectRegistryUVE::FindUVE(const ObjectHandleUVE handle) const noexcept {
    if (handle.registry != m_state->id)
        return nullptr;
    const auto *slot = m_state->objects.FindUVE({handle.index, handle.generation});
    return slot == nullptr ? nullptr : *slot;
}
std::size_t ObjectRegistryUVE::GetLiveCountUVE() const noexcept { return m_state->objects.GetLiveCountUVE(); }
bool ObjectRegistryUVE::IsShutdownUVE() const noexcept { return m_state->shutdown; }
void ObjectRegistryUVE::ClearUVE() noexcept {
    m_state->objects.ForEachUVE([](const Containers::SlotHandleUVE, ObjectUVE *object) {
        object->m_handle = {};
        object->m_registry.reset();
    });
    m_state->objects.ClearUVE();
}
void ObjectRegistryUVE::ShutdownUVE() noexcept {
    m_state->shutdown = true;
    ClearUVE();
}
} // namespace UVE::Core
