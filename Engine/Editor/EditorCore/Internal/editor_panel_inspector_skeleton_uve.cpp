// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

// Skeleton3D in the Inspector: where its bones come from, and what they are.
//
// Bones are authored where they belong - an armature in Blender or any DCC tool - and arrive with
// the model exported from it. This node never invents a bone; a new Skeleton3D is empty until it
// is pointed at a rigged model, and pointing it again (Reload) picks up an edited rig.

#include "uve/editor/editor_uve.h"

#include <algorithm>
#include <array>
#include <cfloat>
#include <cstdio>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <numbers>
#include <string>
#include <typeindex>
#include <vector>

#include <imgui.h>

#include "uve/asset/gltf_skeleton_uve.h"
#include "uve/entity/i_entity_manager_uve.h"
#include "uve/math/quaternion_uve.h"
#include "uve/nodes/3d/skeleton_3d_uve.h"
#include "uve/scene/scene_component_metadata_uve.h"

namespace UVE::Editor {
namespace {

[[nodiscard]] bool BeginSkeletonRowsUVE(const char* const id) {
    if (!ImGui::BeginTable(id, 2, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_NoSavedSettings)) {
        return false;
    }
    ImGui::TableSetupColumn("##label", ImGuiTableColumnFlags_WidthStretch, 0.42F);
    ImGui::TableSetupColumn("##value", ImGuiTableColumnFlags_WidthStretch, 0.58F);
    return true;
}

void ReadOnlyRowUVE(const char* const label, const char* const value) {
    ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(0);
    ImGui::TextUnformatted(label);
    ImGui::TableSetColumnIndex(1);
    ImGui::TextDisabled("%s", value);
}

} // namespace

bool EditorUVE::BindSelectedSkeletonSourceUVE(const std::filesystem::path& relativeSource) {
    const Core::TypeMetadataEntryUVE* const entry =
        Scene::FindSceneComponentMetadataUVE(std::type_index(typeid(Scene::Skeleton3DNodeComponentUVE)));
    Scene::IEntityManagerUVE& entityManager = m_services->GetEntityManagerUVE();
    if (entry == nullptr || !IsDocumentEntityUVE(m_selectedEntity) ||
        !entityManager.HasComponentUVE<Scene::Skeleton3DNodeComponentUVE>(m_selectedEntity)) {
        return false;
    }
    Scene::Skeleton3DNodeComponentUVE next =
        entityManager.GetComponentUVE<Scene::Skeleton3DNodeComponentUVE>(m_selectedEntity);
    next.skeletonAssetPath = relativeSource.generic_string();
    next.bones.clear();
    if (!relativeSource.empty()) {
        const Asset::ProjectFileSnapshotUVE project = m_services->GetProjectFileIndexUVE().GetSnapshotUVE();
        const std::optional<Asset::GltfSkeletonUVE> skeleton =
            Asset::ReadGltfSkeletonUVE(project.contentRoot / relativeSource, Scene::kMaximumSkeletonBonesUVE);
        if (!skeleton.has_value()) {
            m_skeletonSourceStatus = relativeSource.filename().string() +
                                     " has no skeleton this engine can read (a glTF skin of at most " +
                                     std::to_string(Scene::kMaximumSkeletonBonesUVE) + " bones).";
            return false;
        }
        for (const Asset::GltfJointUVE& joint : skeleton->joints) {
            next.bones.push_back(Scene::SkeletonBoneUVE{joint.name, joint.parentIndex, joint.translation, joint.rotation,
                                                        joint.scale});
        }
    }
    m_skeletonSourceStatus.clear();
    m_selectedSkeletonBone.clear();
    return SetSelectedComponentValueUVE(*entry, &next);
}

void EditorUVE::DrawSkeletonSourcePropertyUVE(const Core::TypeMetadataEntryUVE& entry,
                                              const Core::TypeMetadataPropertyUVE& property, const void* const instance) {
    const auto& skeleton = *static_cast<const Scene::Skeleton3DNodeComponentUVE*>(instance);
    const bool writable = property.IsAuthoringWritableUVE() && IsAuthoringCommandAllowedUVE();
    if (!BeginSkeletonRowsUVE("##skeleton-source")) {
        return;
    }
    ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(0);
    // No revert icon: resetting the path alone would leave the bones behind. Clear does both.
    static_cast<void>(DrawMetadataPropertyLabelUVE(entry, property, instance, false));
    ImGui::TableSetColumnIndex(1);
    ImGui::BeginDisabled(!writable);
    ImGui::SetNextItemWidth(-FLT_MIN);
    const std::string preview = skeleton.skeletonAssetPath.empty()
                                    ? std::string{"(none)"}
                                    : std::filesystem::path{skeleton.skeletonAssetPath}.filename().string();
    // Offered: every model source whose file declares a skeleton - the same test the Content
    // Browser uses to label a file Model rather than Mesh.
    std::filesystem::path chosen;
    bool picked = false;
    if (ImGui::BeginCombo("##skeleton-source", preview.c_str())) {
        if (ImGui::Selectable("(none)", skeleton.skeletonAssetPath.empty()) && !skeleton.skeletonAssetPath.empty()) {
            picked = true;
        }
        bool any = false;
        const Asset::ProjectFileSnapshotUVE project = m_services->GetProjectFileIndexUVE().GetSnapshotUVE();
        for (const Asset::ProjectFileEntryUVE& file : project.entries) {
            if (file.kind == Asset::ProjectFileEntryKindUVE::Directory || !IsRiggedModelSourceUVE(file.relativePath)) {
                continue;
            }
            any = true;
            const std::string path = file.relativePath.generic_string();
            if (ImGui::Selectable((file.relativePath.filename().string() + "##" + path).c_str(),
                                  path == skeleton.skeletonAssetPath)) {
                chosen = file.relativePath;
                picked = true;
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("%s", path.c_str());
            }
        }
        if (!any) {
            ImGui::TextDisabled("No rigged models yet. Export an armature from Blender as glTF (.glb) into the project.");
        }
        ImGui::EndCombo();
    }
    ImGui::EndDisabled();
    ImGui::EndTable();

    // Reload re-reads the same file, for a rig edited and re-exported; Clear empties the node.
    if (!skeleton.skeletonAssetPath.empty()) {
        ImGui::BeginDisabled(!writable);
        if (ImGui::SmallButton("Reload Bones")) {
            chosen = skeleton.skeletonAssetPath;
            picked = true;
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Read the bones again from %s, after the rig was edited and re-exported.",
                              skeleton.skeletonAssetPath.c_str());
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("Clear")) {
            chosen.clear();
            picked = true;
        }
        ImGui::EndDisabled();
    }
    if (!m_skeletonSourceStatus.empty()) {
        ImGui::PushTextWrapPos(0.0F);
        ImGui::TextColored(ImVec4{0.95F, 0.62F, 0.35F, 1.0F}, "%s", m_skeletonSourceStatus.c_str());
        ImGui::PopTextWrapPos();
    }
    if (picked) {
        static_cast<void>(BindSelectedSkeletonSourceUVE(chosen));
    }
}

