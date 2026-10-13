// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <optional>
#include <utility>
#include <vector>

#include "uve/logging/assert_uve.h"

namespace UVE::Containers {

/// `SlotHandleUVE` is the default handle for `HandleTableUVE<T>`: a plain index + generation
/// pair. The default-constructed handle `{0, 0}` is also the invalid handle, and the table
/// never mints generation 0 (fresh slots start at `NextGenerationUVE(0)`), so the invalid
/// handle can never match a live slot.
struct SlotHandleUVE {
    std::uint32_t index = 0U;
    std::uint32_t generation = 0U;

    [[nodiscard]] static constexpr SlotHandleUVE FromIndexAndGenerationUVE(
        std::uint32_t indexIn, std::uint32_t generationIn) noexcept {
        return SlotHandleUVE{indexIn, generationIn};
    }

    [[nodiscard]] constexpr std::uint32_t IndexUVE() const noexcept { return index; }
    [[nodiscard]] constexpr std::uint32_t GenerationUVE() const noexcept { return generation; }

    [[nodiscard]] static constexpr std::uint32_t NextGenerationUVE(
        std::uint32_t current) noexcept {
        const std::uint32_t next = current + 1U;
        return next == 0U ? 1U : next;
    }

    [[nodiscard]] static constexpr SlotHandleUVE InvalidUVE() noexcept { return SlotHandleUVE{}; }
};

[[nodiscard]] constexpr bool operator==(const SlotHandleUVE& lhs,
                                        const SlotHandleUVE& rhs) noexcept {
    return lhs.index == rhs.index && lhs.generation == rhs.generation;
}

[[nodiscard]] constexpr bool operator!=(const SlotHandleUVE& lhs,
                                        const SlotHandleUVE& rhs) noexcept {
    return !(lhs == rhs);
}

}  // namespace UVE::Containers

template <>
struct std::hash<UVE::Containers::SlotHandleUVE> {
    [[nodiscard]] std::size_t operator()(const UVE::Containers::SlotHandleUVE& handle) const noexcept {
        const std::uint64_t combined = (static_cast<std::uint64_t>(handle.index) << 32) |
                                       static_cast<std::uint64_t>(handle.generation);
        return std::hash<std::uint64_t>{}(combined);
    }
};

namespace UVE::Containers {

/// `HandleTableUVE<T, Handle>` is a generational slot map: it owns `T` occupants in reusable
/// slots and mints handles that stay safe across destroy/recreate cycles. Looking up a handle
/// whose slot was released and reused fails the generation check instead of aliasing the new
/// occupant — the use-after-destroy protection that raw indices and monotonic ids cannot give.
/// Replaces the hand-rolled `unordered_map<id, T>` + counter voice tables (and, later, the
/// entity-slot and resource-slot managers built on the same idea).
///
/// A `Handle` must provide this interface (see `SlotHandleUVE` above and
/// `Audio::VoiceHandleUVE`, which packs the same pair into one `uint32_t`):
///   `static Handle FromIndexAndGenerationUVE(uint32_t index, uint32_t generation);`
///   `uint32_t IndexUVE() const; uint32_t GenerationUVE() const;`
///   `static uint32_t NextGenerationUVE(uint32_t current);`
/// Generations come only from `NextGenerationUVE`: a fresh slot gets `NextGenerationUVE(0)`,
/// a released slot gets `NextGenerationUVE(previous)`. A handle is live only while its
/// generation equals its slot's — so any generation `NextGenerationUVE` can never produce
/// (notably the invalid encoding, which is why both shipped handles skip 0 on wrap) never
/// matches a live slot, making foreign/stale/invalid handles safe `Find`/`Release`/`Contains`
/// inputs that simply report "not live". Handles with narrower index ranges assert in
/// `FromIndexAndGenerationUVE` when the table outgrows them.
///
/// Growth: vector-backed and unbounded, matching the map-based predecessors that grew without
/// limit — acquiring past all free slots appends a new one. `FindUVE` pointers stay valid
/// across `ReleaseUVE` of *other* handles and across `ForEachUVE`; `AcquireUVE` may reallocate
/// (invalidating all), and `ReleaseUVE`/`ClearUVE` destroy the pointed-to occupant. `ReleaseUVE`
/// inside a `ForEachUVE` callback is safe; `AcquireUVE` inside one is not.
///
/// `T` needs no default constructor (dead slots hold no `T`); it must be move-constructible
/// for slot-vector growth. `ReleaseUVE` throws only if the free list itself must grow
/// (`std::bad_alloc`).
///
/// Thread-safety: none — externally synchronize shared use (the audio devices keep the mutexes
/// they already had for their maps).
template <typename T, typename Handle = SlotHandleUVE>
class HandleTableUVE final {
public:
    HandleTableUVE() = default;

