// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
#include "thread_control.h"
#include <array>
#include <future>
#include <limits>
#include <string>
#include <vector>
#include <gtest/gtest.h>
#include "thread_naming.h"
#include "thread_pool.h"
#include "job_counter.h"
namespace {
using namespace UVE::Threading;
struct Worker {
    std::promise<void> finish;
    std::thread thread{[f = finish.get_future()] { f.wait(); }};
    ~Worker() {
        finish.set_value();
        thread.join();
    }
};
TEST(ThreadControlTest, NonJoinableAndJoinedThreadsAreRejectedBeforeNativeAccess) {
    std::thread invalid;
    std::vector<std::uint32_t> cpus{42U};
    auto p = ThreadPriority::Background;
    EXPECT_EQ(GetThreadAffinity(invalid, cpus).code, ThreadControlCode::InvalidThread);
    EXPECT_EQ(SetThreadAffinity(invalid, cpus).code, ThreadControlCode::InvalidThread);
    EXPECT_EQ(GetThreadPriority(invalid, p).code, ThreadControlCode::InvalidThread);
    EXPECT_EQ(SetThreadPriority(invalid, p).code, ThreadControlCode::InvalidThread);
    invalid = std::thread([] {});
    invalid.join();
    EXPECT_EQ(GetThreadPriority(invalid, p).code, ThreadControlCode::InvalidThread);
    EXPECT_EQ(cpus, (std::vector<std::uint32_t>{42U}));
    EXPECT_EQ(p, ThreadPriority::Background);
}
TEST(ThreadControlTest, InvalidArgumentsCannotChangeCurrentSettings) {
    EXPECT_EQ(SetCurrentThreadAffinity({}).code, ThreadControlCode::InvalidArgument);
    EXPECT_EQ(SetCurrentThreadPriority(static_cast<ThreadPriority>(255)).code,
              ThreadControlCode::InvalidArgument);
    Worker worker;
    EXPECT_EQ(SetThreadAffinity(worker.thread, {}).code, ThreadControlCode::InvalidArgument);
    EXPECT_EQ(SetThreadPriority(worker.thread, static_cast<ThreadPriority>(255)).code,
              ThreadControlCode::InvalidArgument);
}
TEST(ThreadControlTest, ActualAllowedAffinityCanBeNarrowedAndRestoredOnAWorker) {
    Worker worker;
    const auto cap = GetThreadControlCapabilities();
    std::vector<std::uint32_t> original{42U};
    const auto result = GetThreadAffinity(worker.thread, original);
    if (!cap.affinity || !cap.threadObject) {
        EXPECT_EQ(result.code, ThreadControlCode::Unsupported);
        EXPECT_EQ(original, (std::vector<std::uint32_t>{42U}));
        EXPECT_EQ(SetThreadAffinity(worker.thread, original).code, ThreadControlCode::Unsupported);
        return;
    }
    ASSERT_TRUE(result.IsApplied());
    ASSERT_FALSE(original.empty());
    const std::array<std::uint32_t, 1> selected{original.front()};
    ASSERT_TRUE(SetThreadAffinity(worker.thread, selected).IsApplied());
    std::vector<std::uint32_t> observed;
    ASSERT_TRUE(GetThreadAffinity(worker.thread, observed).IsApplied());
    EXPECT_EQ(observed, (std::vector<std::uint32_t>{original.front()}));
    const std::array<std::uint32_t, 2> repeated{original.front(), original.front()};
    const std::array<std::uint32_t, 1> invalid{std::numeric_limits<std::uint32_t>::max()};
    EXPECT_EQ(SetThreadAffinity(worker.thread, repeated).code, ThreadControlCode::InvalidArgument);
    EXPECT_EQ(SetThreadAffinity(worker.thread, invalid).code, ThreadControlCode::InvalidArgument);
    ASSERT_TRUE(GetThreadAffinity(worker.thread, observed).IsApplied());
    EXPECT_EQ(observed, (std::vector<std::uint32_t>{original.front()}));
    EXPECT_TRUE(SetThreadAffinity(worker.thread, original).IsApplied());
}
TEST(ThreadControlTest, BackgroundAndNormalPrioritiesRoundTripOrExplicitlyReportUnsupported) {
    Worker worker;
    const auto cap = GetThreadControlCapabilities();
    auto out = ThreadPriority::Realtime;
    if (!cap.priority || !cap.threadObject) {
        EXPECT_EQ(SetThreadPriority(worker.thread, ThreadPriority::Normal).code,
                  ThreadControlCode::Unsupported);
        EXPECT_EQ(GetThreadPriority(worker.thread, out).code, ThreadControlCode::Unsupported);
        EXPECT_EQ(out, ThreadPriority::Realtime);
        return;
    }
    ASSERT_TRUE(SetThreadPriority(worker.thread, ThreadPriority::Background).IsApplied());
    ASSERT_TRUE(GetThreadPriority(worker.thread, out).IsApplied());
    EXPECT_EQ(out, ThreadPriority::Background);
    ASSERT_TRUE(SetThreadPriority(worker.thread, ThreadPriority::Normal).IsApplied());
    ASSERT_TRUE(GetThreadPriority(worker.thread, out).IsApplied());
    EXPECT_EQ(out, ThreadPriority::Normal);
}
TEST(ThreadControlTest, RealtimeNeverSilentlyDowngradesOrChangesTheProcessClass) {
    Worker worker;
    const auto cap = GetThreadControlCapabilities();
    const auto result = SetThreadPriority(worker.thread, ThreadPriority::Realtime);
    if (!cap.realtime || !cap.threadObject) {
        EXPECT_EQ(result.code, ThreadControlCode::Unsupported);
        return;
    }
    if (result.IsApplied()) {
        auto out = ThreadPriority::Normal;
        EXPECT_TRUE(GetThreadPriority(worker.thread, out).IsApplied());
        EXPECT_EQ(out, ThreadPriority::Realtime);
        EXPECT_TRUE(SetThreadPriority(worker.thread, ThreadPriority::Normal).IsApplied());
    } else {
        EXPECT_EQ(result.code, ThreadControlCode::PermissionDenied);
        EXPECT_NE(result.nativeError, 0U);
    }
}
TEST(ThreadControlTest, CurrentThreadOperationsRunOnPoolWorkerWithoutRenamingOrAffectingCaller) {
    ThreadPool pool{1U};
    JobCounter counter;
    pool.Submit(
        [] {
            const auto cap = GetThreadControlCapabilities();
            std::vector<std::uint32_t> cpus{42U};
            if (cap.affinity) {
                ASSERT_TRUE(GetCurrentThreadAffinity(cpus).IsApplied());
                ASSERT_FALSE(cpus.empty());
                const std::array<std::uint32_t, 1> selected{cpus.front()};
                EXPECT_TRUE(SetCurrentThreadAffinity(selected).IsApplied());
                EXPECT_TRUE(SetCurrentThreadAffinity(cpus).IsApplied());
            } else {
                EXPECT_EQ(GetCurrentThreadAffinity(cpus).code, ThreadControlCode::Unsupported);
                EXPECT_EQ(cpus, (std::vector<std::uint32_t>{42U}));
            }
            auto priority = ThreadPriority::Background;
            if (cap.priority) {
                EXPECT_TRUE(SetCurrentThreadPriority(ThreadPriority::Normal).IsApplied());
                EXPECT_TRUE(GetCurrentThreadPriority(priority).IsApplied());
                EXPECT_EQ(priority, ThreadPriority::Normal);
            } else {
                EXPECT_EQ(GetCurrentThreadPriority(priority).code, ThreadControlCode::Unsupported);
                EXPECT_EQ(priority, ThreadPriority::Background);
            }
            std::string name;
#if defined(__linux__) || defined(__APPLE__)
            EXPECT_TRUE(TryGetCurrentThreadName(name));
            EXPECT_EQ(name, "UVEWorker0");
#else
            if (TryGetCurrentThreadName(name)) {
                EXPECT_EQ(name, "UVEWorker0");
            }
#endif
        },
        counter);
    counter.Wait();
}
} // namespace
