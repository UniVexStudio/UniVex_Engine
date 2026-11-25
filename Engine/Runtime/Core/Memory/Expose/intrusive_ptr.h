// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <type_traits>
#include <utility>

#include "ref_counted.h"

namespace UVE::Memory {

/// `IntrusivePtr<T>` is shared ownership over a `RefCounted` derivation: copying adds a
/// reference, destruction drops one, the last drop destroys the object. The `T*` constructor
/// takes a FRESH object (zero references) and brings it to one — wrapping the same raw pointer
/// twice double-counts, so fresh objects go straight into the first pointer and never sit raw.
///
/// Null is a valid state (an empty pointer holds nothing and drops nothing). Comparison is by
/// identity, not by value: two pointers are equal iff they point at the same object.
template <typename T>
class IntrusivePtr final {
public:
    IntrusivePtr() noexcept = default;

    /// Wraps a fresh `RefCounted` object (see above). Null stays empty.
    explicit IntrusivePtr(T* pointer) noexcept : m_pointer(pointer) { AddRef(); }

    ~IntrusivePtr() { Release(); }

    IntrusivePtr(const IntrusivePtr& other) noexcept : m_pointer(other.m_pointer) { AddRef(); }

    IntrusivePtr& operator=(const IntrusivePtr& other) noexcept {
        if (this != &other) {
            Release();
            m_pointer = other.m_pointer;
            AddRef();
        }
        return *this;
    }

    IntrusivePtr(IntrusivePtr&& other) noexcept : m_pointer(other.m_pointer) {
        other.m_pointer = nullptr;
    }

    IntrusivePtr& operator=(IntrusivePtr&& other) noexcept {
        if (this != &other) {
            Release();
            m_pointer = other.m_pointer;
            other.m_pointer = nullptr;
        }
        return *this;
    }

    /// Drops the held reference (destroying the object when last) and goes empty.
    void Reset() noexcept {
        Release();
        m_pointer = nullptr;
    }

    [[nodiscard]] T* Get() noexcept { return m_pointer; }
    [[nodiscard]] const T* Get() const noexcept { return m_pointer; }
    [[nodiscard]] T& operator*() const noexcept { return *m_pointer; }
    [[nodiscard]] T* operator->() const noexcept { return m_pointer; }
    explicit operator bool() const noexcept { return m_pointer != nullptr; }

    [[nodiscard]] friend bool operator==(const IntrusivePtr& lhs,
                                         const IntrusivePtr& rhs) noexcept {
        return lhs.m_pointer == rhs.m_pointer;
    }

    [[nodiscard]] friend bool operator!=(const IntrusivePtr& lhs,
                                         const IntrusivePtr& rhs) noexcept {
        return !(lhs == rhs);
    }

private:
    void AddRef() noexcept {
        if (m_pointer != nullptr) {
            m_pointer->AddRef();
        }
    }

    void Release() noexcept {
        if (m_pointer != nullptr) {
            m_pointer->Release();
        }
    }

    T* m_pointer = nullptr;
};

} // namespace UVE::Memory
