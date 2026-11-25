// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cstddef>
#include <initializer_list>
#include <new>
#include <span>
#include <type_traits>
#include <utility>

#include "engine_assert.h"

namespace UVE::Containers {

/// `SmallVector<T, InlineCapacity>` is a vector-like sequence container with a fixed-size
/// inline buffer: the first `InlineCapacity` elements live inside the container (no allocation),
/// and pushing past that spills onto the heap with doubling growth. The engine's replacement
/// for small `std::vector<T>`s on hot paths where the common case fits inline but the uncommon
/// case must still work (rather than assert, as `FixedArray` does).
///
/// Bounds contract: same as `FixedArray` — debug assert, unchecked release, no exceptions at
/// the API boundary. Growth may throw what allocation and `T`'s copy/move throw (matching
/// `std::vector`); a failed growth leaves the container unchanged.
///
/// Spill policy: capacity starts at `InlineCapacity` and doubles on every growth; there is no
/// shrink — once spilled, the heap buffer is kept until destruction. `Capacity()` reports the
/// current capacity, so `Capacity() > InlineCapacity` tells callers a spill happened. Unlike
/// `FixedArray` there is no `Full()` — a small vector is never full, it grows.
///
/// `T` requirements mirror `std::vector<T>`; `T`'s move constructor SHOULD be noexcept (a
/// throwing move still works — growth falls back to copying — but pays the copy).
///
/// Thread-safety: none, like `std::vector` — externally synchronize shared use.
///
/// Naming: `begin`/`end` stay lowercase for range-based-for STL interop; every other member
/// carries the `UVE` suffix per engine convention.
template <typename T, std::size_t InlineCapacity>
class SmallVector final {
public:
    static_assert(InlineCapacity > 0U,
                  "SmallVector with zero inline capacity is just a vector; use std::vector.");

    SmallVector() noexcept : m_data(InlineData()) {}

    SmallVector(const SmallVector& other) : m_data(InlineData()) {
        if (other.m_size > InlineCapacity) {
            Grow(other.m_size);
        }
        for (std::size_t index = 0U; index < other.m_size; ++index) {
            PushBack(other[index]);
        }
    }

    // The inline branch cannot grow (capacity InlineCapacity always fits other's inline
    // elements), so this only throws if `T`'s move throws — hence the conditional noexcept.
    SmallVector(SmallVector&& other) noexcept(std::is_nothrow_move_constructible_v<T>)
        : m_data(InlineData()) {
        if (other.IsSpilled()) {
            m_data = other.m_data;
            m_size = other.m_size;
            m_capacity = other.m_capacity;
            other.m_data = other.InlineData();
            other.m_size = 0U;
            other.m_capacity = InlineCapacity;
        } else {
            for (std::size_t index = 0U; index < other.m_size; ++index) {
                PushBack(std::move(other[index]));
            }
            other.Clear();
        }
    }

    // Braced initialization, matching `std::vector` (deliberately non-explicit: aggregate
    // members like `FrameTaskDefinition::dependencies` list-initialize from `{...}`, which
    // cannot call an explicit constructor). Over-capacity lists spill, as a push loop would.
    SmallVector(std::initializer_list<T> items) : m_data(InlineData()) {
        if (items.size() > InlineCapacity) {
            Grow(items.size());
        }
        for (const T& item : items) {
            PushBack(item);
        }
    }

    ~SmallVector() {
        Clear();
        if (IsSpilled()) {
            ::operator delete(m_data, std::align_val_t(alignof(T)));
        }
    }

    SmallVector& operator=(const SmallVector& other) {
        if (this != &other) {
            Clear();
            if (other.m_size > m_capacity) {
                Grow(other.m_size);
            }
            for (std::size_t index = 0U; index < other.m_size; ++index) {
                PushBack(other[index]);
            }
        }
        return *this;
    }

    // Same no-grow argument as the move constructor: other's inline elements always fit, so this
    // only throws if `T`'s move throws.
    SmallVector& operator=(SmallVector&& other) noexcept(std::is_nothrow_move_constructible_v<T>) {
        if (this != &other) {
            Clear();
            if (other.IsSpilled()) {
                if (IsSpilled()) {
                    ::operator delete(m_data, std::align_val_t(alignof(T)));
                }
                m_data = other.m_data;
                m_size = other.m_size;
                m_capacity = other.m_capacity;
                other.m_data = other.InlineData();
                other.m_size = 0U;
                other.m_capacity = InlineCapacity;
            } else {
                for (std::size_t index = 0U; index < other.m_size; ++index) {
                    PushBack(std::move(other[index]));
                }
                other.Clear();
            }
        }
        return *this;
    }

