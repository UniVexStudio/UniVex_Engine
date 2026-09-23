// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <algorithm>
#include <array>
#include <cstdint>

#include <imgui.h>

#include "uve/component/animation_player_component_uve.h"
#include "uve/component/audio_source_component_uve.h"
#include "uve/component/camera_component_uve.h"
#include "uve/component/collider_component_uve.h"
#include "uve/component/entity_uve.h"
#include "uve/component/light_component_uve.h"
#include "uve/component/mesh_component_uve.h"
#include "uve/component/particle_emitter_component_uve.h"
#include "uve/component/primitive_mesh_component_uve.h"
#include "uve/component/rigid_body_component_uve.h"
#include "uve/component/script_component_uve.h"
#include "uve/entity/i_entity_manager_uve.h"
#include "uve/editor/editor_uve.h" // EditorSceneComponentKindUVE, used by the classifier below
#include "uve/nodes/3d/world_environment_3d_uve.h"

namespace UVE::Editor {

/// Node-kind icons for the hierarchy and inspector, drawn procedurally into an ImDrawList.
///
/// WHY THIS IS A SHARED HEADER. These were file-local helpers in editor_uve.cpp, used by the
/// hierarchy panel AND by two inspector drawers. The panels are being split into their own
/// translation units, so a file-local copy would mean two versions of the same glyph drifting
/// apart - the hierarchy showing one camera icon and the inspector another, with nothing to say
/// which was right. One definition, linked, before the first caller moves out.
///
/// The drawing is procedural - AddRect, AddTriangle, AddCircle - rather than glyph or image
/// based. That is worth knowing because it is the intended replacement point: swapping these
/// bodies for a name-keyed icon lookup changes this file and nothing else, precisely because
/// every caller now goes through DrawHierarchyNodeIconUVE rather than drawing its own shapes.
/// Moved here unchanged; no glyph is redrawn as part of the move.

enum class HierarchyNodeIconKindUVE {
    // The neutral glyph every unregistered name falls back to. Named after the node's current
    // spelling; this was `Empty` before the kind rename, and the icon table below keeps the
    // legacy "empty" name mapped here alongside "node_3d".
    Node3D,
    Mesh,
    Camera,
    Light,
    Environment,
    Physics,
    Audio,
    Particle,
    Script,
    Animation,
};

inline void DrawNodeMeshIconUVE(ImDrawList& drawList, const ImVec2 center, const float radius, const ImU32 color) {
    const float half = radius * 0.5F;
    drawList.AddRect(ImVec2{center.x - half, center.y - half}, ImVec2{center.x + half, center.y + half}, color,
                     radius * 0.12F, 0, 1.3F);
}

inline void DrawNodeCameraIconUVE(ImDrawList& drawList, const ImVec2 center, const float radius, const ImU32 color) {
    const float bodyHalfWidth = radius * 0.48F;
    const float bodyHalfHeight = radius * 0.34F;
    drawList.AddRect(ImVec2{center.x - bodyHalfWidth, center.y - bodyHalfHeight},
                     ImVec2{center.x + bodyHalfWidth * 0.3F, center.y + bodyHalfHeight}, color, radius * 0.1F, 0,
                     1.3F);
    const std::array<ImVec2, 3> lens{
        ImVec2{center.x + bodyHalfWidth * 0.3F, center.y - bodyHalfHeight * 0.7F},
        ImVec2{center.x + bodyHalfWidth * 0.3F, center.y + bodyHalfHeight * 0.7F},
        ImVec2{center.x + bodyHalfWidth * 1.15F, center.y}};
    drawList.AddTriangle(lens[0], lens[1], lens[2], color, 1.3F);
}

inline void DrawNodePhysicsIconUVE(ImDrawList& drawList, const ImVec2 center, const float radius, const ImU32 color) {
    drawList.AddCircle(center, radius * 0.5F, color, 16, 1.3F);
    drawList.AddLine(ImVec2{center.x - radius * 0.5F, center.y}, ImVec2{center.x + radius * 0.5F, center.y}, color,
                     1.1F);
}

inline void DrawNodeAudioIconUVE(ImDrawList& drawList, const ImVec2 center, const float radius, const ImU32 color) {
    const float coneDepth = radius * 0.32F;
    const std::array<ImVec2, 4> cone{
        ImVec2{center.x - radius * 0.55F, center.y - coneDepth * 0.55F},
        ImVec2{center.x - radius * 0.15F, center.y - coneDepth * 0.55F},
        ImVec2{center.x + radius * 0.25F, center.y - coneDepth},
        ImVec2{center.x + radius * 0.25F, center.y + coneDepth}};
    drawList.AddLine(cone[0], cone[1], color, 1.2F);
    drawList.AddLine(cone[1], cone[2], color, 1.2F);
    drawList.AddLine(cone[0], ImVec2{cone[0].x, center.y + coneDepth * 0.55F}, color, 1.2F);
    drawList.AddLine(ImVec2{cone[0].x, center.y + coneDepth * 0.55F}, ImVec2{cone[1].x, center.y + coneDepth * 0.55F},
                     color, 1.2F);
    drawList.AddLine(ImVec2{cone[1].x, center.y + coneDepth * 0.55F}, cone[3], color, 1.2F);
    for (int arc = 1; arc <= 2; ++arc) {
        const float arcRadius = radius * (0.35F + 0.22F * static_cast<float>(arc));
        drawList.PathArcTo(ImVec2{cone[2].x, center.y}, arcRadius, -0.6F, 0.6F, 8);
        drawList.PathStroke(color, 0, 1.1F);
    }
}

inline void DrawNodeParticleIconUVE(ImDrawList& drawList, const ImVec2 center, const float radius, const ImU32 color) {
    drawList.AddCircleFilled(center, radius * 0.18F, color, 10);
    const std::array<ImVec2, 3> sparkOffsets{ImVec2{0.5F, -0.55F}, ImVec2{-0.55F, 0.15F}, ImVec2{0.3F, 0.55F}};
    for (const ImVec2& offset : sparkOffsets) {
        drawList.AddCircleFilled(ImVec2{center.x + offset.x * radius, center.y + offset.y * radius}, radius * 0.1F,
                                 color, 8);
    }
}

inline void DrawNodeScriptIconUVE(ImDrawList& drawList, const ImVec2 center, const float radius, const ImU32 color) {
    const float armX = radius * 0.22F;
    const float armY = radius * 0.32F;
    const float tipX = radius * 0.5F;
    drawList.AddLine(ImVec2{center.x - armX, center.y - armY}, ImVec2{center.x - tipX, center.y}, color, 1.3F);
    drawList.AddLine(ImVec2{center.x - tipX, center.y}, ImVec2{center.x - armX, center.y + armY}, color, 1.3F);
    drawList.AddLine(ImVec2{center.x + armX, center.y - armY}, ImVec2{center.x + tipX, center.y}, color, 1.3F);
    drawList.AddLine(ImVec2{center.x + tipX, center.y}, ImVec2{center.x + armX, center.y + armY}, color, 1.3F);
}

inline void DrawNodeAnimationIconUVE(ImDrawList& drawList, const ImVec2 center, const float radius, const ImU32 color) {
    drawList.AddCircle(center, radius * 0.5F, color, 20, 1.2F);
    const float triHalf = radius * 0.2F;
    drawList.AddTriangleFilled(ImVec2{center.x - triHalf * 0.5F, center.y - triHalf},
                               ImVec2{center.x - triHalf * 0.5F, center.y + triHalf},
                               ImVec2{center.x + triHalf * 0.9F, center.y}, color);
}

inline void DrawNode3DIconUVE(ImDrawList& drawList, const ImVec2 center, const float radius, const ImU32 color) {
    drawList.AddCircle(center, radius * 0.42F, color, 16, 1.2F);
}

[[nodiscard]] inline HierarchyNodeIconKindUVE ClassifyHierarchyNodeIconUVE(Scene::IEntityManagerUVE& entityManager,
                                                                     const Scene::EntityUVE entity) noexcept {
    if (entityManager.HasComponentUVE<Scene::CameraComponentUVE>(entity)) {
        return HierarchyNodeIconKindUVE::Camera;
    }
    if (entityManager.HasComponentUVE<Scene::LightComponentUVE>(entity)) {
        return HierarchyNodeIconKindUVE::Light;
    }
    if (entityManager.HasComponentUVE<Scene::MeshComponentUVE>(entity) ||
        entityManager.HasComponentUVE<Scene::PrimitiveMeshComponentUVE>(entity)) {
        return HierarchyNodeIconKindUVE::Mesh;
    }
    if (entityManager.HasComponentUVE<Scene::WorldEnvironment3DNodeComponentUVE>(entity)) {
        return HierarchyNodeIconKindUVE::Environment;
    }
    if (entityManager.HasComponentUVE<Scene::AudioSourceComponentUVE>(entity)) {
        return HierarchyNodeIconKindUVE::Audio;
    }
    if (entityManager.HasComponentUVE<Scene::ParticleEmitterComponentUVE>(entity)) {
        return HierarchyNodeIconKindUVE::Particle;
    }
    if (entityManager.HasComponentUVE<Scene::ScriptComponentUVE>(entity)) {
        return HierarchyNodeIconKindUVE::Script;
    }
    if (entityManager.HasComponentUVE<Scene::AnimationPlayerComponentUVE>(entity)) {
        return HierarchyNodeIconKindUVE::Animation;
    }
    // Checked after Mesh: primitives (Cube/UVSphere/Plane) also carry a ColliderComponentUVE, and
    // should read as their mesh, not as a generic physics body.
    if (entityManager.HasComponentUVE<Scene::ColliderComponentUVE>(entity) ||
        entityManager.HasComponentUVE<Scene::RigidBodyComponentUVE>(entity)) {
        return HierarchyNodeIconKindUVE::Physics;
    }
    return HierarchyNodeIconKindUVE::Node3D;
}

// Light/Environment reuse the existing "sun"/"environment" general icon textures (already used
// elsewhere in this file) rather than new procedural glyphs - takes the texture ids as parameters
// so this stays a free function; the caller (a EditorUVE member) is the one with m_uiAssets access.
inline void DrawHierarchyNodeIconUVE(ImDrawList& drawList, const ImVec2 center, const float radius,
                              const HierarchyNodeIconKindUVE kind, const std::uintptr_t sunTextureId,
                              const std::uintptr_t environmentTextureId) {
    const ImU32 color = ImGui::GetColorU32(ImGuiCol_Text);
    switch (kind) {
        case HierarchyNodeIconKindUVE::Mesh: DrawNodeMeshIconUVE(drawList, center, radius, color); break;
        case HierarchyNodeIconKindUVE::Camera: DrawNodeCameraIconUVE(drawList, center, radius, color); break;
        case HierarchyNodeIconKindUVE::Light:
            if (sunTextureId != 0U) {
                const float half = radius * 0.55F;
                drawList.AddImage(static_cast<ImTextureID>(sunTextureId), ImVec2{center.x - half, center.y - half},
                                  ImVec2{center.x + half, center.y + half});
            } else {
                DrawNode3DIconUVE(drawList, center, radius, color);
            }
            break;
        case HierarchyNodeIconKindUVE::Environment:
            if (environmentTextureId != 0U) {
                const float half = radius * 0.55F;
                drawList.AddImage(static_cast<ImTextureID>(environmentTextureId),
                                  ImVec2{center.x - half, center.y - half}, ImVec2{center.x + half, center.y + half});
            } else {
                DrawNode3DIconUVE(drawList, center, radius, color);
            }
            break;
        case HierarchyNodeIconKindUVE::Physics: DrawNodePhysicsIconUVE(drawList, center, radius, color); break;
        case HierarchyNodeIconKindUVE::Audio: DrawNodeAudioIconUVE(drawList, center, radius, color); break;
        case HierarchyNodeIconKindUVE::Particle: DrawNodeParticleIconUVE(drawList, center, radius, color); break;
        case HierarchyNodeIconKindUVE::Script: DrawNodeScriptIconUVE(drawList, center, radius, color); break;
        case HierarchyNodeIconKindUVE::Animation: DrawNodeAnimationIconUVE(drawList, center, radius, color); break;
        case HierarchyNodeIconKindUVE::Node3D: default: DrawNode3DIconUVE(drawList, center, radius, color); break;
    }
}

/// Icon-then-label rows, shared by the hierarchy and the inspector.
///
/// Moved here for the same reason as the glyphs themselves: both were file-local in
/// editor_uve.cpp and both are used from inside AND outside the inspector, so splitting that
/// panel out would otherwise have meant a second copy. DrawNativeIconLabelUVE is inline because a
/// non-inline definition in a header fails to link the moment a second translation unit includes
/// it - the mistake I made and caught on the first of these headers.

inline void DrawNativeIconLabelUVE(const std::uintptr_t textureId, const char* const label) {
    if (textureId != 0U) {
        // Centred on the text line rather than hanging from its top, so icon and name share a
        // middle whatever the font size.
        const float line = ImGui::GetTextLineHeight();
        const float size = std::min(16.0F, line);
        const ImVec2 cursor = ImGui::GetCursorScreenPos();
        ImGui::Dummy(ImVec2{size, line});
        ImGui::GetWindowDrawList()->AddImage(static_cast<ImTextureID>(textureId),
                                             ImVec2{cursor.x, cursor.y + ((line - size) * 0.5F)},
                                             ImVec2{cursor.x + size, cursor.y + ((line + size) * 0.5F)});
        ImGui::SameLine(0.0F, 6.0F);
    }
    ImGui::TextUnformatted(label);
}

// Same icon-before-name convention as DrawNativeIconLabelUVE(), for Inspector sections that need a
// procedurally-drawn glyph (no bitmap/SVG asset) rather than one of the few existing general icon
// textures - `drawIcon` matches every DrawNode*IconUVE/DrawHierarchyNodeIconUVE signature already
// established for the Scene Hierarchy, reused here rather than duplicated.
template <typename DrawIconUVE>
void DrawProceduralIconLabelUVE(const float radius, const char* const label, DrawIconUVE&& drawIcon) {
    ImDrawList* const drawList = ImGui::GetWindowDrawList();
    const ImVec2 cursor = ImGui::GetCursorScreenPos();
    // The glyph is centred on the text line, and its slot is exactly the line's height, so the
    // name's baseline is the same as any plain row's.
    const float line = ImGui::GetTextLineHeight();
    const float glyphRadius = std::min(radius, line * 0.5F);
    ImGui::Dummy(ImVec2{glyphRadius * 2.0F, line});
    const ImVec2 center{cursor.x + glyphRadius, cursor.y + (line * 0.5F)};
    drawIcon(*drawList, center, glyphRadius, ImGui::GetColorU32(ImGuiCol_Text));
    ImGui::SameLine(0.0F, 6.0F);
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

// Folder rows in the Filesystem/Contents browser previously rendered with no icon at all
// (ClassifyContentBrowserEntryUVE() -> Folder resolved straight to a null texture) - a real,
// confirmed gap, not a stylistic choice. Drawn procedurally (tab + body rectangles), matching this
// file's own established icon convention rather than adding a new SVG asset for one glyph.
inline void DrawFolderIconUVE(ImDrawList& drawList, const ImVec2 center, const float radius, const ImU32 color) {
    const float halfWidth = radius * 0.72F;
    const float halfHeight = radius * 0.52F;
    const float tabWidth = halfWidth * 0.55F;
    const float tabHeight = radius * 0.20F;
    const float rounding = radius * 0.12F;
    const ImVec2 bodyMin{center.x - halfWidth, center.y - halfHeight + tabHeight};
    const ImVec2 bodyMax{center.x + halfWidth, center.y + halfHeight};
    drawList.AddRectFilled(bodyMin, bodyMax, color, rounding);
    const ImVec2 tabMin{center.x - halfWidth, center.y - halfHeight};
    const ImVec2 tabMax{tabMin.x + tabWidth, tabMin.y + tabHeight};
    drawList.AddRectFilled(tabMin, tabMax, color, rounding * 0.6F);
}

// ---- Scene Hierarchy per-node icons ---------------------------------------------------------
// The editor's own recognized entity components are exactly the 10 EditorSceneComponentKindUVE
// values (see editor_uve.h) - the "Add Component" popup's own master list. Rather than 37 bespoke
// icons for every Scene::Nodes::SceneNodeKindUVE preset (most of which just add one of these same
// 10 components to a plain entity), one procedural icon is drawn per actual component the entity
// carries, checked in the same priority order a user would expect to identify it visually first
// (Camera/Light/Mesh before the more generic Physics/Script/Animation) - the base-node icon (a plain ring,
// matching Godot's own bare Node3D icon) when none of the 10 match.
[[nodiscard]] constexpr HierarchyNodeIconKindUVE ClassifySceneComponentKindIconUVE(
    const EditorSceneComponentKindUVE kind) noexcept {
    switch (kind) {
        case EditorSceneComponentKindUVE::Camera: return HierarchyNodeIconKindUVE::Camera;
        case EditorSceneComponentKindUVE::Mesh: return HierarchyNodeIconKindUVE::Mesh;
        case EditorSceneComponentKindUVE::Light: return HierarchyNodeIconKindUVE::Light;
        case EditorSceneComponentKindUVE::Collider:
        case EditorSceneComponentKindUVE::RigidBody: return HierarchyNodeIconKindUVE::Physics;
        case EditorSceneComponentKindUVE::AudioSource: return HierarchyNodeIconKindUVE::Audio;
        case EditorSceneComponentKindUVE::ParticleEmitter: return HierarchyNodeIconKindUVE::Particle;
        case EditorSceneComponentKindUVE::Script: return HierarchyNodeIconKindUVE::Script;
        case EditorSceneComponentKindUVE::AnimationPlayer: return HierarchyNodeIconKindUVE::Animation;
        case EditorSceneComponentKindUVE::WorldEnvironment: return HierarchyNodeIconKindUVE::Environment;
        case EditorSceneComponentKindUVE::CharacterController: return HierarchyNodeIconKindUVE::Physics;
        case EditorSceneComponentKindUVE::Canvas:
        case EditorSceneComponentKindUVE::UIText:
        case EditorSceneComponentKindUVE::UIImage:
        case EditorSceneComponentKindUVE::UIButton: return HierarchyNodeIconKindUVE::Node3D;
        case EditorSceneComponentKindUVE::PhysicsInterpolation: return HierarchyNodeIconKindUVE::Physics;
        case EditorSceneComponentKindUVE::EditorDescription: return HierarchyNodeIconKindUVE::Node3D;
        // The four properties every node has in common carry no icon meaning of their own - they
        // describe when and how a node runs, not what it is - so they leave the node's icon as
        // whatever the rest of its components already decided.
        case EditorSceneComponentKindUVE::Process:
        case EditorSceneComponentKindUVE::ThreadGroup:
        case EditorSceneComponentKindUVE::AutoTranslate:
        case EditorSceneComponentKindUVE::NodeMetadata: return HierarchyNodeIconKindUVE::Node3D;
    }
    return HierarchyNodeIconKindUVE::Node3D;
}

} // namespace UVE::Editor
