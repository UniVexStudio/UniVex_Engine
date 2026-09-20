// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <string>
#include <vector>

#include "uve/render_systems/i_renderer_3d_uve.h"

namespace UVE::Editor {

/// Turns a frame's renderer diagnostics into rows a panel can print.
///
/// WHY THIS IS ITS OWN FILE. The editor's debug dock formatted diagnostics inline, three
/// ImGui::Text calls deep inside DrawBottomDockContentUVE, which meant the formatting could only
/// be read by launching the editor and could not be tested at all. Reading a counter and deciding
/// what it means is not drawing - it is the part most likely to be wrong, and the only part worth
/// asserting - so it lives here, with no imgui dependency, and the dock just prints what it
/// returns.
///
/// This is also the seam the renderer work connects to. The optimisation passes added counters
/// for the placement caches, the visibility clustering and the shadow batcher, all of which are
/// invisible from inside the editor today: a regression that doubles the frame's draw calls while
/// producing a pixel-identical image is exactly the kind this reports and nothing else does.
struct EditorRenderStatRowUVE final {
    /// Group heading this row belongs under, e.g. "Culling". Rows sharing a label are contiguous.
    std::string section;
    std::string label;
    std::string value;

    /// True when the row is reporting something that deserves attention rather than merely a
    /// number - a cache missing every frame, a batcher that merged nothing. Panels can highlight
    /// it; the judgement of what counts as concerning is made here, once, next to the numbers
    /// that justify it rather than scattered through drawing code.
    bool isConcerning = false;
};

/// Builds the rows for one frame's diagnostics. Pure: same input, same output, no engine access.
///
/// Rows are ordered so the pipeline reads top to bottom - what the scene contained, what culling
/// removed, what survived to be drawn - because a stat panel read out of order invites the wrong
/// conclusion about which stage is responsible for a number.
[[nodiscard]] std::vector<EditorRenderStatRowUVE> BuildEditorRenderStatRowsUVE(
    const Render::Renderer3DFrameDiagnosticsUVE& diagnostics);

/// Percentage helper shared by the rows, exposed because it has one edge case worth pinning: a
/// zero total returns 0, not a division by zero, and that case is reached constantly (an empty
/// scene, a frame before the first cull) rather than being a theoretical concern.
[[nodiscard]] float ComputeEditorStatPercentageUVE(std::size_t part, std::size_t total) noexcept;

} // namespace UVE::Editor
