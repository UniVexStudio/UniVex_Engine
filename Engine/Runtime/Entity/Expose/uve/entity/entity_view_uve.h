// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <array>
#include <cstddef>
#include <iterator>
#include <tuple>
#include <utility>
#include <vector>

#include "uve/component/entity_uve.h"
#include "uve/component/component_access_uve.h"

namespace UVE::Scene {

/// An iterable snapshot of the entities currently carrying every one of `TComponents...` — the
/// range-for answer to `ForEachUVE`'s callback.
///
/// WHAT IT IS. `manager.ViewUVE<A, B>()` collects the matching entities once and iterates the
/// snapshot: `for (auto entry : view)` with `entry.entity` plus `entry.GetUVE<A>()` /
/// `entry.GetUVE<B>()`. Components resolve to live references (not copies), so the view stays
/// cheap no matter how wide the components are, and mutations through the entry hit the live
/// archetype storage.
///
/// WHAT IT IS NOT. It is not a cached query: no incremental update, no change detection — a new
/// `ViewUVE()` call re-snapshots. It is also not iteration insurance: the snapshot holds raw
/// component pointers with exactly `ForEachUVE`'s lifetime rule, so a structural change to a
/// matching entity mid-iteration invalidates (record those through `EntityCommandBufferUVE`
/// instead). Iteration order is the manager's, unspecified beyond "every match exactly once".
///
/// At least one component is required: an empty pack would mean "every entity", which deserves
/// its own explicit call if anyone ever wants it, not an accident of a forgotten type argument.
///
/// Thread-safety: not thread-safe — build and iterate on the scene thread, like the manager.
template <typename... TComponents>
class EntityViewUVE final {
    static_assert(sizeof...(TComponents) > 0U, "EntityViewUVE needs at least one component type.");

public:
    /// One matched row as the manager hands it over: the entity plus its component pointers in
    /// `TComponents...` order. Built by `ViewUVE()`; callers never construct one directly.
    struct RowUVE final {
        EntityUVE entity = kInvalidEntityUVE;
        std::array<void*, sizeof...(TComponents)> pointers{};
    };

    /// One iterated match: the entity plus live references to its matched components.
    struct EntryUVE final {
        EntityUVE entity = kInvalidEntityUVE;
        std::tuple<PublicComponentUVE<TComponents>&...> components;

        /// The live `T` for this entry. `T` must be one of the view's component types.
        template <typename T>
        [[nodiscard]] PublicComponentUVE<T>& GetUVE() {
            return std::get<PublicComponentUVE<T>&>(components);
        }
    };

    /// Forward iterator over the snapshot. Dereferencing retypes the row's pointers.
    class IteratorUVE final {
    public:
        using iterator_category = std::forward_iterator_tag;
        using value_type = EntryUVE;
        using difference_type = std::ptrdiff_t;
        using pointer = void;
        using reference = EntryUVE;

        IteratorUVE() = default;

        [[nodiscard]] EntryUVE operator*() const {
            return MakeEntryUVE(m_view->m_rows[m_index], std::index_sequence_for<TComponents...>{});
        }

        IteratorUVE& operator++() {
            ++m_index;
            return *this;
        }

        IteratorUVE operator++(int) {
            IteratorUVE copy = *this;
            ++m_index;
            return copy;
        }

        [[nodiscard]] bool operator==(const IteratorUVE& other) const noexcept {
            return m_view == other.m_view && m_index == other.m_index;
        }

        [[nodiscard]] bool operator!=(const IteratorUVE& other) const noexcept {
            return !(*this == other);
        }

    private:
        friend class EntityViewUVE;

        IteratorUVE(const EntityViewUVE* view, const std::size_t index) : m_view(view), m_index(index) {
        }

        template <std::size_t... Is>
        [[nodiscard]] static EntryUVE MakeEntryUVE(const RowUVE& row, std::index_sequence<Is...>) {
            return EntryUVE{row.entity, std::tie(*static_cast<PublicComponentUVE<TComponents>*>(row.pointers[Is])...)};
        }

        const EntityViewUVE* m_view = nullptr;
        std::size_t m_index = 0U;
    };

    /// Builds a view over already-matched `rows`. Callers use `ViewUVE()` on the manager.
    explicit EntityViewUVE(std::vector<RowUVE> rows) : m_rows(std::move(rows)) {
    }

    [[nodiscard]] IteratorUVE begin() const {
        return IteratorUVE{this, 0U};
    }

    [[nodiscard]] IteratorUVE end() const {
        return IteratorUVE{this, m_rows.size()};
    }

    /// Rows in the snapshot.
    [[nodiscard]] std::size_t SizeUVE() const noexcept {
        return m_rows.size();
    }

    /// True when the snapshot matched nothing.
    [[nodiscard]] bool IsEmptyUVE() const noexcept {
        return m_rows.empty();
    }

private:
    std::vector<RowUVE> m_rows;
};

namespace Detail {

/// Copies one `ForEachErased` callback's pointer vector into a view row's fixed array, in order.
template <typename... TComponents, std::size_t... Is>
typename EntityViewUVE<TComponents...>::RowUVE MakeViewRowUVE(const EntityUVE entity,
                                                              const std::vector<void*>& pointers,
                                                              std::index_sequence<Is...>) {
    typename EntityViewUVE<TComponents...>::RowUVE row;
    row.entity = entity;
    ((row.pointers[Is] = pointers[Is]), ...);
    return row;
}

} // namespace Detail

} // namespace UVE::Scene
