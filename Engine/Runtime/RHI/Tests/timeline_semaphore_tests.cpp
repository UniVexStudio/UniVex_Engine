// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
//
// Timeline semaphore contract tests. The Null device's semaphore table is the one part of the
// model that is genuinely concurrent (a wait blocks, so the signal must come from another
// thread), which lets these cases exercise real blocking, timeouts and counter monotonicity
// without a GPU. Backend capability honesty is asserted here too: the default capability set
// never advertises semaphores, so a backend that does not implement them cannot silently claim
// support.

#include "null_render_device.h"

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

using Result = RenderOperationResult;

TEST(TimelineSemaphoreTest, UnsupportedByDefaultSoNoBackendClaimsItImplicitly) {
    EXPECT_EQ(RenderDeviceCapabilities{}.timelineSemaphores, RenderFeatureSupport::Unsupported);
    EXPECT_EQ(kInvalidSemaphoreHandle.value, 0U);
    EXPECT_TRUE(IsSemaphoreSignalValueValid(3U, 4U));
    EXPECT_FALSE(IsSemaphoreSignalValueValid(3U, 3U));
    EXPECT_FALSE(IsSemaphoreSignalValueValid(3U, 2U));
    EXPECT_TRUE(IsSemaphoreWaitValueValid(0U));
}

TEST(TimelineSemaphoreTest, NullModelsCountersThatStartAtTheRequestedValue) {
    NullRenderDevice device;
    EXPECT_EQ(device.GetCapabilities().timelineSemaphores, RenderFeatureSupport::Model);
    SemaphoreHandle semaphore;
    ASSERT_EQ(device.CreateSemaphore(SemaphoreDesc{5U}, semaphore), Result::Success);
    EXPECT_NE(semaphore, kInvalidSemaphoreHandle);
    std::uint64_t counter = 0U;
    ASSERT_EQ(device.GetSemaphoreCounter(semaphore, counter), Result::Success);
    EXPECT_EQ(counter, 5U);
    // A wait for a value the counter already reached resolves immediately, even with no timeout.
    EXPECT_EQ(device.WaitSemaphore(semaphore, 5U, 0U), Result::Success);
    EXPECT_EQ(device.WaitSemaphore(semaphore, 6U, 0U), Result::TimedOut);
    EXPECT_EQ(device.DestroySemaphore(semaphore), Result::Success);
}

TEST(TimelineSemaphoreTest, SignalsMustStrictlyIncreaseAndFailuresLeaveStateUntouched) {
    NullRenderDevice device;
    SemaphoreHandle semaphore;
    ASSERT_EQ(device.CreateSemaphore(SemaphoreDesc{0U}, semaphore), Result::Success);
    EXPECT_EQ(device.SignalSemaphore(semaphore, 0U), Result::InvalidArgument);
    EXPECT_EQ(device.SignalSemaphore(semaphore, 4U), Result::Success);
    EXPECT_EQ(device.SignalSemaphore(semaphore, 4U), Result::InvalidArgument);
    EXPECT_EQ(device.SignalSemaphore(semaphore, 3U), Result::InvalidArgument);
    std::uint64_t counter = 0U;
    ASSERT_EQ(device.GetSemaphoreCounter(semaphore, counter), Result::Success);
    EXPECT_EQ(counter, 4U) << "a refused signal must not advance the counter";

    // Unknown and destroyed handles are refused, and the output argument is left untouched.
    counter = 77U;
    EXPECT_EQ(device.GetSemaphoreCounter(SemaphoreHandle{999999U}, counter), Result::InvalidHandle);
    EXPECT_EQ(counter, 77U);
    EXPECT_EQ(device.SignalSemaphore(SemaphoreHandle{999999U}, 9U), Result::InvalidHandle);
    EXPECT_EQ(device.WaitSemaphore(SemaphoreHandle{999999U}, 0U, 0U), Result::InvalidHandle);
    EXPECT_EQ(device.DestroySemaphore(semaphore), Result::Success);
    EXPECT_EQ(device.DestroySemaphore(semaphore), Result::InvalidHandle);
    EXPECT_EQ(device.GetSemaphoreCounter(semaphore, counter), Result::InvalidHandle);
    EXPECT_EQ(counter, 77U);
}

