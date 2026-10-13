// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#pragma once

#include <cstddef>
#include <limits>
#include <new>
#include <type_traits>

#include "uve/logging/assert_uve.h"
#include "uve/memory/i_allocator_uve.h"

namespace UVE::Memory {

/// `StdAllocatorUVE<T>` adapts any `IAllocatorUVE` to the C++ allocator interface, so
/// `std::vector`, `std::string` and friends can allocate through the engine's allocators
/// (pool, stack, heap) instead of the global heap.
///
/// The adaptor is deliberately thin and honest: one `allocate(n)` call is exactly one
/// `AllocateUVE(n * sizeof(T), alignof(T), ...)` call on the backing allocator. There is no
/// fallback — a request that exceeds a pool's block size, or arrives at an exhausted pool,
/// surfaces the backing allocator's own contract (debug `UVE_ASSERT`, release throw), never a
/// silent heap allocation. Size your pools for the containers they serve (`reserve()` what the
/// vector will hold), the same way `FixedArrayUVE` callers check `FullUVE()`.
///
/// Stateful semantics follow `std::pmr::polymorphic_allocator`: the allocator is *not*
/// propagated on copy/move-assign or swap (all three traits are `false_type`), so a container
/// keeps its backing allocator across assignment and unequal-allocator moves degrade to
/// element-wise moves rather than cross-allocator pointer steals. Swapping two containers with
/// different backing allocators is undefined — the same rule as `std::pmr`.
///
/// Equality is backing-allocator identity: two adaptors compare equal if and only if they point
/// at the same `IAllocatorUVE`. The file/line call-site label does not participate — it only
/// annotates tracker records, so it cannot affect deallocation safety.
///
/// Lifetime: the backing allocator must outlive every container and every adaptor copy.
/// The default constructor exists only because containers require it; a default-constructed
/// adaptor has no backing allocator and `allocate()` asserts — always construct containers with
/// an explicitly adapted allocator.
///
/// Source location: STL `allocate(n)` carries no call site, so the adaptor labels every
/// allocation with the file/line captured at *adaptor construction* (pass `__FILE__`/
/// `__LINE__`, or leave the `nullptr`/`0` defaults). Leak reports then point at the container's
/// creation site instead of nowhere.
///
/// Thread-safety: whatever the backing allocator provides (the pool/stack/heap allocators are
/// all single-threaded by contract) — the adaptor adds no synchronization of its own.
template <typename T>
class StdAllocatorUVE {
public:
    using value_type = T;
    using size_type = std::size_t;
    using difference_type = std::ptrdiff_t;
    using propagate_on_container_copy_assignment = std::false_type;
    using propagate_on_container_move_assignment = std::false_type;
    using propagate_on_container_swap = std::false_type;
    using is_always_equal = std::false_type;

    template <typename U>
    struct rebind {
        using other = StdAllocatorUVE<U>;
    };

    StdAllocatorUVE() noexcept = default;

    explicit StdAllocatorUVE(IAllocatorUVE& allocator, const char* sourceFile = nullptr,
                             int sourceLine = 0) noexcept
        : m_allocator(&allocator), m_sourceFile(sourceFile), m_sourceLine(sourceLine) {}

    template <typename U>
    StdAllocatorUVE(const StdAllocatorUVE<U>& other) noexcept
        : m_allocator(other.m_allocator),
          m_sourceFile(other.m_sourceFile),
          m_sourceLine(other.m_sourceLine) {}

    [[nodiscard]] T* allocate(std::size_t count) {
        UVE_ASSERT(m_allocator != nullptr);
        if (count > max_size()) {
            throw std::bad_array_new_length();
        }
        void* const memory = m_allocator->AllocateUVE(count * sizeof(T), alignof(T),
                                                      m_sourceFile, m_sourceLine);
        return static_cast<T*>(memory);
    }

    void deallocate(T* pointer, std::size_t /*count*/) noexcept {
        UVE_ASSERT(m_allocator != nullptr);
        m_allocator->DeallocateUVE(pointer);
    }

    [[nodiscard]] constexpr std::size_t max_size() const noexcept {
        return std::numeric_limits<std::size_t>::max() / sizeof(T);
    }

private:
    template <typename U>
    friend class StdAllocatorUVE;

    template <typename A, typename B>
    friend bool operator==(const StdAllocatorUVE<A>& lhs, const StdAllocatorUVE<B>& rhs) noexcept;

    IAllocatorUVE* m_allocator = nullptr;
    const char* m_sourceFile = nullptr;
    int m_sourceLine = 0;
};

template <typename A, typename B>
[[nodiscard]] bool operator==(const StdAllocatorUVE<A>& lhs,
                              const StdAllocatorUVE<B>& rhs) noexcept {
    return lhs.m_allocator == rhs.m_allocator;
}

template <typename A, typename B>
[[nodiscard]] bool operator!=(const StdAllocatorUVE<A>& lhs,
                              const StdAllocatorUVE<B>& rhs) noexcept {
    return !(lhs == rhs);
}

} // namespace UVE::Memory
