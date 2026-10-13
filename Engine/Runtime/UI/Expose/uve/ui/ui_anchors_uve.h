// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

namespace UVE::Math {
struct Vector2UVE;
}

namespace UVE::Scene {
class IEntityManagerUVE;
}

namespace UVE::UI {

/// Resolves every anchored widget's rect against its parent (or `viewportSize` for roots):
/// resolvedMin = parentMin + anchorMin * parentSize + offsetMin, likewise for max, sizes clamped
/// at zero per axis. Widgets resolve shallowest-first so an anchored parent's fresh rect feeds
/// its children in the same pass; depth ties break by entity handle, and hierarchy cycles
/// degrade to a stable order via the shared ancestry cap instead of hanging.
///
/// A parent qualifies by carrying a container, button, image, slider, progress-bar, checkbox,
/// dropdown, text-input, or scroll-container rect, in that priority - anything else anchors
/// against the viewport. Buttons, images, containers, sliders, progress bars, checkboxes,
/// dropdowns, text inputs, and scroll containers take the full resolved rect; texts take the
/// resolved minimum as positionPixels. (Tooltips
/// position themselves and never anchor; dropdown popups likewise, while the dropdown's box
/// anchors.) Anchored entities without
/// any positioned component are skipped, and an invalid anchor component is skipped fail-closed.
/// Runs before the stack/grid pass, which then overwrites container children's positions - for
/// those widgets anchors contribute size, the container contributes position.
void ResolveUIAnchorsUVE(Scene::IEntityManagerUVE& entityManager, const Math::Vector2UVE& viewportSize);

} // namespace UVE::UI
