// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
//
// Timeline semaphore contract tests. The Null device's semaphore table is the one part of the
// model that is genuinely concurrent (a wait blocks, so the signal must come from another
// thread), which lets these cases exercise real blocking, timeouts and counter monotonicity
// without a GPU. Backend capability honesty is asserted here too: the default capability set
// never advertises semaphores, so a backend that does not implement them cannot silently claim
// support.

#include "uve/rhi_null/null_render_device_uve.h"

#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <span>
#include <thread>
#include <utility>
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


// ---- Foundation WP4: submission-level queue sync -----------------------------------------
TEST(TimelineSemaphoreUVETest, NullSubmitDescRecordsWaitsAndSignalsVerbatim) {
    NullRenderDeviceUVE device;
    SemaphoreHandleUVE gate, done;
    ASSERT_EQ(device.CreateSemaphoreUVE(SemaphoreDescUVE{2U}, gate), ResultUVE::Success);
    ASSERT_EQ(device.CreateSemaphoreUVE(SemaphoreDescUVE{0U}, done), ResultUVE::Success);
    const std::array<SemaphoreWaitUVE, 1U> waits{SemaphoreWaitUVE{gate, 7U}};
    const std::array<SemaphoreSignalUVE, 1U> signals{SemaphoreSignalUVE{done, 1U}};
    std::unique_ptr<ICommandBufferUVE> commands = device.CreateCommandBufferUVE();
    ASSERT_NE(commands, nullptr);
    commands->BeginRenderPassUVE(RenderPassDescUVE{});
    commands->DrawUVE(3U, 1U);
    commands->EndRenderPassUVE();
    ASSERT_EQ(device.SubmitWithSyncUVE(std::move(commands), SubmitDescUVE{waits, signals}), ResultUVE::Recorded);
    EXPECT_EQ(device.GetLastSubmittedCommandsUVE().size(), 3U) << "the work itself is submitted exactly like SubmitUVE";
    const std::vector<RecordedDeviceOperationUVE>& operations = device.GetRecordedDeviceOperationsUVE();
    ASSERT_FALSE(operations.empty());
    const auto* const record = std::get_if<SubmitOperationUVE>(&operations.back());
    ASSERT_NE(record, nullptr);
    ASSERT_EQ(record->waits.size(), 1U);
    EXPECT_EQ(record->waits[0].semaphore, gate);
    EXPECT_EQ(record->waits[0].value, 7U);
    ASSERT_EQ(record->signals.size(), 1U);
    EXPECT_EQ(record->signals[0].semaphore, done);
    EXPECT_EQ(record->signals[0].value, 1U);
    std::uint64_t counter = 99U;
    ASSERT_EQ(device.GetSemaphoreCounterUVE(done, counter), ResultUVE::Success);
    EXPECT_EQ(counter, 0U) << "a recorded signal is a request, never invented GPU progress";
    EXPECT_EQ(device.WaitSemaphoreUVE(done, 1U, 0U), ResultUVE::TimedOut);
    // An empty desc is exactly SubmitUVE and records nothing extra.
    const std::size_t before = device.GetRecordedDeviceOperationsUVE().size();
    std::unique_ptr<ICommandBufferUVE> plain = device.CreateCommandBufferUVE();
    ASSERT_NE(plain, nullptr);
    EXPECT_EQ(device.SubmitWithSyncUVE(std::move(plain), SubmitDescUVE{}), ResultUVE::Recorded);
    EXPECT_EQ(device.GetRecordedDeviceOperationsUVE().size(), before);
    EXPECT_EQ(device.DestroySemaphoreUVE(gate), ResultUVE::Success);
    EXPECT_EQ(device.DestroySemaphoreUVE(done), ResultUVE::Success);
}

TEST(TimelineSemaphoreUVETest, NullSubmitDescRejectsBadSemaphores) {
    NullRenderDeviceUVE device;
    SemaphoreHandleUVE live, destroyed;
    ASSERT_EQ(device.CreateSemaphoreUVE(SemaphoreDescUVE{4U}, live), ResultUVE::Success);
    ASSERT_EQ(device.CreateSemaphoreUVE(SemaphoreDescUVE{0U}, destroyed), ResultUVE::Success);
    ASSERT_EQ(device.DestroySemaphoreUVE(destroyed), ResultUVE::Success);
    const auto submit = [&device](const std::span<const SemaphoreWaitUVE> waits,
                                  const std::span<const SemaphoreSignalUVE> signals) {
        std::unique_ptr<ICommandBufferUVE> commands = device.CreateCommandBufferUVE();
        return device.SubmitWithSyncUVE(std::move(commands), SubmitDescUVE{waits, signals});
    };
    const std::array<SemaphoreWaitUVE, 1U> waitDestroyed{SemaphoreWaitUVE{destroyed, 1U}};
    const std::array<SemaphoreSignalUVE, 1U> signalDestroyed{SemaphoreSignalUVE{destroyed, 1U}};
    const std::array<SemaphoreWaitUVE, 1U> waitLive{SemaphoreWaitUVE{live, 9U}};
    const std::array<SemaphoreSignalUVE, 1U> signalStale{SemaphoreSignalUVE{live, 4U}};
    const std::array<SemaphoreSignalUVE, 1U> signalLive{SemaphoreSignalUVE{live, 5U}};
    const std::array<SemaphoreSignalUVE, 2U> signalTwice{SemaphoreSignalUVE{live, 5U}, SemaphoreSignalUVE{live, 6U}};
    EXPECT_EQ(submit(waitDestroyed, {}), ResultUVE::InvalidHandle);
    EXPECT_EQ(submit({}, signalDestroyed), ResultUVE::InvalidHandle);
    EXPECT_EQ(submit({}, signalStale), ResultUVE::InvalidArgument) << "a signal must exceed the current counter";
    EXPECT_EQ(submit(waitLive, signalLive), ResultUVE::InvalidArgument) << "one semaphore may not be in both lists";
    EXPECT_EQ(submit({}, signalTwice), ResultUVE::InvalidArgument) << "one semaphore may not be signalled twice";
    EXPECT_EQ(submit(waitLive, {}), ResultUVE::Recorded) << "a wait on a future value is a valid request";
    EXPECT_TRUE(device.GetRecordedDeviceOperationsUVE().size() >= 1U);
    std::size_t submitRecords = 0U;
    for (const RecordedDeviceOperationUVE& operation : device.GetRecordedDeviceOperationsUVE()) {
        if (std::holds_alternative<SubmitOperationUVE>(operation)) {
            ++submitRecords;
        }
    }
    EXPECT_EQ(submitRecords, 1U) << "refused submits record nothing";
    EXPECT_EQ(device.DestroySemaphoreUVE(live), ResultUVE::Success);
}
} // namespace
} // namespace UVE::Render::Tests
