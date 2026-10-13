// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cstddef>
#include <new>
#include <span>
#include <type_traits>
#include <utility>

#include "uve/logging/assert_uve.h"

namespace UVE::Containers {

/// `FixedArrayUVE<T, Capacity>` is a fixed-capacity, stack-allocated sequence container: the
/// engine's replacement for `std::array<T, N>` indexing-with-a-separate-count and for small
/// `std::vector<T>`s that must never allocate (per-frame scheduler storage, the extracted light
/// list). Only the first `SizeUVE()` slots hold live objects; capacity is fixed at compile time
/// and the container never grows, shrinks, or touches the heap.
///
/// Bounds contract: element access asserts in debug (`UVE_ASSERT`) and is unchecked in release,
/// matching the engine's assert-for-programming-errors convention — never an exception, never a
/// runtime error code. `PushBackUVE` on a full container is a programming error and asserts;
/// callers facing possibly-overflowing input MUST check `FullUVE()` first and take their own
/// fallible path (see `FrameTaskGraphUVE::AddTaskUVE`, which returns `CapacityExceeded`).
///
/// Lifetime: elements are constructed in place on push and destroyed on pop/clear/destruction.
/// Copy/move copy/move the live elements; a moved-from container is left empty. `T` needs no
/// default constructor (slots start with inactive storage, not value-initialized `T`s).
///
/// Thread-safety: none, like `std::vector` — externally synchronize shared use.
///
/// Naming: `begin`/`end` stay lowercase for range-based-for STL interop; every other member
/// carries the `UVE` suffix per engine convention.
template <typename T, std::size_t Capacity>
class FixedArrayUVE final {
public:
    static_assert(Capacity > 0U,
                  "FixedArrayUVE with zero capacity holds nothing and asserts on every push; "
                  "that is never what a caller wants.");

    FixedArrayUVE() noexcept = default;

    FixedArrayUVE(const FixedArrayUVE& other) {
        for (std::size_t index = 0U; index < other.SizeUVE(); ++index) {
            PushBackUVE(other[index]);
        }
    }

    FixedArrayUVE(FixedArrayUVE&& other) noexcept(std::is_nothrow_move_constructible_v<T>) {
        for (std::size_t index = 0U; index < other.SizeUVE(); ++index) {
            PushBackUVE(std::move(other[index]));
        }
        other.ClearUVE();
    }

    ~FixedArrayUVE() { ClearUVE(); }

    FixedArrayUVE& operator=(const FixedArrayUVE& other) {
        if (this != &other) {
            ClearUVE();
            for (std::size_t index = 0U; index < other.SizeUVE(); ++index) {
                PushBackUVE(other[index]);
            }
        }
        return *this;
    }

    FixedArrayUVE& operator=(FixedArrayUVE&& other) noexcept(std::is_nothrow_move_constructible_v<T>) {
        if (this != &other) {
            ClearUVE();
            for (std::size_t index = 0U; index < other.SizeUVE(); ++index) {
                PushBackUVE(std::move(other[index]));
            }
            other.ClearUVE();
        }
        return *this;
    }

    [[nodiscard]] constexpr std::size_t SizeUVE() const noexcept { return m_size; }
    [[nodiscard]] static constexpr std::size_t CapacityUVE() noexcept { return Capacity; }
    [[nodiscard]] constexpr bool EmptyUVE() const noexcept { return m_size == 0U; }
    [[nodiscard]] constexpr bool FullUVE() const noexcept { return m_size >= Capacity; }

    [[nodiscard]] T& operator[](std::size_t index) noexcept {
        UVE_ASSERT(index < m_size);
        return m_slots[index].element;
    }

    [[nodiscard]] const T& operator[](std::size_t index) const noexcept {
        UVE_ASSERT(index < m_size);
        return m_slots[index].element;
    }

    [[nodiscard]] T& FrontUVE() noexcept {
        UVE_ASSERT(!EmptyUVE());
        return m_slots[0U].element;
    }

    [[nodiscard]] const T& FrontUVE() const noexcept {
        UVE_ASSERT(!EmptyUVE());
        return m_slots[0U].element;
    }

    [[nodiscard]] T& BackUVE() noexcept {
        UVE_ASSERT(!EmptyUVE());
        return m_slots[m_size - 1U].element;
    }

    [[nodiscard]] const T& BackUVE() const noexcept {
        UVE_ASSERT(!EmptyUVE());
        return m_slots[m_size - 1U].element;
    }

    [[nodiscard]] T* DataUVE() noexcept { return &m_slots[0U].element; }
    [[nodiscard]] const T* DataUVE() const noexcept { return &m_slots[0U].element; }

    void PushBackUVE(const T& value) {
        UVE_ASSERT(!FullUVE());
        new (&m_slots[m_size].element) T(value);
        ++m_size;
    }

    void PushBackUVE(T&& value) {
        UVE_ASSERT(!FullUVE());
        new (&m_slots[m_size].element) T(std::move(value));
        ++m_size;
    }

    void PopBackUVE() noexcept {
        UVE_ASSERT(!EmptyUVE());
        m_slots[m_size - 1U].element.~T();
        --m_size;
    }

    void ClearUVE() noexcept {
        while (!EmptyUVE()) {
            PopBackUVE();
        }
    }

    [[nodiscard]] std::span<T> AsSpanUVE() noexcept { return std::span<T>(DataUVE(), m_size); }

    [[nodiscard]] std::span<const T> AsSpanUVE() const noexcept {
        return std::span<const T>(DataUVE(), m_size);
    }

    [[nodiscard]] T* begin() noexcept { return DataUVE(); }
    [[nodiscard]] const T* begin() const noexcept { return DataUVE(); }
    [[nodiscard]] T* end() noexcept { return DataUVE() + m_size; }
    [[nodiscard]] const T* end() const noexcept { return DataUVE() + m_size; }

private:
    // Inactive-until-pushed storage: a union with a user-provided no-op constructor leaves the
    // element lifetime fully manual (so `T` need not be default-constructible), and member
    // access through the union needs no reinterpret_cast, keeping -Wcast-align quiet.
    union SlotUVE {
        T element;

        SlotUVE() noexcept {}
        ~SlotUVE() noexcept {}

        SlotUVE(const SlotUVE&) = delete;
        SlotUVE(SlotUVE&&) = delete;
        SlotUVE& operator=(const SlotUVE&) = delete;
        SlotUVE& operator=(SlotUVE&&) = delete;
    };

    SlotUVE m_slots[Capacity];
    std::size_t m_size = 0U;
};

}  // namespace UVE::Containers
