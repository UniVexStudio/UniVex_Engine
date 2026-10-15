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
#include "uve/logging/assert_uve.h"

namespace UVE::Containers {
struct SlotHandleUVE {
    std::uint32_t index = 0U;
    std::uint32_t generation = 0U;
    [[nodiscard]] static constexpr SlotHandleUVE FromIndexAndGenerationUVE(std::uint32_t i,
                                                                           std::uint32_t g) noexcept {
        return {i, g};
    }
    [[nodiscard]] constexpr std::uint32_t IndexUVE() const noexcept { return index; }
    [[nodiscard]] constexpr std::uint32_t GenerationUVE() const noexcept { return generation; }
    [[nodiscard]] static constexpr std::uint32_t NextGenerationUVE(std::uint32_t current) noexcept {
        const auto next = current + 1U;
        return next == 0U ? 1U : next;
    }
    [[nodiscard]] static constexpr SlotHandleUVE InvalidUVE() noexcept { return {}; }
};
[[nodiscard]] constexpr bool operator==(const SlotHandleUVE &a, const SlotHandleUVE &b) noexcept {
    return a.index == b.index && a.generation == b.generation;
}
[[nodiscard]] constexpr bool operator!=(const SlotHandleUVE &a, const SlotHandleUVE &b) noexcept {
    return !(a == b);
}
} // namespace UVE::Containers

template <> struct std::hash<UVE::Containers::SlotHandleUVE> {
    [[nodiscard]] std::size_t operator()(const UVE::Containers::SlotHandleUVE &handle) const noexcept {
        return std::hash<std::uint64_t>{}((static_cast<std::uint64_t>(handle.index) << 32U) |
                                          handle.generation);
    }
};

