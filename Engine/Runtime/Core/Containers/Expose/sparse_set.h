// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <vector>

#include "engine_assert.h"

namespace UVE::Containers {

/// `SparseSet<T>` is the canonical ECS storage structure: a sparse index (id → dense slot)
/// plus dense id/value arrays, giving O(1) insert/remove/lookup with contiguous iteration over
/// the live values. Removal swaps the last element into the hole, so dense order is NOT stable
/// across removes — iterate ids via `Ids()` when order matters to the caller.
///
/// Ids are caller-managed `uint32_t`s (entity indices, voice slots, type ordinals). The sparse
/// array grows to the largest id ever inserted, so ids must be reasonably dense: a single huge
/// id allocates the whole sparse spine. That is the documented trade-off of the structure, not
/// a bug; callers with sparse 64-bit keys want a hash map instead.
///
/// Bounds contract: like `FixedArray`, access asserts in debug and is unchecked in release.
/// `Get` on an absent id, like `Insert` of a present one, is a programming error — the
/// `bool` returns on insert/remove are for callers that genuinely cannot know (deserializers),
/// not a substitute for checking `Contains` first.
///
/// Naming: `begin`/`end` stay lowercase for range-based-for STL interop; every other member
/// carries the `UVE` suffix per engine convention.
template <typename T>
class SparseSet final {
public:
    using Index = std::uint32_t;
    static constexpr Index kInvalidIndex = std::numeric_limits<Index>::max();

    SparseSet() = default;

    /// Inserts `value` under `id`. Returns false (leaving the set untouched) when `id` is
    /// already present.
    bool Insert(const Index id, T value) {
        UVE_ASSERT(id != kInvalidIndex); // The tombstone is not a usable id.
        if (id >= m_sparse.size()) {
            m_sparse.resize(static_cast<std::size_t>(id) + 1U, kInvalidIndex);
        }
        if (m_sparse[id] != kInvalidIndex) {
            return false;
        }
        m_sparse[id] = static_cast<Index>(m_denseIds.size());
        m_denseIds.push_back(id);
        m_denseValues.push_back(std::move(value));
        return true;
    }

    /// Removes `id`. Returns false (leaving the set untouched) when `id` is absent.
    bool Remove(const Index id) noexcept(std::is_nothrow_move_assignable_v<T>) {
        if (!Contains(id)) {
            return false;
        }
        const Index slot = m_sparse[id];
        const Index last = static_cast<Index>(m_denseIds.size() - 1U);
        if (slot != last) {
            m_denseIds[slot] = m_denseIds[last];
            m_denseValues[slot] = std::move(m_denseValues[last]);
            m_sparse[m_denseIds[slot]] = slot;
        }
        m_denseIds.pop_back();
        m_denseValues.pop_back();
        m_sparse[id] = kInvalidIndex;
        return true;
    }

    /// True iff `id` is present. Never asserts — the presence test callers ask first.
    [[nodiscard]] bool Contains(const Index id) const noexcept {
        return id < m_sparse.size() && m_sparse[id] != kInvalidIndex;
    }

    /// The value under `id`. Asserts presence.
    [[nodiscard]] T& Get(const Index id) {
        UVE_ASSERT(Contains(id));
        return m_denseValues[m_sparse[id]];
    }

    /// Const overload of `Get`.
    [[nodiscard]] const T& Get(const Index id) const {
        UVE_ASSERT(Contains(id));
        return m_denseValues[m_sparse[id]];
    }

    /// Number of live entries.
    [[nodiscard]] std::size_t Size() const noexcept { return m_denseValues.size(); }

    /// True when no entries are live.
    [[nodiscard]] bool Empty() const noexcept { return m_denseValues.empty(); }

    /// Removes every entry. The sparse spine keeps its size (ids reinsert without regrowing).
    void Clear() {
        m_denseIds.clear();
        m_denseValues.clear();
        for (Index& slot : m_sparse) {
            slot = kInvalidIndex;
        }
    }

    /// The live ids in dense order — parallel to iteration order, so `Ids()[i]` is the id
    /// of the i-th iterated value.
    [[nodiscard]] std::span<const Index> Ids() const noexcept { return m_denseIds; }

    typename std::vector<T>::iterator begin() noexcept { return m_denseValues.begin(); }
    typename std::vector<T>::iterator end() noexcept { return m_denseValues.end(); }
    typename std::vector<T>::const_iterator begin() const noexcept { return m_denseValues.begin(); }
    typename std::vector<T>::const_iterator end() const noexcept { return m_denseValues.end(); }

private:
    std::vector<Index> m_sparse;
    std::vector<Index> m_denseIds;
    std::vector<T> m_denseValues;
};

} // namespace UVE::Containers
