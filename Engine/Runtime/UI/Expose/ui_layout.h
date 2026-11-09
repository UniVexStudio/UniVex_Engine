// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

namespace UVE::Scene {
class IEntityManagerUVE;
}

namespace UVE::UI {

class UIFontAtlasUVE;

/// Positions every stack container's direct hierarchy children inside the container rect: padding
/// insets all four sides, spacing separates consecutive children along the direction axis, and
/// the cross axis follows the alignment. Children run in siblingOrder (the entity handle breaks
/// ties); containers position shallowest-first so a nested container lands in its final parent
/// before it positions its own children. The position pass writes positions only - child sizes
/// are preserved - while each auto-sized axis is fitted to content in a deepest-first resize pass
/// ahead of it, so a nested auto-sized container settles before its parent measures it. An
/// invalid container is skipped entirely, by both passes.
///
/// A nonzero `wrapAfter` turns the stack into a grid: at most that many items per line along the
/// direction axis, uniform cells sized to the largest participating child, spacing on both axes,
/// and `alignment` resolving within each cell. The grid always starts at the inner edge, and a
/// short line is still spaced as a grid, never packed as a stack.
///
/// Buttons, images, sliders, progress bars, and checkboxes lay out at their rect size, nested
/// containers at their own rect size, and text at {measured width, fontSize} - the width comes
/// from `fontAtlas`'s advance sums, so horizontal stacks and grids pace text exactly as it draws.
/// (Tooltips position themselves at the pointer and never stack; dropdown popups likewise, while
/// the dropdown's box stacks.) A child carrying several positioned components advances the stack
/// by one extent - button first, image second, container third, slider fourth, progress fifth,
/// checkbox sixth, dropdown seventh, text input eighth, scroll container ninth, text last -
/// but every positioned component it has moves to
/// the laid-out
/// position together, so a container with its own background quad stays in one piece. Children
/// without any positioned component are ignored, not spaced. Depth walks reuse the canvas ancestry cap, so a hierarchy cycle degrades
/// to an arbitrary-but-stable container order instead of hanging the frame.
///
/// Anchors (resolved before this pass) feed measurement: an auto axis overwrites whatever the
/// anchor pass wrote for that axis - anchors contribute position, content contributes size - and
/// there is no iteration between them, so an anchored stretch child inside an auto-sized parent
/// settles in one shot from the pre-layout size.
void LayoutUIContainersUVE(Scene::IEntityManagerUVE& entityManager, const UIFontAtlasUVE& fontAtlas);

} // namespace UVE::UI
