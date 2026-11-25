// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
#pragma once
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include "world_system.h"

namespace UVE::World {
/// Owns a persistent primary scene plus independently unloadable scenes. Each uses the existing
/// ECS/SceneGraph; there is no new serialization format. The primary uses the constructor's
/// event bus, preserving old callers; secondary scenes isolate entity events on their own buses.
/// Systems register before Start; names/dependencies are validated before any start callback.
/// The existing FrameTaskGraph/FrameScheduler provide deterministic owner-thread execution:
/// dependency order, then phase/priority/registration order, scenes in creation order, one graph
/// propagation between BeforeTransforms and AfterTransforms. No parallel gameplay is implied.
///
/// Start rolls back failed initialization in reverse order (including the failing start).
/// Unload/shutdown stop systems while their scene is alive, detach its identity, then destroy
/// entities in index order. The primary cannot be unloaded independently; Shutdown clears it.
/// Structural world mutation during callbacks fails Busy. Mutating an unloading scene from its
/// entity-destruction notifications is not permitted. Scene/entity borrows expire at unload;
/// all operations and lifetimes are controlled by one owning thread.
class World final {
  public:
    static constexpr std::size_t kMaximumSystems = 128U;
    static constexpr std::size_t kMaximumScenes = 256U;
    World(Memory::IAllocator &allocator, Events::IEventSystem &eventSystem);
    ~World();
    World(const World &) = delete;
    World &operator=(const World &) = delete;

    [[nodiscard]] WorldResult RegisterSystem(WorldSystemDesc system);
    [[nodiscard]] WorldResult UnregisterSystem(std::string_view name);
    [[nodiscard]] WorldResult Start();
    [[nodiscard]] WorldResult TryTick(float deltaTimeSeconds);
    /// Legacy wrapper: invalid deltas assert in Debug and clamp to zero in Release as before;
    /// lifecycle/system failure is logged rather than silently reported as a successful tick.
    void Tick(float deltaTimeSeconds);
    [[nodiscard]] WorldResult Shutdown() noexcept;

    /// Optional population callback can use SceneSerializer/PrefabSystem on the new scene.
    /// Rejection/exception discards that scene without altering existing scenes or outHandle.
    [[nodiscard]] WorldResult CreateScene(std::string name, SceneHandle &outHandle,
                                                std::function<bool(Scene &)> populate = {});
    [[nodiscard]] WorldResult UnloadScene(SceneHandle handle);
    [[nodiscard]] WorldResult SetSceneEnabled(SceneHandle handle, bool enabled);
    [[nodiscard]] Scene *GetScene(SceneHandle handle) noexcept;
    [[nodiscard]] const Scene *GetScene(SceneHandle handle) const noexcept;
    [[nodiscard]] SceneHandle GetPrimarySceneHandle() const noexcept;
    [[nodiscard]] std::size_t GetSceneCount() const noexcept;
    [[nodiscard]] WorldState GetState() const noexcept;
    /// Persistent primary storage, retained (empty) after Shutdown for legacy reference stability.
    [[nodiscard]] UVE::Scene::IEntityManager &GetEntityManager() noexcept;
    [[nodiscard]] const UVE::Scene::IEntityManager &GetEntityManager() const noexcept;
    [[nodiscard]] std::uint64_t GetFrameCount() const noexcept;
    [[nodiscard]] float GetTotalTimeSeconds() const noexcept;

  private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};
} // namespace UVE::World
