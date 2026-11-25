// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <array>
#include <cstddef>
#include <optional>
#include <utility>

#include "engine_assert.h"

namespace UVE::Containers {

/// `RingBuffer<T, Capacity>` is a fixed-capacity, stack-allocated bounded FIFO: the recent-
/// message ring, the last-N-frames sample window, the capture that must not grow. Storage never
/// touches the heap; `T` needs no default constructor (slots hold `optional<T>`).
///
/// Overflow policy: pushing onto a full ring overwrites the oldest element and bumps
/// `DroppedCount()` — silent loss is never acceptable, so the loss is counted, following
/// `ProfilerCapture`'s explicit-dropped-record-counters precedent. Callers that cannot lose
/// data MUST check `Full()` first (like `FixedArray::PushBack`'s contract, except the
/// ring degrades by policy instead of asserting).
///
/// Bounds contract: `Front`/`Back`/`PopFront` on an empty ring assert in debug and are
/// unchecked in release, matching the module's convention.
///
/// Naming: `begin`/`end` stay lowercase for range-based-for STL interop (oldest → newest);
/// every other member carries the `UVE` suffix per engine convention.
template <typename T, std::size_t Capacity>
class RingBuffer final {
public:
    static_assert(Capacity > 0U, "RingBuffer with zero capacity holds nothing; that is never "
                                 "what a caller wants.");

    RingBuffer() = default;

    /// Pushes `value` at the back, overwriting (and counting) the oldest element when full.
    void PushBack(T value) {
        if (Full()) {
            m_slots[m_head] = std::move(value);
            m_head = (m_head + 1U) % Capacity;
            ++m_dropped;
            return;
        }
        m_slots[(m_head + m_count) % Capacity] = std::move(value);
        ++m_count;
    }

    /// Removes the oldest element. Asserts non-empty.
    void PopFront() {
        UVE_ASSERT(!Empty());
        m_slots[m_head].reset();
        m_head = (m_head + 1U) % Capacity;
        --m_count;
    }

    /// The oldest element. Asserts non-empty.
    [[nodiscard]] T& Front() {
        UVE_ASSERT(!Empty());
        return *m_slots[m_head];
    }

    /// Const overload of `Front`.
    [[nodiscard]] const T& Front() const {
        UVE_ASSERT(!Empty());
        return *m_slots[m_head];
    }

    /// The newest element. Asserts non-empty.
    [[nodiscard]] T& Back() {
        UVE_ASSERT(!Empty());
        return *m_slots[(m_head + m_count - 1U) % Capacity];
    }

    /// Const overload of `Back`.
    [[nodiscard]] const T& Back() const {
        UVE_ASSERT(!Empty());
        return *m_slots[(m_head + m_count - 1U) % Capacity];
    }

    /// The i-th oldest element (0 == Front). Asserts `index < Size()`.
    [[nodiscard]] T& At(const std::size_t index) {
        UVE_ASSERT(index < m_count);
        return *m_slots[(m_head + index) % Capacity];
    }

    /// Const overload of `At`.
    [[nodiscard]] const T& At(const std::size_t index) const {
        UVE_ASSERT(index < m_count);
        return *m_slots[(m_head + index) % Capacity];
    }

    /// Number of live elements.
    [[nodiscard]] std::size_t Size() const noexcept { return m_count; }

    /// True when no elements are live.
    [[nodiscard]] bool Empty() const noexcept { return m_count == 0U; }

    /// True when the next push would overwrite.
    [[nodiscard]] bool Full() const noexcept { return m_count == Capacity; }

    /// How many pushes overwrote a live element. Never reset except by `Clear`.
    [[nodiscard]] std::size_t DroppedCount() const noexcept { return m_dropped; }

    /// Removes every element and zeroes the drop counter.
    void Clear() noexcept {
        for (auto& slot : m_slots) {
            slot.reset();
        }
        m_head = 0U;
        m_count = 0U;
        m_dropped = 0U;
    }

    class Iterator final {
    public:
        Iterator(RingBuffer* owner, const std::size_t position) noexcept
            : m_owner(owner), m_position(position) {}
        T& operator*() const { return m_owner->At(m_position); }
        Iterator& operator++() noexcept {
            ++m_position;
            return *this;
        }
        [[nodiscard]] bool operator==(const Iterator& other) const noexcept {
            return m_position == other.m_position;
        }
        [[nodiscard]] bool operator!=(const Iterator& other) const noexcept {
            return !(*this == other);
        }

    private:
        RingBuffer* m_owner;
        std::size_t m_position;
    };

    class ConstIterator final {
    public:
        ConstIterator(const RingBuffer* owner, const std::size_t position) noexcept
            : m_owner(owner), m_position(position) {}
        const T& operator*() const { return m_owner->At(m_position); }
        ConstIterator& operator++() noexcept {
            ++m_position;
            return *this;
        }
        [[nodiscard]] bool operator==(const ConstIterator& other) const noexcept {
            return m_position == other.m_position;
        }
        [[nodiscard]] bool operator!=(const ConstIterator& other) const noexcept {
            return !(*this == other);
        }

    private:
        const RingBuffer* m_owner;
        std::size_t m_position;
    };

    Iterator begin() noexcept { return Iterator{this, 0U}; }
    Iterator end() noexcept { return Iterator{this, m_count}; }
    ConstIterator begin() const noexcept { return ConstIterator{this, 0U}; }
    ConstIterator end() const noexcept { return ConstIterator{this, m_count}; }

private:
    std::array<std::optional<T>, Capacity> m_slots{};
    std::size_t m_head = 0U;
    std::size_t m_count = 0U;
    std::size_t m_dropped = 0U;
};

} // namespace UVE::Containers