void EditorUVE::DrawSkeletonBonesPropertyUVE(const Core::TypeMetadataEntryUVE&,
                                             const Core::TypeMetadataPropertyUVE&, const void* const instance) {
    const auto& skeleton = *static_cast<const Scene::Skeleton3DNodeComponentUVE*>(instance);
    const std::string header = "Bones (" + std::to_string(skeleton.bones.size()) + ")##skeleton-bones";
    constexpr ImGuiTreeNodeFlags kGroupFlags =
        ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_FramePadding | ImGuiTreeNodeFlags_DefaultOpen;
    if (!ImGui::TreeNodeEx(header.c_str(), kGroupFlags)) {
        return;
    }
    if (skeleton.bones.empty()) {
        ImGui::PushTextWrapPos(0.0F);
        ImGui::TextDisabled("No bones. Pick a Source model with an armature; bones come from the DCC tool.");
        ImGui::PopTextWrapPos();
        ImGui::TreePop();
        return;
    }

    // Children by parent, once per draw: bones are ordered parent-first, so one pass builds it.
    std::vector<std::vector<std::size_t>> children(skeleton.bones.size());
    std::vector<std::size_t> roots;
    for (std::size_t index = 0U; index < skeleton.bones.size(); ++index) {
        const std::int32_t parent = skeleton.bones[index].parentIndex;
        if (parent < 0) {
            roots.push_back(index);
        } else {
            children[static_cast<std::size_t>(parent)].push_back(index);
        }
    }
    const float listHeight = std::min(220.0F, (static_cast<float>(skeleton.bones.size()) + 1.0F) *
                                                  ImGui::GetTextLineHeightWithSpacing());
    if (ImGui::BeginChild("##bone-tree", ImVec2{0.0F, listHeight}, ImGuiChildFlags_Borders)) {
        const std::function<void(std::size_t)> drawBone = [&](const std::size_t index) {
            const Scene::SkeletonBoneUVE& bone = skeleton.bones[index];
            ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_DefaultOpen |
                                       ImGuiTreeNodeFlags_OpenOnArrow;
            if (children[index].empty()) {
                flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
            }
            if (bone.name == m_selectedSkeletonBone) {
                flags |= ImGuiTreeNodeFlags_Selected;
            }
            ImGui::PushID(static_cast<int>(index));
            const bool open = ImGui::TreeNodeEx("##bone", flags, "%s", bone.name.c_str());
            if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) {
                m_selectedSkeletonBone = bone.name;
            }
            ImGui::PopID();
            if (open && !children[index].empty()) {
                for (const std::size_t child : children[index]) {
                    drawBone(child);
                }
                ImGui::TreePop();
            }
        };
        for (const std::size_t root : roots) {
            drawBone(root);
        }
    }
    ImGui::EndChild();

    // The selected bone's rest pose, relative to its parent, as authored in the DCC tool.
    const auto selected = std::find_if(skeleton.bones.begin(), skeleton.bones.end(),
                                       [this](const Scene::SkeletonBoneUVE& bone) { return bone.name == m_selectedSkeletonBone; });
    if (selected != skeleton.bones.end() && BeginSkeletonRowsUVE("##bone-rest")) {
        constexpr float kDegrees = 180.0F / std::numbers::pi_v<float>;
        Math::Vector3UVE euler{};
        const bool haveEuler = Math::TryToEulerUVE(selected->localRotation, euler);
        const std::string parent = selected->parentIndex < 0
                                       ? std::string{"(root)"}
                                       : skeleton.bones[static_cast<std::size_t>(selected->parentIndex)].name;
        std::array<char, 96> text{};
        ReadOnlyRowUVE("Parent", parent.c_str());
        std::snprintf(text.data(), text.size(), "%.3f  %.3f  %.3f", static_cast<double>(selected->localPosition.x),
                      static_cast<double>(selected->localPosition.y), static_cast<double>(selected->localPosition.z));
        ReadOnlyRowUVE("Rest Position", text.data());
        if (haveEuler) {
            std::snprintf(text.data(), text.size(), "%.1f  %.1f  %.1f", static_cast<double>(euler.x * kDegrees),
                          static_cast<double>(euler.y * kDegrees), static_cast<double>(euler.z * kDegrees));
        } else {
            std::snprintf(text.data(), text.size(), "-");
        }
        ReadOnlyRowUVE("Rest Rotation", text.data());
        std::snprintf(text.data(), text.size(), "%.3f  %.3f  %.3f", static_cast<double>(selected->localScale.x),
                      static_cast<double>(selected->localScale.y), static_cast<double>(selected->localScale.z));
        ReadOnlyRowUVE("Rest Scale", text.data());
        ImGui::EndTable();
    }
    ImGui::TreePop();
}

} // namespace UVE::Editor
