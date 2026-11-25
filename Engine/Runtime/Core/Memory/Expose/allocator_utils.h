// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#pragma once

#include <new>
#include <utility>

#include "i_allocator.h"

namespace UVE::Memory {

/// Allocates space for a T (via `allocator.Allocate(sizeof(T), alignof(T), sourceFile,
/// sourceLine)`) and placement-constructs it with `args`, forwarded to T's constructor. This is
/// the sanctioned way engine code builds an object on top of a custom UVE allocator without
/// writing raw placement-new at the call site — pair with Destroy<T>() to tear the object
/// back down. Prefer the UVE_CONSTRUCT() macro below over calling this directly, so
/// `sourceFile`/`sourceLine` are captured automatically.
template <typename T, typename... TArgs>
[[nodiscard]] T* Construct(IAllocator& allocator, const char* sourceFile, int sourceLine,
                               TArgs&&... args) {
    void* const memory = allocator.Allocate(sizeof(T), alignof(T), sourceFile, sourceLine);
    return ::new (memory) T(std::forward<TArgs>(args)...);
}

/// Explicitly destroys `*object` (calling its destructor) and then deallocates it via
/// `allocator`. `object` must have been returned by a prior Construct<T>() call against the
/// same allocator instance. Safe to call with `object == nullptr` (a no-op, matching
/// std::default_delete's null-safety convention).
template <typename T>
void Destroy(IAllocator& allocator, T* object) {
    if (object == nullptr) {
        return;
    }
    object->~T();
    allocator.Deallocate(object);
}

} // namespace UVE::Memory

/// Constructs a `Type` on `allocator` (an IAllocator&), forwarding any additional arguments
/// to its constructor, with `sourceFile`/`sourceLine` captured automatically from the call
/// site — the same ergonomics UVE_LOG provides for logging. Prefer this over calling
/// UVE::Memory::Construct<T>() directly.
#define UVE_CONSTRUCT(allocator, Type, ...) \
    ::UVE::Memory::Construct<Type>((allocator), __FILE__, __LINE__ __VA_OPT__(, ) __VA_ARGS__)
