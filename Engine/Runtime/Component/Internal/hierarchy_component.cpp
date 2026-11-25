// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "hierarchy_component.h"

#include <atomic>

namespace UVE::Scene {

std::int64_t NextSiblingOrder() noexcept {
    // One counter for the process. Orders are compared only between siblings, so any strictly
    // increasing source works; 64 bits cannot run out in a session.
    static std::atomic<std::int64_t> next{0};
    return next.fetch_add(1, std::memory_order_relaxed) + 1;
}

bool IsHierarchyRoot(const HierarchyComponent& component) noexcept {
    return component.parent == kInvalidEntity;
}

bool IsHierarchyComponentValidForEntity(const HierarchyComponent& component,
                                           const Entity selfEntity) noexcept {
    if (selfEntity == kInvalidEntity) {
        return true;
    }
    return component.parent != selfEntity;
}

bool CompareEntityHandleOrder(const Entity lhs, const Entity rhs) noexcept {
    if (lhs.index != rhs.index) {
        return lhs.index < rhs.index;
    }
    return lhs.generation < rhs.generation;
}

bool CompareSiblingHierarchyOrder(const std::int64_t lhsOrder, const Entity lhsEntity,
                                     const std::int64_t rhsOrder,
                                     const Entity rhsEntity) noexcept {
    if (lhsOrder != rhsOrder) {
        return lhsOrder < rhsOrder;
    }
    return CompareEntityHandleOrder(lhsEntity, rhsEntity);
}

bool AssignParentAndAdvanceSiblingOrder(HierarchyComponent& component,
                                           const Entity newParent) noexcept {
    if (component.parent == newParent) {
        return false;
    }
    component.parent = newParent;
    component.siblingOrder = NextSiblingOrder();
    return true;
}

} // namespace UVE::Scene
