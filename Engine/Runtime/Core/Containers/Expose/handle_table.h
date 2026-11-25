// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
#pragma once
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <optional>
#include <stdexcept>
#include <utility>
#include <vector>
#include "engine_assert.h"

namespace UVE::Containers {
struct SlotHandle {
    std::uint32_t index = 0U;
    std::uint32_t generation = 0U;
    [[nodiscard]] static constexpr SlotHandle FromIndexAndGeneration(std::uint32_t i,
                                                                           std::uint32_t g) noexcept {
        return {i, g};
    }
    [[nodiscard]] constexpr std::uint32_t Index() const noexcept { return index; }
    [[nodiscard]] constexpr std::uint32_t Generation() const noexcept { return generation; }
    [[nodiscard]] static constexpr std::uint32_t NextGeneration(std::uint32_t current) noexcept {
        const auto next = current + 1U;
        return next == 0U ? 1U : next;
    }
    [[nodiscard]] static constexpr SlotHandle Invalid() noexcept { return {}; }
};
[[nodiscard]] constexpr bool operator==(const SlotHandle &a, const SlotHandle &b) noexcept {
    return a.index == b.index && a.generation == b.generation;
}
[[nodiscard]] constexpr bool operator!=(const SlotHandle &a, const SlotHandle &b) noexcept {
    return !(a == b);
}
} // namespace UVE::Containers

template <> struct std::hash<UVE::Containers::SlotHandle> {
    [[nodiscard]] std::size_t operator()(const UVE::Containers::SlotHandle &handle) const noexcept {
        return std::hash<std::uint64_t>{}((static_cast<std::uint64_t>(handle.index) << 32U) |
                                          handle.generation);
    }
};

