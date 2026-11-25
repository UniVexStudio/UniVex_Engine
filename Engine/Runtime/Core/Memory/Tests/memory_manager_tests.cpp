// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#include "memory_manager.h"

#include <algorithm>
#include <memory>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "log_sink.h"
#include "logger.h"

namespace UVE::Memory::Tests {
namespace {

TEST(MemoryManagerTest, RecordAllocationAndDeallocation_UpdateActiveStats) {
    MemoryManager manager;
    EXPECT_EQ(manager.GetActiveAllocationCount(), 0U);
    EXPECT_EQ(manager.GetActiveBytes(), 0U);

    int dummyA = 0;
    int dummyB = 0;
    manager.RecordAllocation(&dummyA, 100, 8, "Test", __FILE__, __LINE__);
    manager.RecordAllocation(&dummyB, 50, 8, "Test", __FILE__, __LINE__);
    EXPECT_EQ(manager.GetActiveAllocationCount(), 2U);
    EXPECT_EQ(manager.GetActiveBytes(), 150U);

    manager.RecordDeallocation(&dummyA);
    EXPECT_EQ(manager.GetActiveAllocationCount(), 1U);
    EXPECT_EQ(manager.GetActiveBytes(), 50U);

    manager.RecordDeallocation(&dummyB);
    EXPECT_EQ(manager.GetActiveAllocationCount(), 0U);
}

TEST(MemoryManagerTest, PeakBytes_TracksHighWaterMarkAndNeverDecreases) {
    MemoryManager manager;
    int dummyA = 0;
    int dummyB = 0;
    manager.RecordAllocation(&dummyA, 100, 8, "Test", __FILE__, __LINE__);
    manager.RecordAllocation(&dummyB, 100, 8, "Test", __FILE__, __LINE__);
    EXPECT_EQ(manager.GetPeakBytes(), 200U);

    manager.RecordDeallocation(&dummyA);
    EXPECT_EQ(manager.GetPeakBytes(), 200U);
    EXPECT_EQ(manager.GetActiveBytes(), 100U);

    manager.RecordDeallocation(&dummyB);
}

TEST(MemoryManagerTest, AllocationId_IsUniqueAndMonotonicallyIncreasing) {
    MemoryManager manager;
    int dummyA = 0;
    int dummyB = 0;
    manager.RecordAllocation(&dummyA, 8, 8, "Test", __FILE__, __LINE__);
    manager.RecordAllocation(&dummyB, 8, 8, "Test", __FILE__, __LINE__);

    const std::vector<AllocationRecord> leaks = manager.GetLeakedAllocations();
    ASSERT_EQ(leaks.size(), 2U);

    const AllocationRecord* recordA = nullptr;
    const AllocationRecord* recordB = nullptr;
    for (const AllocationRecord& record : leaks) {
        if (record.pointer == &dummyA) {
            recordA = &record;
        } else if (record.pointer == &dummyB) {
            recordB = &record;
        }
    }
    ASSERT_NE(recordA, nullptr);
    ASSERT_NE(recordB, nullptr);
    EXPECT_GT(recordA->allocationId, 0U);
    EXPECT_EQ(recordB->allocationId, recordA->allocationId + 1);

    manager.RecordDeallocation(&dummyA);
    manager.RecordDeallocation(&dummyB);
}

TEST(MemoryManagerTest, GetDefaultAllocator_ReturnsWorkingSelfTrackedAllocator) {
    MemoryManager manager;
    IAllocator& allocator = manager.GetDefaultAllocator();

    void* const pointer = allocator.Allocate(64, 8, __FILE__, __LINE__);
    ASSERT_NE(pointer, nullptr);
    EXPECT_EQ(manager.GetActiveAllocationCount(), 1U);
    EXPECT_EQ(manager.GetActiveBytes(), 64U);

    allocator.Deallocate(pointer);
    EXPECT_EQ(manager.GetActiveAllocationCount(), 0U);
}

TEST(MemoryManagerTest, HasLeaksAndGetLeakedAllocations_FlagIntentionallyUnfreedAllocation) {
    MemoryManager manager;
    EXPECT_FALSE(manager.HasLeaks());

    int leaked = 0;
    manager.RecordAllocation(&leaked, 42, 8, "LeakyAllocator", __FILE__, 12345);

    EXPECT_TRUE(manager.HasLeaks());
    const std::vector<AllocationRecord> leaks = manager.GetLeakedAllocations();
    ASSERT_EQ(leaks.size(), 1U);
    EXPECT_EQ(leaks[0].pointer, &leaked);
    EXPECT_EQ(leaks[0].sizeBytes, 42U);
    EXPECT_EQ(leaks[0].allocatorTag, "LeakyAllocator");
    EXPECT_EQ(leaks[0].sourceLine, 12345);
    EXPECT_GT(leaks[0].allocationId, 0U);

    manager.RecordDeallocation(&leaked); // clean up so the test doesn't itself "leak"
}

TEST(MemoryManagerTest, LogLeakReport_EmitsErrorPerLeak) {
    Debug::Logger logger;
    logger.Init(Debug::LogLevel::Trace);
    auto memorySink = std::make_unique<Debug::MemorySink>();
    Debug::MemorySink* const memorySinkPtr = memorySink.get();
    logger.AddSink(std::move(memorySink));

    MemoryManager manager;
    int leaked = 0;
    manager.RecordAllocation(&leaked, 16, 8, "LeakyAllocator", __FILE__, __LINE__);

    manager.LogLeakReport();

    const std::vector<Debug::LogMessage> messages = memorySinkPtr->GetMessages();
    const bool foundLeakError =
        std::any_of(messages.begin(), messages.end(), [](const Debug::LogMessage& message) {
            return message.level == Debug::LogLevel::Error &&
                   message.message.contains("Memory leak");
        });
    EXPECT_TRUE(foundLeakError);

    manager.RecordDeallocation(&leaked);
    logger.Shutdown();
}

} // namespace
} // namespace UVE::Memory::Tests
