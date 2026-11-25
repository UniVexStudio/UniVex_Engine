// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
#include "object_registry.h"
#include <atomic>
#include <limits>
#include <stdexcept>
#include "handle_table.h"

namespace UVE::Core {
namespace Detail {
struct ObjectRegistryState final {
    explicit ObjectRegistryState(const std::uint64_t identity) : id(identity) {}
    const std::uint64_t id;
    bool shutdown = false;
    Containers::HandleTable<Object *> objects;
};
} // namespace Detail
namespace {
std::uint64_t NextRegistryId() {
    static std::atomic<std::uint64_t> next{1U};
    auto candidate = next.load(std::memory_order_relaxed);
    while (candidate != 0U) {
        const auto successor = candidate == std::numeric_limits<std::uint64_t>::max() ? 0U : candidate + 1U;
        if (next.compare_exchange_weak(candidate, successor, std::memory_order_relaxed))
            return candidate;
    }
    throw std::overflow_error("ObjectRegistry identity space exhausted");
}
} // namespace
Object::~Object() {
    if (const auto state = m_registry.lock())
        state->objects.Release({m_handle.index, m_handle.generation});
}
ObjectRegistry::ObjectRegistry()
    : m_state(std::make_shared<Detail::ObjectRegistryState>(NextRegistryId())) {}
ObjectRegistry::~ObjectRegistry() { Shutdown(); }
bool ObjectRegistry::Register(Object &object, ObjectHandle &outHandle) {
    if (m_state->shutdown || object.m_handle.IsValid())
        return false;
    const auto slot = m_state->objects.Acquire(&object);
    object.m_registry = m_state;
    object.m_handle = {m_state->id, slot.index, slot.generation};
    outHandle = object.m_handle;
    return true;
}
bool ObjectRegistry::Unregister(const ObjectHandle handle) noexcept {
    auto *object = Find(handle);
    if (object == nullptr)
        return false;
    object->m_handle = {};
    object->m_registry.reset();
    return m_state->objects.Release({handle.index, handle.generation});
}
Object *ObjectRegistry::Find(const ObjectHandle handle) noexcept {
    if (handle.registry != m_state->id)
        return nullptr;
    auto *slot = m_state->objects.Find({handle.index, handle.generation});
    return slot == nullptr ? nullptr : *slot;
}
const Object *ObjectRegistry::Find(const ObjectHandle handle) const noexcept {
    if (handle.registry != m_state->id)
        return nullptr;
    const auto *slot = m_state->objects.Find({handle.index, handle.generation});
    return slot == nullptr ? nullptr : *slot;
}
std::size_t ObjectRegistry::GetLiveCount() const noexcept { return m_state->objects.GetLiveCount(); }
bool ObjectRegistry::IsShutdown() const noexcept { return m_state->shutdown; }
void ObjectRegistry::Clear() noexcept {
    m_state->objects.ForEach([](const Containers::SlotHandle, Object *object) {
        object->m_handle = {};
        object->m_registry.reset();
    });
    m_state->objects.Clear();
}
void ObjectRegistry::Shutdown() noexcept {
    m_state->shutdown = true;
    Clear();
}
} // namespace UVE::Core
