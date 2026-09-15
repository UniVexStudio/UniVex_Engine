// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cstdint>

#include "uve/events/i_event_system_uve.h"
#include "uve/memory/i_allocator_uve.h"
#include "uve/scene/entity_manager_uve.h"
#include "uve/scene/scene_graph_uve.h"

namespace UVE::World {

/// WorldUVE is the runtime container the docx's target architecture calls for: it owns one
/// EntityManagerUVE (the actual, single loaded scene's entity/component storage) and drives its
/// per-frame update, so Scene::SceneGraphUVE/EntityManagerUVE themselves stay non-ticking pure
/// data-and-operations types, exactly as they already are. This is deliberately new code, not a
/// port - UNIVEX-ENGINE's EngineCoreUVE (~500 lines) also owns rendering, windowing, and audio
/// lifecycle, none of which exist in this slice, so porting it would pull in everything this
/// restructuring explicitly defers.
///
/// TickUVE() today only advances frame/time bookkeeping and propagates the scene's transform
/// hierarchy (Scene::ISceneGraphUVE::UpdateUVE) - there is no gameplay/physics/animation *system*
/// to run yet, since those modules are out of scope for this slice; a future Gameplay/Physics
/// module would plug into TickUVE() the same way SceneGraphUVE does today.
///
/// Deliberately not a singleton: whatever hosts a WorldUVE (a future Application/Engine class, or
/// a test) owns and passes it explicitly - see the restructuring plan's stated preference for
/// minimal global state.
///
/// Thread-safety: not thread-safe; TickUVE() and the entity manager must be driven from a single
/// owning thread, matching EntityManagerUVE's own contract.
class WorldUVE final {
public:
    WorldUVE(Memory::IAllocatorUVE& allocator, Events::IEventSystemUVE& eventSystem);

    WorldUVE(const WorldUVE&) = delete;
    WorldUVE& operator=(const WorldUVE&) = delete;

    /// Advances frame bookkeeping (frame count, accumulated time) and propagates the scene's
    /// parent/child transform hierarchy. `deltaTimeSeconds` must be finite and non-negative;
    /// violations are asserted in debug builds and clamped to 0 in release, matching
    /// EntityManagerUVE's own fail-safe conventions elsewhere in this codebase.
    void TickUVE(float deltaTimeSeconds);

    [[nodiscard]] Scene::IEntityManagerUVE& GetEntityManagerUVE() noexcept { return m_entityManager; }
    [[nodiscard]] const Scene::IEntityManagerUVE& GetEntityManagerUVE() const noexcept { return m_entityManager; }

    [[nodiscard]] std::uint64_t GetFrameCountUVE() const noexcept { return m_frameCount; }
    [[nodiscard]] float GetTotalTimeSecondsUVE() const noexcept { return m_totalTimeSeconds; }

private:
    Scene::EntityManagerUVE m_entityManager;
    Scene::SceneGraphUVE m_sceneGraph;
    std::uint64_t m_frameCount = 0;
    float m_totalTimeSeconds = 0.0F;
};

} // namespace UVE::World
