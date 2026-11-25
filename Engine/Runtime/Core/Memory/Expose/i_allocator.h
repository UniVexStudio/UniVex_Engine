// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#pragma once

#include <cstddef>

namespace UVE::Memory {

/// IAllocator is the interface every UVE allocator implements (HeapAllocator,
/// PoolAllocator, StackAllocator), so engine code can be written against "an allocator"
/// without caring which concrete strategy backs it.
/// There are deliberately no default arguments on Allocate() — default arguments on virtual
/// functions resolve against the static type, not the dynamic one, a well-known footgun when
/// calling through a base-class pointer/reference. `sourceFile`/`sourceLine` are required so a
/// tracked allocator can report a meaningful call site in leak reports; pass `__FILE__`/
/// `__LINE__` directly, or prefer the `UVE_CONSTRUCT(allocator, Type, ...)` macro
/// (`allocator_utils_uve.h`), which captures them automatically the same way `UVE_LOG` does.
/// Thread-safety: implementations document their own contract individually; none of the
/// concrete UVE allocators are thread-safe by themselves (see each class's doc comment) — wrap
/// external synchronization around a shared instance if multiple threads must share one.
class IAllocator {
public:
    virtual ~IAllocator() = default;

    /// Allocates `sizeBytes` with the given `alignment` (must satisfy IsValidAlignment()).
    /// `sourceFile`/`sourceLine` identify the call site for tracking/leak-report purposes; pass
    /// `nullptr`/`0` if unavailable. Never returns nullptr — implementations UVE_ASSERT on
    /// failure/exhaustion rather than returning a null pointer for the caller to (potentially
    /// forget to) check.
    [[nodiscard]] virtual void* Allocate(std::size_t sizeBytes, std::size_t alignment,
                                             const char* sourceFile, int sourceLine) = 0;

    /// Frees a pointer previously returned by Allocate() on this same instance.
    virtual void Deallocate(void* pointer) = 0;

    /// Returns the number of bytes this allocator currently has outstanding (allocated but not
    /// yet deallocated).
    [[nodiscard]] virtual std::size_t GetAllocatedBytes() const = 0;
};

} // namespace UVE::Memory