namespace UVE::Containers {
/// Owning generational slot map. Clear preserves generation history; exhausted slots retire
/// permanently instead of reviving stale handles. Generation zero is never minted.
/// Handle supplies FromIndexAndGenerationUVE, IndexUVE, GenerationUVE, NextGenerationUVE;
/// generations must increase until wrap (which retires the slot). Narrow index domains may
/// reject indices in their handle factory. T need not be default constructible.
///
/// Handles are TABLE-RELATIVE, not owner tagged. Use only the issuing table (or a scoped
/// wrapper such as ObjectRegistryUVE). Moving transfers that domain to the destination;
/// replacing/copy-assigning a table invalidates the destination's old domain.
/// Growth can invalidate Find pointers. Releasing another slot does not. Release inside
/// ForEach is safe; Acquire inside it and reentrant mutation from T constructors/destructors
/// are not. Release/Clear never allocate. Vector's normal relocation guarantees apply when
/// T has a throwing move; a non-copyable throwing move cannot promise strong rollback.
/// Thread-safety: externally synchronize all access, including borrowed-pointer use.
template <typename T, typename Handle = SlotHandleUVE> class HandleTableUVE final {
  public:
    HandleTableUVE() = default;
    HandleTableUVE(const HandleTableUVE &) = default;
    HandleTableUVE &operator=(const HandleTableUVE &other) {
        if (this != &other) {
            HandleTableUVE copy(other);
            m_slots.swap(copy.m_slots);
            std::swap(m_freeHead, copy.m_freeHead);
            std::swap(m_liveCount, copy.m_liveCount);
        }
        return *this;
    }
    HandleTableUVE(HandleTableUVE &&other) noexcept
        : m_slots(std::move(other.m_slots)), m_freeHead(std::exchange(other.m_freeHead, kNoSlotUVE)),
          m_liveCount(std::exchange(other.m_liveCount, 0U)) {
        other.m_slots.clear();
    }
    HandleTableUVE &operator=(HandleTableUVE &&other) noexcept {
        if (this != &other) {
            m_slots = std::move(other.m_slots);
            m_freeHead = std::exchange(other.m_freeHead, kNoSlotUVE);
            m_liveCount = std::exchange(other.m_liveCount, 0U);
            other.m_slots.clear();
        }
        return *this;
    }
    template <typename... Args> Handle AcquireUVE(Args &&...args) {
        if (m_freeHead != kNoSlotUVE) {
            const auto index = m_freeHead;
            auto &slot = m_slots[index];
            const auto handle = Handle::FromIndexAndGenerationUVE(index, slot.generation);
            slot.occupant.emplace(std::forward<Args>(args)...); // before consuming the free slot
            m_freeHead = slot.nextFree;
            slot.nextFree = kNoSlotUVE;
            ++m_liveCount;
            return handle;
        }
        if (m_slots.size() >= static_cast<std::size_t>(kNoSlotUVE)) {
            throw std::length_error("HandleTableUVE index space exhausted");
        }
        const auto index = static_cast<std::uint32_t>(m_slots.size());
        SlotUVE slot;
        slot.generation = Handle::NextGenerationUVE(0U);
        UVE_ASSERT(slot.generation != 0U);
        const auto handle = Handle::FromIndexAndGenerationUVE(index, slot.generation);
        slot.occupant.emplace(std::forward<Args>(args)...);
        m_slots.push_back(std::move(slot));
        ++m_liveCount;
        return handle;
    }
    /// False for invalid, stale or out-of-range handles. The dead-slot free list cannot allocate.
    bool ReleaseUVE(Handle handle) noexcept {
        auto *slot = FindSlotUVE(handle);
        if (slot == nullptr)
            return false;
        AdvanceUVE(*slot);
        slot->occupant.reset();
        if (slot->generation != 0U) {
            slot->nextFree = m_freeHead;
            m_freeHead = handle.IndexUVE();
        }
        --m_liveCount;
        return true;
    }
    [[nodiscard]] T *FindUVE(Handle handle) noexcept {
        auto *slot = FindSlotUVE(handle);
        return slot == nullptr ? nullptr : &*slot->occupant;
    }
    [[nodiscard]] const T *FindUVE(Handle handle) const noexcept {
        const auto *slot = FindSlotUVE(handle);
        return slot == nullptr ? nullptr : &*slot->occupant;
    }
    [[nodiscard]] bool ContainsUVE(Handle handle) const noexcept { return FindSlotUVE(handle) != nullptr; }
    [[nodiscard]] std::size_t GetLiveCountUVE() const noexcept { return m_liveCount; }
    template <typename Callback> void ForEachUVE(Callback &&callback) {
        for (std::size_t index = 0U; index < m_slots.size(); ++index) {
            auto &slot = m_slots[index];
            if (slot.occupant)
                callback(
                    Handle::FromIndexAndGenerationUVE(static_cast<std::uint32_t>(index), slot.generation),
                    *slot.occupant);
        }
    }
    template <typename Callback> void ForEachUVE(Callback &&callback) const {
        for (std::size_t index = 0U; index < m_slots.size(); ++index) {
            const auto &slot = m_slots[index];
            if (slot.occupant)
                callback(
                    Handle::FromIndexAndGenerationUVE(static_cast<std::uint32_t>(index), slot.generation),
                    *slot.occupant);
        }
    }
    /// Destroy occupants, not their generation history. Retired slots stay retired.
    void ClearUVE() noexcept {
        m_freeHead = kNoSlotUVE;
        for (std::size_t index = m_slots.size(); index > 0U; --index) {
            auto &slot = m_slots[index - 1U];
            if (slot.occupant) {
                AdvanceUVE(slot);
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
    static constexpr std::uint32_t kNoSlotUVE = std::numeric_limits<std::uint32_t>::max();
    struct SlotUVE {
        std::optional<T> occupant;
        std::uint32_t generation = 0U;
        std::uint32_t nextFree = kNoSlotUVE;
    };
    static void AdvanceUVE(SlotUVE &slot) noexcept {
        const auto next = Handle::NextGenerationUVE(slot.generation);
        slot.generation = next > slot.generation ? next : 0U;
    }
    [[nodiscard]] SlotUVE *FindSlotUVE(Handle handle) noexcept {
        if (handle.IndexUVE() >= m_slots.size())
            return nullptr;
        auto &slot = m_slots[handle.IndexUVE()];
        return slot.occupant && slot.generation == handle.GenerationUVE() ? &slot : nullptr;
    }
    [[nodiscard]] const SlotUVE *FindSlotUVE(Handle handle) const noexcept {
        if (handle.IndexUVE() >= m_slots.size())
            return nullptr;
        const auto &slot = m_slots[handle.IndexUVE()];
        return slot.occupant && slot.generation == handle.GenerationUVE() ? &slot : nullptr;
    }
    std::vector<SlotUVE> m_slots;
    std::uint32_t m_freeHead = kNoSlotUVE;
    std::size_t m_liveCount = 0U;
};
} // namespace UVE::Containers
