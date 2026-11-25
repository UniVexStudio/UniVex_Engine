// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "aabb.h"
#include "quaternion.h"
#include "collider_component.h"
#include "i_entity_manager.h"
#include "physics_material.h"

namespace UVE::Physics::Detail {

/// One entity's world-space AABB and collision layer/mask, computed once and reused across every
/// query against it. NOT a stable public contract — this is an internal implementation detail
/// shared by CollisionSystem, its transient BVH builder, and RaycastSystem (Increment 16)
/// so their shape extraction and iteration inputs cannot independently drift; nothing outside
/// engine/physics should depend on this type or function.
struct ColliderWorldAabb {
    Scene::Entity entity;
    Math::Aabb worldAabb;
    std::uint32_t collisionLayer;
    std::uint32_t collisionMask;
    Scene::ColliderShapeType shapeType = Scene::ColliderShapeType::Box;
    float shapeRadius = 0.0F;
    Math::Vector3 shapeHalfExtents{};
    Math::Quaternion shapeRotation{};
    Math::Vector3 shapeSegmentStart{};
    Math::Vector3 shapeSegmentEnd{};
    /// The collider's own surface properties, carried alongside the shape so a consumer that resolves
    /// a contact (ragdoll bodies today) does not have to re-fetch the component it was built from.
    /// Defaults are the zero the collision pair resolver already treats as "unset".
    PhysicsMaterial material{};
};

enum class DynamicColliderWorldAabbUpdateCode : std::uint8_t {
    Applied = 0,
    InvalidEntity,
    UnknownProxy,
    StaleGeneration,
    InvalidAabb,
};

struct DynamicColliderWorldAabbUpdateResult final {
    DynamicColliderWorldAabbUpdateCode code = DynamicColliderWorldAabbUpdateCode::UnknownProxy;
    std::string message;

    [[nodiscard]] bool IsApplied() const noexcept {
        return code == DynamicColliderWorldAabbUpdateCode::Applied;
    }
};

/// Owns a copied static median-split topology and supports deterministic refit-only updates of
/// individual collider AABBs. Cache indices and leaf membership never change during updates, so
/// callers retain the original encounter ordering while moved bounds update broad-phase pruning.
class DynamicAabbBvh final {
public:
    static constexpr std::size_t kMaximumProxies = 16384U;

    explicit DynamicAabbBvh(std::vector<ColliderWorldAabb> colliders);

    [[nodiscard]] bool IsValid() const noexcept {
        return m_isValid;
    }

    [[nodiscard]] const std::vector<ColliderWorldAabb>& GetColliders() const noexcept {
        return m_colliders;
    }

    [[nodiscard]] DynamicColliderWorldAabbUpdateResult UpdateAabb(
        Scene::Entity entity, const Math::Aabb& worldAabb) noexcept;

    [[nodiscard]] bool Query(const Math::Aabb& bounds, std::vector<std::size_t>& candidates) const;

private:
    static constexpr std::size_t kInvalidObjectIndex = static_cast<std::size_t>(-1);

    struct Object final {
        Math::Aabb bounds;
        std::size_t begin = 0U;
        std::size_t end = 0U;
        std::size_t left = kInvalidObjectIndex;
        std::size_t right = kInvalidObjectIndex;
        std::size_t parent = kInvalidObjectIndex;
        bool isLeaf = false;
    };

    [[nodiscard]] std::size_t BuildObject(std::size_t begin, std::size_t end,
                                            std::size_t parent);
    void RefitFromLeaf(std::size_t leafObjectIndex) noexcept;

    std::vector<ColliderWorldAabb> m_colliders;
    std::vector<std::size_t> m_indices;
    std::vector<std::size_t> m_leafObjectByCacheIndex;
    std::vector<Object> m_objects;
    std::size_t m_root = kInvalidObjectIndex;
    bool m_isValid = true;
};

/// Builds a flat cache of every entity with both WorldTransformComponent and
/// ColliderComponent via one ForEach pass. CollisionSystem builds its transient static
/// BVH from this snapshot, while RaycastSystem retains its deterministic linear query path.
[[nodiscard]] std::vector<ColliderWorldAabb> BuildColliderWorldAabbCache(
    Scene::IEntityManager& entityManager);

} // namespace UVE::Physics::Detail
