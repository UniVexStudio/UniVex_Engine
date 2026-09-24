// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

#include <imgui.h>

#include "editor_chrome_layout_uve.h"

namespace UVE::Editor {

/// Small drawing helpers shared by the editor's panels. The node, category and content type
/// pictures themselves are textures (EditorUiAssetsUVE, built from assets/icons/); what is left
/// here is the icon-then-label row and the viewport's move glyph.

/// Icon-then-label rows, shared by the hierarchy and the inspector.
///
/// Moved here because it was file-local in editor_uve.cpp and is used from inside AND outside the
/// inspector, so splitting that panel out would otherwise have meant a second copy. DrawNativeIconLabelUVE is inline because a
/// non-inline definition in a header fails to link the moment a second translation unit includes
/// it - the mistake I made and caught on the first of these headers.

inline void DrawNativeIconLabelUVE(const std::uintptr_t textureId, const char* const label) {
    if (textureId != 0U) {
        // Centred on the text line rather than hanging from its top, so icon and name share a
        // middle whatever the font size.
        const float line = ImGui::GetTextLineHeight();
        // Whole pixels, so an icon texture's texels land on screen pixels and stay sharp.
        const float size = std::min(16.0F, std::floor(line));
        const ImVec2 cursor = ImGui::GetCursorScreenPos();
        ImGui::Dummy(ImVec2{size, line});
        const ImVec2 iconMin{std::floor(cursor.x), std::floor(cursor.y + ((line - size) * 0.5F))};
        ImGui::GetWindowDrawList()->AddImage(static_cast<ImTextureID>(textureId), iconMin,
                                             ImVec2{iconMin.x + size, iconMin.y + size});
        ImGui::SameLine(0.0F, 6.0F);
    }
    ImGui::TextUnformatted(label);
}

inline void DrawMoveIconUVE(ImDrawList& drawList, const ImVec2 center, const float radius, const ImU32 color) {
    const float armLength = radius * 0.62F;
    const float headSize = radius * 0.30F;
    const std::array<ImVec2, 4> directions{ImVec2{1.0F, 0.0F}, ImVec2{-1.0F, 0.0F}, ImVec2{0.0F, 1.0F},
                                           ImVec2{0.0F, -1.0F}};
    for (const ImVec2& direction : directions) {
        const ImVec2 tip{center.x + direction.x * armLength, center.y + direction.y * armLength};
        drawList.AddLine(center, tip, color, 1.5F);
        const ImVec2 perpendicular{-direction.y, direction.x};
        const ImVec2 baseA{tip.x - direction.x * headSize + perpendicular.x * headSize * 0.55F,
                           tip.y - direction.y * headSize + perpendicular.y * headSize * 0.55F};
        const ImVec2 baseB{tip.x - direction.x * headSize - perpendicular.x * headSize * 0.55F,
                           tip.y - direction.y * headSize - perpendicular.y * headSize * 0.55F};
        drawList.AddTriangleFilled(tip, baseA, baseB, color);
    }
}

/// A node icon at the start of a menu line, sized and centred like a Scene row's, followed on the
/// same line by whatever the caller draws next (the Scene "+" and the Content "+ Add" menus).
/// Nothing when the texture is missing.
inline void DrawNodePickerIconUVE(const std::uintptr_t textureId) {
    if (textureId == 0U) {
        return;
    }
    const float line = ImGui::GetTextLineHeight();
    const float size = std::min(kHierarchyNodeIconSizeUVE, std::floor(line));
    const ImVec2 cursor = ImGui::GetCursorScreenPos();
    ImGui::Dummy(ImVec2{size, line});
    const ImVec2 iconMin{std::floor(cursor.x), std::floor(cursor.y + ((line - size) * 0.5F))};
    ImGui::GetWindowDrawList()->AddImage(static_cast<ImTextureID>(textureId), iconMin,
                                         ImVec2{iconMin.x + size, iconMin.y + size});
    ImGui::SameLine(0.0F, ImGui::GetStyle().ItemInnerSpacing.x);
}

} // namespace UVE::Editor
