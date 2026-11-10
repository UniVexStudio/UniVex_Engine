// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <string_view>

#include <imgui.h>

#include "scene_object_registry.h"
#include "editor_chrome_layout.h"

namespace UVE::Editor {

/// Category indicator colors: Color identity for object types and assets.
[[nodiscard]] inline ImU32 GetCategoryIndicatorColorUVE(const std::string_view categoryOrGroup) noexcept {
    if (categoryOrGroup == "3D" || categoryOrGroup == "Shapes" || categoryOrGroup == "Mesh" ||
        categoryOrGroup == "Model" || categoryOrGroup == "Geometry" || categoryOrGroup == "Entity") {
        return IM_COL32(0, 168, 255, 255); // Cyan / Sky Blue
    }
    if (categoryOrGroup == "Lighting" || categoryOrGroup == "Lights") {
        return IM_COL32(245, 158, 11, 255); // Amber Gold
    }
    if (categoryOrGroup == "Material" || categoryOrGroup == "Shader" || categoryOrGroup == "Materials") {
        return IM_COL32(16, 185, 129, 255); // Emerald Green
    }
    if (categoryOrGroup == "Texture" || categoryOrGroup == "Textures") {
        return IM_COL32(249, 115, 22, 255); // Deep Orange
    }
    if (categoryOrGroup == "Animation" || categoryOrGroup == "Animations" || categoryOrGroup == "Sequencer") {
        return IM_COL32(168, 85, 247, 255); // Purple / Lavender
    }
    if (categoryOrGroup == "Physics" || categoryOrGroup == "Colliders") {
        return IM_COL32(6, 182, 212, 255); // Teal / Cyan
    }
    if (categoryOrGroup == "Audio" || categoryOrGroup == "Sound") {
        return IM_COL32(239, 68, 68, 255); // Coral Red
    }
    if (categoryOrGroup == "UI" || categoryOrGroup == "Canvas" || categoryOrGroup == "2D") {
        return IM_COL32(99, 102, 241, 255); // Indigo / Violet
    }
    if (categoryOrGroup == "Script" || categoryOrGroup == "Logic" || categoryOrGroup == "Code") {
        return IM_COL32(56, 189, 248, 255); // Sky Blue
    }
    if (categoryOrGroup == "Folder" || categoryOrGroup == "Directory" || categoryOrGroup == "Scene") {
        return IM_COL32(245, 158, 11, 255); // Folder Amber
    }
    if (categoryOrGroup == "Camera") {
        return IM_COL32(59, 130, 246, 255); // Bright Blue
    }
    if (categoryOrGroup == "World" || categoryOrGroup == "Navigation") {
        return IM_COL32(34, 197, 94, 255); // Forest / World Green
    }
    if (categoryOrGroup == "Combat" || categoryOrGroup == "Gameplay") {
        return IM_COL32(244, 63, 94, 255); // Rose / Red
    }
    if (categoryOrGroup == "VFX") {
        return IM_COL32(249, 115, 22, 255); // VFX Orange
    }
    if (categoryOrGroup == "Rendering") {
        return IM_COL32(0, 168, 255, 255); // Rendering Cyan
    }
    if (categoryOrGroup == "Optimization") {
        return IM_COL32(100, 116, 139, 255); // Slate
    }
    return IM_COL32(148, 163, 184, 255); // Default Slate
}

[[nodiscard]] inline ImU32 GetSceneObjectIndicatorColorUVE(const Scene::Objects::SceneObjectKindUVE kind) noexcept {
    if (kind == Scene::Objects::SceneObjectKindUVE::Light3D ||
        kind == Scene::Objects::SceneObjectKindUVE::DirectionalLight3D) {
        return IM_COL32(245, 158, 11, 255); // Amber Gold for lights
    }
    const Scene::Objects::SceneObjectDescriptorUVE* const desc =
        Scene::Objects::FindSceneObjectDescriptorUVE(kind);
    if (desc != nullptr) {
        return GetCategoryIndicatorColorUVE(desc->category);
    }
    return IM_COL32(0, 168, 255, 255);
}
inline void DrawSlotIndicatorIconUVE(const std::uintptr_t textureId, const ImU32 indicatorColor = 0U) {
    const float line = ImGui::GetTextLineHeight();
    const float slotSize = std::max(20.0F, std::floor(line + 2.0F));
    const ImVec2 cursor = ImGui::GetCursorScreenPos();
    ImGui::Dummy(ImVec2{slotSize, line});
    if (textureId == 0U) {
        ImGui::SameLine(0.0F, ImGui::GetStyle().ItemInnerSpacing.x + 3.0F);
        return;
    }

    ImDrawList* const drawList = ImGui::GetWindowDrawList();
    const float slotY = std::floor(cursor.y + ((line - slotSize) * 0.5F));
    const ImVec2 slotMin{std::floor(cursor.x), slotY};
    const ImVec2 slotMax{slotMin.x + slotSize, slotY + slotSize};

    // 1. Slot frame background (dark recessed box)
    constexpr float kRounding = 3.5F;
    drawList->AddRectFilled(slotMin, slotMax, IM_COL32(20, 22, 28, 255), kRounding);
    drawList->AddRect(slotMin, slotMax, IM_COL32(45, 49, 60, 200), kRounding, 0, 1.0F);

    // 2. Category indicator bar on the left edge (2.5px width)
    if (indicatorColor != 0U) {
        drawList->AddRectFilled(ImVec2{slotMin.x, slotMin.y + 1.0F},
                                ImVec2{slotMin.x + 2.5F, slotMax.y - 1.0F},
                                indicatorColor, 1.5F);
    }

    // 3. Crisp monochrome icon centered inside the slot
    const float iconSize = std::floor(slotSize * 0.70F);
    const float iconX = std::floor(slotMin.x + 2.5F + (slotSize - 2.5F - iconSize) * 0.5F);
    const float iconY = std::floor(slotMin.y + (slotSize - iconSize) * 0.5F);
    drawList->AddImage(static_cast<ImTextureID>(textureId),
                       ImVec2{iconX, iconY},
                       ImVec2{iconX + iconSize, iconY + iconSize});

    ImGui::SameLine(0.0F, ImGui::GetStyle().ItemInnerSpacing.x + 4.0F);
}

/// Icon-then-label rows, shared by the hierarchy and the inspector.
inline void DrawNativeIconLabelUVE(const std::uintptr_t textureId, const char* const label) {
    if (textureId != 0U) {
        const float line = ImGui::GetTextLineHeight();
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

inline void DrawSelectIconUVE(ImDrawList& drawList, const ImVec2 center, const float radius, const ImU32 color) {
    const ImVec2 tip{center.x - radius * 0.45F, center.y - radius * 0.55F};
    const ImVec2 bottom{center.x - radius * 0.45F, center.y + radius * 0.45F};
    const ImVec2 right{center.x + radius * 0.45F, center.y + radius * 0.10F};
    drawList.AddTriangleFilled(tip, bottom, right, color);
}

inline void DrawRotateIconUVE(ImDrawList& drawList, const ImVec2 center, const float radius, const ImU32 color) {
    const float arcRadius = radius * 0.58F;
    constexpr float kPi = 3.14159265358979323846F;
    constexpr float kStartAngle = -0.35F * kPi;
    constexpr float kEndAngle = 1.15F * kPi;
    drawList.PathArcTo(center, arcRadius, kStartAngle, kEndAngle, 24);
    drawList.PathStroke(color, ImDrawFlags_None, 1.5F);
    const ImVec2 tip{center.x + std::cos(kEndAngle) * arcRadius, center.y + std::sin(kEndAngle) * arcRadius};
    const ImVec2 tangent{-std::sin(kEndAngle), std::cos(kEndAngle)};
    const float headSize = radius * 0.28F;
    const ImVec2 outward{std::cos(kEndAngle), std::sin(kEndAngle)};
    const ImVec2 baseA{tip.x - tangent.x * headSize + outward.x * headSize * 0.5F,
                       tip.y - tangent.y * headSize + outward.y * headSize * 0.5F};
    const ImVec2 baseB{tip.x + tangent.x * headSize + outward.x * headSize * 0.5F,
                       tip.y + tangent.y * headSize + outward.y * headSize * 0.5F};
    drawList.AddTriangleFilled(tip, baseA, baseB, color);
}

inline void DrawScaleIconUVE(ImDrawList& drawList, const ImVec2 center, const float radius, const ImU32 color) {
    const float half = radius * 0.42F;
    const ImVec2 topLeft{center.x - half, center.y - half};
    const ImVec2 bottomRight{center.x + half, center.y + half};
    drawList.AddRect(topLeft, bottomRight, color, 0.0F, 0, 1.4F);
    const float handle = radius * 0.20F;
    const std::array<ImVec2, 4> corners{topLeft, ImVec2{bottomRight.x, topLeft.y}, bottomRight,
                                        ImVec2{topLeft.x, bottomRight.y}};
    for (const ImVec2& corner : corners) {
        drawList.AddRectFilled(ImVec2{corner.x - handle * 0.5F, corner.y - handle * 0.5F},
                               ImVec2{corner.x + handle * 0.5F, corner.y + handle * 0.5F}, color);
    }
}

inline void DrawUniversalIconUVE(ImDrawList& drawList, const ImVec2 center, const float radius, const ImU32 color) {
    drawList.AddCircle(center, radius * 0.62F, color, 20, 1.3F);
    const float half = radius * 0.22F;
    drawList.AddRectFilled(ImVec2{center.x - half, center.y - half}, ImVec2{center.x + half, center.y + half},
                           color);
}

inline void DrawGridIconUVE(ImDrawList& drawList, const ImVec2 center, const float radius, const ImU32 color) {
    const float half = radius * 0.5F;
    const float third = half * 2.0F / 3.0F;
    drawList.AddRect(ImVec2{center.x - half, center.y - half}, ImVec2{center.x + half, center.y + half}, color,
                     0.0F, 0, 1.2F);
    for (int index = 1; index < 3; ++index) {
        const float offset = -half + third * static_cast<float>(index);
        drawList.AddLine(ImVec2{center.x - half, center.y + offset}, ImVec2{center.x + half, center.y + offset},
                         color, 1.0F);
        drawList.AddLine(ImVec2{center.x + offset, center.y - half}, ImVec2{center.x + offset, center.y + half},
                         color, 1.0F);
    }
}

inline void DrawViewportModeIconUVE(ImDrawList& drawList, const ImVec2 center, const float radius, const ImU32 color) {
    const float half = radius * 0.72F;
    drawList.AddRect(ImVec2{center.x - half, center.y - half}, ImVec2{center.x + half, center.y + half}, color,
                     1.5F, 0, 1.4F);
    drawList.AddLine(ImVec2{center.x - half, center.y}, ImVec2{center.x + half, center.y}, color, 1.2F);
}

/// An object icon at the start of a menu line, sized and centred like a Scene row's.
inline void DrawObjectPickerIconUVE(const std::uintptr_t textureId, const ImU32 indicatorColor = 0U) {
    if (indicatorColor != 0U) {
        DrawSlotIndicatorIconUVE(textureId, indicatorColor);
        return;
    }
    if (textureId == 0U) {
        return;
    }
    const float line = ImGui::GetTextLineHeight();
    const float size = std::min(kHierarchyObjectIconSizeUVE, std::floor(line));
    const ImVec2 cursor = ImGui::GetCursorScreenPos();
    ImGui::Dummy(ImVec2{size, line});
    const ImVec2 iconMin{std::floor(cursor.x), std::floor(cursor.y + ((line - size) * 0.5F))};
    ImGui::GetWindowDrawList()->AddImage(static_cast<ImTextureID>(textureId), iconMin,
                                         ImVec2{iconMin.x + size, iconMin.y + size});
    ImGui::SameLine(0.0F, ImGui::GetStyle().ItemInnerSpacing.x);
}

} // namespace UVE::Editor
