// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <string_view>

#include "uve/component/collider_component_uve.h"
#include "uve/component/entity_uve.h"
#include "uve/component/rigid_body_component_uve.h"
#include "uve/nodes/3d/node_3d_common_uve.h"

namespace UVE::Scene {

class IEntityManagerUVE;

struct AnimatableBody3DNodeComponentUVE final {
    Math::Vector3UVE targetVelocity{};
    float interpolation = 1.0F;
    bool active = true;
};

[[nodiscard]] bool IsAnimatableBody3DNodeComponentValidUVE(const AnimatableBody3DNodeComponentUVE& value) noexcept;

/// Authoring definition for the AnimatableBody3D scene node: the component set and defaults a
/// freshly created AnimatableBody3D entity attaches — a collider, a kinematic rigid body (so
/// physics never fights the authored target-velocity motion), and the animatable body's own node
/// component. This recipe used to be the one remaining inline multi-component recipe hardcoded
/// in EditorUVE's creation switch; it now lives in the kind's own home file like every other
/// kind. Per Engine/Runtime/Scene/README.md's "one truth per concept" rule this holds the
/// *recipe*, not a second copy of component storage.
struct AnimatableBody3DNodeDefinitionUVE final {
    /// Default document-entity name for a freshly created node of this kind. (Previously the
    /// generic "Empty" — the editor created this kind as a bare entity plus three components.)
    static constexpr std::string_view defaultName = "AnimatableBody3D";

    /// Collision authored defaults; the entity's transform is attached by the creation shell.
    ColliderComponentUVE collider{};
    /// Kinematic by contract — the authored target velocity drives the body, never gravity
    /// (mirrors CharacterBody3DNodeDefinitionUVE's own kinematic contract).
    RigidBodyComponentUVE body = MakeDefaultBodyUVE();
    /// The animatable body's own authored defaults (zero target velocity, full interpolation).
    AnimatableBody3DNodeComponentUVE animatableBody{};

    [[nodiscard]] static RigidBodyComponentUVE MakeDefaultBodyUVE() noexcept {
        RigidBodyComponentUVE body{};
        body.isKinematic = true;
        return body;
    }
};

[[nodiscard]] bool IsAnimatableBody3DNodeDefinitionValidUVE(const AnimatableBody3DNodeDefinitionUVE& value) noexcept;

/// Attaches this node's components to `entity` using the definition's authored defaults. The
/// entity must be alive and must not already have any of the attached component types.
/// Every application first guarantees the Node3D baseline (Transform/WorldTransform/
/// Hierarchy/Name) through EnsureNode3DBaselineUVE - this kind is Node3D plus its recipe.
void ApplyAnimatableBody3DNodeDefinitionUVE(IEntityManagerUVE& entityManager, EntityUVE entity,
                                            const AnimatableBody3DNodeDefinitionUVE& value);

} // namespace UVE::Scene
