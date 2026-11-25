// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <atomic>
#include <cstdint>

namespace UVE::Memory {

/// `RefCounted` is the intrusive reference-count base: the count lives in the object, so a
/// raw `T*` converts to an owning `IntrusivePtr<T>` without a control block. New objects
/// start at zero references; the first `IntrusivePtr` wrapping one brings it to one.
///
/// Lifetime rule: the object self-destroys (`delete this`) when the last reference releases.
/// That makes this the heap-ownership counterpart to `UniquePtr` (which destroys through an
/// allocator): intrusive objects are always plain-`new` heap objects, never allocator-built.
/// Mixing the two — an allocator-built object wrapped intrusively — double-frees; the types do
/// not prevent it, the documented rule does.
///
/// Thread-safety: the count is atomic, so references may be copied and dropped from any thread.
/// The object's own thread-safety is still its own business.
class RefCounted {
public:
    virtual ~RefCounted() = default;

    RefCounted(const RefCounted&) = delete;
    RefCounted& operator=(const RefCounted&) = delete;
    RefCounted(RefCounted&&) = delete;
    RefCounted& operator=(RefCounted&&) = delete;

    /// Adds one reference. Called by `IntrusivePtr`, not by hand.
    void AddRef() noexcept { m_references.fetch_add(1U, std::memory_order_relaxed); }

    /// Drops one reference, destroying the object when the last one goes. Called by
    /// `IntrusivePtr`, not by hand.
    void Release() noexcept {
        if (m_references.fetch_sub(1U, std::memory_order_acq_rel) == 1U) {
            delete this;
        }
    }

    /// Current reference count. Debugging/introspection only — never branch on it.
    [[nodiscard]] std::uint32_t RefCount() const noexcept {
        return m_references.load(std::memory_order_relaxed);
    }

protected:
    RefCounted() = default;

private:
    std::atomic<std::uint32_t> m_references{0U};
};

} // namespace UVE::Memory
