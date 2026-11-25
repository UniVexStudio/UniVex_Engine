// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#include "stack_allocator.h"

#include <cstddef>
#include <new>
#include <string_view>
#include <vector>

#include <gtest/gtest.h>

#include "platform.h"

namespace UVE::Memory::Tests {
namespace {

class FakeTracker final : public IMemoryTracker {
public:
    void RecordAllocation(void*, std::size_t, std::size_t, std::string_view, const char*,
                              int) override {
        ++allocationCount;
    }
    void RecordDeallocation(void* pointer) override { deallocatedPointers.push_back(pointer); }

    int allocationCount = 0;
    std::vector<void*> deallocatedPointers;
};

TEST(StackAllocatorTest, SequentialAllocations_BumpOffsetForward) {
    StackAllocator stack(1024);
    void* const first = stack.Allocate(16, 8, __FILE__, __LINE__);
    void* const second = stack.Allocate(16, 8, __FILE__, __LINE__);
    EXPECT_NE(first, second);
    EXPECT_GT(stack.GetAllocatedBytes(), 0U);
}

#if UVE_DEBUG
TEST(StackAllocatorDeathTest, ExhaustingCapacity_Asserts) {
    StackAllocator stack(16);
    EXPECT_DEATH({ (void)stack.Allocate(1024, 8, __FILE__, __LINE__); }, "");
}

TEST(StackAllocatorDeathTest, OutOfOrderDeallocate_Asserts) {
    StackAllocator stack(256);
    void* const first = stack.Allocate(16, 8, __FILE__, __LINE__);
    (void)stack.Allocate(16, 8, __FILE__, __LINE__); // second allocation, now on top
    EXPECT_DEATH({ stack.Deallocate(first); }, "");
}

TEST(StackAllocatorDeathTest, MarkerFromDifferentAllocator_Asserts) {
    StackAllocator stackA(256);
    StackAllocator stackB(256);
    const StackMarker markerFromB = stackB.GetMarker();
    EXPECT_DEATH({ stackA.RewindToMarker(markerFromB); }, "");
}

TEST(StackAllocatorDeathTest, StaleMarkerBeyondCurrentOffset_Asserts) {
    StackAllocator stack(256);
    const StackMarker earlyMarker = stack.GetMarker();
    (void)stack.Allocate(16, 8, __FILE__, __LINE__);
    const StackMarker lateMarker = stack.GetMarker();

    stack.RewindToMarker(earlyMarker); // current offset now back before lateMarker's position
    EXPECT_DEATH({ stack.RewindToMarker(lateMarker); }, "");
}
#else
TEST(StackAllocatorTest, ExhaustingCapacity_ThrowsBadAllocInsteadOfOverrunningBuffer) {
    StackAllocator stack(16);
    EXPECT_THROW({ (void)stack.Allocate(1024, 8, __FILE__, __LINE__); }, std::bad_alloc);
    // The failed allocation must not have moved the offset forward.
    EXPECT_EQ(stack.GetAllocatedBytes(), 0U);
}
#endif

TEST(StackAllocatorTest, MarkerRewind_FreesAndSpaceIsReusable) {
    StackAllocator stack(1024);
    const StackMarker marker = stack.GetMarker();
    (void)stack.Allocate(64, 8, __FILE__, __LINE__);
    (void)stack.Allocate(64, 8, __FILE__, __LINE__);
    EXPECT_GT(stack.GetAllocatedBytes(), 0U);

    stack.RewindToMarker(marker);
    EXPECT_EQ(stack.GetAllocatedBytes(), 0U);

    void* const reused = stack.Allocate(64, 8, __FILE__, __LINE__);
    ASSERT_NE(reused, nullptr);
}

TEST(StackAllocatorTest, Deallocate_StrictLifo_Succeeds) {
    StackAllocator stack(256);
    void* const first = stack.Allocate(16, 8, __FILE__, __LINE__);
    void* const second = stack.Allocate(16, 8, __FILE__, __LINE__);

    stack.Deallocate(second);
    stack.Deallocate(first);
    EXPECT_EQ(stack.GetAllocatedBytes(), 0U);
}

TEST(StackAllocatorTest, RewindToMarker_ReportsOneDeallocationPerFreedAllocation) {
    FakeTracker tracker;
    StackAllocator stack(1024, &tracker);
    const StackMarker marker = stack.GetMarker();

    (void)stack.Allocate(16, 8, __FILE__, __LINE__);
    (void)stack.Allocate(16, 8, __FILE__, __LINE__);
    (void)stack.Allocate(16, 8, __FILE__, __LINE__);
    EXPECT_EQ(tracker.allocationCount, 3);

    stack.RewindToMarker(marker);
    EXPECT_EQ(tracker.deallocatedPointers.size(), 3U);
}

TEST(StackAllocatorTest, Reset_FreesEverything) {
    StackAllocator stack(256);
    (void)stack.Allocate(16, 8, __FILE__, __LINE__);
    (void)stack.Allocate(16, 8, __FILE__, __LINE__);
    stack.Reset();
    EXPECT_EQ(stack.GetAllocatedBytes(), 0U);
}

} // namespace
} // namespace UVE::Memory::Tests
