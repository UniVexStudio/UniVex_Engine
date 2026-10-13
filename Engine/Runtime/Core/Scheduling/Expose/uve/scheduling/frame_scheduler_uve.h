// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include "uve/containers/fixed_array_uve.h"
#include "uve/containers/small_vector_uve.h"
#include "uve/threading/i_thread_pool_uve.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <span>
#include <string>

/// The frame scheduler: a small, validated, domain-tagged task graph for per-frame work, one
/// level above `Threading::JobGraphUVE`.
///
/// WHY TWO GRAPHS. `JobGraphUVE` is the general dependency DAG (asset import, shader
/// preprocessing — ad-hoc jobs with ad-hoc lifetimes). `FrameTaskGraphUVE` is the frame's
/// fixed plan: up to 256 named tasks (`FixedArrayUVE`-capped, so the plan never allocates),
/// each tagged with a `FrameTaskDomainUVE` (Animation, ECS, Physics, ...) and validated up
/// front with result codes instead of asserts. `FrameSchedulerUVE` then executes the plan on a
/// `Threading::IThreadPoolUVE`. The split is lifetime and vocabulary: one-shot job cascades go
/// to the job graph, the repeating frame plan goes here.
///
/// HONEST STATUS. The graph and scheduler are built, validated and tested — and almost nothing
/// claims them yet: `EngineCoreUVE`'s Update/LateUpdate chain is still single-threaded
/// main-thread code, and the only live pool users are assets, particles and shaders. Making a
/// system parallel is therefore a wiring problem (express its frame work as tasks and execute),
/// not a missing-infrastructure problem.
namespace UVE::Core {

enum class FrameTaskDomainUVE : std::uint8_t {
    Animation = 0,
    ECS,
    RenderPreparation,
    Physics,
    Streaming,
    Audio,
    Assets,
    Scripting,
    Editor,
    Count,
};

using FrameTaskIdUVE = std::uint32_t;

struct FrameTaskDefinitionUVE final {
    FrameTaskIdUVE id = 0U;
    FrameTaskDomainUVE domain = FrameTaskDomainUVE::Animation;
    std::string name;
    // Inline capacity == kMaximumDependenciesPerTaskUVE (a literal: this struct precedes the
    // constant). AddTaskUVE rejects longer lists before storing, so accepted tasks never spill
    // and task dependency lists never heap-allocate.
    Containers::SmallVectorUVE<FrameTaskIdUVE, 32U> dependencies;
    std::function<void()> action;
};

enum class FrameTaskGraphMutationCodeUVE : std::uint8_t {
    Accepted = 0,
    CapacityExceeded,
    InvalidId,
    DuplicateId,
    InvalidDomain,
    InvalidName,
    InvalidAction,
    TooManyDependencies,
    InvalidDependency,
    DuplicateDependency,
};

struct FrameTaskGraphMutationResultUVE final {
    FrameTaskGraphMutationCodeUVE code = FrameTaskGraphMutationCodeUVE::InvalidId;
    FrameTaskIdUVE taskId = 0U;
    std::string message;

    [[nodiscard]] bool IsAcceptedUVE() const noexcept {
        return code == FrameTaskGraphMutationCodeUVE::Accepted;
    }
};

enum class FrameTaskGraphValidationCodeUVE : std::uint8_t {
    Valid = 0,
    EmptyGraph,
    InvalidTask,
    UnknownDependency,
    CyclicDependency,
};

struct FrameTaskGraphValidationResultUVE final {
    FrameTaskGraphValidationCodeUVE code = FrameTaskGraphValidationCodeUVE::EmptyGraph;
    FrameTaskIdUVE taskId = 0U;
    std::string message;

    [[nodiscard]] bool IsValidUVE() const noexcept {
        return code == FrameTaskGraphValidationCodeUVE::Valid;
    }
};

class FrameTaskGraphUVE final {
public:
    static constexpr std::size_t kMaximumTasksUVE = 256U;
    static constexpr std::size_t kMaximumDependenciesPerTaskUVE = 32U;
    // Pinned: the dependencies member above inlines exactly this many ids, so drift here would
    // silently change spill behavior.
    static_assert(kMaximumDependenciesPerTaskUVE == 32U);
    static constexpr std::size_t kMaximumTaskNameBytesUVE = 128U;

    [[nodiscard]] FrameTaskGraphMutationResultUVE AddTaskUVE(FrameTaskDefinitionUVE task);
    [[nodiscard]] FrameTaskGraphValidationResultUVE ValidateUVE() const;

    [[nodiscard]] std::size_t GetTaskCountUVE() const noexcept {
        return m_tasks.SizeUVE();
    }

    [[nodiscard]] std::span<const FrameTaskDefinitionUVE> GetTasksUVE() const noexcept {
        return m_tasks.AsSpanUVE();
    }

private:
    Containers::FixedArrayUVE<FrameTaskDefinitionUVE, kMaximumTasksUVE> m_tasks;
};

enum class FrameScheduleResultCodeUVE : std::uint8_t {
    Completed = 0,
    InvalidGraph,
    TaskFailed,
    SubmissionFailed,
};

struct FrameScheduleResultUVE final {
    FrameScheduleResultCodeUVE code = FrameScheduleResultCodeUVE::InvalidGraph;
    std::size_t completedTaskCount = 0U;
    std::size_t taskCount = 0U;
    std::string message;

    [[nodiscard]] bool IsCompletedUVE() const noexcept {
        return code == FrameScheduleResultCodeUVE::Completed;
    }
};

class FrameSchedulerUVE final {
public:
    explicit FrameSchedulerUVE(Threading::IThreadPoolUVE& threadPool) noexcept
        : m_threadPool(threadPool) {}

    FrameSchedulerUVE(const FrameSchedulerUVE&) = delete;
    FrameSchedulerUVE& operator=(const FrameSchedulerUVE&) = delete;

    [[nodiscard]] FrameScheduleResultUVE ExecuteUVE(const FrameTaskGraphUVE& graph) const;

private:
    Threading::IThreadPoolUVE& m_threadPool;
};

} // namespace UVE::Core