TEST(TimelineSemaphoreTest, WaitsBlockUntilAnotherThreadSignalsAndHonorTheirTimeout) {
    NullRenderDevice device;
    SemaphoreHandle semaphore;
    ASSERT_EQ(device.CreateSemaphore(SemaphoreDesc{0U}, semaphore), Result::Success);

    // An unreached value with a real but short timeout must come back TimedOut, not hang.
    const auto start = std::chrono::steady_clock::now();
    EXPECT_EQ(device.WaitSemaphore(semaphore, 1U, 2'000'000ULL), Result::TimedOut);
    const auto elapsed = std::chrono::steady_clock::now() - start;
    EXPECT_GE(std::chrono::duration_cast<std::chrono::microseconds>(elapsed).count(), 1'000);

    std::atomic<bool> waiterReturned{false};
    Result waitResult = Result::DeviceUnavailable;
    std::thread waiter([&] {
        waitResult = device.WaitSemaphore(semaphore, 3U, 2'000'000'000ULL);
        waiterReturned.store(true);
    });
    // Give the waiter a moment to actually block before signalling: the timeout is generous
    // enough that this cannot turn the test into a race on the timeout itself.
    while (!waiterReturned.load() && std::chrono::steady_clock::now() - start < std::chrono::seconds(1)) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    std::uint64_t counter = 0U;
    ASSERT_EQ(device.GetSemaphoreCounter(semaphore, counter), Result::Success);
    EXPECT_EQ(counter, 0U) << "the waiter must not have advanced the counter by itself";
    EXPECT_EQ(device.SignalSemaphore(semaphore, 3U), Result::Success);
    waiter.join();
    EXPECT_TRUE(waiterReturned.load());
    EXPECT_EQ(waitResult, Result::Success);

    // A waiter can also be released by destroying the semaphore; that reports InvalidHandle
    // rather than pretending the value was reached.
    Result destroyedWait = Result::DeviceUnavailable;
    std::thread destroyWaiter([&] { destroyedWait = device.WaitSemaphore(semaphore, 9U, 2'000'000'000ULL); });
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
    EXPECT_EQ(device.DestroySemaphore(semaphore), Result::Success);
    destroyWaiter.join();
    EXPECT_EQ(destroyedWait, Result::InvalidHandle);
}

TEST(TimelineSemaphoreTest, DeviceOperationHistoryKeepsTheExactSignalAndWaitArguments) {
    NullRenderDevice device;
    SemaphoreHandle semaphore;
    ASSERT_EQ(device.CreateSemaphore(SemaphoreDesc{2U}, semaphore), Result::Success);
    ASSERT_EQ(device.SignalSemaphore(semaphore, 8U), Result::Success);
    ASSERT_EQ(device.WaitSemaphore(semaphore, 9U, 0U), Result::TimedOut);
    ASSERT_EQ(device.WaitSemaphore(semaphore, 8U, 0U), Result::Success);

    const auto &operations = device.GetRecordedDeviceOperations();
    ASSERT_GE(operations.size(), 3U);
    const auto &signal = std::get<SignalSemaphoreOperation>(operations[operations.size() - 3U]);
    EXPECT_EQ(signal.semaphore, semaphore);
    EXPECT_EQ(signal.value, 8U);
    const auto &timedOut = std::get<WaitSemaphoreOperation>(operations[operations.size() - 2U]);
    EXPECT_EQ(timedOut.value, 9U);
    EXPECT_FALSE(timedOut.satisfied) << "a timed-out wait must not be recorded as satisfied";
    const auto &satisfied = std::get<WaitSemaphoreOperation>(operations.back());
    EXPECT_EQ(satisfied.value, 8U);
    EXPECT_TRUE(satisfied.satisfied);
}

TEST(TimelineSemaphoreTest, IndependentSemaphoresKeepIndependentCounters) {
    NullRenderDevice device;
    SemaphoreHandle first;
    SemaphoreHandle second;
    ASSERT_EQ(device.CreateSemaphore(SemaphoreDesc{1U}, first), Result::Success);
    ASSERT_EQ(device.CreateSemaphore(SemaphoreDesc{1U}, second), Result::Success);
    EXPECT_NE(first, second);
    ASSERT_EQ(device.SignalSemaphore(first, 10U), Result::Success);
    std::uint64_t counter = 0U;
    ASSERT_EQ(device.GetSemaphoreCounter(second, counter), Result::Success);
    EXPECT_EQ(counter, 1U);
    EXPECT_EQ(device.WaitSemaphore(second, 10U, 0U), Result::TimedOut);
    EXPECT_EQ(device.WaitSemaphore(first, 10U, 0U), Result::Success);
    EXPECT_EQ(device.DestroySemaphore(first), Result::Success);
    EXPECT_EQ(device.DestroySemaphore(second), Result::Success);
}


// ---- Foundation WP4: submission-level queue sync -----------------------------------------
TEST(TimelineSemaphoreTest, NullSubmitDescRecordsWaitsAndSignalsVerbatim) {
    NullRenderDevice device;
    SemaphoreHandle gate, done;
    ASSERT_EQ(device.CreateSemaphore(SemaphoreDesc{2U}, gate), Result::Success);
    ASSERT_EQ(device.CreateSemaphore(SemaphoreDesc{0U}, done), Result::Success);
    const std::array<SemaphoreWait, 1U> waits{SemaphoreWait{gate, 7U}};
    const std::array<SemaphoreSignal, 1U> signals{SemaphoreSignal{done, 1U}};
    std::unique_ptr<ICommandBuffer> commands = device.CreateCommandBuffer();
    ASSERT_NE(commands, nullptr);
    commands->BeginRenderPass(RenderPassDesc{});
    commands->Draw(3U, 1U);
    commands->EndRenderPass();
    ASSERT_EQ(device.SubmitWithSync(std::move(commands), SubmitDesc{waits, signals}), Result::Recorded);
    EXPECT_EQ(device.GetLastSubmittedCommands().size(), 3U) << "the work itself is submitted exactly like Submit";
    const std::vector<RecordedDeviceOperation>& operations = device.GetRecordedDeviceOperations();
    ASSERT_FALSE(operations.empty());
    const auto* const record = std::get_if<SubmitOperation>(&operations.back());
    ASSERT_NE(record, nullptr);
    ASSERT_EQ(record->waits.size(), 1U);
    EXPECT_EQ(record->waits[0].semaphore, gate);
    EXPECT_EQ(record->waits[0].value, 7U);
    ASSERT_EQ(record->signals.size(), 1U);
    EXPECT_EQ(record->signals[0].semaphore, done);
    EXPECT_EQ(record->signals[0].value, 1U);
    std::uint64_t counter = 99U;
    ASSERT_EQ(device.GetSemaphoreCounter(done, counter), Result::Success);
    EXPECT_EQ(counter, 0U) << "a recorded signal is a request, never invented GPU progress";
    EXPECT_EQ(device.WaitSemaphore(done, 1U, 0U), Result::TimedOut);
    // An empty desc is exactly Submit and records nothing extra.
    const std::size_t before = device.GetRecordedDeviceOperations().size();
    std::unique_ptr<ICommandBuffer> plain = device.CreateCommandBuffer();
    ASSERT_NE(plain, nullptr);
    EXPECT_EQ(device.SubmitWithSync(std::move(plain), SubmitDesc{}), Result::Recorded);
    EXPECT_EQ(device.GetRecordedDeviceOperations().size(), before);
    EXPECT_EQ(device.DestroySemaphore(gate), Result::Success);
    EXPECT_EQ(device.DestroySemaphore(done), Result::Success);
}

TEST(TimelineSemaphoreTest, NullSubmitDescRejectsBadSemaphores) {
    NullRenderDevice device;
    SemaphoreHandle live, destroyed;
    ASSERT_EQ(device.CreateSemaphore(SemaphoreDesc{4U}, live), Result::Success);
    ASSERT_EQ(device.CreateSemaphore(SemaphoreDesc{0U}, destroyed), Result::Success);
    ASSERT_EQ(device.DestroySemaphore(destroyed), Result::Success);
    const auto submit = [&device](const std::span<const SemaphoreWait> waits,
                                  const std::span<const SemaphoreSignal> signals) {
        std::unique_ptr<ICommandBuffer> commands = device.CreateCommandBuffer();
        return device.SubmitWithSync(std::move(commands), SubmitDesc{waits, signals});
    };
    const std::array<SemaphoreWait, 1U> waitDestroyed{SemaphoreWait{destroyed, 1U}};
    const std::array<SemaphoreSignal, 1U> signalDestroyed{SemaphoreSignal{destroyed, 1U}};
    const std::array<SemaphoreWait, 1U> waitLive{SemaphoreWait{live, 9U}};
    const std::array<SemaphoreSignal, 1U> signalStale{SemaphoreSignal{live, 4U}};
    const std::array<SemaphoreSignal, 1U> signalLive{SemaphoreSignal{live, 5U}};
    const std::array<SemaphoreSignal, 2U> signalTwice{SemaphoreSignal{live, 5U}, SemaphoreSignal{live, 6U}};
    EXPECT_EQ(submit(waitDestroyed, {}), Result::InvalidHandle);
    EXPECT_EQ(submit({}, signalDestroyed), Result::InvalidHandle);
    EXPECT_EQ(submit({}, signalStale), Result::InvalidArgument) << "a signal must exceed the current counter";
    EXPECT_EQ(submit(waitLive, signalLive), Result::InvalidArgument) << "one semaphore may not be in both lists";
    EXPECT_EQ(submit({}, signalTwice), Result::InvalidArgument) << "one semaphore may not be signalled twice";
    EXPECT_EQ(submit(waitLive, {}), Result::Recorded) << "a wait on a future value is a valid request";
    EXPECT_TRUE(device.GetRecordedDeviceOperations().size() >= 1U);
    std::size_t submitRecords = 0U;
    for (const RecordedDeviceOperation& operation : device.GetRecordedDeviceOperations()) {
        if (std::holds_alternative<SubmitOperation>(operation)) {
            ++submitRecords;
        }
    }
    EXPECT_EQ(submitRecords, 1U) << "refused submits record nothing";
    EXPECT_EQ(device.DestroySemaphore(live), Result::Success);
}
} // namespace
} // namespace UVE::Render::Tests
