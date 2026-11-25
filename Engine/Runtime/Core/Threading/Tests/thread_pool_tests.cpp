// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#include "thread_pool.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

#include "log_sink.h"
#include "logger.h"
#include "job_counter.h"

namespace UVE::Threading::Tests {
namespace {

TEST(ThreadPoolTest, ExplicitWorkerCount_IsHonored) {
    ThreadPool pool(3);
    EXPECT_EQ(pool.GetWorkerCount(), 3U);
}

TEST(ThreadPoolTest, AutoWorkerCount_ResolvesToAtLeastOne) {
    ThreadPool pool(0);
    EXPECT_GE(pool.GetWorkerCount(), 1U);
}

TEST(ThreadPoolTest, SubmitWithCounter_AllJobsRunExactlyOnce) {
    ThreadPool pool(4);
    JobCounter counter;
    std::atomic<int> completed{0};
    constexpr int kJobCount = 50;

    for (int i = 0; i < kJobCount; ++i) {
        pool.Submit([&completed] { completed.fetch_add(1, std::memory_order_relaxed); }, counter);
    }
    counter.Wait();

    EXPECT_EQ(completed.load(), kJobCount);
}

TEST(ThreadPoolTest, SubmitWithCounter_HighVolumeStress) {
    ThreadPool pool(4);
    JobCounter counter;
    std::atomic<int> completed{0};
    constexpr int kJobCount = 1000;

    for (int i = 0; i < kJobCount; ++i) {
        pool.Submit([&completed] { completed.fetch_add(1, std::memory_order_relaxed); }, counter);
    }
    counter.Wait();

    EXPECT_EQ(completed.load(), kJobCount);
}

TEST(ThreadPoolTest, SubmitWithoutCounter_JobEventuallyRuns) {
    ThreadPool pool(2);
    std::atomic<bool> ran{false};
    pool.Submit([&ran] { ran.store(true, std::memory_order_relaxed); });

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(1);
    while (!ran.load(std::memory_order_relaxed) && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    EXPECT_TRUE(ran.load());
}

TEST(ThreadPoolTest, JobsRunOnWorkerThread_NotCallingThread) {
    ThreadPool pool(2);
    JobCounter counter;
    const std::thread::id callingThreadId = std::this_thread::get_id();
    std::thread::id jobThreadId{};

    pool.Submit([&jobThreadId] { jobThreadId = std::this_thread::get_id(); }, counter);
    counter.Wait();

    EXPECT_NE(jobThreadId, callingThreadId);
}

TEST(ThreadPoolTest, RecursiveSubmission_CompletesWithoutDeadlock) {
    ThreadPool pool(2);
    JobCounter outerCounter;
    std::atomic<int> innerCompleted{0};

    pool.Submit(
        [&pool, &innerCompleted] {
            JobCounter innerCounter;
            for (int i = 0; i < 5; ++i) {
                pool.Submit(
                    [&innerCompleted] { innerCompleted.fetch_add(1, std::memory_order_relaxed); },
                    innerCounter);
            }
            innerCounter.Wait();
        },
        outerCounter);

    outerCounter.Wait();
    EXPECT_EQ(innerCompleted.load(), 5);
}

TEST(ThreadPoolTest, DestructorDrainsPendingWork) {
    std::atomic<int> completed{0};
    constexpr int kJobCount = 20;
    {
        ThreadPool pool(2);
        for (int i = 0; i < kJobCount; ++i) {
            pool.Submit([&completed] {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
                completed.fetch_add(1, std::memory_order_relaxed);
            });
        }
        // pool destructs here, blocking until every worker has drained its queue and joined.
    }
    EXPECT_EQ(completed.load(), kJobCount);
}

TEST(ThreadPoolTest, ExceptionInJob_IsCaughtAndLoggedAsError_PoolKeepsRunning) {
    Debug::Logger logger;
    logger.Init(Debug::LogLevel::Trace);
    auto memorySink = std::make_unique<Debug::MemorySink>();
    Debug::MemorySink* const memorySinkPtr = memorySink.get();
    logger.AddSink(std::move(memorySink));

    ThreadPool pool(2);
    JobCounter counter;
    std::atomic<bool> normalJobRan{false};

    pool.Submit([] { throw std::runtime_error("boom"); }, counter);
    pool.Submit([&normalJobRan] { normalJobRan.store(true, std::memory_order_relaxed); }, counter);
    counter.Wait();

    EXPECT_TRUE(normalJobRan.load());

    const std::vector<Debug::LogMessage> messages = memorySinkPtr->GetMessages();
    const bool foundExceptionError =
        std::any_of(messages.begin(), messages.end(), [](const Debug::LogMessage& message) {
            return message.level == Debug::LogLevel::Error &&
                   message.message.contains("boom");
        });
    EXPECT_TRUE(foundExceptionError);

    logger.Shutdown();
}

TEST(ThreadPoolTest, UnknownExceptionInJob_IsCaughtAndLoggedGenerically) {
    Debug::Logger logger;
    logger.Init(Debug::LogLevel::Trace);
    auto memorySink = std::make_unique<Debug::MemorySink>();
    Debug::MemorySink* const memorySinkPtr = memorySink.get();
    logger.AddSink(std::move(memorySink));

    ThreadPool pool(2);
    JobCounter counter;
    pool.Submit([] { throw 42; }, counter);
    counter.Wait();

    const std::vector<Debug::LogMessage> messages = memorySinkPtr->GetMessages();
    const bool foundGenericError =
        std::any_of(messages.begin(), messages.end(), [](const Debug::LogMessage& message) {
            return message.level == Debug::LogLevel::Error &&
                   message.message.contains("unknown");
        });
    EXPECT_TRUE(foundGenericError);

    logger.Shutdown();
}

TEST(ThreadPoolTest, DebugStats_PendingCountRisesAndFallsBackToZero) {
    ThreadPool pool(1);
    JobCounter counter;
    std::atomic<bool> canProceed{false};

    pool.Submit(
        [&canProceed] {
            while (!canProceed.load(std::memory_order_relaxed)) {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        },
        counter);

    // GetPendingJobCount() is incremented synchronously inside Submit(), before the job is
    // even picked up by a worker, so this is observable immediately without a race.
    EXPECT_GE(pool.GetPendingJobCount(), 1U);

    canProceed.store(true, std::memory_order_relaxed);
    counter.Wait();

    // JobCounter is notified immediately before ThreadPool decrements its independent
    // pending-stat counter. Let that last bookkeeping instruction settle without asserting a
    // scheduler-dependent ordering between two separate atomics.
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(100);
    while (pool.GetPendingJobCount() != 0U && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::yield();
    }
    EXPECT_EQ(pool.GetPendingJobCount(), 0U);
}

TEST(ThreadPoolTest, DebugStats_ActiveWorkerCountNeverExceedsWorkerCount) {
    ThreadPool pool(3);
    JobCounter counter;
    std::atomic<bool> canProceed{false};
    std::atomic<std::size_t> maxObservedActive{0};

    for (int i = 0; i < 6; ++i) {
        pool.Submit(
            [&canProceed] {
                while (!canProceed.load(std::memory_order_relaxed)) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
                }
            },
            counter);
    }

    for (int i = 0; i < 20; ++i) {
        const std::size_t active = pool.GetActiveWorkerCount();
        std::size_t currentMax = maxObservedActive.load(std::memory_order_relaxed);
        while (active > currentMax && !maxObservedActive.compare_exchange_weak(currentMax, active)) {
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }

    canProceed.store(true, std::memory_order_relaxed);
    counter.Wait();

    EXPECT_LE(maxObservedActive.load(), pool.GetWorkerCount());
}

TEST(ThreadPoolTest, IsWorkerThread_TrueInsideJobFalseOnCallingThread) {
    ThreadPool pool(2);
    JobCounter counter;
    std::atomic<bool> wasWorkerThreadInsideJob{false};

    EXPECT_FALSE(pool.IsWorkerThread());

    pool.Submit(
        [&pool, &wasWorkerThreadInsideJob] {
            wasWorkerThreadInsideJob.store(pool.IsWorkerThread(), std::memory_order_relaxed);
        },
        counter);
    counter.Wait();

    EXPECT_TRUE(wasWorkerThreadInsideJob.load());
}

TEST(ThreadPoolTest, IdleWorkerStealsJobsQueuedBehindBlockedWorkers) {
    // Deterministic by construction: nothing here depends on one worker happening to run dry
    // before the others, which is what made sleep-based versions of this test fail under a
    // parallel ctest run.
    //
    // Three workers are pinned inside gate jobs until the test releases them. Submission is
    // round-robin, so most of the fast jobs that follow land on those pinned workers' queues,
    // where their owners cannot reach them. The only way all of them finish before the gates
    // open is for the one free worker to steal them. The waits are bounded, so a pool that
    // cannot steal fails the test instead of hanging it.
    constexpr std::size_t kWorkerCount = 4;
    constexpr std::size_t kGateCount = kWorkerCount - 1;
    constexpr int kFastJobCount = 60;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    const auto waitUntil = [deadline](const auto& isDone) {
        while (!isDone() && std::chrono::steady_clock::now() < deadline) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        return isDone();
    };

    ThreadPool pool(kWorkerCount);
    JobCounter counter;
    std::atomic<std::size_t> gatesEntered{0};
    std::atomic<bool> releaseGates{false};
    std::atomic<int> fastJobsDone{0};

    // The gates wait for the release alone, with no deadline of their own: the test thread never
    // blocks on the pool before releasing them, so they cannot hang it, and a shared deadline
    // would let the blocked workers start draining their queues at the very moment the count
    // below is read.
    for (std::size_t i = 0; i < kGateCount; ++i) {
        pool.Submit(
            [&gatesEntered, &releaseGates] {
                gatesEntered.fetch_add(1, std::memory_order_acq_rel);
                while (!releaseGates.load(std::memory_order_acquire)) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
                }
            },
            counter);
    }

    // A worker running a gate job cannot pick up anything else, so once every gate has been
    // entered, exactly kGateCount distinct workers are occupied and one is free.
    const bool allGatesEntered = waitUntil(
        [&gatesEntered] { return gatesEntered.load(std::memory_order_acquire) == kGateCount; });

    int fastJobsDoneWhileGated = 0;
    if (allGatesEntered) {
        for (int i = 0; i < kFastJobCount; ++i) {
            pool.Submit(
                [&fastJobsDone] { fastJobsDone.fetch_add(1, std::memory_order_acq_rel); }, counter);
        }
        waitUntil([&fastJobsDone] {
            return fastJobsDone.load(std::memory_order_acquire) == kFastJobCount;
        });
        fastJobsDoneWhileGated = fastJobsDone.load(std::memory_order_acquire);
    }

    // Every path reaches here, so no job outlives the locals it captured.
    releaseGates.store(true, std::memory_order_release);
    counter.Wait();

    ASSERT_TRUE(allGatesEntered) << "the gate jobs never all started";
    EXPECT_EQ(fastJobsDoneWhileGated, kFastJobCount)
        << "jobs queued behind the blocked workers were never picked up by the idle one";
    EXPECT_GT(pool.GetStolenJobCount(), 0U);
}

} // namespace
} // namespace UVE::Threading::Tests
