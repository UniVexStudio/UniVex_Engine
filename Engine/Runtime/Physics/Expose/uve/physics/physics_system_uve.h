// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#pragma once

#include <cstdint>
#include <vector>

#include "uve/math/vector3_uve.h"
#include "uve/physics/i_collision_system_uve.h"
#include "uve/physics/i_physics_system_uve.h"

namespace UVE::Physics {
class PhysicsConstraintSystemUVE;
}

namespace UVE::Threading {
class IThreadPoolUVE;
}

namespace UVE::Physics {

/// PhysicsSystemUVE is the concrete, engine-standard implementation of IPhysicsSystemUVE.
/// Composes an ICollisionSystemUVE& (dependency injection, matching Renderer3DUVE's precedent
/// for a service that needs a constructor-injected collaborator) and holds one small piece of
/// genuinely-owned config (gravity) — no per-entity state, which is what all persists in
/// Rigid3DComponentUVE::velocity instead, so PhysicsSystemUVE stays trivially testable in
/// isolation despite not being fully stateless like CameraSystemUVE/MeshRendererUVE.
class PhysicsSystemUVE final : public IPhysicsSystemUVE {
public:
    /// `gravity` defaults to Earth-like {0, -9.81, 0} (Y-up, matching this engine's convention
    /// throughout). `collisionSystem` must outlive this PhysicsSystemUVE.
    explicit PhysicsSystemUVE(ICollisionSystemUVE& collisionSystem,
                               Math::Vector3UVE gravity = Math::Vector3UVE{0.0F, -9.81F, 0.0F});

    /// Integrates finite rigid-body positions with widened intermediates before safe float-domain
    /// publication; invalid inputs and results preserve the prior state atomically.
    void StepUVE(Scene::IEntityManagerUVE& entityManager, Scene::ISceneGraphUVE& sceneGraph,
                 float fixedDeltaTimeSeconds) override;

    /// Attaches a caller-owned bounded positional constraint system. The solver is invoked after
    /// collision resolution; passing nullptr restores the legacy integration-only behavior.
    void SetConstraintSystemUVE(PhysicsConstraintSystemUVE* constraintSystem) noexcept;

    /// Attaches the pool island-parallel contact resolution runs on. `nullptr` (the default) keeps
    /// every step on the calling thread. The pool is borrowed, not owned, and must outlive this
    /// system - the same non-owning contract as the constraint system above.
    void SetThreadPoolUVE(Threading::IThreadPoolUVE* threadPool) noexcept;

    /// How many simulation islands the last step's pair list formed. Zero when there were no pairs.
    /// Reported so a caller can see whether a scene is even shaped to benefit from parallelism.
    [[nodiscard]] std::uint32_t GetLastStepIslandCountUVE() const noexcept {
        return m_lastStepIslandCountUVE;
    }

    /// How many of those islands were actually solved on the worker pool last step. Zero means the
    /// step ran single-threaded: parallelism disabled, no pool, one worker, too few pairs, or a
    /// single island - all of which are legitimate outcomes, not failures.
    [[nodiscard]] std::uint32_t GetLastStepParallelIslandCountUVE() const noexcept {
        return m_lastStepParallelIslandCountUVE;
    }

    void SetGravityUVE(Math::Vector3UVE gravity) noexcept override;
    [[nodiscard]] Math::Vector3UVE GetGravityUVE() const noexcept override;

    void SetSimulationSettingsUVE(const PhysicsSimulationSettingsUVE& settings) noexcept override;
    [[nodiscard]] PhysicsSimulationSettingsUVE GetSimulationSettingsUVE() const noexcept override;

    /// Digest of the state this step published, when deterministic stepping is enabled. Recorded
    /// rather than returned from StepUVE so the interface stays one call with one job, and so a
    /// caller driving many steps can compare digests without threading a return value through.
    [[nodiscard]] std::uint64_t GetLastStepDigestUVE() const noexcept override { return m_lastStepDigest; }

private:
    /// Resolves every pair in the step's list, island by island. Sequential order and island order
    /// produce identical state (islands share no bodies), so this is the one place parallelism is
    /// allowed to change how the work runs without changing what it computes.
    void ResolveCollisionPairsUVE(Scene::IEntityManagerUVE& entityManager, Scene::ISceneGraphUVE& sceneGraph,
                                  const std::vector<CollisionPairUVE>& pairs, float fixedDeltaTimeSeconds);

    ICollisionSystemUVE* m_collisionSystem;
    PhysicsConstraintSystemUVE* m_constraintSystem = nullptr;
    Threading::IThreadPoolUVE* m_threadPool = nullptr;
    Math::Vector3UVE m_gravity;
    PhysicsSimulationSettingsUVE m_settings{};
    std::uint64_t m_lastStepDigest = 0U;
    std::uint32_t m_lastStepIslandCountUVE = 0U;
    std::uint32_t m_lastStepParallelIslandCountUVE = 0U;
};

} // namespace UVE::Physics
