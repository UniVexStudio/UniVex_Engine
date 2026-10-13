// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
#include "uve/world/world_uve.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>
#include <stdexcept>
#include <utility>
#include <vector>
#include "uve/logging/assert_uve.h"
#include "uve/logging/logging_macros_uve.h"
#include "uve/object/object_registry_uve.h"
#include "uve/scheduling/frame_scheduler_uve.h"
#include "uve/utilities/scope_guard_uve.h"

namespace UVE::World {
struct WorldUVE::ImplUVE {
    ImplUVE(WorldUVE &ownerIn, Memory::IAllocatorUVE &allocatorIn, Events::IEventSystemUVE &events)
        : owner(ownerIn), allocator(allocatorIn) {
        auto primary = std::unique_ptr<SceneUVE>(new SceneUVE("Primary", allocator, &events));
        SceneHandleUVE handle;
        if (!registry.RegisterUVE(*primary, handle))
            throw std::logic_error("primary scene registration failed");
        scenes.push_back(std::move(primary));
    }
    WorldUVE &owner;
    Memory::IAllocatorUVE &allocator;
    Core::ObjectRegistryUVE registry;
    std::vector<std::unique_ptr<SceneUVE>> scenes;
    std::vector<WorldSystemDescUVE> systems;
    Core::FrameTaskGraphUVE plan;
    std::vector<std::size_t> lifecycleOrder;
    SceneUVE *currentScene = nullptr;
    float currentDelta = 0.0F;
    WorldStateUVE state = WorldStateUVE::Created;
    bool busy = false;
    std::uint64_t frameCount = 0U;
    float totalSeconds = 0.0F;

