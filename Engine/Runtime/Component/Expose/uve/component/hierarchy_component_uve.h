// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#pragma once

#include <cstdint>

#include "uve/component/entity_uve.h"

namespace UVE::Scene {

/// A sibling order that sorts after every one handed out before it. Thread-safe.
[[nodiscard]] std::int64_t NextSiblingOrderUVE() noexcept;

/// The parent link for a scene-graph entity — internal infrastructure SceneGraphUVE owns, not
/// one of the master spec's named built-in components. Deliberately holds only the parent (not
/// a children list too), so there is exactly one place the parent/child relationship is
/// recorded; SceneGraphUVE::GetChildrenUVE() derives the children of a given entity by
/// scanning for this component rather than maintaining a second, redundant index. `parent ==
/// kInvalidEntityUVE` means "this entity is a scene root."
///
/// `siblingOrder` places the entity among its parent's children: lower first. Only the
/// comparison matters, never the number. A new link takes the next order, so it lands after the
/// siblings already there, and SceneGraphUVE gives a moved node a fresh one. The number is never
/// saved: a scene file lists siblings in order, and loading hands out orders in that sequence.
struct HierarchyComponentUVE final {
    EntityUVE parent = kInvalidEntityUVE;
    std::int64_t siblingOrder = NextSiblingOrderUVE();
};

} // namespace UVE::Scene
