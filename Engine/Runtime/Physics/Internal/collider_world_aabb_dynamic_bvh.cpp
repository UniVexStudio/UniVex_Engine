// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "collider_world_aabb_cache.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <numeric>
#include <utility>

namespace UVE::Physics::Detail {
namespace {

constexpr std::size_t kBvhLeafSize = 4U;
constexpr std::size_t kBvhMaximumTraversalStack = 128U;

[[nodiscard]] bool IsValidAabb(const Math::Aabb& aabb) noexcept {
    return std::isfinite(aabb.min.x) && std::isfinite(aabb.min.y) && std::isfinite(aabb.min.z) &&
           std::isfinite(aabb.max.x) && std::isfinite(aabb.max.y) && std::isfinite(aabb.max.z) &&
           aabb.min.x <= aabb.max.x && aabb.min.y <= aabb.max.y && aabb.min.z <= aabb.max.z;
}

} // namespace

DynamicAabbBvh::DynamicAabbBvh(std::vector<ColliderWorldAabb> colliders)
    : m_colliders(std::move(colliders)), m_indices(m_colliders.size()),
      m_leafObjectByCacheIndex(m_colliders.size(), kInvalidObjectIndex) {
    if (m_colliders.size() > kMaximumProxies ||
        !std::all_of(m_colliders.begin(), m_colliders.end(), [](const ColliderWorldAabb& collider) {
            return collider.entity != Scene::kInvalidEntity && IsValidAabb(collider.worldAabb);
        })) {
        m_isValid = false;
        return;
    }
    for (std::size_t firstIndex = 0U; firstIndex < m_colliders.size(); ++firstIndex) {
        for (std::size_t secondIndex = firstIndex + 1U; secondIndex < m_colliders.size(); ++secondIndex) {
            if (m_colliders[firstIndex].entity == m_colliders[secondIndex].entity) {
                m_isValid = false;
                return;
            }
        }
    }
    std::iota(m_indices.begin(), m_indices.end(), 0U);
    if (!m_indices.empty()) {
        m_objects.reserve(m_indices.size() * 2U);
        m_root = BuildObject(0U, m_indices.size(), kInvalidObjectIndex);
    }
}

std::size_t DynamicAabbBvh::BuildObject(const std::size_t begin, const std::size_t end,
                                            const std::size_t parent) {
    const std::size_t objectIndex = m_objects.size();
    m_objects.push_back(Object{});

    Math::Aabb bounds = m_colliders[m_indices[begin]].worldAabb;
    for (std::size_t index = begin + 1U; index < end; ++index) {
        bounds = bounds.Union(m_colliders[m_indices[index]].worldAabb);
    }

    Object& object = m_objects[objectIndex];
    object.bounds = bounds;
    object.begin = begin;
    object.end = end;
    object.parent = parent;
    if (end - begin <= kBvhLeafSize) {
        object.isLeaf = true;
        for (std::size_t index = begin; index < end; ++index) {
            m_leafObjectByCacheIndex[m_indices[index]] = objectIndex;
        }
        return objectIndex;
    }

    const Math::Vector3 extents = bounds.GetExtents();
    std::size_t splitAxis = 0U;
    if (extents.y > extents.x && extents.y >= extents.z) {
        splitAxis = 1U;
    } else if (extents.z > extents.x && extents.z > extents.y) {
        splitAxis = 2U;
    }
    const auto CenterOnAxis = [&](const std::size_t cacheIndex) noexcept {
        const Math::Vector3 center = m_colliders[cacheIndex].worldAabb.GetCenter();
        return splitAxis == 0U ? center.x : (splitAxis == 1U ? center.y : center.z);
    };
    std::stable_sort(m_indices.begin() + static_cast<std::ptrdiff_t>(begin),
                     m_indices.begin() + static_cast<std::ptrdiff_t>(end),
                     [&](const std::size_t lhs, const std::size_t rhs) {
                         const float leftCenter = CenterOnAxis(lhs);
                         const float rightCenter = CenterOnAxis(rhs);
                         return leftCenter < rightCenter ||
                                (leftCenter == rightCenter && lhs < rhs);
                     });

    const std::size_t middle = begin + (end - begin) / 2U;
    const std::size_t left = BuildObject(begin, middle, objectIndex);
    const std::size_t right = BuildObject(middle, end, objectIndex);
    m_objects[objectIndex].left = left;
    m_objects[objectIndex].right = right;
    return objectIndex;
}

void DynamicAabbBvh::RefitFromLeaf(const std::size_t leafObjectIndex) noexcept {
    std::size_t objectIndex = leafObjectIndex;
    while (objectIndex != kInvalidObjectIndex) {
        Object& object = m_objects[objectIndex];
        if (object.isLeaf) {
            object.bounds = m_colliders[m_indices[object.begin]].worldAabb;
            for (std::size_t index = object.begin + 1U; index < object.end; ++index) {
                object.bounds = object.bounds.Union(m_colliders[m_indices[index]].worldAabb);
            }
        } else {
            object.bounds = m_objects[object.left].bounds.Union(m_objects[object.right].bounds);
        }
        objectIndex = object.parent;
    }
}

DynamicColliderWorldAabbUpdateResult DynamicAabbBvh::UpdateAabb(
    const Scene::Entity entity, const Math::Aabb& worldAabb) noexcept {
    if (entity == Scene::kInvalidEntity) {
        return {DynamicColliderWorldAabbUpdateCode::InvalidEntity,
                "Dynamic BVH update rejected because the entity handle is invalid."};
    }
    if (!IsValidAabb(worldAabb)) {
        return {DynamicColliderWorldAabbUpdateCode::InvalidAabb,
                "Dynamic BVH update rejected because the world AABB is invalid."};
    }
    if (!m_isValid) {
        return {DynamicColliderWorldAabbUpdateCode::UnknownProxy,
                "Dynamic BVH update rejected because the copied proxy topology is invalid."};
    }

    const auto iterator = std::find_if(m_colliders.begin(), m_colliders.end(),
                                       [entity](const ColliderWorldAabb& collider) {
                                           return collider.entity == entity;
                                       });
    if (iterator == m_colliders.end()) {
        const bool sameIndex = std::any_of(m_colliders.begin(), m_colliders.end(), [entity](const auto& collider) {
            return collider.entity.index == entity.index;
        });
        return {sameIndex ? DynamicColliderWorldAabbUpdateCode::StaleGeneration
                          : DynamicColliderWorldAabbUpdateCode::UnknownProxy,
                sameIndex ? "Dynamic BVH update rejected because the entity generation is stale."
                          : "Dynamic BVH update rejected because the entity is not a BVH proxy."};
    }

    const std::size_t cacheIndex = static_cast<std::size_t>(iterator - m_colliders.begin());
    iterator->worldAabb = worldAabb;
    RefitFromLeaf(m_leafObjectByCacheIndex[cacheIndex]);
    return {DynamicColliderWorldAabbUpdateCode::Applied, "Dynamic BVH AABB update applied."};
}

bool DynamicAabbBvh::Query(const Math::Aabb& bounds,
                                 std::vector<std::size_t>& candidates) const {
    candidates.clear();
    if (!m_isValid || !IsValidAabb(bounds)) {
        return false;
    }
    if (m_indices.empty()) {
        return true;
    }

    std::array<std::size_t, kBvhMaximumTraversalStack> stack{};
    std::size_t stackSize = 0U;
    stack[stackSize++] = m_root;
    while (stackSize > 0U) {
        const Object& object = m_objects[stack[--stackSize]];
        if (!bounds.Intersects(object.bounds)) {
            continue;
        }
        if (object.isLeaf) {
            candidates.insert(candidates.end(), m_indices.begin() + static_cast<std::ptrdiff_t>(object.begin),
                              m_indices.begin() + static_cast<std::ptrdiff_t>(object.end));
            continue;
        }
        if (stackSize + 2U > stack.size()) {
            return false;
        }
        stack[stackSize++] = object.right;
        stack[stackSize++] = object.left;
    }
    return true;
}

} // namespace UVE::Physics::Detail

// EOF
