// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#pragma once

#include <cstdint>

#include "entity.h"

namespace UVE::Scene {

/// A sibling order that sorts after every one handed out before it. Thread-safe.
[[nodiscard]] std::int64_t NextSiblingOrder() noexcept;

/// The parent link for a scene-graph entity — internal infrastructure SceneGraph owns, not
/// one of the master spec's named built-in components. Deliberately holds only the parent (not
/// a children list too), so there is exactly one place the parent/child relationship is
/// recorded; SceneGraph::GetChildren() derives the children of a given entity by
/// scanning for this component. `ChildrenIndex` keeps a derived parent->children map for bulk
/// queries, but it is a cache (rebuilt, never authored), so this link stays the one place the
/// relationship is recorded. `parent == kInvalidEntity` means "this entity is a scene root."
///
/// `siblingOrder` places the entity among its parent's children: lower first. Only the
/// comparison matters, never the number. A new link takes the next order, so it lands after the
/// siblings already there, and SceneGraph gives a moved object a fresh one. The number is never
/// saved: a scene file lists siblings in order, and loading hands out orders in that sequence.
struct HierarchyComponent final {
    Entity parent = kInvalidEntity;
    std::int64_t siblingOrder = NextSiblingOrder();
};

/// True when `component` is a scene root (`parent == kInvalidEntity`).
[[nodiscard]] bool IsHierarchyRoot(const HierarchyComponent& component) noexcept;

/// True when `component` does not parent `selfEntity` to itself.
[[nodiscard]] bool IsHierarchyComponentValidForEntity(
    const HierarchyComponent& component, Entity selfEntity = kInvalidEntity) noexcept;

/// Deterministic tie-break order between two `Entity` handles (`index`, then `generation`).
[[nodiscard]] bool CompareEntityHandleOrder(Entity lhs, Entity rhs) noexcept;

/// Canonical sibling ordering shared by `SceneGraph::GetChildren` and `ChildrenIndex`:
/// lower `siblingOrder` first, then `CompareEntityHandleOrder`.
[[nodiscard]] bool CompareSiblingHierarchyOrder(std::int64_t lhsOrder, Entity lhsEntity,
                                                   std::int64_t rhsOrder,
                                                   Entity rhsEntity) noexcept;

/// Updates `component.parent` to `newParent` and hands out a fresh `NextSiblingOrder()` when
/// the parent link changed. Returns true when `parent` changed.
bool AssignParentAndAdvanceSiblingOrder(HierarchyComponent& component,
                                           Entity newParent) noexcept;

} // namespace UVE::Scene
