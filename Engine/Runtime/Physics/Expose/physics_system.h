// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#pragma once

#include <cstdint>
#include <vector>

#include "vector3.h"
#include "i_collision_system.h"
#include "i_physics_system.h"

namespace UVE::Physics {
class PhysicsConstraintSystem;
}

namespace UVE::Threading {
class IThreadPool;
}

namespace UVE::Physics {

/// PhysicsSystem is the concrete, engine-standard implementation of IPhysicsSystem.
/// Composes an ICollisionSystem& (dependency injection, matching Renderer3D's precedent
/// for a service that needs a constructor-injected collaborator) and holds one small piece of
/// genuinely-owned config (gravity) — no per-entity state, which is what all persists in
/// Rigid3DComponent::velocity instead, so PhysicsSystem stays trivially testable in
/// isolation despite not being fully stateless like CameraSystem/MeshRenderer.
class PhysicsSystem final : public IPhysicsSystem {
public:
    /// `gravity` defaults to Earth-like {0, -9.81, 0} (Y-up, matching this engine's convention
    /// throughout). `collisionSystem` must outlive this PhysicsSystem.
    explicit PhysicsSystem(ICollisionSystem& collisionSystem,
                               Math::Vector3 gravity = Math::Vector3{0.0F, -9.81F, 0.0F});

    /// Integrates finite rigid-body positions with widened intermediates before safe float-domain
    /// publication; invalid inputs and results preserve the prior state atomically.
    void Step(Scene::IEntityManager& entityManager, Scene::ISceneGraph& sceneGraph,
                 float fixedDeltaTimeSeconds) override;

    /// Attaches a caller-owned bounded positional constraint system. The solver is invoked after
    /// collision resolution; passing nullptr restores the legacy integration-only behavior.
    void SetConstraintSystem(PhysicsConstraintSystem* constraintSystem) noexcept;

    /// Attaches the pool island-parallel contact resolution runs on. `nullptr` (the default) keeps
    /// every step on the calling thread. The pool is borrowed, not owned, and must outlive this
    /// system - the same non-owning contract as the constraint system above.
    void SetThreadPool(Threading::IThreadPool* threadPool) noexcept;

    /// How many simulation islands the last step's pair list formed. Zero when there were no pairs.
    /// Reported so a caller can see whether a scene is even shaped to benefit from parallelism.
    [[nodiscard]] std::uint32_t GetLastStepIslandCount() const noexcept {
        return m_lastStepIslandCount;
    }

    /// How many of those islands were actually solved on the worker pool last step. Zero means the
    /// step ran single-threaded: parallelism disabled, no pool, one worker, too few pairs, or a
    /// single island - all of which are legitimate outcomes, not failures.
    [[nodiscard]] std::uint32_t GetLastStepParallelIslandCount() const noexcept {
        return m_lastStepParallelIslandCount;
    }

    void SetGravity(Math::Vector3 gravity) noexcept override;
    [[nodiscard]] Math::Vector3 GetGravity() const noexcept override;

    void SetSimulationSettings(const PhysicsSimulationSettings& settings) noexcept override;
    [[nodiscard]] PhysicsSimulationSettings GetSimulationSettings() const noexcept override;

    /// Digest of the state this step published, when deterministic stepping is enabled. Recorded
    /// rather than returned from Step so the interface stays one call with one job, and so a
    /// caller driving many steps can compare digests without threading a return value through.
    [[nodiscard]] std::uint64_t GetLastStepDigest() const noexcept override { return m_lastStepDigest; }

private:
    /// Resolves every pair in the step's list, island by island. Sequential order and island order
    /// produce identical state (islands share no bodies), so this is the one place parallelism is
    /// allowed to change how the work runs without changing what it computes.
    void ResolveCollisionPairs(Scene::IEntityManager& entityManager, Scene::ISceneGraph& sceneGraph,
                                  const std::vector<CollisionPair>& pairs, float fixedDeltaTimeSeconds);

    ICollisionSystem* m_collisionSystem;
    PhysicsConstraintSystem* m_constraintSystem = nullptr;
    Threading::IThreadPool* m_threadPool = nullptr;
    Math::Vector3 m_gravity;
    PhysicsSimulationSettings m_settings{};
    std::uint64_t m_lastStepDigest = 0U;
    std::uint32_t m_lastStepIslandCount = 0U;
    std::uint32_t m_lastStepParallelIslandCount = 0U;
};

} // namespace UVE::Physics