    /// Constructs a `T` from `args` in a reused free slot (or a new trailing slot) and returns
    /// its handle. A throwing `T` constructor leaves the table unchanged.
    template <typename... TArgs>
    Handle AcquireUVE(TArgs&&... args) {
        UVE_ASSERT(m_slots.size() < static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max()));
        if (!m_freeIndices.empty()) {
            const std::uint32_t index = m_freeIndices.back();
            // Emplace BEFORE popping: a throwing T constructor leaves the free list intact.
            m_slots[index].occupant.emplace(std::forward<TArgs>(args)...);
            m_freeIndices.pop_back();
            ++m_liveCount;
            return Handle::FromIndexAndGenerationUVE(index, m_slots[index].generation);
        }
        const auto index = static_cast<std::uint32_t>(m_slots.size());
        SlotUVE slot;
        slot.generation = Handle::NextGenerationUVE(0U);
        slot.occupant.emplace(std::forward<TArgs>(args)...);
        m_slots.push_back(std::move(slot));
        ++m_liveCount;
        return Handle::FromIndexAndGenerationUVE(index, m_slots.back().generation);
    }

    /// Destroys the occupant and frees its slot (bumping the generation so the handle goes
    /// stale). Returns false — without asserting — for stale, foreign, or invalid handles, so
    /// callers facing user-supplied handles can log-and-continue.
    bool ReleaseUVE(Handle handle) {
        SlotUVE* const slot = FindSlotUVE(handle);
        if (slot == nullptr) {
            return false;
        }
        slot->occupant.reset();
        slot->generation = Handle::NextGenerationUVE(slot->generation);
        m_freeIndices.push_back(handle.IndexUVE());
        --m_liveCount;
        return true;
    }

    /// Returns the live occupant, or nullptr for a stale/foreign/invalid handle. See the class
    /// doc comment for pointer-stability rules.
    [[nodiscard]] T* FindUVE(Handle handle) {
        SlotUVE* const slot = FindSlotUVE(handle);
        return slot == nullptr ? nullptr : &(*slot->occupant);
    }

    [[nodiscard]] const T* FindUVE(Handle handle) const {
        const SlotUVE* const slot = FindSlotUVE(handle);
        return slot == nullptr ? nullptr : &(*slot->occupant);
    }

    [[nodiscard]] bool ContainsUVE(Handle handle) const { return FindSlotUVE(handle) != nullptr; }

    [[nodiscard]] std::size_t GetLiveCountUVE() const noexcept { return m_liveCount; }

    /// Calls `callback(handle, occupant)` for every live slot, in slot order. Releasing inside
    /// the callback is safe; acquiring is not.
    template <typename Callback>
    void ForEachUVE(Callback&& callback) {
        for (std::size_t index = 0U; index < m_slots.size(); ++index) {
            SlotUVE& slot = m_slots[index];
            if (slot.occupant.has_value()) {
                callback(Handle::FromIndexAndGenerationUVE(static_cast<std::uint32_t>(index),
                                                           slot.generation),
                         *slot.occupant);
            }
        }
    }

    template <typename Callback>
    void ForEachUVE(Callback&& callback) const {
        for (std::size_t index = 0U; index < m_slots.size(); ++index) {
            const SlotUVE& slot = m_slots[index];
            if (slot.occupant.has_value()) {
                callback(Handle::FromIndexAndGenerationUVE(static_cast<std::uint32_t>(index),
                                                           slot.generation),
                         *slot.occupant);
            }
        }
    }

    void ClearUVE() noexcept {
        m_slots.clear();
        m_freeIndices.clear();
        m_liveCount = 0U;
    }

private:
    struct SlotUVE {
        std::optional<T> occupant;
        std::uint32_t generation = 0U;
    };

    [[nodiscard]] SlotUVE* FindSlotUVE(Handle handle) {
        const std::size_t index = static_cast<std::size_t>(handle.IndexUVE());
        if (index >= m_slots.size()) {
            return nullptr;
        }
        SlotUVE& slot = m_slots[index];
        if (!slot.occupant.has_value() || slot.generation != handle.GenerationUVE()) {
            return nullptr;
        }
        return &slot;
    }

    [[nodiscard]] const SlotUVE* FindSlotUVE(Handle handle) const {
        const std::size_t index = static_cast<std::size_t>(handle.IndexUVE());
        if (index >= m_slots.size()) {
            return nullptr;
        }
        const SlotUVE& slot = m_slots[index];
        if (!slot.occupant.has_value() || slot.generation != handle.GenerationUVE()) {
            return nullptr;
        }
        return &slot;
    }

    std::vector<SlotUVE> m_slots;
    std::vector<std::uint32_t> m_freeIndices;
    std::size_t m_liveCount = 0U;
};

}  // namespace UVE::Containers
