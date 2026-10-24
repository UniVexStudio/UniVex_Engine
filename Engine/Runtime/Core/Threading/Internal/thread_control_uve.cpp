// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#endif
#endif
#include "uve/threading/thread_control_uve.h"
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
bool ValidPriorityUVE(ThreadPriorityUVE p) noexcept {
    return p == ThreadPriorityUVE::Normal || p == ThreadPriorityUVE::Background ||
           p == ThreadPriorityUVE::Realtime;
}
#if defined(__linux__)
ThreadControlResultUVE ResultUVE(int error) noexcept {
    ThreadControlCodeUVE code = ThreadControlCodeUVE::SystemError;
    switch (error) {
    case 0:
        code = ThreadControlCodeUVE::Applied;
        break;
    case ESRCH:
        code = ThreadControlCodeUVE::InvalidThread;
        break;
    case EINVAL:
        code = ThreadControlCodeUVE::InvalidArgument;
        break;
    case EPERM:
    case EACCES:
        code = ThreadControlCodeUVE::PermissionDenied;
        break;
    case ENOTSUP:
        code = ThreadControlCodeUVE::Unsupported;
        break;
    default:
        break;
    }
    return {code, static_cast<std::uint32_t>(error)};
}
ThreadControlResultUVE SetAffinityUVE(pthread_t thread, std::span<const std::uint32_t> cpus) noexcept {
#if defined(__ANDROID__)
    static_cast<void>(thread);
    static_cast<void>(cpus);
    return {};
#else
    cpu_set_t mask;
    CPU_ZERO(&mask);
    for (auto cpu : cpus) {
        if (cpu >= CPU_SETSIZE || CPU_ISSET(cpu, &mask))
            return {ThreadControlCodeUVE::InvalidArgument, 0U};
        CPU_SET(cpu, &mask);
    }
    return ResultUVE(pthread_setaffinity_np(thread, sizeof(mask), &mask));
#endif
}
ThreadControlResultUVE GetAffinityUVE(pthread_t thread, std::vector<std::uint32_t> &out) {
#if defined(__ANDROID__)
    static_cast<void>(thread);
    static_cast<void>(out);
    return {};
#else
    cpu_set_t mask;
    CPU_ZERO(&mask);
    const auto result = ResultUVE(pthread_getaffinity_np(thread, sizeof(mask), &mask));
    if (!result.IsAppliedUVE())
        return result;
    std::vector<std::uint32_t> candidate;
    for (std::uint32_t cpu = 0U; cpu < CPU_SETSIZE; ++cpu)
        if (CPU_ISSET(cpu, &mask))
            candidate.push_back(cpu);
    out = std::move(candidate);
    return result;
#endif
}
ThreadControlResultUVE SetPriorityUVE(pthread_t thread, ThreadPriorityUVE priority) noexcept {
    int policy = SCHED_OTHER;
    sched_param parameters{};
    if (priority == ThreadPriorityUVE::Background)
        policy = SCHED_BATCH;
    if (priority == ThreadPriorityUVE::Realtime) {
        policy = SCHED_RR;
        parameters.sched_priority = sched_get_priority_min(policy);
        if (parameters.sched_priority == -1)
            return ResultUVE(errno);
    }
    return ResultUVE(pthread_setschedparam(thread, policy, &parameters));
}
ThreadControlResultUVE GetPriorityUVE(pthread_t thread, ThreadPriorityUVE &out) noexcept {
    int policy = 0;
    sched_param parameters{};
    const auto result = ResultUVE(pthread_getschedparam(thread, &policy, &parameters));
    if (!result.IsAppliedUVE())
        return result;
    switch (policy) {
    case SCHED_OTHER:
        out = ThreadPriorityUVE::Normal;
        break;
    case SCHED_BATCH:
        out = ThreadPriorityUVE::Background;
        break;
    case SCHED_RR:
        out = ThreadPriorityUVE::Realtime;
        break;
    default:
        return {};
    }
    return result;
}
#elif defined(_WIN32)
ThreadControlResultUVE ResultUVE(DWORD error) noexcept {
    ThreadControlCodeUVE code = ThreadControlCodeUVE::SystemError;
    switch (error) {
    case ERROR_SUCCESS:
        code = ThreadControlCodeUVE::Applied;
        break;
    case ERROR_INVALID_HANDLE:
        code = ThreadControlCodeUVE::InvalidThread;
        break;
    case ERROR_INVALID_PARAMETER:
        code = ThreadControlCodeUVE::InvalidArgument;
        break;
    case ERROR_ACCESS_DENIED:
    case ERROR_PRIVILEGE_NOT_HELD:
        code = ThreadControlCodeUVE::PermissionDenied;
        break;
    case ERROR_NOT_SUPPORTED:
        code = ThreadControlCodeUVE::Unsupported;
        break;
    default:
        break;
    }
    return {code, error};
}
ThreadControlResultUVE SetAffinityUVE(HANDLE thread, std::span<const std::uint32_t> cpus) noexcept {
#if defined(_WIN64) && _WIN32_WINNT >= 0x0601
    GROUP_AFFINITY mask{};
    const auto group = cpus.front() / 64U;
    if (group >= GetActiveProcessorGroupCount())
        return {ThreadControlCodeUVE::InvalidArgument, 0U};
    mask.Group = static_cast<WORD>(group);
    for (auto cpu : cpus) {
        const auto bit = KAFFINITY{1} << (cpu % 64U);
        if (cpu / 64U != group || (mask.Mask & bit) != 0U)
            return {ThreadControlCodeUVE::InvalidArgument, 0U};
        mask.Mask |= bit;
    }
    return ResultUVE(SetThreadGroupAffinity(thread, &mask, nullptr) ? ERROR_SUCCESS : GetLastError());
#else
    static_cast<void>(thread);
    static_cast<void>(cpus);
    return {};
#endif
}
ThreadControlResultUVE GetAffinityUVE(HANDLE thread, std::vector<std::uint32_t> &out) {
#if defined(_WIN64) && _WIN32_WINNT >= 0x0601
    GROUP_AFFINITY mask{};
    if (!GetThreadGroupAffinity(thread, &mask))
        return ResultUVE(GetLastError());
    std::vector<std::uint32_t> candidate;
    for (std::uint32_t bit = 0U; bit < 64U; ++bit)
        if ((mask.Mask & (KAFFINITY{1} << bit)) != 0U) {
            candidate.push_back(static_cast<std::uint32_t>(mask.Group) * 64U + bit);
        }
    out = std::move(candidate);
    return ResultUVE(ERROR_SUCCESS);
#else
    static_cast<void>(thread);
    static_cast<void>(out);
    return {};
#endif
}
ThreadControlResultUVE SetPriorityUVE(HANDLE thread, ThreadPriorityUVE priority) noexcept {
    int native = THREAD_PRIORITY_NORMAL;
    if (priority == ThreadPriorityUVE::Background)
        native = THREAD_PRIORITY_BELOW_NORMAL;
    if (priority == ThreadPriorityUVE::Realtime) {
        if (GetPriorityClass(GetCurrentProcess()) != REALTIME_PRIORITY_CLASS)
            return {};
        native = THREAD_PRIORITY_TIME_CRITICAL;
    }
    return ResultUVE(SetThreadPriority(thread, native) ? ERROR_SUCCESS : GetLastError());
}
ThreadControlResultUVE GetPriorityUVE(HANDLE thread, ThreadPriorityUVE &out) noexcept {
    const int native = GetThreadPriority(thread);
    if (native == THREAD_PRIORITY_ERROR_RETURN)
        return ResultUVE(GetLastError());
    if (native == THREAD_PRIORITY_NORMAL)
        out = ThreadPriorityUVE::Normal;
    else if (native == THREAD_PRIORITY_BELOW_NORMAL)
        out = ThreadPriorityUVE::Background;
    else if (native == THREAD_PRIORITY_TIME_CRITICAL &&
             GetPriorityClass(GetCurrentProcess()) == REALTIME_PRIORITY_CLASS)
        out = ThreadPriorityUVE::Realtime;
    else
        return {}; // do not lie about an externally selected class
    return ResultUVE(ERROR_SUCCESS);
}
#endif
} // namespace
ThreadControlCapabilitiesUVE GetThreadControlCapabilitiesUVE() noexcept {
#if defined(__ANDROID__)
    // Bionic does not expose the pthread affinity entry points used by this adapter.
    return {false, true, true, true, 0U};
#elif defined(__linux__)
    return {true, true, true, true, CPU_SETSIZE};
#elif defined(_WIN32)
    ThreadControlCapabilitiesUVE capabilities;
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
ThreadControlResultUVE SetCurrentThreadAffinityUVE(std::span<const std::uint32_t> cpus) noexcept {
    if (cpus.empty())
        return {ThreadControlCodeUVE::InvalidArgument, 0U};
#if defined(__linux__)
    return SetAffinityUVE(pthread_self(), cpus);
#elif defined(_WIN32)
    return SetAffinityUVE(GetCurrentThread(), cpus);
#else
    return {};
#endif
}
ThreadControlResultUVE GetCurrentThreadAffinityUVE(std::vector<std::uint32_t> &out) {
#if defined(__linux__)
    return GetAffinityUVE(pthread_self(), out);
#elif defined(_WIN32)
    return GetAffinityUVE(GetCurrentThread(), out);
#else
    static_cast<void>(out);
    return {};
#endif
}
ThreadControlResultUVE SetCurrentThreadPriorityUVE(ThreadPriorityUVE p) noexcept {
    if (!ValidPriorityUVE(p))
        return {ThreadControlCodeUVE::InvalidArgument, 0U};
#if defined(__linux__)
    return SetPriorityUVE(pthread_self(), p);
#elif defined(_WIN32)
    return SetPriorityUVE(GetCurrentThread(), p);
#else
    return {};
#endif
}
ThreadControlResultUVE GetCurrentThreadPriorityUVE(ThreadPriorityUVE &out) noexcept {
#if defined(__linux__)
    return GetPriorityUVE(pthread_self(), out);
#elif defined(_WIN32)
    return GetPriorityUVE(GetCurrentThread(), out);
#else
    static_cast<void>(out);
    return {};
#endif
}
ThreadControlResultUVE SetThreadAffinityUVE(std::thread &t, std::span<const std::uint32_t> cpus) noexcept {
    if (!t.joinable())
        return {ThreadControlCodeUVE::InvalidThread, 0U};
    if (cpus.empty())
        return {ThreadControlCodeUVE::InvalidArgument, 0U};
#if defined(__linux__) || (defined(_WIN32) && defined(_MSC_VER))
    return SetAffinityUVE(t.native_handle(), cpus);
#else
    return {};
#endif
}
ThreadControlResultUVE GetThreadAffinityUVE(std::thread &t, std::vector<std::uint32_t> &out) {
    if (!t.joinable())
        return {ThreadControlCodeUVE::InvalidThread, 0U};
#if defined(__linux__) || (defined(_WIN32) && defined(_MSC_VER))
    return GetAffinityUVE(t.native_handle(), out);
#else
    static_cast<void>(out);
    return {};
#endif
}
ThreadControlResultUVE SetThreadPriorityUVE(std::thread &t, ThreadPriorityUVE p) noexcept {
    if (!t.joinable())
        return {ThreadControlCodeUVE::InvalidThread, 0U};
    if (!ValidPriorityUVE(p))
        return {ThreadControlCodeUVE::InvalidArgument, 0U};
#if defined(__linux__) || (defined(_WIN32) && defined(_MSC_VER))
    return SetPriorityUVE(t.native_handle(), p);
#else
    return {};
#endif
}
ThreadControlResultUVE GetThreadPriorityUVE(std::thread &t, ThreadPriorityUVE &out) noexcept {
    if (!t.joinable())
        return {ThreadControlCodeUVE::InvalidThread, 0U};
#if defined(__linux__) || (defined(_WIN32) && defined(_MSC_VER))
    return GetPriorityUVE(t.native_handle(), out);
#else
    static_cast<void>(out);
    return {};
#endif
}
} // namespace UVE::Threading
