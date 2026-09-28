// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

// The Timeline's animation picker: an AnimationPlayer's own list of animations, which one it
// plays, and the ways to grow the list - a new blank clip, any clip already in the project, or a
// .uvanim dragged in from Content. Every change to the player is one undo step; clip files are
// written only by New, Rename and Duplicate, which say so in the status line.

#include "uve/editor/editor_uve.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

#include <imgui.h>

#include "uve/asset/animation_clip_asset_uve.h"
#include "uve/component/animation_mixer_component_uve.h"
#include "uve/component/animation_player_component_uve.h"
#include "uve/component/hierarchy_component_uve.h"
#include "uve/component/name_component_uve.h"
#include "uve/nodes/3d/skeleton_3d_uve.h"
#include "uve/object/type_metadata_uve.h"
#include "uve/scene/scene_component_metadata_uve.h"

namespace UVE::Editor {
namespace {

[[nodiscard]] std::string LowerUVE(std::string text) {
    std::ranges::transform(text, text.begin(), [](const unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return text;
}

/// `directory/stem.uvanim`, or `stem_2`, `stem_3`... when that file exists.
[[nodiscard]] std::filesystem::path UniqueClipPathUVE(const std::filesystem::path& directory, const std::string& stem) {
    std::error_code error;
    std::filesystem::path candidate = directory / (stem + ".uvanim");
    for (int suffix = 2; std::filesystem::exists(candidate, error) && suffix < 10000; ++suffix) {
        candidate = directory / (stem + "_" + std::to_string(suffix) + ".uvanim");
    }
    return candidate;
}

} // namespace

bool EditorUVE::EditAnimationPlayerUVE(const Scene::EntityUVE player,
                                       const std::function<void(Scene::AnimationPlayerComponentUVE&)>& change) {
    if (!IsAuthoringCommandAllowedUVE()) {
        return false;
    }
    Scene::IEntityManagerUVE& entityManager = m_services->GetEntityManagerUVE();
    if (!entityManager.IsAliveUVE(player) || !entityManager.HasComponentUVE<Scene::AnimationPlayerComponentUVE>(player)) {
        return false;
    }
    const Core::TypeMetadataEntryUVE* const entry = Scene::GetSceneComponentMetadataRegistryUVE().FindTypeByIndexUVE(
        std::type_index(typeid(Scene::AnimationPlayerComponentUVE)));
    if (entry == nullptr || !entry->HasFactoryUVE()) {
        return false;
    }
    auto& component = entityManager.GetComponentUVE<Scene::AnimationPlayerComponentUVE>(player);
    const Scene::AnimationPlayerComponentUVE original = component;
    Core::TypeInstanceUVE before = Core::TypeInstanceUVE::CloneUVE(*entry, &component);
    change(component);
    if (component.HasSameSettingsUVE(original) || !before.IsValidUVE()) {
        return false;
    }
    Core::TypeInstanceUVE after = Core::TypeInstanceUVE::CloneUVE(*entry, &component);
    if (!after.IsValidUVE()) {
        component = original;
        return false;
    }
    // Same history entry as an Inspector edit: undo restores the whole player.
    const EditorSelectionSnapshotUVE selection = CaptureSelectionSnapshotUVE();
    const bool dirtyBefore = m_sceneDirty;
    m_sceneDirty = true;
    RecordHistoryUVE(ComponentPropertyHistoryEntryUVE{player, entry, std::move(before), std::move(after), selection,
                                                      selection, dirtyBefore, true});
    return true;
}

bool EditorUVE::AddClipToAnimationPlayerUVE(const Scene::EntityUVE player, const std::filesystem::path& absoluteClip) {
    Asset::AnimationClipAssetUVE probe;
    if (!Asset::LoadAnimationClipAssetUVE(absoluteClip, probe)) {
        m_timeline.status = "Not an animation: " + absoluteClip.filename().string();
        return false;
    }
    const Asset::AssetGuidUVE guid = m_services->GetAssetDatabaseUVE().RegisterUVE(absoluteClip);
    const bool changed = EditAnimationPlayerUVE(player, [guid](Scene::AnimationPlayerComponentUVE& component) {
        if (std::ranges::find(component.library, guid) == component.library.end()) {
            component.library.push_back(guid);
        }
        component.clip = guid;
    });
    if (changed) {
        m_timeline.status = "Added " + probe.clipId;
        m_timeline.clipCards.erase(guid.value);
    }
    return changed;
}

void EditorUVE::DrawAnimationPickerUVE(const Scene::EntityUVE player, const Scene::EntityUVE skeletonEntity) {
    Scene::IEntityManagerUVE& entityManager = m_services->GetEntityManagerUVE();
    Asset::IAssetDatabaseUVE& assetDatabase = m_services->GetAssetDatabaseUVE();
    const Scene::AnimationPlayerComponentUVE& component =
        entityManager.GetComponentUVE<Scene::AnimationPlayerComponentUVE>(player);

    // The list this player offers: its library, and its clip even when an older save never listed it.
    std::vector<Asset::AssetGuidUVE> list = component.library;
    if (component.clip != Asset::AssetGuidUVE{} && std::ranges::find(list, component.clip) == list.end()) {
        list.insert(list.begin(), component.clip);
    }
    const auto cardOf = [&](const Asset::AssetGuidUVE guid) -> const AnimationTimelineStateUVE::ClipCardUVE& {
        auto found = m_timeline.clipCards.find(guid.value);
        if (found == m_timeline.clipCards.end()) {
            AnimationTimelineStateUVE::ClipCardUVE card;
            card.path = assetDatabase.ResolveUVE(guid);
            Asset::AnimationClipAssetUVE clip;
            if (!card.path.empty() && Asset::LoadAnimationClipAssetUVE(card.path, clip)) {
                card.readable = true;
                card.name = clip.clipId.empty() ? card.path.stem().string() : clip.clipId;
                card.durationSeconds = clip.durationSeconds;
                card.skeletal = clip.IsSkeletalUVE();
            } else {
                card.name = card.path.empty() ? std::string{"(missing clip)"} : card.path.stem().string() + " (unreadable)";
            }
            found = m_timeline.clipCards.emplace(guid.value, std::move(card)).first;
        }
        return found->second;
    };

    // The button: the current animation's name, opening the picker.
    const std::string current = component.clip != Asset::AssetGuidUVE{}
                                    ? (m_timeline.clip != nullptr && m_timeline.clipGuid == component.clip
                                           ? m_timeline.clip->clipId
                                           : cardOf(component.clip).name)
                                    : std::string{"No animation"};
    const std::string label = current + (list.size() > 1U ? "  (" + std::to_string(list.size()) + ")" : "") +
                              "##tl-anim";
    const float width = std::min(260.0F, ImGui::CalcTextSize(label.c_str(), nullptr, true).x + 34.0F);
    const bool clicked = ImGui::Button(label.c_str(), ImVec2{std::max(140.0F, width) + 14.0F, 0.0F});
    const ImVec2 buttonMin = ImGui::GetItemRectMin();
    const ImVec2 buttonMax = ImGui::GetItemRectMax();
    {
        // A picker, so it says so: a down arrow at the right end.
        const float cx = buttonMax.x - 10.0F;
        const float cy = (buttonMin.y + buttonMax.y) * 0.5F;
        ImGui::GetWindowDrawList()->AddTriangleFilled(ImVec2{cx - 4.0F, cy - 2.0F}, ImVec2{cx + 4.0F, cy - 2.0F},
                                                      ImVec2{cx, cy + 3.0F}, ImGui::GetColorU32(ImGuiCol_Text));
    }
    if (clicked) {
        m_timeline.clipCards.clear(); // re-read names and lengths each time it opens
        m_timeline.pickerSearch.clear();
        ImGui::OpenPopup("##tl-anim-picker");
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("This player's animations: switch, add, rename, duplicate, remove");
    }
    // Opens under the button, never over it.
    if (ImGui::IsPopupOpen("##tl-anim-picker")) {
        ImGui::SetNextWindowPos(ImVec2{buttonMin.x, buttonMax.y + 2.0F}, ImGuiCond_Appearing);
    }

    const Asset::ProjectFileSnapshotUVE project = m_services->GetProjectFileIndexUVE().GetSnapshotUVE();
    ImGui::SetNextWindowSizeConstraints(ImVec2{340.0F, 0.0F}, ImVec2{460.0F, 520.0F});
    if (ImGui::BeginPopup("##tl-anim-picker")) {
        char search[128];
        std::snprintf(search, sizeof(search), "%s", m_timeline.pickerSearch.c_str());
        if (ImGui::IsWindowAppearing()) {
            ImGui::SetKeyboardFocusHere();
        }
        ImGui::SetNextItemWidth(-1.0F);
        if (ImGui::InputTextWithHint("##tl-anim-search", "Search animations", search, sizeof(search))) {
            m_timeline.pickerSearch = search;
        }
        const std::string needle = LowerUVE(m_timeline.pickerSearch);
        const auto matches = [&needle](const std::string& name) {
            return needle.empty() || LowerUVE(name).find(needle) != std::string::npos;
        };

        ImGui::Spacing();
        ImGui::TextDisabled("ON THIS PLAYER  %zu", list.size());
        std::optional<Asset::AssetGuidUVE> choose;
        std::optional<Asset::AssetGuidUVE> removeGuid;
        std::optional<Asset::AssetGuidUVE> duplicateGuid;
        std::optional<Asset::AssetGuidUVE> renameGuid;
        for (const Asset::AssetGuidUVE guid : list) {
            const AnimationTimelineStateUVE::ClipCardUVE& card = cardOf(guid);
            if (!matches(card.name)) {
                continue;
            }
            ImGui::PushID(static_cast<int>(guid.value & 0x7FFFFFFFU));
            const bool isCurrent = guid == component.clip;
            char right[48];
            std::snprintf(right, sizeof(right), "%s  %.2f s", card.skeletal ? "bones" : "node", card.durationSeconds);
            const float rightWidth = ImGui::CalcTextSize(right).x;
            if (ImGui::Selectable(card.name.c_str(), isCurrent, ImGuiSelectableFlags_AllowOverlap)) {
                choose = guid;
            }
            if (ImGui::IsItemHovered() && !card.path.empty()) {
                ImGui::SetTooltip("%s", card.path.string().c_str());
            }
            if (ImGui::BeginPopupContextItem("##tl-anim-item")) {
                if (ImGui::MenuItem("Rename...")) {
                    renameGuid = guid;
                }
                if (ImGui::MenuItem("Duplicate", nullptr, false, card.readable)) {
                    duplicateGuid = guid;
                }
                ImGui::Separator();
                if (ImGui::MenuItem("Remove from Player")) {
                    removeGuid = guid;
                }
                ImGui::EndPopup();
            }
            ImGui::SameLine(ImGui::GetWindowContentRegionMax().x - rightWidth);
            ImGui::TextDisabled("%s", right);
            ImGui::PopID();
        }
        if (list.empty()) {
            ImGui::TextDisabled("  No animations yet.");
        }

        ImGui::Separator();
        bool createNew = false;
        if (ImGui::Selectable("+  New Animation")) {
            createNew = true;
        }
        // Every clip in the project the player does not have yet, with its folder.
        std::optional<std::filesystem::path> addPath;
        if (ImGui::BeginMenu("+  Add from Project")) {
            int shown = 0;
            for (const Asset::ProjectFileEntryUVE& entry : project.entries) {
                if (entry.kind != Asset::ProjectFileEntryKindUVE::File || entry.relativePath.extension() != ".uvanim") {
                    continue;
                }
                const std::filesystem::path absolute = project.contentRoot / entry.relativePath;
                const bool listed = entry.registeredAssetGuid.has_value() &&
                                    std::ranges::find(list, *entry.registeredAssetGuid) != list.end();
                if (listed || !matches(entry.relativePath.stem().string())) {
                    continue;
                }
                ++shown;
                const std::string folder = entry.relativePath.parent_path().string();
                if (ImGui::MenuItem(entry.relativePath.stem().string().c_str(), folder.empty() ? "Content" : folder.c_str())) {
                    addPath = absolute;
                }
            }
            if (shown == 0) {
                ImGui::TextDisabled("Every clip in the project is already here.");
            }
            ImGui::EndMenu();
        }
        ImGui::TextDisabled("Or drag a .uvanim from Content onto the Timeline.");
        ImGui::EndPopup();

        // ---- Apply what was picked, after the popup is closed -------------------------------
        if (choose.has_value()) {
            const Asset::AssetGuidUVE guid = *choose;
            static_cast<void>(EditAnimationPlayerUVE(player, [guid](Scene::AnimationPlayerComponentUVE& p) {
                if (std::ranges::find(p.library, guid) == p.library.end()) {
                    p.library.push_back(guid);
                }
                p.clip = guid;
            }));
            ImGui::CloseCurrentPopup();
        }
        if (addPath.has_value()) {
            static_cast<void>(AddClipToAnimationPlayerUVE(player, *addPath));
        }
        if (removeGuid.has_value()) {
            const Asset::AssetGuidUVE guid = *removeGuid;
            static_cast<void>(EditAnimationPlayerUVE(player, [guid](Scene::AnimationPlayerComponentUVE& p) {
                std::erase(p.library, guid);
                if (p.clip == guid) {
                    p.clip = p.library.empty() ? Asset::AssetGuidUVE{} : p.library.front();
                }
            }));
            m_timeline.status = "Removed from the player (the file is still in the project)";
        }
        if (duplicateGuid.has_value()) {
            const AnimationTimelineStateUVE::ClipCardUVE card = cardOf(*duplicateGuid);
            Asset::AnimationClipAssetUVE clip;
            if (Asset::LoadAnimationClipAssetUVE(card.path, clip)) {
                const std::filesystem::path copy = UniqueClipPathUVE(card.path.parent_path(), card.path.stem().string() + "_copy");
                clip.clipId = copy.stem().string();
                if (Asset::SaveAnimationClipAssetUVE(clip, copy)) {
                    static_cast<void>(AddClipToAnimationPlayerUVE(player, copy));
                    m_timeline.status = "Duplicated to " + copy.filename().string();
                }
            }
        }
        if (renameGuid.has_value()) {
            if (*renameGuid != component.clip) {
                const Asset::AssetGuidUVE guid = *renameGuid;
                static_cast<void>(EditAnimationPlayerUVE(player, [guid](Scene::AnimationPlayerComponentUVE& p) { p.clip = guid; }));
            }
            m_timeline.renameClipRequested = true;
        }
        if (createNew) {
            // A one-second clip beside the current one (or in Content/Animations), keyed at both
            // ends with the skeleton's rest pose, so it plays and is ready to edit.
            std::filesystem::path directory = project.contentRoot / "Animations";
            if (component.clip != Asset::AssetGuidUVE{}) {
                const std::filesystem::path currentPath = assetDatabase.ResolveUVE(component.clip);
                if (!currentPath.empty()) {
                    directory = currentPath.parent_path();
                }
            }
            std::error_code error;
            std::filesystem::create_directories(directory, error);
            const std::string owner = entityManager.HasComponentUVE<Scene::NameComponentUVE>(player)
                                          ? entityManager.GetComponentUVE<Scene::NameComponentUVE>(player).name
                                          : std::string{"Animation"};
            const std::filesystem::path path = UniqueClipPathUVE(directory, "New_Animation");
            Asset::AnimationClipAssetUVE clip;
            clip.clipId = path.stem().string();
            clip.durationSeconds = 1.0;
            if (skeletonEntity != Scene::kInvalidEntityUVE && entityManager.IsAliveUVE(skeletonEntity) &&
                entityManager.HasComponentUVE<Scene::Skeleton3DNodeComponentUVE>(skeletonEntity)) {
                for (const Scene::SkeletonBoneUVE& bone :
                     entityManager.GetComponentUVE<Scene::Skeleton3DNodeComponentUVE>(skeletonEntity).bones) {
                    Asset::AnimationAssetSampleUVE rest;
                    rest.pose.position = bone.localPosition;
                    rest.pose.rotation = bone.localRotation;
                    rest.pose.scale = bone.localScale;
                    Asset::AnimationAssetSampleUVE end = rest;
                    end.timeSeconds = 1.0;
                    clip.bones.push_back(Asset::AnimationAssetBoneTrackUVE{bone.name, {rest, end}});
                }
            }
            if (clip.bones.empty()) {
                Asset::AnimationAssetSampleUVE start;
                Asset::AnimationAssetSampleUVE end;
                end.timeSeconds = 1.0;
                clip.samples = {start, end};
            }
            if (Asset::SaveAnimationClipAssetUVE(clip, path) && AddClipToAnimationPlayerUVE(player, path)) {
                m_timeline.renameClipRequested = true;
                m_timeline.status = "New animation for " + owner + ": " + path.filename().string();
            } else {
                m_timeline.status = "Could not create " + path.filename().string();
            }
        }
    }

    // Naming the current clip: the name scripts and trees use; the file keeps its name.
    if (m_timeline.renameClipRequested && m_timeline.clip != nullptr && m_timeline.clipGuid == component.clip) {
        m_timeline.renameClipRequested = false;
        m_timeline.clipName = m_timeline.clip->clipId;
        ImGui::OpenPopup("##tl-anim-name");
    }
    if (ImGui::BeginPopup("##tl-anim-name")) {
        ImGui::TextDisabled("Animation name");
        char buffer[129];
        std::snprintf(buffer, sizeof(buffer), "%s", m_timeline.clipName.c_str());
        if (ImGui::IsWindowAppearing()) {
            ImGui::SetKeyboardFocusHere();
        }
        ImGui::SetNextItemWidth(240.0F);
        const bool entered = ImGui::InputText("##tl-anim-name-field", buffer, sizeof(buffer),
                                              ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);
        m_timeline.clipName = buffer;
        if (entered && !m_timeline.clipName.empty() && m_timeline.clip != nullptr) {
            // Written at once, with any unsaved key edits, so the name is never only in memory.
            auto renamed = std::make_shared<Asset::AnimationClipAssetUVE>(*m_timeline.clip);
            renamed->clipId = m_timeline.clipName;
            const std::filesystem::path path = assetDatabase.ResolveUVE(m_timeline.clipGuid);
            if (Asset::SaveAnimationClipAssetUVE(*renamed, path)) {
                m_timeline.retired = m_timeline.clip;
                m_timeline.clip = std::move(renamed);
                m_timeline.dirty = false;
                m_timeline.clipCards.erase(m_timeline.clipGuid.value);
                m_timeline.status = "Renamed to " + m_timeline.clipName;
            }
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

} // namespace UVE::Editor
