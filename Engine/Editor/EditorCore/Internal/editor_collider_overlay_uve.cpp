// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

// The editor's "Show Collision" overlay: the bridge between the physics layer's wireframe
// geometry and the viewport's line list.
//
// What lives here and what does not. The GEOMETRY is `Physics::BuildColliderDebugLinesUVE` - pure
// math over one collider, no ECS, unit-tested without a world. WHICH colliders exist and whether
// they can be hit is `Scene::BuildColliderDebugSceneUVE`, which asks the same participation
// helpers the collision system asks. This file owns exactly one thing those two cannot:
// translating the result into the viewport's own layout (plain float triples, no engine math
// types), so EditorCore keeps its documented promise of not linking the viewport module.

#include <cstddef>
#include <span>
#include <vector>

#include "uve/editor/editor_uve.h"

#include "uve/objects/3d/collider_debug_scene_uve.h"

namespace UVE::Editor {
namespace {

/// Enough for a level's worth of props at the default collider budget: a few hundred boxes and a
/// dozen capsules fit with room to spare. 16K segments is 384 KB of line data, and a level past it
/// degrades to the first shapes that fit rather than to an unbounded allocation on a debug toggle.
constexpr std::size_t kMaximumOverlayLinesUVE = 16384U;

/// Reused across frames so a visible overlay does not allocate every frame. Thread-local because
/// an EditorUVE is main-thread-only, and a static would still be wrong if a second editor instance
/// ever runs in the same process - 384 KB is cheap, aliasing between two editors is not.
[[nodiscard]] std::vector<Physics::ColliderDebugLineUVE>& OverlayScratchUVE() {
    static thread_local std::vector<Physics::ColliderDebugLineUVE> scratch(kMaximumOverlayLinesUVE);
    return scratch;
}

} // namespace

void EditorUVE::BuildColliderOverlayUVE(std::vector<ViewportColliderLineUVE>& outLines) const {
    outLines.clear();
    if (!m_viewportOverlayState.collisionShapesVisible) {
        return;
    }

    std::vector<Physics::ColliderDebugLineUVE>& scratch = OverlayScratchUVE();

    Scene::ColliderDebugSceneSettingsUVE settings;
    // The overlay is a view of the whole document, so the layer filter stays open: narrowing it is
    // a per-project need and belongs on the settings struct, not hard-coded here.
    settings.maximumColliders = 1024U;

    const Scene::ColliderDebugSceneStatsUVE stats = Scene::BuildColliderDebugSceneUVE(
        m_services->GetEntityManagerUVE(), settings,
        std::span<Physics::ColliderDebugLineUVE>{scratch.data(), kMaximumOverlayLinesUVE});

    outLines.reserve(stats.lineCount);
    for (std::size_t index = 0U; index < stats.lineCount; ++index) {
        const Physics::ColliderDebugLineUVE& line = scratch[index];
        outLines.push_back(ViewportColliderLineUVE{{line.from.x, line.from.y, line.from.z},
                                                   {line.to.x, line.to.y, line.to.z}});
    }
}

bool EditorUVE::SetViewportCollisionShapesUVE(const bool visible) {
    const bool previous = m_viewportOverlayState.collisionShapesVisible;
    m_viewportOverlayState.collisionShapesVisible = visible;
    if (!visible) {
        // Dropping the geometry with the flag keeps the "no lines while off" invariant true for
        // the host, which reads both fields without re-checking the flag first.
        m_viewportOverlayState.colliderLines.clear();
        m_entityViewportOverlayState.colliderLines.clear();
        m_retargetViewportOverlayState.colliderLines.clear();
    }
    return previous != visible;
}

} // namespace UVE::Editor
