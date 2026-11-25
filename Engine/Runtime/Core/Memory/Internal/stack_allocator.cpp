// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#include "stack_allocator.h"

#include <cstdint>
#include <cstring>
#include <new>

#include "engine_assert.h"
#include "alignment_utils.h"

namespace UVE::Memory {

namespace {

// Trailing per-allocation header: written immediately after each block's payload, storing
// enough information to reconstruct the block's user pointer and rewind past it in one step.
// Read/written via memcpy (not a reinterpret_cast'd pointer), since its address (the end of a
// variably-aligned payload) has no guaranteed alignment of its own — same rationale as
// PoolAllocator's intrusive free-list pointer.
struct StackBlockHeader {
    std::size_t blockStartOffset; // offset (from the buffer start) this block's padding began at
    std::size_t sizeBytes;        // the payload's requested size
};

void WriteHeader(void* address, const StackBlockHeader& header) noexcept {
    std::memcpy(address, &header, sizeof(StackBlockHeader));
}

StackBlockHeader ReadHeader(void* address) noexcept {
    StackBlockHeader header{};
    std::memcpy(&header, address, sizeof(StackBlockHeader));
    return header;
}

} // namespace

StackAllocator::StackAllocator(std::size_t capacityBytes, IMemoryTracker* tracker,
                                      std::string_view allocatorTag)
    : m_capacityBytes(capacityBytes), m_buffer(nullptr), m_tracker(tracker), m_allocatorTag(allocatorTag) {
    UVE_ASSERT(capacityBytes > 0);
    m_buffer = ::operator new(capacityBytes, std::align_val_t{alignof(std::max_align_t)});
}

StackAllocator::~StackAllocator() {
    ::operator delete(m_buffer, std::align_val_t{alignof(std::max_align_t)});
}

void* StackAllocator::Allocate(std::size_t sizeBytes, std::size_t alignment,
                                      const char* sourceFile, int sourceLine) {
    UVE_ASSERT(IsValidAlignment(alignment));

    const std::size_t blockStartOffset = m_currentOffset;
    const std::uintptr_t rawAddress =
        reinterpret_cast<std::uintptr_t>(BufferBytes() + blockStartOffset);
    const std::uintptr_t alignedAddress = AlignUp(rawAddress, alignment);
    const std::size_t userPointerOffset =
        static_cast<std::size_t>(alignedAddress - reinterpret_cast<std::uintptr_t>(m_buffer));

    const std::size_t headerOffset = userPointerOffset + sizeBytes;
    const std::size_t newOffset = headerOffset + sizeof(StackBlockHeader);
    UVE_ASSERT(newOffset <= m_capacityBytes);
    if (newOffset > m_capacityBytes) {
        UVE_ERROR("StackAllocator: allocation of {} bytes exceeds remaining capacity ({} of {} bytes used)",
                   sizeBytes, m_currentOffset, m_capacityBytes);
        throw std::bad_alloc{};
    }

    void* const userPointer = BufferBytes() + userPointerOffset;
    WriteHeader(BufferBytes() + headerOffset, StackBlockHeader{blockStartOffset, sizeBytes});
    m_currentOffset = newOffset;

    if (m_tracker != nullptr) {
        m_tracker->RecordAllocation(userPointer, sizeBytes, alignment, m_allocatorTag, sourceFile,
                                        sourceLine);
    }
    return userPointer;
}

void StackAllocator::Deallocate(void* pointer) {
    if (pointer == nullptr) {
        return;
    }
    UVE_ASSERT(m_currentOffset >= sizeof(StackBlockHeader));

    const std::size_t headerOffset = m_currentOffset - sizeof(StackBlockHeader);
    const StackBlockHeader header = ReadHeader(BufferBytes() + headerOffset);
    const std::size_t userPointerOffset = headerOffset - header.sizeBytes;
    void* const topUserPointer = BufferBytes() + userPointerOffset;

    UVE_ASSERT(pointer == topUserPointer);

    m_currentOffset = header.blockStartOffset;

    if (m_tracker != nullptr) {
        m_tracker->RecordDeallocation(pointer);
    }
}

std::size_t StackAllocator::GetAllocatedBytes() const {
    return m_currentOffset;
}

StackMarker StackAllocator::GetMarker() const noexcept {
    return StackMarker{m_currentOffset, this};
}

void StackAllocator::RewindToMarker(const StackMarker& marker) {
    UVE_ASSERT(marker.owner == this);
    UVE_ASSERT(marker.offset <= m_currentOffset);
    RewindToOffset(marker.offset);
}

void StackAllocator::Reset() {
    RewindToOffset(0);
}

void StackAllocator::RewindToOffset(std::size_t targetOffset) {
    while (m_currentOffset > targetOffset) {
        UVE_ASSERT(m_currentOffset >= sizeof(StackBlockHeader));
        const std::size_t headerOffset = m_currentOffset - sizeof(StackBlockHeader);
        const StackBlockHeader header = ReadHeader(BufferBytes() + headerOffset);
        const std::size_t userPointerOffset = headerOffset - header.sizeBytes;
        void* const userPointer = BufferBytes() + userPointerOffset;

        m_currentOffset = header.blockStartOffset;

        if (m_tracker != nullptr) {
            m_tracker->RecordDeallocation(userPointer);
        }
    }
}

std::byte* StackAllocator::BufferBytes() const noexcept {
    return static_cast<std::byte*>(m_buffer);
}

} // namespace UVE::Memory
