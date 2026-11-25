// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "frame_scheduler.h"
#include "topological_order.h"

#include <algorithm>
#include <condition_variable>
#include <exception>
#include <limits>
#include <memory>
#include <mutex>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace UVE::Core {
namespace {

[[nodiscard]] std::size_t FindTaskIndex(std::span<const FrameTaskDefinition> tasks,
                                           FrameTaskId taskId) noexcept {
    for (std::size_t index = 0U; index < tasks.size(); ++index) {
        if (tasks[index].id == taskId) {
            return index;
        }
    }
    return tasks.size();
}

[[nodiscard]] bool IsValidDomain(FrameTaskDomain domain) noexcept {
    return static_cast<std::uint8_t>(domain) <
           static_cast<std::uint8_t>(FrameTaskDomain::Count);
}

[[nodiscard]] FrameTaskGraphMutationResult MakeMutationError(
    FrameTaskGraphMutationCode code, FrameTaskId taskId, const char* message) {
    return FrameTaskGraphMutationResult{code, taskId, message};
}

[[nodiscard]] FrameTaskGraphValidationResult MakeValidationError(
    FrameTaskGraphValidationCode code, FrameTaskId taskId, const char* message) {
    return FrameTaskGraphValidationResult{code, taskId, message};
}

[[nodiscard]] std::string ExceptionMessage() {
    try {
        throw;
    } catch (const std::exception& exception) {
        return exception.what();
    } catch (...) {
        return "unknown non-standard exception";
    }
}

struct SchedulerState final {
    explicit SchedulerState(const FrameTaskGraph& graphIn,
                               Threading::IThreadPool& threadPoolIn)
        : graph(graphIn), threadPool(threadPoolIn),
          remainingDependencies(graphIn.GetTaskCount(), 0U),
          scheduled(graphIn.GetTaskCount(), false), completed(graphIn.GetTaskCount(), false),
          dependents(graphIn.GetTaskCount()) {}

    const FrameTaskGraph& graph;
    Threading::IThreadPool& threadPool;
    std::vector<std::size_t> remainingDependencies;
    std::vector<bool> scheduled;
    std::vector<bool> completed;
    std::vector<std::vector<std::size_t>> dependents;
    std::mutex mutex;
    std::condition_variable condition;
    std::size_t pendingTaskCount = 0U;
    std::size_t completedTaskCount = 0U;
    bool failed = false;
    FrameScheduleResultCode failureCode = FrameScheduleResultCode::TaskFailed;
    std::string failureMessage;
};

void SubmitTask(const std::shared_ptr<SchedulerState>& state, std::size_t taskIndex);

void MarkSubmissionFailure(const std::shared_ptr<SchedulerState>& state,
                              const std::string& message) {
    std::lock_guard<std::mutex> lock(state->mutex);
    state->failed = true;
    state->failureCode = FrameScheduleResultCode::SubmissionFailed;
    state->failureMessage = message;
    if (state->pendingTaskCount > 0U) {
        --state->pendingTaskCount;
    }
    state->condition.notify_all();
}

void CompleteTask(const std::shared_ptr<SchedulerState>& state, std::size_t taskIndex,
                     bool actionSucceeded, const std::string& actionError) {
    std::vector<std::size_t> readyTasks;
    {
        std::lock_guard<std::mutex> lock(state->mutex);
        if (state->pendingTaskCount > 0U) {
            --state->pendingTaskCount;
        }
        if (!actionSucceeded) {
            state->failed = true;
            state->failureCode = FrameScheduleResultCode::TaskFailed;
            state->failureMessage = actionError;
        } else if (!state->failed) {
            state->completed[taskIndex] = true;
            ++state->completedTaskCount;
            for (const std::size_t dependentIndex : state->dependents[taskIndex]) {
                if (state->remainingDependencies[dependentIndex] > 0U) {
                    --state->remainingDependencies[dependentIndex];
                }
                if (state->remainingDependencies[dependentIndex] == 0U &&
                    !state->scheduled[dependentIndex]) {
                    state->scheduled[dependentIndex] = true;
                    ++state->pendingTaskCount;
                    readyTasks.push_back(dependentIndex);
                }
            }
        }
        if (state->pendingTaskCount == 0U) {
            state->condition.notify_all();
        }
    }

    for (const std::size_t readyTask : readyTasks) {
        SubmitTask(state, readyTask);
    }
}

void ExecuteTask(const std::shared_ptr<SchedulerState>& state, std::size_t taskIndex) {
    {
        std::lock_guard<std::mutex> lock(state->mutex);
        if (state->failed) {
            if (state->pendingTaskCount > 0U) {
                --state->pendingTaskCount;
            }
            if (state->pendingTaskCount == 0U) {
                state->condition.notify_all();
            }
            return;
        }
    }

    bool actionSucceeded = true;
    std::string actionError;
    try {
        state->graph.GetTasks()[taskIndex].action();
    } catch (...) {
        actionSucceeded = false;
        actionError = ExceptionMessage();
    }
    CompleteTask(state, taskIndex, actionSucceeded, actionError);
}

void SubmitTask(const std::shared_ptr<SchedulerState>& state, std::size_t taskIndex) {
    try {
        state->threadPool.Submit(
            [state, taskIndex]() { ExecuteTask(state, taskIndex); });
    } catch (...) {
        MarkSubmissionFailure(state, ExceptionMessage());
    }
}

} // namespace

