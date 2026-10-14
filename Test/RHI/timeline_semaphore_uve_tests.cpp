// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
//
// Timeline semaphore contract tests. The Null device's semaphore table is the one part of the
// model that is genuinely concurrent (a wait blocks, so the signal must come from another
// thread), which lets these cases exercise real blocking, timeouts and counter monotonicity
// without a GPU. Backend capability honesty is asserted here too: the default capability set
// never advertises semaphores, so a backend that does not implement them cannot silently claim
// support.

#include "uve/rhi_null/null_render_device_uve.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <thread>
#include <variant>
#include <vector>

#include <gtest/gtest.h>

namespace UVE::Render::Tests {
namespace {

using ResultUVE = RenderOperationResultUVE;

TEST(TimelineSemaphoreUVETest, UnsupportedByDefaultSoNoBackendClaimsItImplicitly) {
    EXPECT_EQ(RenderDeviceCapabilitiesUVE{}.timelineSemaphores, RenderFeatureSupportUVE::Unsupported);
    EXPECT_EQ(kInvalidSemaphoreHandleUVE.value, 0U);
    EXPECT_TRUE(IsSemaphoreSignalValueValidUVE(3U, 4U));
    EXPECT_FALSE(IsSemaphoreSignalValueValidUVE(3U, 3U));
    EXPECT_FALSE(IsSemaphoreSignalValueValidUVE(3U, 2U));
    EXPECT_TRUE(IsSemaphoreWaitValueValidUVE(0U));
}

TEST(TimelineSemaphoreUVETest, NullModelsCountersThatStartAtTheRequestedValue) {
    NullRenderDeviceUVE device;
    EXPECT_EQ(device.GetCapabilitiesUVE().timelineSemaphores, RenderFeatureSupportUVE::Model);
    SemaphoreHandleUVE semaphore;
    ASSERT_EQ(device.CreateSemaphoreUVE(SemaphoreDescUVE{5U}, semaphore), ResultUVE::Success);
    EXPECT_NE(semaphore, kInvalidSemaphoreHandleUVE);
    std::uint64_t counter = 0U;
    ASSERT_EQ(device.GetSemaphoreCounterUVE(semaphore, counter), ResultUVE::Success);
    EXPECT_EQ(counter, 5U);
    // A wait for a value the counter already reached resolves immediately, even with no timeout.
    EXPECT_EQ(device.WaitSemaphoreUVE(semaphore, 5U, 0U), ResultUVE::Success);
    EXPECT_EQ(device.WaitSemaphoreUVE(semaphore, 6U, 0U), ResultUVE::TimedOut);
    EXPECT_EQ(device.DestroySemaphoreUVE(semaphore), ResultUVE::Success);
}

TEST(TimelineSemaphoreUVETest, SignalsMustStrictlyIncreaseAndFailuresLeaveStateUntouched) {
    NullRenderDeviceUVE device;
    SemaphoreHandleUVE semaphore;
    ASSERT_EQ(device.CreateSemaphoreUVE(SemaphoreDescUVE{0U}, semaphore), ResultUVE::Success);
    EXPECT_EQ(device.SignalSemaphoreUVE(semaphore, 0U), ResultUVE::InvalidArgument);
    EXPECT_EQ(device.SignalSemaphoreUVE(semaphore, 4U), ResultUVE::Success);
    EXPECT_EQ(device.SignalSemaphoreUVE(semaphore, 4U), ResultUVE::InvalidArgument);
    EXPECT_EQ(device.SignalSemaphoreUVE(semaphore, 3U), ResultUVE::InvalidArgument);
    std::uint64_t counter = 0U;
    ASSERT_EQ(device.GetSemaphoreCounterUVE(semaphore, counter), ResultUVE::Success);
    EXPECT_EQ(counter, 4U) << "a refused signal must not advance the counter";

    // Unknown and destroyed handles are refused, and the output argument is left untouched.
    counter = 77U;
    EXPECT_EQ(device.GetSemaphoreCounterUVE(SemaphoreHandleUVE{999999U}, counter), ResultUVE::InvalidHandle);
    EXPECT_EQ(counter, 77U);
    EXPECT_EQ(device.SignalSemaphoreUVE(SemaphoreHandleUVE{999999U}, 9U), ResultUVE::InvalidHandle);
    EXPECT_EQ(device.WaitSemaphoreUVE(SemaphoreHandleUVE{999999U}, 0U, 0U), ResultUVE::InvalidHandle);
    EXPECT_EQ(device.DestroySemaphoreUVE(semaphore), ResultUVE::Success);
    EXPECT_EQ(device.DestroySemaphoreUVE(semaphore), ResultUVE::InvalidHandle);
    EXPECT_EQ(device.GetSemaphoreCounterUVE(semaphore, counter), ResultUVE::InvalidHandle);
    EXPECT_EQ(counter, 77U);
}

TEST(TimelineSemaphoreUVETest, WaitsBlockUntilAnotherThreadSignalsAndHonorTheirTimeout) {
    NullRenderDeviceUVE device;
    SemaphoreHandleUVE semaphore;
    ASSERT_EQ(device.CreateSemaphoreUVE(SemaphoreDescUVE{0U}, semaphore), ResultUVE::Success);

    // An unreached value with a real but short timeout must come back TimedOut, not hang.
    const auto start = std::chrono::steady_clock::now();
    EXPECT_EQ(device.WaitSemaphoreUVE(semaphore, 1U, 2'000'000ULL), ResultUVE::TimedOut);
    const auto elapsed = std::chrono::steady_clock::now() - start;
    EXPECT_GE(std::chrono::duration_cast<std::chrono::microseconds>(elapsed).count(), 1'000);

    std::atomic<bool> waiterReturned{false};
    ResultUVE waitResult = ResultUVE::DeviceUnavailable;
    std::thread waiter([&] {
        waitResult = device.WaitSemaphoreUVE(semaphore, 3U, 2'000'000'000ULL);
        waiterReturned.store(true);
    });
    // Give the waiter a moment to actually block before signalling: the timeout is generous
    // enough that this cannot turn the test into a race on the timeout itself.
    while (!waiterReturned.load() && std::chrono::steady_clock::now() - start < std::chrono::seconds(1)) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    std::uint64_t counter = 0U;
    ASSERT_EQ(device.GetSemaphoreCounterUVE(semaphore, counter), ResultUVE::Success);
    EXPECT_EQ(counter, 0U) << "the waiter must not have advanced the counter by itself";
    EXPECT_EQ(device.SignalSemaphoreUVE(semaphore, 3U), ResultUVE::Success);
    waiter.join();
    EXPECT_TRUE(waiterReturned.load());
    EXPECT_EQ(waitResult, ResultUVE::Success);

    // A waiter can also be released by destroying the semaphore; that reports InvalidHandle
    // rather than pretending the value was reached.
    ResultUVE destroyedWait = ResultUVE::DeviceUnavailable;
    std::thread destroyWaiter([&] { destroyedWait = device.WaitSemaphoreUVE(semaphore, 9U, 2'000'000'000ULL); });
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
    EXPECT_EQ(device.DestroySemaphoreUVE(semaphore), ResultUVE::Success);
    destroyWaiter.join();
    EXPECT_EQ(destroyedWait, ResultUVE::InvalidHandle);
}

TEST(TimelineSemaphoreUVETest, DeviceOperationHistoryKeepsTheExactSignalAndWaitArguments) {
    NullRenderDeviceUVE device;
    SemaphoreHandleUVE semaphore;
    ASSERT_EQ(device.CreateSemaphoreUVE(SemaphoreDescUVE{2U}, semaphore), ResultUVE::Success);
    ASSERT_EQ(device.SignalSemaphoreUVE(semaphore, 8U), ResultUVE::Success);
    ASSERT_EQ(device.WaitSemaphoreUVE(semaphore, 9U, 0U), ResultUVE::TimedOut);
    ASSERT_EQ(device.WaitSemaphoreUVE(semaphore, 8U, 0U), ResultUVE::Success);

    const auto &operations = device.GetRecordedDeviceOperationsUVE();
    ASSERT_GE(operations.size(), 3U);
    const auto &signal = std::get<SignalSemaphoreOperationUVE>(operations[operations.size() - 3U]);
    EXPECT_EQ(signal.semaphore, semaphore);
    EXPECT_EQ(signal.value, 8U);
    const auto &timedOut = std::get<WaitSemaphoreOperationUVE>(operations[operations.size() - 2U]);
    EXPECT_EQ(timedOut.value, 9U);
    EXPECT_FALSE(timedOut.satisfied) << "a timed-out wait must not be recorded as satisfied";
    const auto &satisfied = std::get<WaitSemaphoreOperationUVE>(operations.back());
    EXPECT_EQ(satisfied.value, 8U);
    EXPECT_TRUE(satisfied.satisfied);
}

TEST(TimelineSemaphoreUVETest, IndependentSemaphoresKeepIndependentCounters) {
    NullRenderDeviceUVE device;
    SemaphoreHandleUVE first;
    SemaphoreHandleUVE second;
    ASSERT_EQ(device.CreateSemaphoreUVE(SemaphoreDescUVE{1U}, first), ResultUVE::Success);
    ASSERT_EQ(device.CreateSemaphoreUVE(SemaphoreDescUVE{1U}, second), ResultUVE::Success);
    EXPECT_NE(first, second);
    ASSERT_EQ(device.SignalSemaphoreUVE(first, 10U), ResultUVE::Success);
    std::uint64_t counter = 0U;
    ASSERT_EQ(device.GetSemaphoreCounterUVE(second, counter), ResultUVE::Success);
    EXPECT_EQ(counter, 1U);
    EXPECT_EQ(device.WaitSemaphoreUVE(second, 10U, 0U), ResultUVE::TimedOut);
    EXPECT_EQ(device.WaitSemaphoreUVE(first, 10U, 0U), ResultUVE::Success);
    EXPECT_EQ(device.DestroySemaphoreUVE(first), ResultUVE::Success);
    EXPECT_EQ(device.DestroySemaphoreUVE(second), ResultUVE::Success);
}

} // namespace
} // namespace UVE::Render::Tests
