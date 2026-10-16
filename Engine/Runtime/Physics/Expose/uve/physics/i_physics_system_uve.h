// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#pragma once

#include <cstddef>

#include "uve/entity/i_entity_manager_uve.h"
#include "uve/math/vector3_uve.h"
#include "uve/scene/i_scene_graph_uve.h"

namespace UVE::Physics {

/// Runtime solver, damping, sleeping, and contact-resolution parameters consumed by PhysicsSystemUVE.
struct PhysicsSimulationSettingsUVE final {
    float defaultLinearDamping = 0.0F;
    float defaultAngularDamping = 0.0F;
    float sleepThreshold = 0.0F;
    bool allowSleeping = false;
    std::size_t solverIterations = 8U;
    float penetrationSlop = 0.0F;
    float maxDepenetrationVelocity = 0.0F;
};

/// IPhysicsSystemUVE is the spec's `PhysicsSystemUVE` (Part 7.5): dynamics integration +
/// collision resolution, driven once per fixed simulation step by `EngineCoreUVE::Update()`
/// (via `Utilities::FixedStepResultUVE`, already computed every frame but previously unconsumed).
/// A from-scratch CPU implementation this increment (see docs/CODING_STANDARDS.md and the
/// Increment 15 plan for why a real Jolt/Bullet backend is deferred) — deliberately interfaced
/// so a future backend-wrapping implementation can replace `PhysicsSystemUVE` without any ECS or
/// call-site change, the same seam `IRenderDeviceUVE` provides for graphics.
/// Thread-safety: not thread-safe — `StepUVE()` must be called only from the main engine thread,
/// once per fixed step, matching `SceneGraphUVE::UpdateUVE()`'s contract (and must run before it
/// each frame, so world transforms this step just wrote are picked up by the same frame's
/// propagation pass).
class IPhysicsSystemUVE {
public:
    virtual ~IPhysicsSystemUVE() = default;

    /// Runs exactly one fixed-size simulation step: integrates every non-kinematic
    /// `Rigid3DComponentUVE` (gravity * gravityScale, semi-implicit Euler, linear drag),
    /// calls `sceneGraph.UpdateUVE()` so `WorldTransformComponentUVE` reflects this step's new
    /// positions, detects collisions among every `ColliderComponentUVE` entity, resolves
    /// overlaps with mass-weighted positional correction and into-surface-only velocity removal
    /// (preserves sliding), optionally solves a caller-attached bounded positional constraint
    /// system, then calls `sceneGraph.UpdateUVE()` again so the resolved positions are immediately
    /// visible too. Every position write goes through
    /// `ISceneGraphUVE::SetLocalTransformUVE()` — never `GetComponentUVE()` directly, so
    /// dirty-flag propagation stays correct throughout.
    virtual void StepUVE(Scene::IEntityManagerUVE& entityManager, Scene::ISceneGraphUVE& sceneGraph,
                          float fixedDeltaTimeSeconds) = 0;

    virtual void SetGravityUVE(Math::Vector3UVE gravity) noexcept {
        static_cast<void>(gravity);
    }

    [[nodiscard]] virtual Math::Vector3UVE GetGravityUVE() const noexcept {
        return Math::Vector3UVE{0.0F, -9.81F, 0.0F};
    }

    virtual void SetSimulationSettingsUVE(const PhysicsSimulationSettingsUVE& settings) noexcept {
        static_cast<void>(settings);
    }

    [[nodiscard]] virtual PhysicsSimulationSettingsUVE GetSimulationSettingsUVE() const noexcept {
        return PhysicsSimulationSettingsUVE{};
    }
};

} // namespace UVE::Physics
