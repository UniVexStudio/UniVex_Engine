// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include <string>
#include <thread>

#include <gtest/gtest.h>

#include "uve/threading/thread_naming_uve.h"

namespace UVE::Threading::Tests {
namespace {

TEST(ThreadNamingUVETest, SetAndGet_RoundTripOnANamedThread) {
    // Names a worker (not the test runner) and reads the name back from inside it, so the
    // runner's own thread name is never disturbed.
    std::string observed;
    bool set = false;
    bool fetched = false;
    std::thread worker([&observed, &set, &fetched] {
        set = SetCurrentThreadNameUVE("uve-test");
        fetched = TryGetCurrentThreadNameUVE(observed);
    });
    worker.join();
    if (!set || !fetched) {
        GTEST_SKIP() << "Thread naming is not supported on this platform.";
    }
    EXPECT_EQ(observed, "uve-test");
}

#if defined(__linux__)
TEST(ThreadNamingUVETest, LongNames_TruncateToFifteenBytesOnLinux) {
    std::string observed;
    bool fetched = false;
    std::thread worker([&observed, &fetched] {
        static_cast<void>(SetCurrentThreadNameUVE("uve-very-long-thread-name-past-the-limit"));
        fetched = TryGetCurrentThreadNameUVE(observed);
    });
    worker.join();
    ASSERT_TRUE(fetched);
    EXPECT_EQ(observed, "uve-very-long-t");
    EXPECT_EQ(observed.size(), 15U);
}
#endif

} // namespace
} // namespace UVE::Threading::Tests
