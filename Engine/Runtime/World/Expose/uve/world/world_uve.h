// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
#pragma once
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include "uve/world/world_system_uve.h"

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
class WorldUVE final {
  public:
    static constexpr std::size_t kMaximumSystemsUVE = 128U;
    static constexpr std::size_t kMaximumScenesUVE = 256U;
    WorldUVE(Memory::IAllocatorUVE &allocator, Events::IEventSystemUVE &eventSystem);
    ~WorldUVE();
    WorldUVE(const WorldUVE &) = delete;
    WorldUVE &operator=(const WorldUVE &) = delete;

    [[nodiscard]] WorldResultUVE RegisterSystemUVE(WorldSystemDescUVE system);
    [[nodiscard]] WorldResultUVE UnregisterSystemUVE(std::string_view name);
    [[nodiscard]] WorldResultUVE StartUVE();
    [[nodiscard]] WorldResultUVE TryTickUVE(float deltaTimeSeconds);
    /// Legacy wrapper: invalid deltas assert in Debug and clamp to zero in Release as before;
    /// lifecycle/system failure is logged rather than silently reported as a successful tick.
    void TickUVE(float deltaTimeSeconds);
    [[nodiscard]] WorldResultUVE ShutdownUVE() noexcept;

    /// Optional population callback can use SceneSerializerUVE/PrefabSystemUVE on the new scene.
    /// Rejection/exception discards that scene without altering existing scenes or outHandle.
    [[nodiscard]] WorldResultUVE CreateSceneUVE(std::string name, SceneHandleUVE &outHandle,
                                                std::function<bool(SceneUVE &)> populate = {});
    [[nodiscard]] WorldResultUVE UnloadSceneUVE(SceneHandleUVE handle);
    [[nodiscard]] WorldResultUVE SetSceneEnabledUVE(SceneHandleUVE handle, bool enabled);
    [[nodiscard]] SceneUVE *GetSceneUVE(SceneHandleUVE handle) noexcept;
    [[nodiscard]] const SceneUVE *GetSceneUVE(SceneHandleUVE handle) const noexcept;
    [[nodiscard]] SceneHandleUVE GetPrimarySceneHandleUVE() const noexcept;
    [[nodiscard]] std::size_t GetSceneCountUVE() const noexcept;
    [[nodiscard]] WorldStateUVE GetStateUVE() const noexcept;
    /// Persistent primary storage, retained (empty) after Shutdown for legacy reference stability.
    [[nodiscard]] Scene::IEntityManagerUVE &GetEntityManagerUVE() noexcept;
    [[nodiscard]] const Scene::IEntityManagerUVE &GetEntityManagerUVE() const noexcept;
    [[nodiscard]] std::uint64_t GetFrameCountUVE() const noexcept;
    [[nodiscard]] float GetTotalTimeSecondsUVE() const noexcept;

  private:
    struct ImplUVE;
    std::unique_ptr<ImplUVE> m_impl;
};
} // namespace UVE::World
