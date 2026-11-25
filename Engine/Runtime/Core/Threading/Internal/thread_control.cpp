// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#endif
#endif
#include "thread_control.h"
#include <cerrno>
#include <utility>
#if defined(__linux__)
#include <pthread.h>
#include <sched.h>
#elif defined(_WIN32)
#include <windows.h>
#endif
namespace UVE::Threading {
namespace {
bool ValidPriority(ThreadPriority p) noexcept {
    return p == ThreadPriority::Normal || p == ThreadPriority::Background ||
           p == ThreadPriority::Realtime;
}
#if defined(__linux__)
ThreadControlResult Result(int error) noexcept {
    ThreadControlCode code = ThreadControlCode::SystemError;
    switch (error) {
    case 0:
        code = ThreadControlCode::Applied;
        break;
    case ESRCH:
        code = ThreadControlCode::InvalidThread;
        break;
    case EINVAL:
        code = ThreadControlCode::InvalidArgument;
        break;
    case EPERM:
    case EACCES:
        code = ThreadControlCode::PermissionDenied;
        break;
    case ENOTSUP:
        code = ThreadControlCode::Unsupported;
        break;
    default:
        break;
    }
    return {code, static_cast<std::uint32_t>(error)};
}
ThreadControlResult SetAffinity(pthread_t thread, std::span<const std::uint32_t> cpus) noexcept {
#if defined(__ANDROID__)
    static_cast<void>(thread);
    static_cast<void>(cpus);
    return {};
#else
    cpu_set_t mask;
    CPU_ZERO(&mask);
    for (auto cpu : cpus) {
        if (cpu >= CPU_SETSIZE || CPU_ISSET(cpu, &mask))
            return {ThreadControlCode::InvalidArgument, 0U};
        CPU_SET(cpu, &mask);
    }
    return Result(pthread_setaffinity_np(thread, sizeof(mask), &mask));
#endif
}
ThreadControlResult GetAffinity(pthread_t thread, std::vector<std::uint32_t> &out) {
#if defined(__ANDROID__)
    static_cast<void>(thread);
    static_cast<void>(out);
    return {};
#else
    cpu_set_t mask;
    CPU_ZERO(&mask);
    const auto result = Result(pthread_getaffinity_np(thread, sizeof(mask), &mask));
    if (!result.IsApplied())
        return result;
    std::vector<std::uint32_t> candidate;
    for (std::uint32_t cpu = 0U; cpu < CPU_SETSIZE; ++cpu)
        if (CPU_ISSET(cpu, &mask))
            candidate.push_back(cpu);
    out = std::move(candidate);
    return result;
#endif
}
ThreadControlResult SetPriority(pthread_t thread, ThreadPriority priority) noexcept {
    int policy = SCHED_OTHER;
    sched_param parameters{};
    if (priority == ThreadPriority::Background)
        policy = SCHED_BATCH;
    if (priority == ThreadPriority::Realtime) {
        policy = SCHED_RR;
        parameters.sched_priority = sched_get_priority_min(policy);
        if (parameters.sched_priority == -1)
            return Result(errno);
    }
    return Result(pthread_setschedparam(thread, policy, &parameters));
}
ThreadControlResult GetPriority(pthread_t thread, ThreadPriority &out) noexcept {
    int policy = 0;
    sched_param parameters{};
    const auto result = Result(pthread_getschedparam(thread, &policy, &parameters));
    if (!result.IsApplied())
        return result;
    switch (policy) {
    case SCHED_OTHER:
        out = ThreadPriority::Normal;
        break;
    case SCHED_BATCH:
        out = ThreadPriority::Background;
        break;
    case SCHED_RR:
        out = ThreadPriority::Realtime;
        break;
    default:
        return {};
    }
    return result;
}
#elif defined(_WIN32)
ThreadControlResult Result(DWORD error) noexcept {
    ThreadControlCode code = ThreadControlCode::SystemError;
    switch (error) {
    case ERROR_SUCCESS:
        code = ThreadControlCode::Applied;
        break;
    case ERROR_INVALID_HANDLE:
        code = ThreadControlCode::InvalidThread;
        break;
    case ERROR_INVALID_PARAMETER:
        code = ThreadControlCode::InvalidArgument;
        break;
    case ERROR_ACCESS_DENIED:
    case ERROR_PRIVILEGE_NOT_HELD:
        code = ThreadControlCode::PermissionDenied;
        break;
    case ERROR_NOT_SUPPORTED:
        code = ThreadControlCode::Unsupported;
        break;
    default:
        break;
    }
    return {code, error};
}
ThreadControlResult SetAffinity(HANDLE thread, std::span<const std::uint32_t> cpus) noexcept {
#if defined(_WIN64) && _WIN32_WINNT >= 0x0601
    GROUP_AFFINITY mask{};
    const auto group = cpus.front() / 64U;
    if (group >= GetActiveProcessorGroupCount())
        return {ThreadControlCode::InvalidArgument, 0U};
    mask.Group = static_cast<WORD>(group);
    for (auto cpu : cpus) {
        const auto bit = KAFFINITY{1} << (cpu % 64U);
        if (cpu / 64U != group || (mask.Mask & bit) != 0U)
            return {ThreadControlCode::InvalidArgument, 0U};
        mask.Mask |= bit;
    }
    return Result(SetThreadGroupAffinity(thread, &mask, nullptr) ? ERROR_SUCCESS : GetLastError());
#else
    static_cast<void>(thread);
    static_cast<void>(cpus);
    return {};
#endif
}
ThreadControlResult GetAffinity(HANDLE thread, std::vector<std::uint32_t> &out) {
#if defined(_WIN64) && _WIN32_WINNT >= 0x0601
    GROUP_AFFINITY mask{};
    if (!GetThreadGroupAffinity(thread, &mask))
        return Result(GetLastError());
    std::vector<std::uint32_t> candidate;
    for (std::uint32_t bit = 0U; bit < 64U; ++bit)
        if ((mask.Mask & (KAFFINITY{1} << bit)) != 0U) {
            candidate.push_back(static_cast<std::uint32_t>(mask.Group) * 64U + bit);
        }
    out = std::move(candidate);
    return Result(ERROR_SUCCESS);
#else
    static_cast<void>(thread);
    static_cast<void>(out);
    return {};
#endif
}
ThreadControlResult SetPriority(HANDLE thread, ThreadPriority priority) noexcept {
    int native = THREAD_PRIORITY_NORMAL;
    if (priority == ThreadPriority::Background)
        native = THREAD_PRIORITY_BELOW_NORMAL;
    if (priority == ThreadPriority::Realtime) {
        if (GetPriorityClass(GetCurrentProcess()) != REALTIME_PRIORITY_CLASS)
            return {};
        native = THREAD_PRIORITY_TIME_CRITICAL;
    }
    return Result(::SetThreadPriority(thread, native) ? ERROR_SUCCESS : GetLastError());
}
ThreadControlResult GetPriority(HANDLE thread, ThreadPriority &out) noexcept {
    const int native = ::GetThreadPriority(thread);
    if (native == THREAD_PRIORITY_ERROR_RETURN)
        return Result(GetLastError());
    if (native == THREAD_PRIORITY_NORMAL)
        out = ThreadPriority::Normal;
    else if (native == THREAD_PRIORITY_BELOW_NORMAL)
        out = ThreadPriority::Background;
    else if (native == THREAD_PRIORITY_TIME_CRITICAL &&
             GetPriorityClass(GetCurrentProcess()) == REALTIME_PRIORITY_CLASS)
        out = ThreadPriority::Realtime;
    else
        return {}; // do not lie about an externally selected class
    return Result(ERROR_SUCCESS);
}
#endif
} // namespace
ThreadControlCapabilities GetThreadControlCapabilities() noexcept {
#if defined(__ANDROID__)
    // Bionic does not expose the pthread affinity entry points used by this adapter.
    return {false, true, true, true, 0U};
#elif defined(__linux__)
    return {true, true, true, true, CPU_SETSIZE};
#elif defined(_WIN32)
    ThreadControlCapabilities capabilities;
    capabilities.priority = true;
    capabilities.realtime = GetPriorityClass(GetCurrentProcess()) == REALTIME_PRIORITY_CLASS;
#if defined(_MSC_VER)
    capabilities.threadObject = true; // MinGW's pthread native handle is NOT a Windows HANDLE
#endif
#if defined(_WIN64) && _WIN32_WINNT >= 0x0601
    capabilities.affinity = true;
    capabilities.cpuIndexLimit = static_cast<std::uint32_t>(GetActiveProcessorGroupCount()) * 64U;
#endif
    return capabilities;
#else
    return {};
#endif
}
ThreadControlResult SetCurrentThreadAffinity(std::span<const std::uint32_t> cpus) noexcept {
    if (cpus.empty())
        return {ThreadControlCode::InvalidArgument, 0U};
#if defined(__linux__)
    return SetAffinity(pthread_self(), cpus);
#elif defined(_WIN32)
    return SetAffinity(GetCurrentThread(), cpus);
#else
    return {};
#endif
}
ThreadControlResult GetCurrentThreadAffinity(std::vector<std::uint32_t> &out) {
#if defined(__linux__)
    return GetAffinity(pthread_self(), out);
#elif defined(_WIN32)
    return GetAffinity(GetCurrentThread(), out);
#else
    static_cast<void>(out);
    return {};
#endif
}
ThreadControlResult SetCurrentThreadPriority(ThreadPriority p) noexcept {
    if (!ValidPriority(p))
        return {ThreadControlCode::InvalidArgument, 0U};
#if defined(__linux__)
    return SetPriority(pthread_self(), p);
#elif defined(_WIN32)
    return SetPriority(GetCurrentThread(), p);
#else
    return {};
#endif
}
ThreadControlResult GetCurrentThreadPriority(ThreadPriority &out) noexcept {
#if defined(__linux__)
    return GetPriority(pthread_self(), out);
#elif defined(_WIN32)
    return GetPriority(GetCurrentThread(), out);
#else
    static_cast<void>(out);
    return {};
#endif
}
ThreadControlResult SetThreadAffinity(std::thread &t, std::span<const std::uint32_t> cpus) noexcept {
    if (!t.joinable())
        return {ThreadControlCode::InvalidThread, 0U};
    if (cpus.empty())
        return {ThreadControlCode::InvalidArgument, 0U};
#if defined(__linux__) || (defined(_WIN32) && defined(_MSC_VER))
    return SetAffinity(t.native_handle(), cpus);
#else
    return {};
#endif
}
ThreadControlResult GetThreadAffinity(std::thread &t, std::vector<std::uint32_t> &out) {
    if (!t.joinable())
        return {ThreadControlCode::InvalidThread, 0U};
#if defined(__linux__) || (defined(_WIN32) && defined(_MSC_VER))
    return GetAffinity(t.native_handle(), out);
#else
    static_cast<void>(out);
    return {};
#endif
}
ThreadControlResult SetThreadPriority(std::thread &t, ThreadPriority p) noexcept {
    if (!t.joinable())
        return {ThreadControlCode::InvalidThread, 0U};
    if (!ValidPriority(p))
        return {ThreadControlCode::InvalidArgument, 0U};
#if defined(__linux__) || (defined(_WIN32) && defined(_MSC_VER))
    return SetPriority(t.native_handle(), p);
#else
    return {};
#endif
}
ThreadControlResult GetThreadPriority(std::thread &t, ThreadPriority &out) noexcept {
    if (!t.joinable())
        return {ThreadControlCode::InvalidThread, 0U};
#if defined(__linux__) || (defined(_WIN32) && defined(_MSC_VER))
    return GetPriority(t.native_handle(), out);
#else
    static_cast<void>(out);
    return {};
#endif
}
} // namespace UVE::Threading