FrameTaskGraphMutationResult FrameTaskGraph::AddTask(FrameTaskDefinition task) {
    if (m_tasks.Full()) {
        return MakeMutationError(FrameTaskGraphMutationCode::CapacityExceeded, task.id,
                                    "frame task graph exceeds its bounded task capacity");
    }
    if (task.id == 0U) {
        return MakeMutationError(FrameTaskGraphMutationCode::InvalidId, task.id,
                                    "frame task identifier must be non-zero");
    }
    if (FindTaskIndex(m_tasks.AsSpan(), task.id) != m_tasks.Size()) {
        return MakeMutationError(FrameTaskGraphMutationCode::DuplicateId, task.id,
                                    "frame task identifier must be unique");
    }
    if (!IsValidDomain(task.domain)) {
        return MakeMutationError(FrameTaskGraphMutationCode::InvalidDomain, task.id,
                                    "frame task domain is invalid");
    }
    if (task.name.empty() || task.name.size() > kMaximumTaskNameBytes) {
        return MakeMutationError(FrameTaskGraphMutationCode::InvalidName, task.id,
                                    "frame task name is empty or too long");
    }
    if (!task.action) {
        return MakeMutationError(FrameTaskGraphMutationCode::InvalidAction, task.id,
                                    "frame task action must be callable");
    }
    if (task.dependencies.Size() > kMaximumDependenciesPerTask) {
        return MakeMutationError(FrameTaskGraphMutationCode::TooManyDependencies, task.id,
                                    "frame task dependency list exceeds its bounded capacity");
    }
    for (std::size_t index = 0U; index < task.dependencies.Size(); ++index) {
        const FrameTaskId dependencyId = task.dependencies[index];
        if (dependencyId == 0U || dependencyId == task.id) {
            return MakeMutationError(FrameTaskGraphMutationCode::InvalidDependency, task.id,
                                        "frame task dependency identifier is invalid");
        }
        bool duplicateDependency = false;
        for (std::size_t previousIndex = 0U; previousIndex < index; ++previousIndex) {
            if (task.dependencies[previousIndex] == dependencyId) {
                duplicateDependency = true;
                break;
            }
        }
        if (duplicateDependency) {
            return MakeMutationError(FrameTaskGraphMutationCode::DuplicateDependency, task.id,
                                        "frame task dependency identifiers must be unique");
        }
    }

    m_tasks.PushBack(std::move(task));
    return FrameTaskGraphMutationResult{FrameTaskGraphMutationCode::Accepted,
                                           m_tasks.Back().id, "accepted"};
}

FrameTaskGraphValidationResult FrameTaskGraph::Validate() const {
    if (m_tasks.Empty()) {
        return MakeValidationError(FrameTaskGraphValidationCode::EmptyGraph, 0U,
                                      "frame task graph must contain at least one task");
    }

    for (const FrameTaskDefinition& task : m_tasks) {
        if (task.id == 0U || !IsValidDomain(task.domain) || task.name.empty() ||
            task.name.size() > kMaximumTaskNameBytes || !task.action ||
            task.dependencies.Size() > kMaximumDependenciesPerTask) {
            return MakeValidationError(FrameTaskGraphValidationCode::InvalidTask, task.id,
                                          "frame task contains invalid metadata or action");
        }
        for (const FrameTaskId dependencyId : task.dependencies) {
            if (FindTaskIndex(m_tasks.AsSpan(), dependencyId) == m_tasks.Size()) {
                return MakeValidationError(FrameTaskGraphValidationCode::UnknownDependency,
                                              task.id, "frame task references an unknown dependency");
            }
        }
    }

    std::vector<std::uint8_t> colors(m_tasks.Size(), 0U);
    const auto visit = [&](const auto& self, std::size_t taskIndex) -> bool {
        if (colors[taskIndex] == 1U) {
            return false;
        }
        if (colors[taskIndex] == 2U) {
            return true;
        }
        colors[taskIndex] = 1U;
        for (const FrameTaskId dependencyId : m_tasks[taskIndex].dependencies) {
            const std::size_t dependencyIndex = FindTaskIndex(m_tasks.AsSpan(), dependencyId);
            if (dependencyIndex == m_tasks.Size() || !self(self, dependencyIndex)) {
                return false;
            }
        }
        colors[taskIndex] = 2U;
        return true;
    };

    for (std::size_t index = 0U; index < m_tasks.Size(); ++index) {
        if (!visit(visit, index)) {
            return MakeValidationError(FrameTaskGraphValidationCode::CyclicDependency,
                                          m_tasks[index].id, "frame task graph contains a cycle");
        }
    }
    return FrameTaskGraphValidationResult{FrameTaskGraphValidationCode::Valid, 0U, "valid"};
}

