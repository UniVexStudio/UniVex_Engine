// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
#pragma once
#include <cstdint>
#include <span>
#include <thread>
#include <vector>
namespace UVE::Threading {
enum class ThreadControlCode : std::uint8_t {
    Applied = 0,
    Unsupported,
    InvalidThread,
    InvalidArgument,
    PermissionDenied,
    SystemError
};
struct ThreadControlResult final {
    ThreadControlCode code = ThreadControlCode::Unsupported;
    std::uint32_t nativeError = 0U;
    [[nodiscard]] bool IsApplied() const noexcept { return code == ThreadControlCode::Applied; }
};
struct ThreadControlCapabilities final {
    bool affinity = false;
    bool priority = false;
    bool threadObject = false;
    bool realtime = false;
    /// Exclusive CPU-index limit, not the count of permitted CPUs. Read affinity for cpuset restrictions.
    std::uint32_t cpuIndexLimit = 0U;
};
/// Intent classes, not portable numeric priorities. Linux: OTHER, BATCH, RR(minimum).
/// Windows: NORMAL, BELOW_NORMAL; Realtime is accepted only in an already-realtime process.
/// No call changes process priority/niceness or silently downgrades. Realtime may require privilege.
enum class ThreadPriority : std::uint8_t { Normal = 0, Background, Realtime };
/// Linux pthreads and Windows current-thread operations; std::thread native control on Linux/MSVC.
/// Other platforms/toolchains explicitly return Unsupported. Capability is not permission.
[[nodiscard]] ThreadControlCapabilities GetThreadControlCapabilities() noexcept;
/// Nonempty unique CPU indices. Windows indices encode processor-group * 64 + CPU, and a
/// requested mask must fit one group. Readback reports actual OS restrictions. Output is unchanged
/// on failure; vector allocation failure propagates without publishing a partial result.
[[nodiscard]] ThreadControlResult
SetCurrentThreadAffinity(std::span<const std::uint32_t> cpus) noexcept;
[[nodiscard]] ThreadControlResult GetCurrentThreadAffinity(std::vector<std::uint32_t> &out);
[[nodiscard]] ThreadControlResult SetCurrentThreadPriority(ThreadPriority priority) noexcept;
[[nodiscard]] ThreadControlResult GetCurrentThreadPriority(ThreadPriority &out) noexcept;
/// Only caller-owned joinable threads; no raw handles. Caller synchronizes move/join/detach,
/// changes and lifetime. An exited thread may return InvalidThread. Pool policy/names are untouched.
[[nodiscard]] ThreadControlResult SetThreadAffinity(std::thread &thread,
                                                          std::span<const std::uint32_t> cpus) noexcept;
[[nodiscard]] ThreadControlResult GetThreadAffinity(std::thread &thread,
                                                          std::vector<std::uint32_t> &out);
[[nodiscard]] ThreadControlResult SetThreadPriority(std::thread &thread,
                                                          ThreadPriority priority) noexcept;
[[nodiscard]] ThreadControlResult GetThreadPriority(std::thread &thread,
                                                          ThreadPriority &out) noexcept;
} // namespace UVE::Threading
