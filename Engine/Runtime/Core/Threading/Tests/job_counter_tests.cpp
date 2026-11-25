// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#include "job_counter.h"

#include <atomic>
#include <chrono>
#include <thread>

#include <gtest/gtest.h>

#include "platform.h"

namespace UVE::Threading::Tests {
namespace {

TEST(JobCounterTest, FreshCounter_WaitReturnsImmediately) {
    JobCounter counter;
    counter.Wait(); // must not hang: pending count starts at zero
}

TEST(JobCounterTest, IncrementThenDecrementFromAnotherThread_UnblocksWait) {
    JobCounter counter;
    counter.Increment();

    std::thread worker([&counter] {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        counter.DecrementAndNotify();
    });

    counter.Wait(); // blocks until the worker thread decrements
    worker.join();
}

TEST(JobCounterTest, MultipleIncrementsRequireMatchingDecrements) {
    JobCounter counter;
    counter.Increment();
    counter.Increment();
    counter.Increment();

    std::atomic<int> decrementsDone{0};
    std::thread worker([&counter, &decrementsDone] {
        for (int i = 0; i < 3; ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
            counter.DecrementAndNotify();
            ++decrementsDone;
        }
    });

    counter.Wait();
    worker.join();
    EXPECT_EQ(decrementsDone.load(), 3);
}

#if UVE_DEBUG
TEST(JobCounterDeathTest, DecrementWithoutIncrement_Asserts) {
    JobCounter counter;
    EXPECT_DEATH({ counter.DecrementAndNotify(); }, "");
}
#else
TEST(JobCounterTest, DecrementWithoutIncrement_IgnoresExtraDecrementInsteadOfUnderflowing) {
    JobCounter counter;
    EXPECT_NO_FATAL_FAILURE(counter.DecrementAndNotify());
    counter.Wait(); // must not hang: the ignored extra decrement must not have gone negative

    // A legitimate subsequent increment/decrement pair still works correctly afterward.
    counter.Increment();
    counter.DecrementAndNotify();
    counter.Wait();
}
#endif

} // namespace
} // namespace UVE::Threading::Tests
