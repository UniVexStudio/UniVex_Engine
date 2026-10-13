// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include <thread>
#include <unordered_map>

#include <gtest/gtest.h>

#include "uve/threading/thread_id_uve.h"

namespace UVE::Threading::Tests {
namespace {

TEST(ThreadIdUVETest, Current_IsStableWithinAThreadAndDistinctAcrossThreads) {
    EXPECT_EQ(ThreadIdUVE::CurrentUVE(), ThreadIdUVE::CurrentUVE());
    EXPECT_NE(ThreadIdUVE::CurrentUVE().ValueUVE(), 0U);
    ThreadIdUVE other{};
    std::thread worker([&other] { other = ThreadIdUVE::CurrentUVE(); });
    worker.join();
    EXPECT_NE(other, ThreadIdUVE::CurrentUVE());
}

TEST(ThreadIdUVETest, FromNative_RoundTripsThroughValue) {
    const ThreadIdUVE id = ThreadIdUVE::FromNativeUVE(std::this_thread::get_id());
    EXPECT_EQ(id, ThreadIdUVE::CurrentUVE());
    const ThreadIdUVE rebuilt{id.ValueUVE()};
    EXPECT_EQ(rebuilt, id);
}

TEST(ThreadIdUVETest, Hash_SupportsUnorderedMaps) {
    std::unordered_map<ThreadIdUVE, int> perThread;
    perThread[ThreadIdUVE::CurrentUVE()] = 11;
    EXPECT_EQ(perThread[ThreadIdUVE::CurrentUVE()], 11);
}

} // namespace
} // namespace UVE::Threading::Tests
