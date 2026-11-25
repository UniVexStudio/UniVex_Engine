// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include <thread>
#include <unordered_map>

#include <gtest/gtest.h>

#include "thread_id.h"

namespace UVE::Threading::Tests {
namespace {

TEST(ThreadIdTest, Current_IsStableWithinAThreadAndDistinctAcrossThreads) {
    EXPECT_EQ(ThreadId::Current(), ThreadId::Current());
    EXPECT_NE(ThreadId::Current().Value(), 0U);
    ThreadId other{};
    std::thread worker([&other] { other = ThreadId::Current(); });
    worker.join();
    EXPECT_NE(other, ThreadId::Current());
}

TEST(ThreadIdTest, FromNative_RoundTripsThroughValue) {
    const ThreadId id = ThreadId::FromNative(std::this_thread::get_id());
    EXPECT_EQ(id, ThreadId::Current());
    const ThreadId rebuilt{id.Value()};
    EXPECT_EQ(rebuilt, id);
}

TEST(ThreadIdTest, Hash_SupportsUnorderedMaps) {
    std::unordered_map<ThreadId, int> perThread;
    perThread[ThreadId::Current()] = 11;
    EXPECT_EQ(perThread[ThreadId::Current()], 11);
}

} // namespace
} // namespace UVE::Threading::Tests
