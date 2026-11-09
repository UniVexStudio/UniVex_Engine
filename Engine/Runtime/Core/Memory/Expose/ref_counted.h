// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <atomic>
#include <cstdint>

namespace UVE::Memory {

/// `RefCountedUVE` is the intrusive reference-count base: the count lives in the object, so a
/// raw `T*` converts to an owning `IntrusivePtrUVE<T>` without a control block. New objects
/// start at zero references; the first `IntrusivePtrUVE` wrapping one brings it to one.
///
/// Lifetime rule: the object self-destroys (`delete this`) when the last reference releases.
/// That makes this the heap-ownership counterpart to `UniquePtrUVE` (which destroys through an
/// allocator): intrusive objects are always plain-`new` heap objects, never allocator-built.
/// Mixing the two — an allocator-built object wrapped intrusively — double-frees; the types do
/// not prevent it, the documented rule does.
///
/// Thread-safety: the count is atomic, so references may be copied and dropped from any thread.
/// The object's own thread-safety is still its own business.
class RefCountedUVE {
public:
    virtual ~RefCountedUVE() = default;

    RefCountedUVE(const RefCountedUVE&) = delete;
    RefCountedUVE& operator=(const RefCountedUVE&) = delete;
    RefCountedUVE(RefCountedUVE&&) = delete;
    RefCountedUVE& operator=(RefCountedUVE&&) = delete;

    /// Adds one reference. Called by `IntrusivePtrUVE`, not by hand.
    void AddRefUVE() noexcept { m_references.fetch_add(1U, std::memory_order_relaxed); }

    /// Drops one reference, destroying the object when the last one goes. Called by
    /// `IntrusivePtrUVE`, not by hand.
    void ReleaseUVE() noexcept {
        if (m_references.fetch_sub(1U, std::memory_order_acq_rel) == 1U) {
            delete this;
        }
    }

    /// Current reference count. Debugging/introspection only — never branch on it.
    [[nodiscard]] std::uint32_t RefCountUVE() const noexcept {
        return m_references.load(std::memory_order_relaxed);
    }

protected:
    RefCountedUVE() = default;

private:
    std::atomic<std::uint32_t> m_references{0U};
};

} // namespace UVE::Memory
