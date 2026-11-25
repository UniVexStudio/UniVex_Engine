// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include "fixed_array.h"
#include "small_vector.h"
#include "i_thread_pool.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <span>
#include <string>
#include <vector>

/// The frame scheduler: a small, validated, domain-tagged task graph for per-frame work, one
/// level above `Threading::JobGraph`.
///
/// WHY TWO GRAPHS. `JobGraph` is the general dependency DAG (asset import, shader
/// preprocessing — ad-hoc jobs with ad-hoc lifetimes). `FrameTaskGraph` is the frame's
/// fixed plan: up to 256 named tasks (`FixedArray`-capped, so the plan never allocates),
/// each tagged with a `FrameTaskDomain` (Animation, ECS, Physics, ...) and validated up
/// front with result codes instead of asserts. `FrameScheduler` then executes the plan on a
/// `Threading::IThreadPool`. The split is lifetime and vocabulary: one-shot job cascades go
/// to the job graph, the repeating frame plan goes here.
///
/// HONEST STATUS. The graph and scheduler are built, validated and tested — and almost nothing
/// claims them yet: `EngineCore`'s Update/LateUpdate chain is still single-threaded
/// main-thread code, and the only live pool users are assets, particles and shaders. Making a
/// system parallel is therefore a wiring problem (express its frame work as tasks and execute),
/// not a missing-infrastructure problem.
namespace UVE::Core {

enum class FrameTaskDomain : std::uint8_t {
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

using FrameTaskId = std::uint32_t;

struct FrameTaskDefinition final {
    FrameTaskId id = 0U;
    FrameTaskDomain domain = FrameTaskDomain::Animation;
    std::string name;
    // Inline capacity == kMaximumDependenciesPerTask (a literal: this struct precedes the
    // constant). AddTask rejects longer lists before storing, so accepted tasks never spill
    // and task dependency lists never heap-allocate.
    Containers::SmallVector<FrameTaskId, 32U> dependencies;
    std::function<void()> action;
};

enum class FrameTaskGraphMutationCode : std::uint8_t {
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

struct FrameTaskGraphMutationResult final {
    FrameTaskGraphMutationCode code = FrameTaskGraphMutationCode::InvalidId;
    FrameTaskId taskId = 0U;
    std::string message;

    [[nodiscard]] bool IsAccepted() const noexcept {
        return code == FrameTaskGraphMutationCode::Accepted;
    }
};

enum class FrameTaskGraphValidationCode : std::uint8_t {
    Valid = 0,
    EmptyGraph,
    InvalidTask,
    UnknownDependency,
    CyclicDependency,
};

struct FrameTaskGraphValidationResult final {
    FrameTaskGraphValidationCode code = FrameTaskGraphValidationCode::EmptyGraph;
    FrameTaskId taskId = 0U;
    std::string message;

    [[nodiscard]] bool IsValid() const noexcept {
        return code == FrameTaskGraphValidationCode::Valid;
    }
};

class FrameTaskGraph final {
public:
    static constexpr std::size_t kMaximumTasks = 256U;
    static constexpr std::size_t kMaximumDependenciesPerTask = 32U;
    // Pinned: the dependencies member above inlines exactly this many ids, so drift here would
    // silently change spill behavior.
    static_assert(kMaximumDependenciesPerTask == 32U);
    static constexpr std::size_t kMaximumTaskNameBytes = 128U;

    [[nodiscard]] FrameTaskGraphMutationResult AddTask(FrameTaskDefinition task);
    [[nodiscard]] FrameTaskGraphValidationResult Validate() const;
    /// Stable topological order: choose the earliest inserted ready task on each step.
    /// Output is unchanged for invalid graphs; no action is invoked during planning.
    [[nodiscard]] FrameTaskGraphValidationResult BuildExecutionOrder(
        std::vector<FrameTaskId>& outOrder) const;

    [[nodiscard]] std::size_t GetTaskCount() const noexcept {
        return m_tasks.Size();
    }

    [[nodiscard]] std::span<const FrameTaskDefinition> GetTasks() const noexcept {
        return m_tasks.AsSpan();
    }

private:
    Containers::FixedArray<FrameTaskDefinition, kMaximumTasks> m_tasks;
};

enum class FrameScheduleResultCode : std::uint8_t {
    Completed = 0,
    InvalidGraph,
    TaskFailed,
    SubmissionFailed,
};

struct FrameScheduleResult final {
    FrameScheduleResultCode code = FrameScheduleResultCode::InvalidGraph;
    std::size_t completedTaskCount = 0U;
    std::size_t taskCount = 0U;
    std::string message;

    [[nodiscard]] bool IsCompleted() const noexcept {
        return code == FrameScheduleResultCode::Completed;
    }
};

class FrameScheduler final {
public:
    explicit FrameScheduler(Threading::IThreadPool& threadPool) noexcept
        : m_threadPool(threadPool) {}

    FrameScheduler(const FrameScheduler&) = delete;
    FrameScheduler& operator=(const FrameScheduler&) = delete;

    [[nodiscard]] FrameScheduleResult Execute(const FrameTaskGraph& graph) const;
    /// Owner-thread execution of the same validated graph. Does not create a pool or imply
    /// parallel gameplay. Stops on an exception and reports the number of completed actions.
    [[nodiscard]] static FrameScheduleResult ExecuteSerial(const FrameTaskGraph& graph);

private:
    Threading::IThreadPool& m_threadPool;
};

} // namespace UVE::Core
