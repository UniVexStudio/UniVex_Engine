// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#include "heap_allocator.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include <gtest/gtest.h>

#include "platform.h"

namespace UVE::Memory::Tests {
namespace {

class FakeTracker final : public IMemoryTracker {
public:
    void RecordAllocation(void* pointer, std::size_t sizeBytes, std::size_t alignment,
                              std::string_view allocatorTag, const char* sourceFile,
                              int sourceLine) override {
        allocations.push_back(AllocationRecord{pointer, sizeBytes, alignment,
                                                    std::string(allocatorTag), sourceFile,
                                                    sourceLine, 0});
    }
    void RecordDeallocation(void* pointer) override { deallocatedPointers.push_back(pointer); }

    std::vector<AllocationRecord> allocations;
    std::vector<void*> deallocatedPointers;
};

TEST(HeapAllocatorTest, AllocateDeallocate_RoundTrip) {
    HeapAllocator allocator;
    void* const pointer = allocator.Allocate(64, 8, __FILE__, __LINE__);
    ASSERT_NE(pointer, nullptr);
    allocator.Deallocate(pointer);
}

TEST(HeapAllocatorTest, Allocate_ReturnsAlignedPointer) {
    HeapAllocator allocator;
    for (const std::size_t alignment : {std::size_t{8}, std::size_t{16}, std::size_t{32}, std::size_t{64}}) {
        void* const pointer = allocator.Allocate(16, alignment, __FILE__, __LINE__);
        EXPECT_EQ(reinterpret_cast<std::uintptr_t>(pointer) % alignment, 0U);
        allocator.Deallocate(pointer);
    }
}

TEST(HeapAllocatorTest, GetAllocatedBytes_TracksOutstandingBytes) {
    HeapAllocator allocator;
    EXPECT_EQ(allocator.GetAllocatedBytes(), 0U);

    void* const first = allocator.Allocate(100, 8, __FILE__, __LINE__);
    EXPECT_EQ(allocator.GetAllocatedBytes(), 100U);

    void* const second = allocator.Allocate(50, 8, __FILE__, __LINE__);
    EXPECT_EQ(allocator.GetAllocatedBytes(), 150U);

    allocator.Deallocate(first);
    EXPECT_EQ(allocator.GetAllocatedBytes(), 50U);

    allocator.Deallocate(second);
    EXPECT_EQ(allocator.GetAllocatedBytes(), 0U);
}

TEST(HeapAllocatorTest, Tracker_ReceivesMatchingAllocationAndDeallocationCalls) {
    FakeTracker tracker;
    HeapAllocator allocator(&tracker, "TestHeap");

    void* const pointer = allocator.Allocate(32, 16, __FILE__, __LINE__);
    ASSERT_EQ(tracker.allocations.size(), 1U);
    EXPECT_EQ(tracker.allocations[0].pointer, pointer);
    EXPECT_EQ(tracker.allocations[0].sizeBytes, 32U);
    EXPECT_EQ(tracker.allocations[0].alignment, 16U);
    EXPECT_EQ(tracker.allocations[0].allocatorTag, "TestHeap");

    allocator.Deallocate(pointer);
    ASSERT_EQ(tracker.deallocatedPointers.size(), 1U);
    EXPECT_EQ(tracker.deallocatedPointers[0], pointer);
}

#if UVE_DEBUG
TEST(HeapAllocatorDeathTest, DeallocateForeignOrDoubleFreedPointer_Asserts) {
    HeapAllocator allocator;
    int foreign = 0;
    EXPECT_DEATH({ allocator.Deallocate(&foreign); }, "");
}
#else
TEST(HeapAllocatorTest, DeallocateForeignOrDoubleFreedPointer_LogsAndNoOpsInsteadOfCorruptingBookkeeping) {
    HeapAllocator allocator;
    void* const pointer = allocator.Allocate(32, 8, __FILE__, __LINE__);
    allocator.Deallocate(pointer);
    const std::size_t bytesAfterFirstFree = allocator.GetAllocatedBytes();

    // A second free of the same (now-foreign) pointer must not touch bookkeeping again.
    EXPECT_NO_FATAL_FAILURE(allocator.Deallocate(pointer));
    EXPECT_EQ(allocator.GetAllocatedBytes(), bytesAfterFirstFree);

    int foreign = 0;
    EXPECT_NO_FATAL_FAILURE(allocator.Deallocate(&foreign));
}
#endif

} // namespace
} // namespace UVE::Memory::Tests
