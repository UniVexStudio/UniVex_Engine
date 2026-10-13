// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <utility>

#include "uve/memory/allocator_utils_uve.h"
#include "uve/memory/i_allocator_uve.h"

namespace UVE::Memory {

/// `UniquePtrUVE<T>` is sole ownership paired with the allocator API: construction goes through
/// `ConstructUVE`, destruction through `DestroyUVE`, and the two can no longer be mismatched by
/// hand. Move-only, like `std::unique_ptr`; the allocator travels with the pointer so a moved
/// pointer still destroys through the allocator that built it.
///
/// A default-constructed (empty) pointer carries no allocator; assigning or move-constructing
/// from a live one adopts its allocator. `ReleaseUVE` detaches without destroying — the caller
/// takes the hand-pairing obligation back, for the rare handoff to non-owning code.
template <typename T>
class UniquePtrUVE final {
public:
    UniquePtrUVE() noexcept = default;

    /// Takes ownership of `pointer`, built by `allocator`. A null pointer needs no allocator.
    explicit UniquePtrUVE(T* pointer, IAllocatorUVE* allocator = nullptr) noexcept
        : m_pointer(pointer), m_allocator(pointer != nullptr ? allocator : nullptr) {}

    ~UniquePtrUVE() { ResetUVE(); }

    UniquePtrUVE(const UniquePtrUVE&) = delete;
    UniquePtrUVE& operator=(const UniquePtrUVE&) = delete;

    UniquePtrUVE(UniquePtrUVE&& other) noexcept
        : m_pointer(other.m_pointer), m_allocator(other.m_allocator) {
        other.m_pointer = nullptr;
        other.m_allocator = nullptr;
    }

    UniquePtrUVE& operator=(UniquePtrUVE&& other) noexcept {
        if (this != &other) {
            ResetUVE();
            m_pointer = other.m_pointer;
            m_allocator = other.m_allocator;
            other.m_pointer = nullptr;
            other.m_allocator = nullptr;
        }
        return *this;
    }

    /// Destroys the owned object (if any) through its allocator and goes empty.
    void ResetUVE() noexcept {
        if (m_pointer != nullptr) {
            DestroyUVE(*m_allocator, m_pointer);
            m_pointer = nullptr;
            m_allocator = nullptr;
        }
    }

    /// Detaches without destroying; the caller owns the hand-pairing from here.
    [[nodiscard]] T* ReleaseUVE() noexcept {
        T* const detached = m_pointer;
        m_pointer = nullptr;
        m_allocator = nullptr;
        return detached;
    }

    [[nodiscard]] T* GetUVE() noexcept { return m_pointer; }
    [[nodiscard]] const T* GetUVE() const noexcept { return m_pointer; }
    [[nodiscard]] T& operator*() const noexcept { return *m_pointer; }
    [[nodiscard]] T* operator->() const noexcept { return m_pointer; }
    explicit operator bool() const noexcept { return m_pointer != nullptr; }

private:
    T* m_pointer = nullptr;
    IAllocatorUVE* m_allocator = nullptr;
};

/// Builds a `T` in `allocator` and returns it owned. Prefer the macro, which captures the
/// construction site for the tracker's leak reports.
template <typename T, typename... TArgs>
[[nodiscard]] UniquePtrUVE<T> MakeUniqueUVE(IAllocatorUVE& allocator, const char* sourceFile,
                                            const int sourceLine, TArgs&&... args) {
    return UniquePtrUVE<T>(ConstructUVE<T>(allocator, sourceFile, sourceLine, std::forward<TArgs>(args)...),
                           &allocator);
}

/// `UVE_MAKE_UNIQUE(Type, allocator, args...)` — `MakeUniqueUVE` with the call site captured,
/// mirroring `UVE_CONSTRUCT`.
#define UVE_MAKE_UNIQUE(Type, allocator, ...) \
    ::UVE::Memory::MakeUniqueUVE<Type>((allocator), __FILE__, __LINE__ __VA_OPT__(, ) __VA_ARGS__)

} // namespace UVE::Memory
