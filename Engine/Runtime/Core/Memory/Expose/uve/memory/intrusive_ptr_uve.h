// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <type_traits>
#include <utility>

#include "uve/memory/ref_counted_uve.h"

namespace UVE::Memory {

/// `IntrusivePtrUVE<T>` is shared ownership over a `RefCountedUVE` derivation: copying adds a
/// reference, destruction drops one, the last drop destroys the object. The `T*` constructor
/// takes a FRESH object (zero references) and brings it to one — wrapping the same raw pointer
/// twice double-counts, so fresh objects go straight into the first pointer and never sit raw.
///
/// Null is a valid state (an empty pointer holds nothing and drops nothing). Comparison is by
/// identity, not by value: two pointers are equal iff they point at the same object.
template <typename T>
class IntrusivePtrUVE final {
public:
    IntrusivePtrUVE() noexcept = default;

    /// Wraps a fresh `RefCountedUVE` object (see above). Null stays empty.
    explicit IntrusivePtrUVE(T* pointer) noexcept : m_pointer(pointer) { AddRefUVE(); }

    ~IntrusivePtrUVE() { ReleaseUVE(); }

    IntrusivePtrUVE(const IntrusivePtrUVE& other) noexcept : m_pointer(other.m_pointer) { AddRefUVE(); }

    IntrusivePtrUVE& operator=(const IntrusivePtrUVE& other) noexcept {
        if (this != &other) {
            ReleaseUVE();
            m_pointer = other.m_pointer;
            AddRefUVE();
        }
        return *this;
    }

    IntrusivePtrUVE(IntrusivePtrUVE&& other) noexcept : m_pointer(other.m_pointer) {
        other.m_pointer = nullptr;
    }

    IntrusivePtrUVE& operator=(IntrusivePtrUVE&& other) noexcept {
        if (this != &other) {
            ReleaseUVE();
            m_pointer = other.m_pointer;
            other.m_pointer = nullptr;
        }
        return *this;
    }

    /// Drops the held reference (destroying the object when last) and goes empty.
    void ResetUVE() noexcept {
        ReleaseUVE();
        m_pointer = nullptr;
    }

    [[nodiscard]] T* GetUVE() noexcept { return m_pointer; }
    [[nodiscard]] const T* GetUVE() const noexcept { return m_pointer; }
    [[nodiscard]] T& operator*() const noexcept { return *m_pointer; }
    [[nodiscard]] T* operator->() const noexcept { return m_pointer; }
    explicit operator bool() const noexcept { return m_pointer != nullptr; }

    [[nodiscard]] friend bool operator==(const IntrusivePtrUVE& lhs,
                                         const IntrusivePtrUVE& rhs) noexcept {
        return lhs.m_pointer == rhs.m_pointer;
    }

    [[nodiscard]] friend bool operator!=(const IntrusivePtrUVE& lhs,
                                         const IntrusivePtrUVE& rhs) noexcept {
        return !(lhs == rhs);
    }

private:
    void AddRefUVE() noexcept {
        if (m_pointer != nullptr) {
            m_pointer->AddRefUVE();
        }
    }

    void ReleaseUVE() noexcept {
        if (m_pointer != nullptr) {
            m_pointer->ReleaseUVE();
        }
    }

    T* m_pointer = nullptr;
};

} // namespace UVE::Memory
