// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cstddef>
#include <initializer_list>
#include <new>
#include <span>
#include <type_traits>
#include <utility>

#include "uve/logging/assert_uve.h"

namespace UVE::Containers {

/// `SmallVectorUVE<T, InlineCapacity>` is a vector-like sequence container with a fixed-size
/// inline buffer: the first `InlineCapacity` elements live inside the container (no allocation),
/// and pushing past that spills onto the heap with doubling growth. The engine's replacement
/// for small `std::vector<T>`s on hot paths where the common case fits inline but the uncommon
/// case must still work (rather than assert, as `FixedArrayUVE` does).
///
/// Bounds contract: same as `FixedArrayUVE` — debug assert, unchecked release, no exceptions at
/// the API boundary. Growth may throw what allocation and `T`'s copy/move throw (matching
/// `std::vector`); a failed growth leaves the container unchanged.
///
/// Spill policy: capacity starts at `InlineCapacity` and doubles on every growth; there is no
/// shrink — once spilled, the heap buffer is kept until destruction. `CapacityUVE()` reports the
/// current capacity, so `CapacityUVE() > InlineCapacity` tells callers a spill happened. Unlike
/// `FixedArrayUVE` there is no `FullUVE()` — a small vector is never full, it grows.
///
/// `T` requirements mirror `std::vector<T>`; `T`'s move constructor SHOULD be noexcept (a
/// throwing move still works — growth falls back to copying — but pays the copy).
///
/// Thread-safety: none, like `std::vector` — externally synchronize shared use.
///
/// Naming: `begin`/`end` stay lowercase for range-based-for STL interop; every other member
/// carries the `UVE` suffix per engine convention.
template <typename T, std::size_t InlineCapacity>
class SmallVectorUVE final {
public:
    static_assert(InlineCapacity > 0U,
                  "SmallVectorUVE with zero inline capacity is just a vector; use std::vector.");

    SmallVectorUVE() noexcept : m_data(InlineDataUVE()) {}

    SmallVectorUVE(const SmallVectorUVE& other) : m_data(InlineDataUVE()) {
        if (other.m_size > InlineCapacity) {
            GrowUVE(other.m_size);
        }
        for (std::size_t index = 0U; index < other.m_size; ++index) {
            PushBackUVE(other[index]);
        }
    }

    // The inline branch cannot grow (capacity InlineCapacity always fits other's inline
    // elements), so this only throws if `T`'s move throws — hence the conditional noexcept.
    SmallVectorUVE(SmallVectorUVE&& other) noexcept(std::is_nothrow_move_constructible_v<T>)
        : m_data(InlineDataUVE()) {
        if (other.IsSpilled()) {
            m_data = other.m_data;
            m_size = other.m_size;
            m_capacity = other.m_capacity;
            other.m_data = other.InlineDataUVE();
            other.m_size = 0U;
            other.m_capacity = InlineCapacity;
        } else {
            for (std::size_t index = 0U; index < other.m_size; ++index) {
                PushBackUVE(std::move(other[index]));
            }
            other.ClearUVE();
        }
    }

    // Braced initialization, matching `std::vector` (deliberately non-explicit: aggregate
    // members like `FrameTaskDefinitionUVE::dependencies` list-initialize from `{...}`, which
    // cannot call an explicit constructor). Over-capacity lists spill, as a push loop would.
    SmallVectorUVE(std::initializer_list<T> items) : m_data(InlineDataUVE()) {
        if (items.size() > InlineCapacity) {
            GrowUVE(items.size());
        }
        for (const T& item : items) {
            PushBackUVE(item);
        }
    }

    ~SmallVectorUVE() {
        ClearUVE();
        if (IsSpilled()) {
            ::operator delete(m_data, std::align_val_t(alignof(T)));
        }
    }

    SmallVectorUVE& operator=(const SmallVectorUVE& other) {
        if (this != &other) {
            ClearUVE();
            if (other.m_size > m_capacity) {
                GrowUVE(other.m_size);
            }
            for (std::size_t index = 0U; index < other.m_size; ++index) {
                PushBackUVE(other[index]);
            }
        }
        return *this;
    }

