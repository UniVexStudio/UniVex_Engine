// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

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
    Empty,
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

inline void DrawNodeEmptyIconUVE(ImDrawList& drawList, const ImVec2 center, const float radius, const ImU32 color) {
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
    return HierarchyNodeIconKindUVE::Empty;
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
                DrawNodeEmptyIconUVE(drawList, center, radius, color);
            }
            break;
        case HierarchyNodeIconKindUVE::Environment:
            if (environmentTextureId != 0U) {
                const float half = radius * 0.55F;
                drawList.AddImage(static_cast<ImTextureID>(environmentTextureId),
                                  ImVec2{center.x - half, center.y - half}, ImVec2{center.x + half, center.y + half});
            } else {
                DrawNodeEmptyIconUVE(drawList, center, radius, color);
            }
            break;
        case HierarchyNodeIconKindUVE::Physics: DrawNodePhysicsIconUVE(drawList, center, radius, color); break;
        case HierarchyNodeIconKindUVE::Audio: DrawNodeAudioIconUVE(drawList, center, radius, color); break;
        case HierarchyNodeIconKindUVE::Particle: DrawNodeParticleIconUVE(drawList, center, radius, color); break;
        case HierarchyNodeIconKindUVE::Script: DrawNodeScriptIconUVE(drawList, center, radius, color); break;
        case HierarchyNodeIconKindUVE::Animation: DrawNodeAnimationIconUVE(drawList, center, radius, color); break;
        case HierarchyNodeIconKindUVE::Empty: default: DrawNodeEmptyIconUVE(drawList, center, radius, color); break;
    }
}

} // namespace UVE::Editor