FrameTaskGraphValidationResult FrameTaskGraph::BuildExecutionOrder(
    std::vector<FrameTaskId>& outOrder) const {
    const auto validation = Validate();
    if (!validation.IsValid()) return validation;
    const auto tasks = GetTasks();
    std::vector<std::vector<std::uint32_t>> edges(tasks.size());
    for (std::size_t index = 0U; index < tasks.size(); ++index) {
        for (auto id : tasks[index].dependencies) {
            edges[index].push_back(static_cast<std::uint32_t>(FindTaskIndex(tasks, id)));
        }
    }
    std::vector<std::uint32_t> indices;
    if (Utilities::BuildTopologicalOrder(edges, indices) != Utilities::TopologicalOrderCode::Ordered) {
        return MakeValidationError(FrameTaskGraphValidationCode::CyclicDependency, 0U,
                                      "frame dependency planning failed");
    }
    std::vector<FrameTaskId> order;
    order.reserve(indices.size());
    for (auto index : indices) order.push_back(tasks[index].id);
    outOrder = std::move(order);
    return validation;
}

FrameScheduleResult FrameScheduler::ExecuteSerial(const FrameTaskGraph& graph) {
    std::vector<FrameTaskId> order;
    const auto validation = graph.BuildExecutionOrder(order);
    if (!validation.IsValid()) {
        return {FrameScheduleResultCode::InvalidGraph, 0U, graph.GetTaskCount(), validation.message};
    }
    std::size_t completed = 0U;
    try {
        for (const auto id : order) {
            graph.GetTasks()[FindTaskIndex(graph.GetTasks(), id)].action();
            ++completed;
        }
    } catch (...) {
        return {FrameScheduleResultCode::TaskFailed, completed, order.size(), ExceptionMessage()};
    }
    return {FrameScheduleResultCode::Completed, completed, order.size(), "completed"};
}

FrameScheduleResult FrameScheduler::Execute(const FrameTaskGraph& graph) const {
    const FrameTaskGraphValidationResult validation = graph.Validate();
    if (!validation.IsValid()) {
        return FrameScheduleResult{FrameScheduleResultCode::InvalidGraph, 0U,
                                      graph.GetTaskCount(), validation.message};
    }

    const std::shared_ptr<SchedulerState> state =
        std::make_shared<SchedulerState>(graph, m_threadPool);
    std::vector<std::size_t> rootTasks;
    for (std::size_t index = 0U; index < graph.GetTasks().size(); ++index) {
        const FrameTaskDefinition& task = graph.GetTasks()[index];
        state->remainingDependencies[index] = task.dependencies.Size();
        for (const FrameTaskId dependencyId : task.dependencies) {
            const std::size_t dependencyIndex = FindTaskIndex(graph.GetTasks(), dependencyId);
            state->dependents[dependencyIndex].push_back(index);
        }
        if (task.dependencies.Empty()) {
            rootTasks.push_back(index);
        }
    }

    {
        std::lock_guard<std::mutex> lock(state->mutex);
        state->pendingTaskCount = rootTasks.size();
        for (const std::size_t rootTask : rootTasks) {
            state->scheduled[rootTask] = true;
        }
    }
    for (const std::size_t rootTask : rootTasks) {
        SubmitTask(state, rootTask);
    }

    std::unique_lock<std::mutex> lock(state->mutex);
    state->condition.wait(lock, [&state]() { return state->pendingTaskCount == 0U; });
    const FrameScheduleResultCode resultCode =
        state->failed ? state->failureCode : FrameScheduleResultCode::Completed;
    const std::string message = state->failed ? state->failureMessage : "completed";
    return FrameScheduleResult{resultCode, state->completedTaskCount,
                                  graph.GetTaskCount(), message};
}

} // namespace UVE::Core