    // Same no-grow argument as the move constructor: other's inline elements always fit, so this
    // only throws if `T`'s move throws.
    SmallVectorUVE& operator=(SmallVectorUVE&& other) noexcept(std::is_nothrow_move_constructible_v<T>) {
        if (this != &other) {
            ClearUVE();
            if (other.IsSpilled()) {
                if (IsSpilled()) {
                    ::operator delete(m_data, std::align_val_t(alignof(T)));
                }
                m_data = other.m_data;
                m_size = other.m_size;
                m_capacity = other.m_capacity;
                other.m_data = other.InlineDataUVE();
                other.m_size = 0U;
                other.m_capacity = InlineCapacity;
            } else {
                for (std::size_t index = 0U; index < other.m_size; ++index) {
                    PushBackUVE(std::move(other[index]));
                }
                other.ClearUVE();
            }
        }
        return *this;
    }

    [[nodiscard]] constexpr std::size_t SizeUVE() const noexcept { return m_size; }
    [[nodiscard]] std::size_t CapacityUVE() const noexcept { return m_capacity; }
    [[nodiscard]] constexpr bool EmptyUVE() const noexcept { return m_size == 0U; }

    [[nodiscard]] T& operator[](std::size_t index) noexcept {
        UVE_ASSERT(index < m_size);
        return m_data[index];
    }

    [[nodiscard]] const T& operator[](std::size_t index) const noexcept {
        UVE_ASSERT(index < m_size);
        return m_data[index];
    }

    [[nodiscard]] T& FrontUVE() noexcept {
        UVE_ASSERT(!EmptyUVE());
        return m_data[0U];
    }

    [[nodiscard]] const T& FrontUVE() const noexcept {
        UVE_ASSERT(!EmptyUVE());
        return m_data[0U];
    }

    [[nodiscard]] T& BackUVE() noexcept {
        UVE_ASSERT(!EmptyUVE());
        return m_data[m_size - 1U];
    }

    [[nodiscard]] const T& BackUVE() const noexcept {
        UVE_ASSERT(!EmptyUVE());
        return m_data[m_size - 1U];
    }

    [[nodiscard]] T* DataUVE() noexcept { return m_data; }
    [[nodiscard]] const T* DataUVE() const noexcept { return m_data; }

    void PushBackUVE(const T& value) {
        if (m_size >= m_capacity) {
            GrowUVE(m_capacity * 2U);
        }
        new (&m_data[m_size]) T(value);
        ++m_size;
    }

    void PushBackUVE(T&& value) {
        if (m_size >= m_capacity) {
            GrowUVE(m_capacity * 2U);
        }
        new (&m_data[m_size]) T(std::move(value));
        ++m_size;
    }

    void PopBackUVE() noexcept {
        UVE_ASSERT(!EmptyUVE());
        m_data[m_size - 1U].~T();
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
    // Same inactive-until-pushed union storage as FixedArrayUVE — see that header.
    union SlotUVE {
        T element;

        SlotUVE() noexcept {}
        ~SlotUVE() noexcept {}

        SlotUVE(const SlotUVE&) = delete;
        SlotUVE(SlotUVE&&) = delete;
        SlotUVE& operator=(const SlotUVE&) = delete;
        SlotUVE& operator=(SlotUVE&&) = delete;
    };

    [[nodiscard]] T* InlineDataUVE() noexcept { return &m_inline[0U].element; }

    [[nodiscard]] const T* InlineDataUVE() const noexcept { return &m_inline[0U].element; }

    [[nodiscard]] bool IsSpilled() const noexcept { return m_data != InlineDataUVE(); }

    void GrowUVE(std::size_t newCapacity) {
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

    SlotUVE m_inline[InlineCapacity];
    T* m_data;
    std::size_t m_size = 0U;
    std::size_t m_capacity = InlineCapacity;
};

}  // namespace UVE::Containers
