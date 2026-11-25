// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <utility>

#include "allocator_utils.h"
#include "i_allocator.h"

namespace UVE::Memory {

/// `UniquePtr<T>` is sole ownership paired with the allocator API: construction goes through
/// `Construct`, destruction through `Destroy`, and the two can no longer be mismatched by
/// hand. Move-only, like `std::unique_ptr`; the allocator travels with the pointer so a moved
/// pointer still destroys through the allocator that built it.
///
/// A default-constructed (empty) pointer carries no allocator; assigning or move-constructing
/// from a live one adopts its allocator. `Release` detaches without destroying — the caller
/// takes the hand-pairing obligation back, for the rare handoff to non-owning code.
template <typename T>
class UniquePtr final {
public:
    UniquePtr() noexcept = default;

    /// Takes ownership of `pointer`, built by `allocator`. A null pointer needs no allocator.
    explicit UniquePtr(T* pointer, IAllocator* allocator = nullptr) noexcept
        : m_pointer(pointer), m_allocator(pointer != nullptr ? allocator : nullptr) {}

    ~UniquePtr() { Reset(); }

    UniquePtr(const UniquePtr&) = delete;
    UniquePtr& operator=(const UniquePtr&) = delete;

    UniquePtr(UniquePtr&& other) noexcept
        : m_pointer(other.m_pointer), m_allocator(other.m_allocator) {
        other.m_pointer = nullptr;
        other.m_allocator = nullptr;
    }

    UniquePtr& operator=(UniquePtr&& other) noexcept {
        if (this != &other) {
            Reset();
            m_pointer = other.m_pointer;
            m_allocator = other.m_allocator;
            other.m_pointer = nullptr;
            other.m_allocator = nullptr;
        }
        return *this;
    }

    /// Destroys the owned object (if any) through its allocator and goes empty.
    void Reset() noexcept {
        if (m_pointer != nullptr) {
            Destroy(*m_allocator, m_pointer);
            m_pointer = nullptr;
            m_allocator = nullptr;
        }
    }

    /// Detaches without destroying; the caller owns the hand-pairing from here.
    [[nodiscard]] T* Release() noexcept {
        T* const detached = m_pointer;
        m_pointer = nullptr;
        m_allocator = nullptr;
        return detached;
    }

    [[nodiscard]] T* Get() noexcept { return m_pointer; }
    [[nodiscard]] const T* Get() const noexcept { return m_pointer; }
    [[nodiscard]] T& operator*() const noexcept { return *m_pointer; }
    [[nodiscard]] T* operator->() const noexcept { return m_pointer; }
    explicit operator bool() const noexcept { return m_pointer != nullptr; }

private:
    T* m_pointer = nullptr;
    IAllocator* m_allocator = nullptr;
};

/// Builds a `T` in `allocator` and returns it owned. Prefer the macro, which captures the
/// construction site for the tracker's leak reports.
template <typename T, typename... TArgs>
[[nodiscard]] UniquePtr<T> MakeUnique(IAllocator& allocator, const char* sourceFile,
                                            const int sourceLine, TArgs&&... args) {
    return UniquePtr<T>(Construct<T>(allocator, sourceFile, sourceLine, std::forward<TArgs>(args)...),
                           &allocator);
}

/// `UVE_MAKE_UNIQUE(Type, allocator, args...)` — `MakeUnique` with the call site captured,
/// mirroring `UVE_CONSTRUCT`.
#define UVE_MAKE_UNIQUE(Type, allocator, ...) \
    ::UVE::Memory::MakeUnique<Type>((allocator), __FILE__, __LINE__ __VA_OPT__(, ) __VA_ARGS__)

} // namespace UVE::Memory
