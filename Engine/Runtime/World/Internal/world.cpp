// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
#include "world.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>
#include <stdexcept>
#include <utility>
#include <vector>
#include "engine_assert.h"
#include "logging_macros.h"
#include "object_registry.h"
#include "physics_interpolation_component.h"
#include "frame_scheduler.h"
#include "scope_guard.h"

namespace UVE::World {
struct World::Impl {
    Impl(World &ownerIn, Memory::IAllocator &allocatorIn, Events::IEventSystem &events)
        : owner(ownerIn), allocator(allocatorIn) {
        auto primary = std::unique_ptr<Scene>(new Scene("Primary", allocator, &events));
        SceneHandle handle;
        if (!registry.Register(*primary, handle))
            throw std::logic_error("primary scene registration failed");
        scenes.push_back(std::move(primary));
    }
    World &owner;
    Memory::IAllocator &allocator;
    Core::ObjectRegistry registry;
    std::vector<std::unique_ptr<Scene>> scenes;
    std::vector<WorldSystemDesc> systems;
    Core::FrameTaskGraph plan;
    std::vector<std::size_t> lifecycleOrder;
    Scene *currentScene = nullptr;
    float currentDelta = 0.0F;
    WorldState state = WorldState::Created;
    bool busy = false;
    std::uint64_t frameCount = 0U;
    float totalSeconds = 0.0F;

