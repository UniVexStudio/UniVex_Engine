// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <array>
#include <cstddef>
#include <optional>
#include <utility>

#include "uve/logging/assert_uve.h"

namespace UVE::Containers {

/// `RingBufferUVE<T, Capacity>` is a fixed-capacity, stack-allocated bounded FIFO: the recent-
/// message ring, the last-N-frames sample window, the capture that must not grow. Storage never
/// touches the heap; `T` needs no default constructor (slots hold `optional<T>`).
///
/// Overflow policy: pushing onto a full ring overwrites the oldest element and bumps
/// `DroppedCountUVE()` — silent loss is never acceptable, so the loss is counted, following
/// `ProfilerCaptureUVE`'s explicit-dropped-record-counters precedent. Callers that cannot lose
/// data MUST check `FullUVE()` first (like `FixedArrayUVE::PushBackUVE`'s contract, except the
/// ring degrades by policy instead of asserting).
///
/// Bounds contract: `FrontUVE`/`BackUVE`/`PopFrontUVE` on an empty ring assert in debug and are
/// unchecked in release, matching the module's convention.
///
/// Naming: `begin`/`end` stay lowercase for range-based-for STL interop (oldest → newest);
/// every other member carries the `UVE` suffix per engine convention.
template <typename T, std::size_t Capacity>
class RingBufferUVE final {
public:
    static_assert(Capacity > 0U, "RingBufferUVE with zero capacity holds nothing; that is never "
                                 "what a caller wants.");

    RingBufferUVE() = default;

    /// Pushes `value` at the back, overwriting (and counting) the oldest element when full.
    void PushBackUVE(T value) {
        if (FullUVE()) {
            m_slots[m_head] = std::move(value);
            m_head = (m_head + 1U) % Capacity;
            ++m_dropped;
            return;
        }
        m_slots[(m_head + m_count) % Capacity] = std::move(value);
        ++m_count;
    }

    /// Removes the oldest element. Asserts non-empty.
    void PopFrontUVE() {
        UVE_ASSERT(!EmptyUVE());
        m_slots[m_head].reset();
        m_head = (m_head + 1U) % Capacity;
        --m_count;
    }

    /// The oldest element. Asserts non-empty.
    [[nodiscard]] T& FrontUVE() {
        UVE_ASSERT(!EmptyUVE());
        return *m_slots[m_head];
    }

    /// Const overload of `FrontUVE`.
    [[nodiscard]] const T& FrontUVE() const {
        UVE_ASSERT(!EmptyUVE());
        return *m_slots[m_head];
    }

    /// The newest element. Asserts non-empty.
    [[nodiscard]] T& BackUVE() {
        UVE_ASSERT(!EmptyUVE());
        return *m_slots[(m_head + m_count - 1U) % Capacity];
    }

    /// Const overload of `BackUVE`.
    [[nodiscard]] const T& BackUVE() const {
        UVE_ASSERT(!EmptyUVE());
        return *m_slots[(m_head + m_count - 1U) % Capacity];
    }

    /// The i-th oldest element (0 == FrontUVE). Asserts `index < SizeUVE()`.
    [[nodiscard]] T& AtUVE(const std::size_t index) {
        UVE_ASSERT(index < m_count);
        return *m_slots[(m_head + index) % Capacity];
    }

    /// Const overload of `AtUVE`.
    [[nodiscard]] const T& AtUVE(const std::size_t index) const {
        UVE_ASSERT(index < m_count);
        return *m_slots[(m_head + index) % Capacity];
    }

    /// Number of live elements.
    [[nodiscard]] std::size_t SizeUVE() const noexcept { return m_count; }

    /// True when no elements are live.
    [[nodiscard]] bool EmptyUVE() const noexcept { return m_count == 0U; }

    /// True when the next push would overwrite.
    [[nodiscard]] bool FullUVE() const noexcept { return m_count == Capacity; }

    /// How many pushes overwrote a live element. Never reset except by `ClearUVE`.
    [[nodiscard]] std::size_t DroppedCountUVE() const noexcept { return m_dropped; }

    /// Removes every element and zeroes the drop counter.
    void ClearUVE() noexcept {
        for (auto& slot : m_slots) {
            slot.reset();
        }
        m_head = 0U;
        m_count = 0U;
        m_dropped = 0U;
    }

    class IteratorUVE final {
    public:
        IteratorUVE(RingBufferUVE* owner, const std::size_t position) noexcept
            : m_owner(owner), m_position(position) {}
        T& operator*() const { return m_owner->AtUVE(m_position); }
        IteratorUVE& operator++() noexcept {
            ++m_position;
            return *this;
        }
        [[nodiscard]] bool operator==(const IteratorUVE& other) const noexcept {
            return m_position == other.m_position;
        }
        [[nodiscard]] bool operator!=(const IteratorUVE& other) const noexcept {
            return !(*this == other);
        }

    private:
        RingBufferUVE* m_owner;
        std::size_t m_position;
    };

    class ConstIteratorUVE final {
    public:
        ConstIteratorUVE(const RingBufferUVE* owner, const std::size_t position) noexcept
            : m_owner(owner), m_position(position) {}
        const T& operator*() const { return m_owner->AtUVE(m_position); }
        ConstIteratorUVE& operator++() noexcept {
            ++m_position;
            return *this;
        }
        [[nodiscard]] bool operator==(const ConstIteratorUVE& other) const noexcept {
            return m_position == other.m_position;
        }
        [[nodiscard]] bool operator!=(const ConstIteratorUVE& other) const noexcept {
            return !(*this == other);
        }

    private:
        const RingBufferUVE* m_owner;
        std::size_t m_position;
    };

    IteratorUVE begin() noexcept { return IteratorUVE{this, 0U}; }
    IteratorUVE end() noexcept { return IteratorUVE{this, m_count}; }
    ConstIteratorUVE begin() const noexcept { return ConstIteratorUVE{this, 0U}; }
    ConstIteratorUVE end() const noexcept { return ConstIteratorUVE{this, m_count}; }

private:
    std::array<std::optional<T>, Capacity> m_slots{};
    std::size_t m_head = 0U;
    std::size_t m_count = 0U;
    std::size_t m_dropped = 0U;
};

} // namespace UVE::Containers
