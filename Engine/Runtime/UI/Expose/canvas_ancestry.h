// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cstddef>
#include <cstdint>

#include "entity.h"

namespace UVE::Scene {
class IEntityManager;
}

namespace UVE::UI {

inline constexpr std::size_t kMaximumCanvasAncestorWalk = 64U;

struct CanvasAncestry final {
    Scene::Entity canvas = Scene::kInvalidEntity;
    std::int32_t sortOrder = 0;
    bool visible = true;
    bool hasCanvas = false;
};

[[nodiscard]] CanvasAncestry ResolveCanvasAncestry(Scene::IEntityManager& entityManager,
                                                         Scene::Entity entity);

[[nodiscard]] bool ShouldDrawUiWidget(Scene::IEntityManager& entityManager, Scene::Entity entity);

/// Hierarchy depth: parent-chain links from `entity` up to its root. Dead entities and entities
/// outside the hierarchy report 0. The walk reuses the ancestry cap (plus a self-parent break),
/// so a cycle bottoms out instead of hanging - the layout and anchor passes sort parent-first
/// on this, and share it rather than each carrying a private walk.
[[nodiscard]] std::size_t UIHierarchyDepth(Scene::IEntityManager& entityManager, Scene::Entity entity);

} // namespace UVE::UI