    WorldResult Compile() {
        std::vector<std::size_t> indices(systems.size());
        std::iota(indices.begin(), indices.end(), 0U);
        std::stable_sort(indices.begin(), indices.end(), [this](const auto a, const auto b) {
            if (systems[a].phase != systems[b].phase)
                return systems[a].phase < systems[b].phase;
            return systems[a].priority < systems[b].priority;
        });
        Core::FrameTaskGraph candidate;
        const auto transformId = static_cast<Core::FrameTaskId>(systems.size() + 1U);
        const auto addTransforms = [&] {
            Core::FrameTaskDefinition task;
            task.id = transformId;
            task.domain = Core::FrameTaskDomain::ECS;
            task.name = "World transform propagation";
            task.action = [this] { currentScene->m_graph.Update(currentScene->m_entities); };
            return candidate.AddTask(std::move(task)).IsAccepted();
        };
        bool addedTransforms = false;
        for (const auto index : indices) {
            const auto &system = systems[index];
            if (!addedTransforms && system.phase == WorldPhase::AfterTransforms) {
                if (!addTransforms())
                    return WorldResult::InvalidArgument;
                addedTransforms = true;
            }
            Core::FrameTaskDefinition task;
            task.id = static_cast<Core::FrameTaskId>(index + 1U);
            task.domain = Core::FrameTaskDomain::ECS;
            task.name = system.name;
            for (const auto &name : system.after) {
                const auto dependency = std::find_if(systems.begin(), systems.end(),
                                                     [&](const auto &entry) { return entry.name == name; });
                if (dependency == systems.end())
                    return WorldResult::MissingDependency;
                if (system.phase < dependency->phase)
                    return WorldResult::InvalidArgument;
                task.dependencies.PushBack(
                    static_cast<Core::FrameTaskId>(dependency - systems.begin() + 1));
            }
            task.action = [this, index] {
                WorldSceneContext context{owner, *currentScene};
                systems[index].tick(context, currentDelta);
            };
            if (!candidate.AddTask(std::move(task)).IsAccepted())
                return WorldResult::InvalidArgument;
        }
        if (!addedTransforms && !addTransforms())
            return WorldResult::InvalidArgument;
        std::vector<Core::FrameTaskId> order;
        const auto validation = candidate.BuildExecutionOrder(order);
        if (!validation.IsValid())
            return WorldResult::DependencyCycle;
        std::vector<std::size_t> starts;
        for (auto id : order)
            if (id != transformId)
                starts.push_back(static_cast<std::size_t>(id - 1U));
        plan = std::move(candidate);
        lifecycleOrder = std::move(starts);
        return WorldResult::Succeeded;
    }
    bool StartScene(Scene &scene) {
        WorldSceneContext context{owner, scene};
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
    bool StopScene(Scene &scene) noexcept {
        bool success = true;
        WorldSceneContext context{owner, scene};
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

World::World(Memory::IAllocator &allocator, Events::IEventSystem &events)
    : m_impl(std::make_unique<Impl>(*this, allocator, events)) {}
World::~World() {
    if (Shutdown() != WorldResult::Succeeded)
        UVE_ERROR("World: shutdown callback failed");
}
WorldResult World::RegisterSystem(WorldSystemDesc system) {
    auto &impl = *m_impl;
    if (impl.busy)
        return WorldResult::Busy;
    if (impl.state != WorldState::Created)
        return WorldResult::InvalidState;
    if (system.name.empty() || system.name.size() > Core::FrameTaskGraph::kMaximumTaskNameBytes ||
        !system.tick || system.after.size() > Core::FrameTaskGraph::kMaximumDependenciesPerTask ||
        (system.phase != WorldPhase::BeforeTransforms && system.phase != WorldPhase::AfterTransforms)) {
        return WorldResult::InvalidArgument;
    }
    if (std::any_of(impl.systems.begin(), impl.systems.end(),
                    [&](const auto &item) { return item.name == system.name; })) {
        return WorldResult::DuplicateName;
    }
    if (impl.systems.size() >= kMaximumSystems)
        return WorldResult::CapacityExceeded;
    impl.systems.push_back(std::move(system));
    return WorldResult::Succeeded;
}
WorldResult World::UnregisterSystem(const std::string_view name) {
    auto &impl = *m_impl;
    if (impl.busy)
        return WorldResult::Busy;
    if (impl.state != WorldState::Created)
        return WorldResult::InvalidState;
    const auto entry = std::find_if(impl.systems.begin(), impl.systems.end(),
                                    [&](const auto &system) { return system.name == name; });
    if (entry == impl.systems.end())
        return WorldResult::InvalidArgument;
    impl.systems.erase(entry);
    return WorldResult::Succeeded;
}
WorldResult World::Start() {
    auto &impl = *m_impl;
    if (impl.busy)
        return WorldResult::Busy;
    if (impl.state == WorldState::Running)
        return WorldResult::Succeeded;
    if (impl.state != WorldState::Created)
        return WorldResult::InvalidState;
    const auto compiled = impl.Compile();
    if (compiled != WorldResult::Succeeded)
        return compiled;
    impl.busy = true;
    Utilities::ScopeGuard done{[&] { impl.busy = false; }};
    for (auto &scene : impl.scenes) {
        if (impl.StartScene(*scene))
            continue;
        bool stopped = true;
        for (auto it = impl.scenes.rbegin(); it != impl.scenes.rend(); ++it) {
            if (!impl.StopScene(**it))
                stopped = false;
        }
        if (!stopped)
            impl.state = WorldState::Failed;
        return WorldResult::InitializationFailed;
    }
    impl.state = WorldState::Running;
    return WorldResult::Succeeded;
}
WorldResult World::TryTick(const float delta) {
    auto &impl = *m_impl;
    if (impl.busy)
        return WorldResult::Busy;
    if (!std::isfinite(delta) || delta < 0.0F || !std::isfinite(impl.totalSeconds + delta) ||
        impl.frameCount == std::numeric_limits<std::uint64_t>::max())
        return WorldResult::InvalidArgument;
    const auto started = Start();
    if (started != WorldResult::Succeeded)
        return started;
    impl.busy = true;
    Utilities::ScopeGuard done{[&] {
        impl.busy = false;
        impl.currentScene = nullptr;
    }};
    impl.currentDelta = delta;
    for (auto &scene : impl.scenes) {
        if (!scene->m_enabled)
            continue;
        impl.currentScene = scene.get();
        const auto result = Core::FrameScheduler::ExecuteSerial(impl.plan);
        if (!result.IsCompleted()) {
            impl.state = WorldState::Failed;
            return WorldResult::SystemFailed;
        }
        ++scene->m_frameCount;
    }
    ++impl.frameCount;
    impl.totalSeconds += delta;
    return WorldResult::Succeeded;
}
void World::Tick(const float delta) {
    UVE_ASSERT(std::isfinite(delta));
    UVE_ASSERT(delta >= 0.0F);
    const float safe = std::isfinite(delta) && delta >= 0.0F ? delta : 0.0F;
    const auto result = TryTick(safe);
    if (result != WorldResult::Succeeded)
        UVE_ERROR("World: tick failed ({})", static_cast<unsigned>(result));
}
WorldResult World::CreateScene(std::string name, SceneHandle &outHandle,
                                        std::function<bool(Scene &)> populate) {
    auto &impl = *m_impl;
    if (impl.busy)
        return WorldResult::Busy;
    if (impl.state == WorldState::Stopped || impl.state == WorldState::Failed)
        return WorldResult::InvalidState;
    if (name.empty() || name.size() > 128U)
        return WorldResult::InvalidArgument;
    if (std::any_of(impl.scenes.begin(), impl.scenes.end(),
                    [&](const auto &scene) { return scene->m_name == name; }))
        return WorldResult::DuplicateName;
    if (impl.scenes.size() >= kMaximumScenes)
        return WorldResult::CapacityExceeded;
    // Reserve before user initialization: a later vector allocation must not bypass stop callbacks.
    if (impl.scenes.size() == impl.scenes.capacity()) {
        impl.scenes.reserve(std::min(kMaximumScenes, impl.scenes.size() * 2U));
    }
    auto scene = std::unique_ptr<Scene>(new Scene(std::move(name), impl.allocator, nullptr));
    SceneHandle handle;
    if (!impl.registry.Register(*scene, handle))
        return WorldResult::InitializationFailed;
    impl.busy = true;
    Utilities::ScopeGuard done{[&] { impl.busy = false; }};
    bool populated = true;
    try {
        if (populate)
            populated = populate(*scene);
    } catch (...) {
        populated = false;
    }
    if (!populated || (impl.state == WorldState::Running && !impl.StartScene(*scene))) {
        const bool stopped = impl.StopScene(*scene);
        static_cast<void>(impl.registry.Unregister(handle));
        scene->ClearEntities();
        return stopped ? WorldResult::InitializationFailed : WorldResult::SystemFailed;
    }
    impl.scenes.push_back(std::move(scene));
    outHandle = handle;
    return WorldResult::Succeeded;
}
WorldResult World::UnloadScene(const SceneHandle handle) {
    auto &impl = *m_impl;
    if (impl.busy)
        return WorldResult::Busy;
    auto *scene = GetScene(handle);
    if (scene == nullptr)
        return WorldResult::UnknownScene;
    if (scene == impl.scenes.front().get())
        return WorldResult::InvalidState;
    impl.busy = true;
    Utilities::ScopeGuard done{[&] { impl.busy = false; }};
    bool stopped = impl.StopScene(*scene);
    static_cast<void>(impl.registry.Unregister(handle));
    try {
        scene->ClearEntities();
    } catch (...) {
        stopped = false;
    }
    std::erase_if(impl.scenes, [scene](const auto &entry) { return entry.get() == scene; });
    return stopped ? WorldResult::Succeeded : WorldResult::SystemFailed;
}
WorldResult World::SetSceneEnabled(const SceneHandle handle, const bool enabled) {
    if (m_impl->busy)
        return WorldResult::Busy;
    auto *scene = GetScene(handle);
    if (scene == nullptr)
        return WorldResult::UnknownScene;
    if (enabled && !scene->m_enabled) {
        scene->m_entities.ForEach<UVE::Scene::PhysicsInterpolationComponent>(
            [](const auto, auto& interpolation) { interpolation.hasPreviousPose = false; });
    }
    scene->m_enabled = enabled;
    return WorldResult::Succeeded;
}
WorldResult World::Shutdown() noexcept {
    auto &impl = *m_impl;
    if (impl.busy)
        return WorldResult::Busy;
    if (impl.state == WorldState::Stopped)
        return WorldResult::Succeeded;
    impl.busy = true;
    bool success = true;
    for (auto it = impl.scenes.rbegin(); it != impl.scenes.rend(); ++it) {
        if (!impl.StopScene(**it))
            success = false;
        static_cast<void>(impl.registry.Unregister((*it)->GetHandle()));
        try {
            (*it)->ClearEntities();
        } catch (...) {
            success = false;
        }
    }
    impl.registry.Shutdown();
    impl.scenes.resize(1U);
    impl.state = WorldState::Stopped;
    impl.busy = false;
    return success ? WorldResult::Succeeded : WorldResult::SystemFailed;
}
Scene *World::GetScene(const SceneHandle handle) noexcept {
    return static_cast<Scene *>(m_impl->registry.Find(handle));
}
const Scene *World::GetScene(const SceneHandle handle) const noexcept {
    return static_cast<const Scene *>(m_impl->registry.Find(handle));
}
SceneHandle World::GetPrimarySceneHandle() const noexcept {
    return m_impl->scenes.front()->GetHandle();
}
std::size_t World::GetSceneCount() const noexcept {
    return m_impl->state == WorldState::Stopped ? 0U : m_impl->scenes.size();
}
WorldState World::GetState() const noexcept { return m_impl->state; }
UVE::Scene::IEntityManager &World::GetEntityManager() noexcept {
    return m_impl->scenes.front()->m_entities;
}
const UVE::Scene::IEntityManager &World::GetEntityManager() const noexcept {
    return m_impl->scenes.front()->m_entities;
}
std::uint64_t World::GetFrameCount() const noexcept { return m_impl->frameCount; }
float World::GetTotalTimeSeconds() const noexcept { return m_impl->totalSeconds; }
} // namespace UVE::World
