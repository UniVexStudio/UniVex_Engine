// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <string_view>

#include <imgui.h>

#include "uve/scene/objects/scene_object_registry_uve.h"
#include "editor_chrome_layout_uve.h"

namespace UVE::Editor {

/// Category indicator colors: Unreal-style color identity for object types and assets.
[[nodiscard]] inline ImU32 GetCategoryIndicatorColorUVE(const std::string_view categoryOrGroup) noexcept {
    if (categoryOrGroup == "3D" || categoryOrGroup == "Shapes" || categoryOrGroup == "Mesh" ||
        categoryOrGroup == "Model" || categoryOrGroup == "Geometry" || categoryOrGroup == "Entity") {
        return IM_COL32(0, 168, 255, 255); // Cyan / Unreal Sky Blue
    }
    if (categoryOrGroup == "Lighting" || categoryOrGroup == "Lights" || categoryOrGroup == "Environment") {
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
    if (categoryOrGroup == "Script" || categoryOrGroup == "Code") {
        return IM_COL32(56, 189, 248, 255); // Sky Blue
    }
    if (categoryOrGroup == "Folder" || categoryOrGroup == "Directory") {
        return IM_COL32(245, 158, 11, 255); // Folder Amber
    }
    if (categoryOrGroup == "Camera") {
        return IM_COL32(59, 130, 246, 255); // Bright Blue
    }
    if (categoryOrGroup == "World") {
        return IM_COL32(34, 197, 94, 255); // Forest / World Green
    }
    return IM_COL32(148, 163, 184, 255); // Slate
}

[[nodiscard]] inline ImU32 GetSceneObjectIndicatorColorUVE(const Scene::Objects::SceneObjectKindUVE kind) noexcept {
    switch (kind) {
        case Scene::Objects::SceneObjectKindUVE::MeshInstance3D:
        case Scene::Objects::SceneObjectKindUVE::BoxMesh3D:
        case Scene::Objects::SceneObjectKindUVE::SphereMesh3D:
        case Scene::Objects::SceneObjectKindUVE::CylinderMesh3D:
        case Scene::Objects::SceneObjectKindUVE::CapsuleMesh3D:
        case Scene::Objects::SceneObjectKindUVE::PlaneMesh3D:
        case Scene::Objects::SceneObjectKindUVE::MultiMeshInstance3D:
        case Scene::Objects::SceneObjectKindUVE::CsgBox3D:
        case Scene::Objects::SceneObjectKindUVE::CsgCylinder3D:
        case Scene::Objects::SceneObjectKindUVE::CsgSphere3D:
        case Scene::Objects::SceneObjectKindUVE::CsgMesh3D:
        case Scene::Objects::SceneObjectKindUVE::CsgCombiner3D:
        case Scene::Objects::SceneObjectKindUVE::Object3D:
            return IM_COL32(0, 168, 255, 255); // Blue

        case Scene::Objects::SceneObjectKindUVE::Light3D:
        case Scene::Objects::SceneObjectKindUVE::DirectionalLight3D:
        case Scene::Objects::SceneObjectKindUVE::PointLight3D:
        case Scene::Objects::SceneObjectKindUVE::SpotLight3D:
        case Scene::Objects::SceneObjectKindUVE::WorldEnvironment3D:
        case Scene::Objects::SceneObjectKindUVE::ReflectionProbe3D:
        case Scene::Objects::SceneObjectKindUVE::FogVolume3D:
        case Scene::Objects::SceneObjectKindUVE::Decal3D:
        case Scene::Objects::SceneObjectKindUVE::VoxelGI3D:
        case Scene::Objects::SceneObjectKindUVE::LightmapGI3D:
            return IM_COL32(245, 158, 11, 255); // Amber

        case Scene::Objects::SceneObjectKindUVE::Camera3D:
        case Scene::Objects::SceneObjectKindUVE::Camera2D:
        case Scene::Objects::SceneObjectKindUVE::SpringArm3D:
        case Scene::Objects::SceneObjectKindUVE::XrCamera3D:
            return IM_COL32(59, 130, 246, 255); // Blue

        case Scene::Objects::SceneObjectKindUVE::Static3D:
        case Scene::Objects::SceneObjectKindUVE::Rigid3D:
        case Scene::Objects::SceneObjectKindUVE::Character3D:
        case Scene::Objects::SceneObjectKindUVE::Player3D:
        case Scene::Objects::SceneObjectKindUVE::Area3D:
        case Scene::Objects::SceneObjectKindUVE::Collider3D:
        case Scene::Objects::SceneObjectKindUVE::CollisionPolygon3D:
        case Scene::Objects::SceneObjectKindUVE::RayCast3D:
        case Scene::Objects::SceneObjectKindUVE::ShapeCast3D:
        case Scene::Objects::SceneObjectKindUVE::PinJoint3D:
        case Scene::Objects::SceneObjectKindUVE::HingeJoint3D:
        case Scene::Objects::SceneObjectKindUVE::SliderJoint3D:
        case Scene::Objects::SceneObjectKindUVE::ConeTwistJoint3D:
        case Scene::Objects::SceneObjectKindUVE::Generic6DOFJoint3D:
        case Scene::Objects::SceneObjectKindUVE::VehicleBody3D:
        case Scene::Objects::SceneObjectKindUVE::VehicleWheel3D:
            return IM_COL32(6, 182, 212, 255); // Teal / Cyan

        case Scene::Objects::SceneObjectKindUVE::AnimationSequencer:
        case Scene::Objects::SceneObjectKindUVE::AnimationGraph:
        case Scene::Objects::SceneObjectKindUVE::Skeleton3D:
        case Scene::Objects::SceneObjectKindUVE::BoneAttachment3D:
        case Scene::Objects::SceneObjectKindUVE::PhysicalBone3D:
        case Scene::Objects::SceneObjectKindUVE::PhysicalBoneSimulator3D:
        case Scene::Objects::SceneObjectKindUVE::LookAtModifier3D:
        case Scene::Objects::SceneObjectKindUVE::TwoBoneIK3D:
        case Scene::Objects::SceneObjectKindUVE::Fabrik3D:
        case Scene::Objects::SceneObjectKindUVE::SpringBone3D:
        case Scene::Objects::SceneObjectKindUVE::PoseModifier3D:
        case Scene::Objects::SceneObjectKindUVE::AimModifier3D:
        case Scene::Objects::SceneObjectKindUVE::ChainIK3D:
        case Scene::Objects::SceneObjectKindUVE::SplineIK3D:
        case Scene::Objects::SceneObjectKindUVE::StateTree3D:
        case Scene::Objects::SceneObjectKindUVE::AnimatedSprite3D:
            return IM_COL32(168, 85, 247, 255); // Purple

        case Scene::Objects::SceneObjectKindUVE::AudioSource3D:
        case Scene::Objects::SceneObjectKindUVE::AudioListener3D:
            return IM_COL32(239, 68, 68, 255); // Coral Red

        case Scene::Objects::SceneObjectKindUVE::ParticleEmitter3D:
        case Scene::Objects::SceneObjectKindUVE::GpuParticles3D:
        case Scene::Objects::SceneObjectKindUVE::CpuParticles3D:
            return IM_COL32(249, 115, 22, 255); // Orange

        case Scene::Objects::SceneObjectKindUVE::Canvas:
        case Scene::Objects::SceneObjectKindUVE::UiText:
        case Scene::Objects::SceneObjectKindUVE::UiImage:
        case Scene::Objects::SceneObjectKindUVE::UiButton:
            return IM_COL32(99, 102, 241, 255); // Indigo

        case Scene::Objects::SceneObjectKindUVE::Folder:
            return IM_COL32(245, 158, 11, 255); // Folder Amber

        case Scene::Objects::SceneObjectKindUVE::LevelStreamer3D:
        case Scene::Objects::SceneObjectKindUVE::WorldPartition3D:
        case Scene::Objects::SceneObjectKindUVE::NavMeshVolume3D:
        case Scene::Objects::SceneObjectKindUVE::Occluder3D:
        case Scene::Objects::SceneObjectKindUVE::WorldQuery3D:
        case Scene::Objects::SceneObjectKindUVE::LodGroup3D:
            return IM_COL32(34, 197, 94, 255); // Green

        default:
            return IM_COL32(0, 168, 255, 255);
    }
}

/// Draws a sleek slot box indicator with a dynamic category color strip on its left edge and
/// the crisp monochrome icon centered inside. Followed on the same line by whatever is drawn next.
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