    [[nodiscard]] constexpr std::size_t Size() const noexcept { return m_size; }
    [[nodiscard]] std::size_t Capacity() const noexcept { return m_capacity; }
    [[nodiscard]] constexpr bool Empty() const noexcept { return m_size == 0U; }

    [[nodiscard]] T& operator[](std::size_t index) noexcept {
        UVE_ASSERT(index < m_size);
        return m_data[index];
    }

    [[nodiscard]] const T& operator[](std::size_t index) const noexcept {
        UVE_ASSERT(index < m_size);
        return m_data[index];
    }

    [[nodiscard]] T& Front() noexcept {
        UVE_ASSERT(!Empty());
        return m_data[0U];
    }

    [[nodiscard]] const T& Front() const noexcept {
        UVE_ASSERT(!Empty());
        return m_data[0U];
    }

    [[nodiscard]] T& Back() noexcept {
        UVE_ASSERT(!Empty());
        return m_data[m_size - 1U];
    }

    [[nodiscard]] const T& Back() const noexcept {
        UVE_ASSERT(!Empty());
        return m_data[m_size - 1U];
    }

    [[nodiscard]] T* Data() noexcept { return m_data; }
    [[nodiscard]] const T* Data() const noexcept { return m_data; }

    void PushBack(const T& value) {
        if (m_size >= m_capacity) {
            Grow(m_capacity * 2U);
        }
        new (&m_data[m_size]) T(value);
        ++m_size;
    }

    void PushBack(T&& value) {
        if (m_size >= m_capacity) {
            Grow(m_capacity * 2U);
        }
        new (&m_data[m_size]) T(std::move(value));
        ++m_size;
    }

    void PopBack() noexcept {
        UVE_ASSERT(!Empty());
        m_data[m_size - 1U].~T();
        --m_size;
    }

    void Clear() noexcept {
        while (!Empty()) {
            PopBack();
        }
    }

    [[nodiscard]] std::span<T> AsSpan() noexcept { return std::span<T>(Data(), m_size); }

    [[nodiscard]] std::span<const T> AsSpan() const noexcept {
        return std::span<const T>(Data(), m_size);
    }

    [[nodiscard]] T* begin() noexcept { return Data(); }
    [[nodiscard]] const T* begin() const noexcept { return Data(); }
    [[nodiscard]] T* end() noexcept { return Data() + m_size; }
    [[nodiscard]] const T* end() const noexcept { return Data() + m_size; }

private:
    // Same inactive-until-pushed union storage as FixedArray — see that header.
    union Slot {
        T element;

        Slot() noexcept {}
        ~Slot() noexcept {}

        Slot(const Slot&) = delete;
        Slot(Slot&&) = delete;
        Slot& operator=(const Slot&) = delete;
        Slot& operator=(Slot&&) = delete;
    };

    [[nodiscard]] T* InlineData() noexcept { return &m_inline[0U].element; }

    [[nodiscard]] const T* InlineData() const noexcept { return &m_inline[0U].element; }

    [[nodiscard]] bool IsSpilled() const noexcept { return m_data != InlineData(); }

    void Grow(std::size_t newCapacity) {
        UVE_ASSERT(newCapacity > m_capacity);
        // Aligned new: alignof(T) may exceed the default alignment (over-aligned math types),
        // and the matching aligned delete below pairs with this call.
        T* const grown = static_cast<T*>(
            ::operator new(newCapacity * sizeof(T), std::align_val_t(alignof(T))));

        // move_if_noexcept is the std::vector rule: prefer moves, but a throwing `T` move must
        // not strand elements half-moved with the source already destroyed — copy instead. On a
        // throwing copy the half-built buffer is torn down and the source is untouched.
        std::size_t constructed = 0U;
        try {
            for (; constructed < m_size; ++constructed) {
                new (&grown[constructed]) T(std::move_if_noexcept(m_data[constructed]));
            }
        } catch (...) {
            for (std::size_t index = 0U; index < constructed; ++index) {
                grown[index].~T();
            }
            ::operator delete(grown, std::align_val_t(alignof(T)));
            throw;
        }

        for (std::size_t index = 0U; index < m_size; ++index) {
            m_data[index].~T();
        }
        if (IsSpilled()) {
            ::operator delete(m_data, std::align_val_t(alignof(T)));
        }
        m_data = grown;
        m_capacity = newCapacity;
    }

    Slot m_inline[InlineCapacity];
    T* m_data;
    std::size_t m_size = 0U;
    std::size_t m_capacity = InlineCapacity;
};

}  // namespace UVE::Containers