namespace UVE::Containers {
/// Owning generational slot map. Clear preserves generation history; exhausted slots retire
/// permanently instead of reviving stale handles. Generation zero is never minted.
/// Handle supplies FromIndexAndGeneration, Index, Generation, NextGeneration;
/// generations must increase until wrap (which retires the slot). Narrow index domains may
/// reject indices in their handle factory. T need not be default constructible.
///
/// Handles are TABLE-RELATIVE, not owner tagged. Use only the issuing table (or a scoped
/// wrapper such as ObjectRegistry). Moving transfers that domain to the destination;
/// replacing/copy-assigning a table invalidates the destination's old domain.
/// Growth can invalidate Find pointers. Releasing another slot does not. Release inside
/// ForEach is safe; Acquire inside it and reentrant mutation from T constructors/destructors
/// are not. Release/Clear never allocate. Vector's normal relocation guarantees apply when
/// T has a throwing move; a non-copyable throwing move cannot promise strong rollback.
/// Thread-safety: externally synchronize all access, including borrowed-pointer use.
template <typename T, typename Handle = SlotHandle> class HandleTable final {
  public:
    HandleTable() = default;
    HandleTable(const HandleTable &) = default;
    HandleTable &operator=(const HandleTable &other) {
        if (this != &other) {
            HandleTable copy(other);
            m_slots.swap(copy.m_slots);
            std::swap(m_freeHead, copy.m_freeHead);
            std::swap(m_liveCount, copy.m_liveCount);
        }
        return *this;
    }
    HandleTable(HandleTable &&other) noexcept
        : m_slots(std::move(other.m_slots)), m_freeHead(std::exchange(other.m_freeHead, kNoSlot)),
          m_liveCount(std::exchange(other.m_liveCount, 0U)) {
        other.m_slots.clear();
    }
    HandleTable &operator=(HandleTable &&other) noexcept {
        if (this != &other) {
            m_slots = std::move(other.m_slots);
            m_freeHead = std::exchange(other.m_freeHead, kNoSlot);
            m_liveCount = std::exchange(other.m_liveCount, 0U);
            other.m_slots.clear();
        }
        return *this;
    }
    template <typename... Args> Handle Acquire(Args &&...args) {
        if (m_freeHead != kNoSlot) {
            const auto index = m_freeHead;
            auto &slot = m_slots[index];
            const auto handle = Handle::FromIndexAndGeneration(index, slot.generation);
            slot.occupant.emplace(std::forward<Args>(args)...); // before consuming the free slot
            m_freeHead = slot.nextFree;
            slot.nextFree = kNoSlot;
            ++m_liveCount;
            return handle;
        }
        if (m_slots.size() >= static_cast<std::size_t>(kNoSlot)) {
            throw std::length_error("HandleTable index space exhausted");
        }
        const auto index = static_cast<std::uint32_t>(m_slots.size());
        Slot slot;
        slot.generation = Handle::NextGeneration(0U);
        UVE_ASSERT(slot.generation != 0U);
        const auto handle = Handle::FromIndexAndGeneration(index, slot.generation);
        slot.occupant.emplace(std::forward<Args>(args)...);
        m_slots.push_back(std::move(slot));
        ++m_liveCount;
        return handle;
    }
    /// False for invalid, stale or out-of-range handles. The dead-slot free list cannot allocate.
    bool Release(Handle handle) noexcept {
        auto *slot = FindSlot(handle);
        if (slot == nullptr)
            return false;
        Advance(*slot);
        slot->occupant.reset();
        if (slot->generation != 0U) {
            slot->nextFree = m_freeHead;
            m_freeHead = handle.Index();
        }
        --m_liveCount;
        return true;
    }
    [[nodiscard]] T *Find(Handle handle) noexcept {
        auto *slot = FindSlot(handle);
        return slot == nullptr ? nullptr : &*slot->occupant;
    }
    [[nodiscard]] const T *Find(Handle handle) const noexcept {
        const auto *slot = FindSlot(handle);
        return slot == nullptr ? nullptr : &*slot->occupant;
    }
    [[nodiscard]] bool Contains(Handle handle) const noexcept { return FindSlot(handle) != nullptr; }
    [[nodiscard]] std::size_t GetLiveCount() const noexcept { return m_liveCount; }
    template <typename Callback> void ForEach(Callback &&callback) {
        for (std::size_t index = 0U; index < m_slots.size(); ++index) {
            auto &slot = m_slots[index];
            if (slot.occupant)
                callback(
                    Handle::FromIndexAndGeneration(static_cast<std::uint32_t>(index), slot.generation),
                    *slot.occupant);
        }
    }
    template <typename Callback> void ForEach(Callback &&callback) const {
        for (std::size_t index = 0U; index < m_slots.size(); ++index) {
            const auto &slot = m_slots[index];
            if (slot.occupant)
                callback(
                    Handle::FromIndexAndGeneration(static_cast<std::uint32_t>(index), slot.generation),
                    *slot.occupant);
        }
    }
    /// Destroy occupants, not their generation history. Retired slots stay retired.
    void Clear() noexcept {
        m_freeHead = kNoSlot;
        for (std::size_t index = m_slots.size(); index > 0U; --index) {
            auto &slot = m_slots[index - 1U];
            if (slot.occupant) {
                Advance(slot);
                slot.occupant.reset();
            }
            if (slot.generation != 0U) {
                slot.nextFree = m_freeHead;
                m_freeHead = static_cast<std::uint32_t>(index - 1U);
            }
        }
        m_liveCount = 0U;
    }

  private:
    static constexpr std::uint32_t kNoSlot = std::numeric_limits<std::uint32_t>::max();
    struct Slot {
        std::optional<T> occupant;
        std::uint32_t generation = 0U;
        std::uint32_t nextFree = kNoSlot;
    };
    static void Advance(Slot &slot) noexcept {
        const auto next = Handle::NextGeneration(slot.generation);
        slot.generation = next > slot.generation ? next : 0U;
    }
    [[nodiscard]] Slot *FindSlot(Handle handle) noexcept {
        if (handle.Index() >= m_slots.size())
            return nullptr;
        auto &slot = m_slots[handle.Index()];
        return slot.occupant && slot.generation == handle.Generation() ? &slot : nullptr;
    }
    [[nodiscard]] const Slot *FindSlot(Handle handle) const noexcept {
        if (handle.Index() >= m_slots.size())
            return nullptr;
        const auto &slot = m_slots[handle.Index()];
        return slot.occupant && slot.generation == handle.Generation() ? &slot : nullptr;
    }
    std::vector<Slot> m_slots;
    std::uint32_t m_freeHead = kNoSlot;
    std::size_t m_liveCount = 0U;
};
} // namespace UVE::Containers
