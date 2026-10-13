// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cstddef>
#include <cstdint>

#include "uve/component/entity_uve.h"

namespace UVE::Scene {
class IEntityManagerUVE;
}

namespace UVE::UI {

inline constexpr std::size_t kMaximumCanvasAncestorWalkUVE = 64U;

struct CanvasAncestryUVE final {
    Scene::EntityUVE canvas = Scene::kInvalidEntityUVE;
    std::int32_t sortOrder = 0;
    bool visible = true;
    bool hasCanvas = false;
};

[[nodiscard]] CanvasAncestryUVE ResolveCanvasAncestryUVE(Scene::IEntityManagerUVE& entityManager,
                                                         Scene::EntityUVE entity);

[[nodiscard]] bool ShouldDrawUiWidgetUVE(Scene::IEntityManagerUVE& entityManager, Scene::EntityUVE entity);

/// Hierarchy depth: parent-chain links from `entity` up to its root. Dead entities and entities
/// outside the hierarchy report 0. The walk reuses the ancestry cap (plus a self-parent break),
/// so a cycle bottoms out instead of hanging - the layout and anchor passes sort parent-first
/// on this, and share it rather than each carrying a private walk.
[[nodiscard]] std::size_t UIHierarchyDepthUVE(Scene::IEntityManagerUVE& entityManager, Scene::EntityUVE entity);

} // namespace UVE::UI
