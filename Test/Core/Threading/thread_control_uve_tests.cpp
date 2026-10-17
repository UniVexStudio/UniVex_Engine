// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
#include "uve/threading/thread_control_uve.h"
#include <array>
#include <future>
#include <limits>
#include <string>
#include <vector>
#include <gtest/gtest.h>
#include "uve/threading/thread_naming_uve.h"
#include "uve/threading/thread_pool_uve.h"
#include "uve/threading/job_counter_uve.h"
namespace {
using namespace UVE::Threading;
struct WorkerUVE {
    std::promise<void> finish;
    std::thread thread{[f = finish.get_future()] { f.wait(); }};
    ~WorkerUVE() {
        finish.set_value();
        thread.join();
    }
};
TEST(ThreadControlUVETest, NonJoinableAndJoinedThreadsAreRejectedBeforeNativeAccess) {
    std::thread invalid;
    std::vector<std::uint32_t> cpus{42U};
    auto p = ThreadPriorityUVE::Background;
    EXPECT_EQ(GetThreadAffinityUVE(invalid, cpus).code, ThreadControlCodeUVE::InvalidThread);
    EXPECT_EQ(SetThreadAffinityUVE(invalid, cpus).code, ThreadControlCodeUVE::InvalidThread);
    EXPECT_EQ(GetThreadPriorityUVE(invalid, p).code, ThreadControlCodeUVE::InvalidThread);
    EXPECT_EQ(SetThreadPriorityUVE(invalid, p).code, ThreadControlCodeUVE::InvalidThread);
    invalid = std::thread([] {});
    invalid.join();
    EXPECT_EQ(GetThreadPriorityUVE(invalid, p).code, ThreadControlCodeUVE::InvalidThread);
    EXPECT_EQ(cpus, (std::vector<std::uint32_t>{42U}));
    EXPECT_EQ(p, ThreadPriorityUVE::Background);
}
TEST(ThreadControlUVETest, InvalidArgumentsCannotChangeCurrentSettings) {
    EXPECT_EQ(SetCurrentThreadAffinityUVE({}).code, ThreadControlCodeUVE::InvalidArgument);
    EXPECT_EQ(SetCurrentThreadPriorityUVE(static_cast<ThreadPriorityUVE>(255)).code,
              ThreadControlCodeUVE::InvalidArgument);
    WorkerUVE worker;
    EXPECT_EQ(SetThreadAffinityUVE(worker.thread, {}).code, ThreadControlCodeUVE::InvalidArgument);
    EXPECT_EQ(SetThreadPriorityUVE(worker.thread, static_cast<ThreadPriorityUVE>(255)).code,
              ThreadControlCodeUVE::InvalidArgument);
}
TEST(ThreadControlUVETest, ActualAllowedAffinityCanBeNarrowedAndRestoredOnAWorker) {
    WorkerUVE worker;
    const auto cap = GetThreadControlCapabilitiesUVE();
    std::vector<std::uint32_t> original{42U};
    const auto result = GetThreadAffinityUVE(worker.thread, original);
    if (!cap.affinity || !cap.threadObject) {
        EXPECT_EQ(result.code, ThreadControlCodeUVE::Unsupported);
        EXPECT_EQ(original, (std::vector<std::uint32_t>{42U}));
        EXPECT_EQ(SetThreadAffinityUVE(worker.thread, original).code, ThreadControlCodeUVE::Unsupported);
        return;
    }
    ASSERT_TRUE(result.IsAppliedUVE());
    ASSERT_FALSE(original.empty());
    const std::array<std::uint32_t, 1> selected{original.front()};
    ASSERT_TRUE(SetThreadAffinityUVE(worker.thread, selected).IsAppliedUVE());
    std::vector<std::uint32_t> observed;
    ASSERT_TRUE(GetThreadAffinityUVE(worker.thread, observed).IsAppliedUVE());
    EXPECT_EQ(observed, (std::vector<std::uint32_t>{original.front()}));
    const std::array<std::uint32_t, 2> repeated{original.front(), original.front()};
    const std::array<std::uint32_t, 1> invalid{std::numeric_limits<std::uint32_t>::max()};
    EXPECT_EQ(SetThreadAffinityUVE(worker.thread, repeated).code, ThreadControlCodeUVE::InvalidArgument);
    EXPECT_EQ(SetThreadAffinityUVE(worker.thread, invalid).code, ThreadControlCodeUVE::InvalidArgument);
    ASSERT_TRUE(GetThreadAffinityUVE(worker.thread, observed).IsAppliedUVE());
    EXPECT_EQ(observed, (std::vector<std::uint32_t>{original.front()}));
    EXPECT_TRUE(SetThreadAffinityUVE(worker.thread, original).IsAppliedUVE());
}
TEST(ThreadControlUVETest, BackgroundAndNormalPrioritiesRoundTripOrExplicitlyReportUnsupported) {
    WorkerUVE worker;
    const auto cap = GetThreadControlCapabilitiesUVE();
    auto out = ThreadPriorityUVE::Realtime;
    if (!cap.priority || !cap.threadObject) {
        EXPECT_EQ(SetThreadPriorityUVE(worker.thread, ThreadPriorityUVE::Normal).code,
                  ThreadControlCodeUVE::Unsupported);
        EXPECT_EQ(GetThreadPriorityUVE(worker.thread, out).code, ThreadControlCodeUVE::Unsupported);
        EXPECT_EQ(out, ThreadPriorityUVE::Realtime);
        return;
    }
    ASSERT_TRUE(SetThreadPriorityUVE(worker.thread, ThreadPriorityUVE::Background).IsAppliedUVE());
    ASSERT_TRUE(GetThreadPriorityUVE(worker.thread, out).IsAppliedUVE());
    EXPECT_EQ(out, ThreadPriorityUVE::Background);
    ASSERT_TRUE(SetThreadPriorityUVE(worker.thread, ThreadPriorityUVE::Normal).IsAppliedUVE());
    ASSERT_TRUE(GetThreadPriorityUVE(worker.thread, out).IsAppliedUVE());
    EXPECT_EQ(out, ThreadPriorityUVE::Normal);
}
TEST(ThreadControlUVETest, RealtimeNeverSilentlyDowngradesOrChangesTheProcessClass) {
    WorkerUVE worker;
    const auto cap = GetThreadControlCapabilitiesUVE();
    const auto result = SetThreadPriorityUVE(worker.thread, ThreadPriorityUVE::Realtime);
    if (!cap.realtime || !cap.threadObject) {
        EXPECT_EQ(result.code, ThreadControlCodeUVE::Unsupported);
        return;
    }
    if (result.IsAppliedUVE()) {
        auto out = ThreadPriorityUVE::Normal;
        EXPECT_TRUE(GetThreadPriorityUVE(worker.thread, out).IsAppliedUVE());
        EXPECT_EQ(out, ThreadPriorityUVE::Realtime);
        EXPECT_TRUE(SetThreadPriorityUVE(worker.thread, ThreadPriorityUVE::Normal).IsAppliedUVE());
    } else {
        EXPECT_EQ(result.code, ThreadControlCodeUVE::PermissionDenied);
        EXPECT_NE(result.nativeError, 0U);
    }
}
TEST(ThreadControlUVETest, CurrentThreadOperationsRunOnPoolWorkerWithoutRenamingOrAffectingCaller) {
    ThreadPoolUVE pool{1U};
    JobCounterUVE counter;
    pool.SubmitUVE(
        [] {
            const auto cap = GetThreadControlCapabilitiesUVE();
            std::vector<std::uint32_t> cpus{42U};
            if (cap.affinity) {
                ASSERT_TRUE(GetCurrentThreadAffinityUVE(cpus).IsAppliedUVE());
                ASSERT_FALSE(cpus.empty());
                const std::array<std::uint32_t, 1> selected{cpus.front()};
                EXPECT_TRUE(SetCurrentThreadAffinityUVE(selected).IsAppliedUVE());
                EXPECT_TRUE(SetCurrentThreadAffinityUVE(cpus).IsAppliedUVE());
            } else {
                EXPECT_EQ(GetCurrentThreadAffinityUVE(cpus).code, ThreadControlCodeUVE::Unsupported);
                EXPECT_EQ(cpus, (std::vector<std::uint32_t>{42U}));
            }
            auto priority = ThreadPriorityUVE::Background;
            if (cap.priority) {
                EXPECT_TRUE(SetCurrentThreadPriorityUVE(ThreadPriorityUVE::Normal).IsAppliedUVE());
                EXPECT_TRUE(GetCurrentThreadPriorityUVE(priority).IsAppliedUVE());
                EXPECT_EQ(priority, ThreadPriorityUVE::Normal);
            } else {
                EXPECT_EQ(GetCurrentThreadPriorityUVE(priority).code, ThreadControlCodeUVE::Unsupported);
                EXPECT_EQ(priority, ThreadPriorityUVE::Background);
            }
            std::string name;
#if defined(__linux__) || defined(__APPLE__)
            EXPECT_TRUE(TryGetCurrentThreadNameUVE(name));
            EXPECT_EQ(name, "UVEWorker0");
#else
            if (TryGetCurrentThreadNameUVE(name)) {
                EXPECT_EQ(name, "UVEWorker0");
            }
#endif
        },
        counter);
    counter.WaitUVE();
}
} // namespace
