// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <vector>

#include "uve/logging/assert_uve.h"

namespace UVE::Containers {

/// `SparseSetUVE<T>` is the canonical ECS storage structure: a sparse index (id → dense slot)
/// plus dense id/value arrays, giving O(1) insert/remove/lookup with contiguous iteration over
/// the live values. Removal swaps the last element into the hole, so dense order is NOT stable
/// across removes — iterate ids via `IdsUVE()` when order matters to the caller.
///
/// Ids are caller-managed `uint32_t`s (entity indices, voice slots, type ordinals). The sparse
/// array grows to the largest id ever inserted, so ids must be reasonably dense: a single huge
/// id allocates the whole sparse spine. That is the documented trade-off of the structure, not
/// a bug; callers with sparse 64-bit keys want a hash map instead.
///
/// Bounds contract: like `FixedArrayUVE`, access asserts in debug and is unchecked in release.
/// `GetUVE` on an absent id, like `InsertUVE` of a present one, is a programming error — the
/// `bool` returns on insert/remove are for callers that genuinely cannot know (deserializers),
/// not a substitute for checking `ContainsUVE` first.
///
/// Naming: `begin`/`end` stay lowercase for range-based-for STL interop; every other member
/// carries the `UVE` suffix per engine convention.
template <typename T>
class SparseSetUVE final {
public:
    using IndexUVE = std::uint32_t;
    static constexpr IndexUVE kInvalidIndexUVE = std::numeric_limits<IndexUVE>::max();

    SparseSetUVE() = default;

    /// Inserts `value` under `id`. Returns false (leaving the set untouched) when `id` is
    /// already present.
    bool InsertUVE(const IndexUVE id, T value) {
        UVE_ASSERT(id != kInvalidIndexUVE); // The tombstone is not a usable id.
        if (id >= m_sparse.size()) {
            m_sparse.resize(static_cast<std::size_t>(id) + 1U, kInvalidIndexUVE);
        }
        if (m_sparse[id] != kInvalidIndexUVE) {
            return false;
        }
        m_sparse[id] = static_cast<IndexUVE>(m_denseIds.size());
        m_denseIds.push_back(id);
        m_denseValues.push_back(std::move(value));
        return true;
    }

    /// Removes `id`. Returns false (leaving the set untouched) when `id` is absent.
    bool RemoveUVE(const IndexUVE id) noexcept(std::is_nothrow_move_assignable_v<T>) {
        if (!ContainsUVE(id)) {
            return false;
        }
        const IndexUVE slot = m_sparse[id];
        const IndexUVE last = static_cast<IndexUVE>(m_denseIds.size() - 1U);
        if (slot != last) {
            m_denseIds[slot] = m_denseIds[last];
            m_denseValues[slot] = std::move(m_denseValues[last]);
            m_sparse[m_denseIds[slot]] = slot;
        }
        m_denseIds.pop_back();
        m_denseValues.pop_back();
        m_sparse[id] = kInvalidIndexUVE;
        return true;
    }

    /// True iff `id` is present. Never asserts — the presence test callers ask first.
    [[nodiscard]] bool ContainsUVE(const IndexUVE id) const noexcept {
        return id < m_sparse.size() && m_sparse[id] != kInvalidIndexUVE;
    }

    /// The value under `id`. Asserts presence.
    [[nodiscard]] T& GetUVE(const IndexUVE id) {
        UVE_ASSERT(ContainsUVE(id));
        return m_denseValues[m_sparse[id]];
    }

    /// Const overload of `GetUVE`.
    [[nodiscard]] const T& GetUVE(const IndexUVE id) const {
        UVE_ASSERT(ContainsUVE(id));
        return m_denseValues[m_sparse[id]];
    }

    /// Number of live entries.
    [[nodiscard]] std::size_t SizeUVE() const noexcept { return m_denseValues.size(); }

    /// True when no entries are live.
    [[nodiscard]] bool EmptyUVE() const noexcept { return m_denseValues.empty(); }

    /// Removes every entry. The sparse spine keeps its size (ids reinsert without regrowing).
    void ClearUVE() {
        m_denseIds.clear();
        m_denseValues.clear();
        for (IndexUVE& slot : m_sparse) {
            slot = kInvalidIndexUVE;
        }
    }

    /// The live ids in dense order — parallel to iteration order, so `IdsUVE()[i]` is the id
    /// of the i-th iterated value.
    [[nodiscard]] std::span<const IndexUVE> IdsUVE() const noexcept { return m_denseIds; }

    typename std::vector<T>::iterator begin() noexcept { return m_denseValues.begin(); }
    typename std::vector<T>::iterator end() noexcept { return m_denseValues.end(); }
    typename std::vector<T>::const_iterator begin() const noexcept { return m_denseValues.begin(); }
    typename std::vector<T>::const_iterator end() const noexcept { return m_denseValues.end(); }

private:
    std::vector<IndexUVE> m_sparse;
    std::vector<IndexUVE> m_denseIds;
    std::vector<T> m_denseValues;
};

} // namespace UVE::Containers
