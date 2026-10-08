// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <optional>
#include <string_view>

#include "uve/component/collider_component_uve.h"
#include "uve/component/entity_uve.h"
#include "uve/math/aabb_uve.h"
#include "uve/math/quaternion_uve.h"
#include "uve/math/ray_uve.h"
#include "uve/math/vector3_uve.h"

namespace UVE::Scene {

class IEntityManagerUVE;

struct Collider3DObjectDefinitionUVE final {
    static constexpr std::string_view defaultName = "Collision Box";
    ColliderComponentUVE collider{};
};

[[nodiscard]] bool IsCollider3DObjectDefinitionValidUVE(const Collider3DObjectDefinitionUVE& value) noexcept;

void ApplyCollider3DObjectDefinitionUVE(IEntityManagerUVE& entityManager, EntityUVE entity,
                                      const Collider3DObjectDefinitionUVE& value);

class Collider3DUVE final {
public:
    [[nodiscard]] static ColliderComponentUVE MakeBoxUVE(const Math::Vector3UVE& halfExtents) noexcept;
    [[nodiscard]] static ColliderComponentUVE MakeSphereUVE(float radius) noexcept;
    [[nodiscard]] static ColliderComponentUVE MakeCapsuleUVE(float radius, float height) noexcept;

    [[nodiscard]] static bool IsParticipatingUVE(const ColliderComponentUVE& collider) noexcept;
    [[nodiscard]] static Math::AabbUVE GetLocalAabbUVE(const ColliderComponentUVE& collider) noexcept;
    [[nodiscard]] static Math::AabbUVE GetWorldAabbUVE(const ColliderComponentUVE& collider,
                                                       const Math::Vector3UVE& center,
                                                       const Math::QuaternionUVE& rotation) noexcept;
    [[nodiscard]] static float GetVolumeUVE(const ColliderComponentUVE& collider) noexcept;

    [[nodiscard]] static bool ContainsPointUVE(const ColliderComponentUVE& collider,
                                               const Math::Vector3UVE& center,
                                               const Math::QuaternionUVE& rotation,
                                               const Math::Vector3UVE& point) noexcept;
    [[nodiscard]] static std::optional<Math::Vector3UVE> ClosestPointUVE(
        const ColliderComponentUVE& collider, const Math::Vector3UVE& center,
        const Math::QuaternionUVE& rotation, const Math::Vector3UVE& point) noexcept;
    [[nodiscard]] static std::optional<Math::RayHitUVE> IntersectRayUVE(
        const ColliderComponentUVE& collider, const Math::Vector3UVE& center,
        const Math::QuaternionUVE& rotation, const Math::RayUVE& ray, float maxDistance) noexcept;
};

} // namespace UVE::Scene
