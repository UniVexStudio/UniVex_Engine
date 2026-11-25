// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <array>
#include <cstddef>
#include <iterator>
#include <tuple>
#include <utility>
#include <vector>

#include "entity.h"
#include "component_access.h"

namespace UVE::Scene {

/// An iterable snapshot of the entities currently carrying every one of `TComponents...` — the
/// range-for answer to `ForEach`'s callback.
///
/// WHAT IT IS. `manager.View<A, B>()` collects the matching entities once and iterates the
/// snapshot: `for (auto entry : view)` with `entry.entity` plus `entry.Get<A>()` /
/// `entry.Get<B>()`. Components resolve to live references (not copies), so the view stays
/// cheap no matter how wide the components are, and mutations through the entry hit the live
/// archetype storage.
///
/// WHAT IT IS NOT. It is not a cached query: no incremental update, no change detection — a new
/// `View()` call re-snapshots. It is also not iteration insurance: the snapshot holds raw
/// component pointers with exactly `ForEach`'s lifetime rule, so a structural change to a
/// matching entity mid-iteration invalidates (record those through `EntityCommandBuffer`
/// instead). Iteration order is the manager's, unspecified beyond "every match exactly once".
///
/// At least one component is required: an empty pack would mean "every entity", which deserves
/// its own explicit call if anyone ever wants it, not an accident of a forgotten type argument.
///
/// Thread-safety: not thread-safe — build and iterate on the scene thread, like the manager.
template <typename... TComponents>
class EntityView final {
    static_assert(sizeof...(TComponents) > 0U, "EntityView needs at least one component type.");

public:
    /// One matched row as the manager hands it over: the entity plus its component pointers in
    /// `TComponents...` order. Built by `View()`; callers never construct one directly.
    struct Row final {
        Entity entity = kInvalidEntity;
        std::array<void*, sizeof...(TComponents)> pointers{};
    };

    /// One iterated match: the entity plus live references to its matched components.
    struct Entry final {
        Entity entity = kInvalidEntity;
        std::tuple<PublicComponent<TComponents>&...> components;

        /// The live `T` for this entry. `T` must be one of the view's component types.
        template <typename T>
        [[nodiscard]] PublicComponent<T>& Get() {
            return std::get<PublicComponent<T>&>(components);
        }
    };

    /// Forward iterator over the snapshot. Dereferencing retypes the row's pointers.
    class Iterator final {
    public:
        using iterator_category = std::forward_iterator_tag;
        using value_type = Entry;
        using difference_type = std::ptrdiff_t;
        using pointer = void;
        using reference = Entry;

        Iterator() = default;

        [[nodiscard]] Entry operator*() const {
            return MakeEntry(m_view->m_rows[m_index], std::index_sequence_for<TComponents...>{});
        }

        Iterator& operator++() {
            ++m_index;
            return *this;
        }

        Iterator operator++(int) {
            Iterator copy = *this;
            ++m_index;
            return copy;
        }

        [[nodiscard]] bool operator==(const Iterator& other) const noexcept {
            return m_view == other.m_view && m_index == other.m_index;
        }

        [[nodiscard]] bool operator!=(const Iterator& other) const noexcept {
            return !(*this == other);
        }

    private:
        friend class EntityView;

        Iterator(const EntityView* view, const std::size_t index) : m_view(view), m_index(index) {
        }

        template <std::size_t... Is>
        [[nodiscard]] static Entry MakeEntry(const Row& row, std::index_sequence<Is...>) {
            return Entry{row.entity, std::tie(*static_cast<PublicComponent<TComponents>*>(row.pointers[Is])...)};
        }

        const EntityView* m_view = nullptr;
        std::size_t m_index = 0U;
    };

    /// Builds a view over already-matched `rows`. Callers use `View()` on the manager.
    explicit EntityView(std::vector<Row> rows) : m_rows(std::move(rows)) {
    }

    [[nodiscard]] Iterator begin() const {
        return Iterator{this, 0U};
    }

    [[nodiscard]] Iterator end() const {
        return Iterator{this, m_rows.size()};
    }

    /// Rows in the snapshot.
    [[nodiscard]] std::size_t Size() const noexcept {
        return m_rows.size();
    }

    /// True when the snapshot matched nothing.
    [[nodiscard]] bool IsEmpty() const noexcept {
        return m_rows.empty();
    }

private:
    std::vector<Row> m_rows;
};

namespace Detail {

/// Copies one `ForEachErased` callback's pointer vector into a view row's fixed array, in order.
template <typename... TComponents, std::size_t... Is>
typename EntityView<TComponents...>::Row MakeViewRow(const Entity entity,
                                                              const std::vector<void*>& pointers,
                                                              std::index_sequence<Is...>) {
    typename EntityView<TComponents...>::Row row;
    row.entity = entity;
    ((row.pointers[Is] = pointers[Is]), ...);
    return row;
}

} // namespace Detail

} // namespace UVE::Scene
