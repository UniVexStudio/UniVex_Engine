// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cstddef>
#include <new>
#include <span>
#include <type_traits>
#include <utility>

#include "engine_assert.h"

namespace UVE::Containers {

/// `FixedArray<T, Capacity>` is a fixed-capacity, stack-allocated sequence container: the
/// engine's replacement for `std::array<T, N>` indexing-with-a-separate-count and for small
/// `std::vector<T>`s that must never allocate (per-frame scheduler storage, the extracted light
/// list). Only the first `Size()` slots hold live objects; capacity is fixed at compile time
/// and the container never grows, shrinks, or touches the heap.
///
/// Bounds contract: element access asserts in debug (`UVE_ASSERT`) and is unchecked in release,
/// matching the engine's assert-for-programming-errors convention — never an exception, never a
/// runtime error code. `PushBack` on a full container is a programming error and asserts;
/// callers facing possibly-overflowing input MUST check `Full()` first and take their own
/// fallible path (see `FrameTaskGraph::AddTask`, which returns `CapacityExceeded`).
///
/// Lifetime: elements are constructed in place on push and destroyed on pop/clear/destruction.
/// Copy/move copy/move the live elements; a moved-from container is left empty. `T` needs no
/// default constructor (slots start with inactive storage, not value-initialized `T`s).
///
/// Thread-safety: none, like `std::vector` — externally synchronize shared use.
///
/// Naming: `begin`/`end` stay lowercase for range-based-for STL interop; every other member
/// carries the `UVE` suffix per engine convention.
template <typename T, std::size_t N>
class FixedArray final {
public:
    static_assert(N > 0U,
                  "FixedArray with zero capacity holds nothing and asserts on every push; "
                  "that is never what a caller wants.");

    FixedArray() noexcept = default;

    FixedArray(const FixedArray& other) {
        for (std::size_t index = 0U; index < other.Size(); ++index) {
            PushBack(other[index]);
        }
    }

    FixedArray(FixedArray&& other) noexcept(std::is_nothrow_move_constructible_v<T>) {
        for (std::size_t index = 0U; index < other.Size(); ++index) {
            PushBack(std::move(other[index]));
        }
        other.Clear();
    }

    ~FixedArray() { Clear(); }

    FixedArray& operator=(const FixedArray& other) {
        if (this != &other) {
            Clear();
            for (std::size_t index = 0U; index < other.Size(); ++index) {
                PushBack(other[index]);
            }
        }
        return *this;
    }

    FixedArray& operator=(FixedArray&& other) noexcept(std::is_nothrow_move_constructible_v<T>) {
        if (this != &other) {
            Clear();
            for (std::size_t index = 0U; index < other.Size(); ++index) {
                PushBack(std::move(other[index]));
            }
            other.Clear();
        }
        return *this;
    }

    [[nodiscard]] constexpr std::size_t Size() const noexcept { return m_size; }
    [[nodiscard]] static constexpr std::size_t Capacity() noexcept { return N; }
    [[nodiscard]] constexpr bool Empty() const noexcept { return m_size == 0U; }
    [[nodiscard]] constexpr bool Full() const noexcept { return m_size >= N; }

    [[nodiscard]] T& operator[](std::size_t index) noexcept {
        UVE_ASSERT(index < m_size);
        return m_slots[index].element;
    }

    [[nodiscard]] const T& operator[](std::size_t index) const noexcept {
        UVE_ASSERT(index < m_size);
        return m_slots[index].element;
    }

    [[nodiscard]] T& Front() noexcept {
        UVE_ASSERT(!Empty());
        return m_slots[0U].element;
    }

    [[nodiscard]] const T& Front() const noexcept {
        UVE_ASSERT(!Empty());
        return m_slots[0U].element;
    }

    [[nodiscard]] T& Back() noexcept {
        UVE_ASSERT(!Empty());
        return m_slots[m_size - 1U].element;
    }

    [[nodiscard]] const T& Back() const noexcept {
        UVE_ASSERT(!Empty());
        return m_slots[m_size - 1U].element;
    }

    [[nodiscard]] T* Data() noexcept { return &m_slots[0U].element; }
    [[nodiscard]] const T* Data() const noexcept { return &m_slots[0U].element; }

    void PushBack(const T& value) {
        UVE_ASSERT(!Full());
        new (&m_slots[m_size].element) T(value);
        ++m_size;
    }

    void PushBack(T&& value) {
        UVE_ASSERT(!Full());
        new (&m_slots[m_size].element) T(std::move(value));
        ++m_size;
    }

    void PopBack() noexcept {
        UVE_ASSERT(!Empty());
        m_slots[m_size - 1U].element.~T();
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
    // Inactive-until-pushed storage: a union with a user-provided no-op constructor leaves the
    // element lifetime fully manual (so `T` need not be default-constructible), and member
    // access through the union needs no reinterpret_cast, keeping -Wcast-align quiet.
    union Slot {
        T element;

        Slot() noexcept {}
        ~Slot() noexcept {}

        Slot(const Slot&) = delete;
        Slot(Slot&&) = delete;
        Slot& operator=(const Slot&) = delete;
        Slot& operator=(Slot&&) = delete;
    };

    Slot m_slots[N];
    std::size_t m_size = 0U;
};

}  // namespace UVE::Containers