    WorldResultUVE CompileUVE() {
        std::vector<std::size_t> indices(systems.size());
        std::iota(indices.begin(), indices.end(), 0U);
        std::stable_sort(indices.begin(), indices.end(), [this](const auto a, const auto b) {
            if (systems[a].phase != systems[b].phase)
                return systems[a].phase < systems[b].phase;
            return systems[a].priority < systems[b].priority;
        });
        Core::FrameTaskGraphUVE candidate;
        const auto transformId = static_cast<Core::FrameTaskIdUVE>(systems.size() + 1U);
        const auto addTransforms = [&] {
            Core::FrameTaskDefinitionUVE task;
            task.id = transformId;
            task.domain = Core::FrameTaskDomainUVE::ECS;
            task.name = "World transform propagation";
            task.action = [this] { currentScene->m_graph.UpdateUVE(currentScene->m_entities); };
            return candidate.AddTaskUVE(std::move(task)).IsAcceptedUVE();
        };
        bool addedTransforms = false;
        for (const auto index : indices) {
            const auto &system = systems[index];
            if (!addedTransforms && system.phase == WorldPhaseUVE::AfterTransforms) {
                if (!addTransforms())
                    return WorldResultUVE::InvalidArgument;
                addedTransforms = true;
            }
            Core::FrameTaskDefinitionUVE task;
            task.id = static_cast<Core::FrameTaskIdUVE>(index + 1U);
            task.domain = Core::FrameTaskDomainUVE::ECS;
            task.name = system.name;
            for (const auto &name : system.after) {
                const auto dependency = std::find_if(systems.begin(), systems.end(),
                                                     [&](const auto &entry) { return entry.name == name; });
                if (dependency == systems.end())
                    return WorldResultUVE::MissingDependency;
                if (system.phase < dependency->phase)
                    return WorldResultUVE::InvalidArgument;
                task.dependencies.PushBackUVE(
                    static_cast<Core::FrameTaskIdUVE>(dependency - systems.begin() + 1));
            }
            task.action = [this, index] {
                WorldSceneContextUVE context{owner, *currentScene};
                systems[index].tick(context, currentDelta);
            };
            if (!candidate.AddTaskUVE(std::move(task)).IsAcceptedUVE())
                return WorldResultUVE::InvalidArgument;
        }
        if (!addedTransforms && !addTransforms())
            return WorldResultUVE::InvalidArgument;
        std::vector<Core::FrameTaskIdUVE> order;
        const auto validation = candidate.BuildExecutionOrderUVE(order);
        if (!validation.IsValidUVE())
            return WorldResultUVE::DependencyCycle;
        std::vector<std::size_t> starts;
        for (auto id : order)
            if (id != transformId)
                starts.push_back(static_cast<std::size_t>(id - 1U));
        plan = std::move(candidate);
        lifecycleOrder = std::move(starts);
        return WorldResultUVE::Succeeded;
    }
    bool StartSceneUVE(SceneUVE &scene) {
        WorldSceneContextUVE context{owner, scene};
        try {
            for (const auto index : lifecycleOrder) {
                ++scene.m_startedSystems; // stop also cleans a failing/partially initialized start
                if (systems[index].start && !systems[index].start(context))
                    return false;
            }
        } catch (...) {
            return false;
        }
        return true;
    }
    bool StopSceneUVE(SceneUVE &scene) noexcept {
        bool success = true;
        WorldSceneContextUVE context{owner, scene};
        while (scene.m_startedSystems != 0U) {
            const auto index = lifecycleOrder[--scene.m_startedSystems];
            try {
                if (systems[index].stop)
                    systems[index].stop(context);
            } catch (...) {
                success = false;
            }
        }
        return success;
    }
};

WorldUVE::WorldUVE(Memory::IAllocatorUVE &allocator, Events::IEventSystemUVE &events)
    : m_impl(std::make_unique<ImplUVE>(*this, allocator, events)) {}
WorldUVE::~WorldUVE() {
    if (ShutdownUVE() != WorldResultUVE::Succeeded)
        UVE_ERROR("WorldUVE: shutdown callback failed");
}
WorldResultUVE WorldUVE::RegisterSystemUVE(WorldSystemDescUVE system) {
    auto &impl = *m_impl;
    if (impl.busy)
        return WorldResultUVE::Busy;
    if (impl.state != WorldStateUVE::Created)
        return WorldResultUVE::InvalidState;
    if (system.name.empty() || system.name.size() > Core::FrameTaskGraphUVE::kMaximumTaskNameBytesUVE ||
        !system.tick || system.after.size() > Core::FrameTaskGraphUVE::kMaximumDependenciesPerTaskUVE ||
        (system.phase != WorldPhaseUVE::BeforeTransforms && system.phase != WorldPhaseUVE::AfterTransforms)) {
        return WorldResultUVE::InvalidArgument;
    }
    if (std::any_of(impl.systems.begin(), impl.systems.end(),
                    [&](const auto &item) { return item.name == system.name; })) {
        return WorldResultUVE::DuplicateName;
    }
    if (impl.systems.size() >= kMaximumSystemsUVE)
        return WorldResultUVE::CapacityExceeded;
    impl.systems.push_back(std::move(system));
    return WorldResultUVE::Succeeded;
}
WorldResultUVE WorldUVE::UnregisterSystemUVE(const std::string_view name) {
    auto &impl = *m_impl;
    if (impl.busy)
        return WorldResultUVE::Busy;
    if (impl.state != WorldStateUVE::Created)
        return WorldResultUVE::InvalidState;
    const auto entry = std::find_if(impl.systems.begin(), impl.systems.end(),
                                    [&](const auto &system) { return system.name == name; });
    if (entry == impl.systems.end())
        return WorldResultUVE::InvalidArgument;
    impl.systems.erase(entry);
    return WorldResultUVE::Succeeded;
}
WorldResultUVE WorldUVE::StartUVE() {
    auto &impl = *m_impl;
    if (impl.busy)
        return WorldResultUVE::Busy;
    if (impl.state == WorldStateUVE::Running)
        return WorldResultUVE::Succeeded;
    if (impl.state != WorldStateUVE::Created)
        return WorldResultUVE::InvalidState;
    const auto compiled = impl.CompileUVE();
    if (compiled != WorldResultUVE::Succeeded)
        return compiled;
    impl.busy = true;
    Utilities::ScopeGuardUVE done{[&] { impl.busy = false; }};
    for (auto &scene : impl.scenes) {
        if (impl.StartSceneUVE(*scene))
            continue;
        bool stopped = true;
        for (auto it = impl.scenes.rbegin(); it != impl.scenes.rend(); ++it) {
            if (!impl.StopSceneUVE(**it))
                stopped = false;
        }
        if (!stopped)
            impl.state = WorldStateUVE::Failed;
        return WorldResultUVE::InitializationFailed;
    }
    impl.state = WorldStateUVE::Running;
    return WorldResultUVE::Succeeded;
}
WorldResultUVE WorldUVE::TryTickUVE(const float delta) {
    auto &impl = *m_impl;
    if (impl.busy)
        return WorldResultUVE::Busy;
    if (!std::isfinite(delta) || delta < 0.0F || !std::isfinite(impl.totalSeconds + delta) ||
        impl.frameCount == std::numeric_limits<std::uint64_t>::max())
        return WorldResultUVE::InvalidArgument;
    const auto started = StartUVE();
    if (started != WorldResultUVE::Succeeded)
        return started;
    impl.busy = true;
    Utilities::ScopeGuardUVE done{[&] {
        impl.busy = false;
        impl.currentScene = nullptr;
    }};
    impl.currentDelta = delta;
    for (auto &scene : impl.scenes) {
        if (!scene->m_enabled)
            continue;
        impl.currentScene = scene.get();
        const auto result = Core::FrameSchedulerUVE::ExecuteSerialUVE(impl.plan);
        if (!result.IsCompletedUVE()) {
            impl.state = WorldStateUVE::Failed;
            return WorldResultUVE::SystemFailed;
        }
        ++scene->m_frameCount;
    }
    ++impl.frameCount;
    impl.totalSeconds += delta;
    return WorldResultUVE::Succeeded;
}
void WorldUVE::TickUVE(const float delta) {
    UVE_ASSERT(std::isfinite(delta));
    UVE_ASSERT(delta >= 0.0F);
    const float safe = std::isfinite(delta) && delta >= 0.0F ? delta : 0.0F;
    const auto result = TryTickUVE(safe);
    if (result != WorldResultUVE::Succeeded)
        UVE_ERROR("WorldUVE: tick failed ({})", static_cast<unsigned>(result));
}
WorldResultUVE WorldUVE::CreateSceneUVE(std::string name, SceneHandleUVE &outHandle,
                                        std::function<bool(SceneUVE &)> populate) {
    auto &impl = *m_impl;
    if (impl.busy)
        return WorldResultUVE::Busy;
    if (impl.state == WorldStateUVE::Stopped || impl.state == WorldStateUVE::Failed)
        return WorldResultUVE::InvalidState;
    if (name.empty() || name.size() > 128U)
        return WorldResultUVE::InvalidArgument;
    if (std::any_of(impl.scenes.begin(), impl.scenes.end(),
                    [&](const auto &scene) { return scene->m_name == name; }))
        return WorldResultUVE::DuplicateName;
    if (impl.scenes.size() >= kMaximumScenesUVE)
        return WorldResultUVE::CapacityExceeded;
    // Reserve before user initialization: a later vector allocation must not bypass stop callbacks.
    if (impl.scenes.size() == impl.scenes.capacity()) {
        impl.scenes.reserve(std::min(kMaximumScenesUVE, impl.scenes.size() * 2U));
    }
    auto scene = std::unique_ptr<SceneUVE>(new SceneUVE(std::move(name), impl.allocator, nullptr));
    SceneHandleUVE handle;
    if (!impl.registry.RegisterUVE(*scene, handle))
        return WorldResultUVE::InitializationFailed;
    impl.busy = true;
    Utilities::ScopeGuardUVE done{[&] { impl.busy = false; }};
    bool populated = true;
    try {
        if (populate)
            populated = populate(*scene);
    } catch (...) {
        populated = false;
    }
    if (!populated || (impl.state == WorldStateUVE::Running && !impl.StartSceneUVE(*scene))) {
        const bool stopped = impl.StopSceneUVE(*scene);
        static_cast<void>(impl.registry.UnregisterUVE(handle));
        scene->ClearEntitiesUVE();
        return stopped ? WorldResultUVE::InitializationFailed : WorldResultUVE::SystemFailed;
    }
    impl.scenes.push_back(std::move(scene));
    outHandle = handle;
    return WorldResultUVE::Succeeded;
}
WorldResultUVE WorldUVE::UnloadSceneUVE(const SceneHandleUVE handle) {
    auto &impl = *m_impl;
    if (impl.busy)
        return WorldResultUVE::Busy;
    auto *scene = GetSceneUVE(handle);
    if (scene == nullptr)
        return WorldResultUVE::UnknownScene;
    if (scene == impl.scenes.front().get())
        return WorldResultUVE::InvalidState;
    impl.busy = true;
    Utilities::ScopeGuardUVE done{[&] { impl.busy = false; }};
    bool stopped = impl.StopSceneUVE(*scene);
    static_cast<void>(impl.registry.UnregisterUVE(handle));
    try {
        scene->ClearEntitiesUVE();
    } catch (...) {
        stopped = false;
    }
    std::erase_if(impl.scenes, [scene](const auto &entry) { return entry.get() == scene; });
    return stopped ? WorldResultUVE::Succeeded : WorldResultUVE::SystemFailed;
}
WorldResultUVE WorldUVE::SetSceneEnabledUVE(const SceneHandleUVE handle, const bool enabled) {
    if (m_impl->busy)
        return WorldResultUVE::Busy;
    auto *scene = GetSceneUVE(handle);
    if (scene == nullptr)
        return WorldResultUVE::UnknownScene;
    scene->m_enabled = enabled;
    return WorldResultUVE::Succeeded;
}
WorldResultUVE WorldUVE::ShutdownUVE() noexcept {
    auto &impl = *m_impl;
    if (impl.busy)
        return WorldResultUVE::Busy;
    if (impl.state == WorldStateUVE::Stopped)
        return WorldResultUVE::Succeeded;
    impl.busy = true;
    bool success = true;
    for (auto it = impl.scenes.rbegin(); it != impl.scenes.rend(); ++it) {
        if (!impl.StopSceneUVE(**it))
            success = false;
        static_cast<void>(impl.registry.UnregisterUVE((*it)->GetHandleUVE()));
        try {
            (*it)->ClearEntitiesUVE();
        } catch (...) {
            success = false;
        }
    }
    impl.registry.ShutdownUVE();
    impl.scenes.resize(1U);
    impl.state = WorldStateUVE::Stopped;
    impl.busy = false;
    return success ? WorldResultUVE::Succeeded : WorldResultUVE::SystemFailed;
}
SceneUVE *WorldUVE::GetSceneUVE(const SceneHandleUVE handle) noexcept {
    return static_cast<SceneUVE *>(m_impl->registry.FindUVE(handle));
}
const SceneUVE *WorldUVE::GetSceneUVE(const SceneHandleUVE handle) const noexcept {
    return static_cast<const SceneUVE *>(m_impl->registry.FindUVE(handle));
}
SceneHandleUVE WorldUVE::GetPrimarySceneHandleUVE() const noexcept {
    return m_impl->scenes.front()->GetHandleUVE();
}
std::size_t WorldUVE::GetSceneCountUVE() const noexcept {
    return m_impl->state == WorldStateUVE::Stopped ? 0U : m_impl->scenes.size();
}
WorldStateUVE WorldUVE::GetStateUVE() const noexcept { return m_impl->state; }
Scene::IEntityManagerUVE &WorldUVE::GetEntityManagerUVE() noexcept {
    return m_impl->scenes.front()->m_entities;
}
const Scene::IEntityManagerUVE &WorldUVE::GetEntityManagerUVE() const noexcept {
    return m_impl->scenes.front()->m_entities;
}
std::uint64_t WorldUVE::GetFrameCountUVE() const noexcept { return m_impl->frameCount; }
float WorldUVE::GetTotalTimeSecondsUVE() const noexcept { return m_impl->totalSeconds; }
} // namespace UVE::World
