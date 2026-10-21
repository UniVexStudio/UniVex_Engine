// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#pragma once

#include <cstdint>

#include "uve/math/vector3_uve.h"
#include "uve/physics/i_collision_system_uve.h"
#include "uve/physics/i_physics_system_uve.h"

namespace UVE::Physics {
class PhysicsConstraintSystemUVE;
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

    void SetGravityUVE(Math::Vector3UVE gravity) noexcept override;
    [[nodiscard]] Math::Vector3UVE GetGravityUVE() const noexcept override;

    void SetSimulationSettingsUVE(const PhysicsSimulationSettingsUVE& settings) noexcept override;
    [[nodiscard]] PhysicsSimulationSettingsUVE GetSimulationSettingsUVE() const noexcept override;

    /// Digest of the state this step published, when deterministic stepping is enabled. Recorded
    /// rather than returned from StepUVE so the interface stays one call with one job, and so a
    /// caller driving many steps can compare digests without threading a return value through.
    [[nodiscard]] std::uint64_t GetLastStepDigestUVE() const noexcept override { return m_lastStepDigest; }

private:
    ICollisionSystemUVE* m_collisionSystem;
    PhysicsConstraintSystemUVE* m_constraintSystem = nullptr;
    Math::Vector3UVE m_gravity;
    PhysicsSimulationSettingsUVE m_settings{};
    std::uint64_t m_lastStepDigest = 0U;
};

} // namespace UVE::Physics
