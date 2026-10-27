// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

// The Timeline's animation picker: an AnimationSequencer's own list of animations, which one it
// plays, and the ways to grow the list - a new blank clip, any clip already in the project, or a
// .uvanim dragged in from Content. Libraries move whole lists at once: any .uvanimlib in the
// project imports in, and the player's list exports back out to one. Linking one instead keeps
// the player following the file. Every change to the player
// is one undo step; clip files are written only by New, Rename and Duplicate, which say so in the
// status line.

#include "uve/editor/editor_uve.h"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdio>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include <imgui.h>

#include "uve/asset/animation_clip_asset_uve.h"
#include "uve/asset/animation_library_asset_uve.h"
#include "uve/component/animation_driver_component_uve.h"
#include "uve/component/animation_sequencer_component_uve.h"
#include "uve/component/hierarchy_component_uve.h"
#include "uve/component/name_component_uve.h"
#include "uve/objects/3d/skeleton_3d_uve.h"
#include "uve/object/type_metadata_uve.h"
#include "uve/scene/scene_component_metadata_uve.h"

namespace UVE::Editor {
namespace {

[[nodiscard]] std::string LowerUVE(std::string text) {
    std::ranges::transform(text, text.begin(), [](const unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return text;
}

/// A name made safe for a `.uvanimlib` stem: letters, digits, spaces, `_` and `-` survive, the
/// rest become spaces, and the ends are trimmed. Empty in, "New" out.
[[nodiscard]] std::string SafeLibraryStemUVE(std::string_view name) {
    std::string stem;
    for (const char character : name) {
        const unsigned char unit = static_cast<unsigned char>(character);
        stem += (std::isalnum(unit) || character == ' ' || character == '_' || character == '-') ? character : ' ';
    }
    const std::size_t first = stem.find_first_not_of(' ');
    if (first == std::string::npos) {
        return "New";
    }
    return stem.substr(first, stem.find_last_not_of(' ') - first + 1U);
}

/// The picker's start folder for `clip`: the folder holding the clip file, or Content/Animations
/// when it is unknown (Content itself when even that is missing). Found through the snapshot's
/// registered GUIDs, never by guessing path spellings.
[[nodiscard]] std::filesystem::path PickerStartDirectoryUVE(const Asset::ProjectFileSnapshotUVE& project,
                                                            Asset::AssetGuidUVE clip) {
    if (clip != Asset::AssetGuidUVE{}) {
        for (const Asset::ProjectFileEntryUVE& entry : project.entries) {
            if (entry.registeredAssetGuid.has_value() && *entry.registeredAssetGuid == clip) {
                return entry.relativePath.parent_path();
            }
        }
    }
    const std::filesystem::path fallback{"Animations"};
    for (const Asset::ProjectFileEntryUVE& entry : project.entries) {
        if (entry.kind == Asset::ProjectFileEntryKindUVE::Directory && entry.relativePath == fallback) {
            return fallback;
        }
    }
    return std::filesystem::path{};
}

/// The library entry name for `guid`: the clip file's stem, or "(missing)" when the database no
/// longer resolves it. Cut to what the file format holds.
[[nodiscard]] std::string SequencerLibraryEntryNameUVE(Asset::IAssetDatabaseUVE& assetDatabase,
                                                       Asset::AssetGuidUVE guid) {
    const std::filesystem::path path = assetDatabase.ResolveUVE(guid);
    std::string name = path.empty() ? std::string{"(missing)"} : path.stem().generic_string();
    if (name.empty()) {
        name = "(missing)";
    }
    if (name.size() > Asset::kMaximumAnimationLibraryIdentifierBytesUVE) {
        name.resize(Asset::kMaximumAnimationLibraryIdentifierBytesUVE);
    }
    return name;
}

/// The linked library's clip GUIDs in file order, or empty when unlinked or unreadable. `name`
/// takes the file's stem for the section header ("(missing library)" when the link resolves to
/// nothing); `broken` is set when a link is set but unusable.
[[nodiscard]] std::vector<Asset::AssetGuidUVE> LinkedSequencerClipsUVE(Asset::IAssetDatabaseUVE& assetDatabase,
                                                                      Asset::AssetGuidUVE libraryRef,
                                                                      std::string& name, bool& broken) {
    std::vector<Asset::AssetGuidUVE> guids;
    if (libraryRef == Asset::AssetGuidUVE{}) {
        return guids;
    }
    const std::filesystem::path path = assetDatabase.ResolveUVE(libraryRef);
    if (path.empty()) {
        name = "(missing library)";
        broken = true;
        return guids;
    }
    Asset::AnimationLibraryAssetUVE library;
    if (!Asset::LoadAnimationLibraryAssetUVE(path, library)) {
        name = path.stem().string();
        broken = true;
        return guids;
    }
    name = path.stem().string();
    guids.reserve(library.entries.size());
    for (const Asset::AnimationLibraryEntryUVE& entry : library.entries) {
        guids.push_back(entry.clip);
    }
    return guids;
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

bool EditorUVE::EditAnimationSequencerUVE(const Scene::EntityUVE player,
                                       const std::function<void(Scene::AnimationSequencerComponentUVE&)>& change) {
    if (!IsAuthoringCommandAllowedUVE()) {
        return false;
    }
    Scene::IEntityManagerUVE& entityManager = m_services->GetEntityManagerUVE();
    if (!entityManager.IsAliveUVE(player) || !entityManager.HasComponentUVE<Scene::AnimationSequencerComponentUVE>(player)) {
        return false;
    }
    const Core::TypeMetadataEntryUVE* const entry = Scene::GetSceneComponentMetadataRegistryUVE().FindTypeByIndexUVE(
        std::type_index(typeid(Scene::AnimationSequencerComponentUVE)));
    if (entry == nullptr || !entry->HasFactoryUVE()) {
        return false;
    }
    auto& component = entityManager.GetComponentUVE<Scene::AnimationSequencerComponentUVE>(player);
    const Scene::AnimationSequencerComponentUVE original = component;
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

bool EditorUVE::AddClipToAnimationSequencerUVE(const Scene::EntityUVE player, const std::filesystem::path& absoluteClip) {
    Asset::AnimationClipAssetUVE probe;
    if (!Asset::LoadAnimationClipAssetUVE(absoluteClip, probe)) {
        m_timeline.status = "Not an animation: " + absoluteClip.filename().string();
        return false;
    }
    const Asset::AssetGuidUVE guid = m_services->GetAssetDatabaseUVE().RegisterUVE(absoluteClip);
    const bool changed = EditAnimationSequencerUVE(player, [guid](Scene::AnimationSequencerComponentUVE& component) {
        if (std::ranges::find(component.library, guid) == component.library.end()) {
            component.library.push_back(guid);
        }
        component.clip = guid;
        component.isPlaying = false;
        component.currentTimeSeconds = 0.0F;
        component.hasStartPose = false;
    });
    if (changed) {
        m_timeline.playing = false;
        m_timeline.timeSeconds = 0.0;
        m_timeline.status = "Added " + probe.clipId;
        m_timeline.clipCards.erase(guid.value);
    }
    return changed;
}

bool EditorUVE::ImportAnimationLibraryIntoSequencerUVE(const Scene::EntityUVE player,
                                                       const std::filesystem::path& absoluteLibrary) {
    if (!IsAuthoringCommandAllowedUVE()) {
        m_timeline.status = "Stop the game to change the player's animations.";
        return false;
    }
    Scene::IEntityManagerUVE& entityManager = m_services->GetEntityManagerUVE();
    if (!entityManager.IsAliveUVE(player) ||
        !entityManager.HasComponentUVE<Scene::AnimationSequencerComponentUVE>(player)) {
        return false;
    }
    Asset::AnimationLibraryAssetUVE library;
    if (!Asset::LoadAnimationLibraryAssetUVE(absoluteLibrary, library)) {
        m_timeline.status = "Not a library: " + absoluteLibrary.filename().string();
        return false;
    }
    const Scene::AnimationSequencerComponentUVE& before =
        entityManager.GetComponentUVE<Scene::AnimationSequencerComponentUVE>(player);
    std::vector<Asset::AssetGuidUVE> fresh;
    for (const Asset::AnimationLibraryEntryUVE& entry : library.entries) {
        if (entry.clip != Asset::AssetGuidUVE{} &&
            std::ranges::find(before.library, entry.clip) == before.library.end() &&
            std::ranges::find(fresh, entry.clip) == fresh.end()) {
            fresh.push_back(entry.clip);
        }
    }
    if (fresh.empty()) {
        m_timeline.status = absoluteLibrary.stem().string() + " adds nothing new to this player.";
        return false;
    }
    const bool changed = EditAnimationSequencerUVE(player, [&fresh](Scene::AnimationSequencerComponentUVE& component) {
        for (const Asset::AssetGuidUVE guid : fresh) {
            component.library.push_back(guid);
        }
        if (component.clip == Asset::AssetGuidUVE{}) {
            component.clip = fresh.front();
        }
    });
    if (changed) {
        for (const Asset::AssetGuidUVE guid : fresh) {
            m_timeline.clipCards.erase(guid.value);
        }
        m_timeline.status = "Imported " + std::to_string(fresh.size()) + " from " + absoluteLibrary.stem().string();
    }
    return changed;
}

bool EditorUVE::ExportAnimationSequencerToLibraryUVE(const Scene::EntityUVE player,
                                                     const std::filesystem::path& absoluteLibrary) {
    Scene::IEntityManagerUVE& entityManager = m_services->GetEntityManagerUVE();
    if (!entityManager.IsAliveUVE(player) ||
        !entityManager.HasComponentUVE<Scene::AnimationSequencerComponentUVE>(player)) {
        return false;
    }
    const Scene::AnimationSequencerComponentUVE& component =
        entityManager.GetComponentUVE<Scene::AnimationSequencerComponentUVE>(player);
    // What the picker offers: the owned list, the clip, and the linked library's entries.
    Asset::IAssetDatabaseUVE& assetDatabase = m_services->GetAssetDatabaseUVE();
    std::string linkedName;
    bool linkedBroken = false;
    const std::vector<Asset::AssetGuidUVE> linked =
        LinkedSequencerClipsUVE(assetDatabase, component.libraryRef, linkedName, linkedBroken);
    const std::vector<Asset::AssetGuidUVE> guids =
        MergeSequencerClipListsUVE(component.library, component.clip, linked);
    if (guids.empty()) {
        m_timeline.status = "Nothing to export: this player has no animations.";
        return false;
    }
    Asset::AnimationLibraryAssetUVE library;
    library.libraryId = absoluteLibrary.stem().generic_string();
    if (library.libraryId.empty()) {
        library.libraryId = "library";
    }
    // The file rejects invalid and duplicate GUIDs, so they are skipped and counted, never written.
    std::size_t skipped = 0U;
    for (const Asset::AssetGuidUVE guid : guids) {
        const bool duplicate = std::ranges::any_of(
            library.entries, [guid](const Asset::AnimationLibraryEntryUVE& entry) { return entry.clip == guid; });
        if (guid == Asset::AssetGuidUVE{} || duplicate ||
            library.entries.size() >= Asset::kMaximumAnimationLibraryEntriesUVE) {
            ++skipped;
            continue;
        }
        Asset::AnimationLibraryEntryUVE entry;
        entry.clip = guid;
        entry.name = SequencerLibraryEntryNameUVE(assetDatabase, guid);
        library.entries.push_back(std::move(entry));
    }
    if (library.entries.empty()) {
        m_timeline.status = "Nothing to export: none of the player's clips can go in a library.";
        return false;
    }
    if (!Asset::SaveAnimationLibraryAssetUVE(library, absoluteLibrary)) {
        m_timeline.status = "Could not write " + absoluteLibrary.filename().string();
        return false;
    }
    m_timeline.status = "Exported " + std::to_string(library.entries.size()) + " to " + absoluteLibrary.filename().string() +
                        (skipped == 0U ? std::string{} : " (" + std::to_string(skipped) + " skipped)");
    return true;
}

std::vector<Asset::AssetGuidUVE> EditorUVE::MergeSequencerClipListsUVE(const std::vector<Asset::AssetGuidUVE>& owned,
                                                                       Asset::AssetGuidUVE clip,
                                                                       const std::vector<Asset::AssetGuidUVE>& linked) {
    std::vector<Asset::AssetGuidUVE> guids = owned;
    if (clip != Asset::AssetGuidUVE{} && std::ranges::find(guids, clip) == guids.end()) {
        guids.insert(guids.begin(), clip);
    }
    for (const Asset::AssetGuidUVE guid : linked) {
        if (guid != Asset::AssetGuidUVE{} && std::ranges::find(guids, guid) == guids.end()) {
            guids.push_back(guid);
        }
    }
    return guids;
}

void EditorUVE::RefreshSequencerLinkedClipsUVE(const Scene::EntityUVE player) {
    m_timeline.linkedLibraryRef = Asset::AssetGuidUVE{};
    m_timeline.linkedLibraryName.clear();
    m_timeline.linkedLibraryClips.clear();
    m_timeline.linkedLibraryBroken = false;
    Scene::IEntityManagerUVE& entityManager = m_services->GetEntityManagerUVE();
    if (!entityManager.IsAliveUVE(player) ||
        !entityManager.HasComponentUVE<Scene::AnimationSequencerComponentUVE>(player)) {
        return;
    }
    const Asset::AssetGuidUVE libraryRef =
        entityManager.GetComponentUVE<Scene::AnimationSequencerComponentUVE>(player).libraryRef;
    if (libraryRef == Asset::AssetGuidUVE{}) {
        return;
    }
    m_timeline.linkedLibraryRef = libraryRef;
    m_timeline.linkedLibraryClips = LinkedSequencerClipsUVE(m_services->GetAssetDatabaseUVE(), libraryRef,
                                                            m_timeline.linkedLibraryName, m_timeline.linkedLibraryBroken);
}

bool EditorUVE::LinkAnimationLibraryToSequencerUVE(const Scene::EntityUVE player,
                                                   const std::filesystem::path& absoluteLibrary) {
    if (!IsAuthoringCommandAllowedUVE()) {
        m_timeline.status = "Stop the game to change the player's animations.";
        return false;
    }
    Scene::IEntityManagerUVE& entityManager = m_services->GetEntityManagerUVE();
    if (!entityManager.IsAliveUVE(player) ||
        !entityManager.HasComponentUVE<Scene::AnimationSequencerComponentUVE>(player)) {
        return false;
    }
    Asset::AnimationLibraryAssetUVE probe;
    if (!Asset::LoadAnimationLibraryAssetUVE(absoluteLibrary, probe)) {
        m_timeline.status = "Not a library: " + absoluteLibrary.filename().string();
        return false;
    }
    const Asset::AssetGuidUVE guid = m_services->GetAssetDatabaseUVE().RegisterUVE(absoluteLibrary);
    if (guid == Asset::AssetGuidUVE{}) {
        m_timeline.status = "Could not use " + absoluteLibrary.filename().string() + " as a library.";
        return false;
    }
    const bool changed = EditAnimationSequencerUVE(
        player, [guid](Scene::AnimationSequencerComponentUVE& component) { component.libraryRef = guid; });
    if (!changed) {
        m_timeline.status = absoluteLibrary.stem().string() + " is already this player's library.";
        return false;
    }
    RefreshSequencerLinkedClipsUVE(player);
    m_timeline.status = "Linked " + absoluteLibrary.stem().string() + ": " +
                        std::to_string(m_timeline.linkedLibraryClips.size()) + " clips offered.";
    return true;
}

bool EditorUVE::UnlinkAnimationLibraryFromSequencerUVE(const Scene::EntityUVE player) {
    if (!IsAuthoringCommandAllowedUVE()) {
        m_timeline.status = "Stop the game to change the player's animations.";
        return false;
    }
    Scene::IEntityManagerUVE& entityManager = m_services->GetEntityManagerUVE();
    if (!entityManager.IsAliveUVE(player) ||
        !entityManager.HasComponentUVE<Scene::AnimationSequencerComponentUVE>(player)) {
        return false;
    }
    const Asset::AssetGuidUVE before =
        entityManager.GetComponentUVE<Scene::AnimationSequencerComponentUVE>(player).libraryRef;
    if (before == Asset::AssetGuidUVE{}) {
        m_timeline.status = "No library linked.";
        return false;
    }
    const std::filesystem::path where = m_services->GetAssetDatabaseUVE().ResolveUVE(before);
    const bool changed = EditAnimationSequencerUVE(player, [](Scene::AnimationSequencerComponentUVE& component) {
        component.libraryRef = Asset::AssetGuidUVE{};
    });
    if (changed) {
        RefreshSequencerLinkedClipsUVE(player);
        m_timeline.status = "Unlinked " + (where.empty() ? std::string{"library"} : where.stem().string()) + ".";
    }
    return changed;
}

void EditorUVE::DrawAnimationPickerUVE(const Scene::EntityUVE player, const Scene::EntityUVE skeletonEntity) {
    Scene::IEntityManagerUVE& entityManager = m_services->GetEntityManagerUVE();
    Asset::IAssetDatabaseUVE& assetDatabase = m_services->GetAssetDatabaseUVE();
    const Scene::AnimationSequencerComponentUVE& component =
        entityManager.GetComponentUVE<Scene::AnimationSequencerComponentUVE>(player);

    // The list this player offers: its library, and its clip even when an older save never listed it.
    std::vector<Asset::AssetGuidUVE> list = component.library;
    if (component.clip != Asset::AssetGuidUVE{} && std::ranges::find(list, component.clip) == list.end()) {
        list.insert(list.begin(), component.clip);
    }
    // The link is live: when it changed (Inspector, undo) while the picker stood open, re-read.
    if (component.libraryRef != m_timeline.linkedLibraryRef) {
        RefreshSequencerLinkedClipsUVE(player);
    }
    // The linked library's entries, minus what the player already lists: the picker's second section.
    std::vector<Asset::AssetGuidUVE> linkedOnly;
    for (const Asset::AssetGuidUVE guid : m_timeline.linkedLibraryClips) {
        if (std::ranges::find(list, guid) == list.end()) {
            linkedOnly.push_back(guid);
        }
    }
    const auto cardOf = [&](const Asset::AssetGuidUVE guid) -> const AnimationTimelineStateUVE::ClipCardUVE& {
        auto found = m_timeline.clipCards.find(guid.value);
        if (found == m_timeline.clipCards.end()) {
            AnimationTimelineStateUVE::ClipCardUVE card;
            card.path = assetDatabase.ResolveUVE(guid);
            Asset::AnimationClipAssetUVE clip;
            if (!card.path.empty() && Asset::LoadAnimationClipAssetUVE(card.path, clip)) {
                card.readable = true;
                card.name = card.path.stem().string(); // the file's name is the animation's name
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
    const std::string current =
        component.clip != Asset::AssetGuidUVE{} ? cardOf(component.clip).name : std::string{"No animation"};
    const std::string label = current + "##tl-anim";
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
        RefreshSequencerLinkedClipsUVE(player); // and the linked file as it is now
        ImGui::OpenPopup("##tl-anim-picker");
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("This player's animations: switch, add, import, quick import, link, export, rename, duplicate, remove");
    }
    // Opens upwards, over the tall part of the window, never over the button or down into the
    // Timeline's little space at the bottom.
    if (ImGui::IsPopupOpen("##tl-anim-picker")) {
        ImGui::SetNextWindowPos(ImVec2{buttonMin.x, buttonMin.y - 2.0F}, ImGuiCond_Appearing, ImVec2{0.0F, 1.0F});
    }

    const Asset::ProjectFileSnapshotUVE project = m_services->GetProjectFileIndexUVE().GetSnapshotUVE();
    ImGui::SetNextWindowSizeConstraints(ImVec2{340.0F, 0.0F}, ImVec2{460.0F, 560.0F});
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
            std::snprintf(right, sizeof(right), "%s  %.2f s", card.skeletal ? "bones" : "object", card.durationSeconds);
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
        if (!m_timeline.linkedLibraryName.empty()) {
            ImGui::Spacing();
            ImGui::TextDisabled("FROM %s  %zu", m_timeline.linkedLibraryName.c_str(), linkedOnly.size());
            if (m_timeline.linkedLibraryBroken) {
                ImGui::TextDisabled("  The linked library cannot be read.");
            } else if (linkedOnly.empty()) {
                ImGui::TextDisabled("  Every clip is already on this player.");
            }
            for (const Asset::AssetGuidUVE guid : linkedOnly) {
                const AnimationTimelineStateUVE::ClipCardUVE& card = cardOf(guid);
                if (!matches(card.name)) {
                    continue;
                }
                ImGui::PushID(static_cast<int>(guid.value & 0x7FFFFFFFU));
                const bool isCurrent = guid == component.clip;
                char right[48];
                std::snprintf(right, sizeof(right), "%s  %.2f s", card.skeletal ? "bones" : "object",
                              card.durationSeconds);
                const float rightWidth = ImGui::CalcTextSize(right).x;
                if (ImGui::Selectable(card.name.c_str(), isCurrent, ImGuiSelectableFlags_AllowOverlap)) {
                    choose = guid;
                }
                if (ImGui::IsItemHovered() && !card.path.empty()) {
                    ImGui::SetTooltip("%s", card.path.string().c_str());
                }
                ImGui::SameLine(ImGui::GetWindowContentRegionMax().x - rightWidth);
                ImGui::TextDisabled("%s", right);
                ImGui::PopID();
            }
        }

        ImGui::Separator();
        bool createNew = false;
        if (ImGui::Selectable("+  New Animation")) {
            createNew = true;
        }
        // Any clip in Content: the file dialog finds it however deep it lives - or take it
        // straight from the quick list.
        bool addFromProject = false;
        std::optional<std::filesystem::path> quickAddPath;
        if (ImGui::BeginMenu("+  Add from Project")) {
            if (ImGui::MenuItem("Import...")) {
                addFromProject = true;
            }
            if (ImGui::BeginMenu("Quick Import")) {
                int shown = 0;
                for (const Asset::ProjectFileEntryUVE& entry : project.entries) {
                    if (entry.kind != Asset::ProjectFileEntryKindUVE::File ||
                        entry.relativePath.extension() != ".uvanim") {
                        continue;
                    }
                    const bool listed = entry.registeredAssetGuid.has_value() &&
                                        std::ranges::find(list, *entry.registeredAssetGuid) != list.end();
                    if (listed || !matches(entry.relativePath.stem().string())) {
                        continue;
                    }
                    ++shown;
                    const std::string folder = entry.relativePath.parent_path().string();
                    if (ImGui::MenuItem(entry.relativePath.stem().string().c_str(),
                                        folder.empty() ? "Content" : folder.c_str())) {
                        quickAddPath = project.contentRoot / entry.relativePath;
                    }
                }
                if (shown == 0) {
                    ImGui::TextDisabled("Every clip in the project is already here.");
                }
                ImGui::EndMenu();
            }
            ImGui::EndMenu();
        }
        // A library from anywhere in Content: importing one appends the clips the player lacks -
        // through the file dialog, or straight from a quick list.
        bool importLibrary = false;
        std::optional<std::filesystem::path> quickImportPath;
        if (ImGui::BeginMenu("+  Import Library")) {
            if (ImGui::MenuItem("Import...")) {
                importLibrary = true;
            }
            if (ImGui::BeginMenu("Quick Import")) {
                int shown = 0;
                for (const Asset::ProjectFileEntryUVE& entry : project.entries) {
                    if (entry.kind != Asset::ProjectFileEntryKindUVE::File ||
                        entry.relativePath.extension() != ".uvanimlib") {
                        continue;
                    }
                    if (!matches(entry.relativePath.stem().string())) {
                        continue;
                    }
                    ++shown;
                    const std::string folder = entry.relativePath.parent_path().string();
                    if (ImGui::MenuItem(entry.relativePath.stem().string().c_str(),
                                        folder.empty() ? "Content" : folder.c_str())) {
                        quickImportPath = project.contentRoot / entry.relativePath;
                    }
                }
                if (shown == 0) {
                    ImGui::TextDisabled("No animation libraries in the project yet.");
                }
                ImGui::EndMenu();
            }
            ImGui::EndMenu();
        }
        bool linkLibrary = false;
        bool unlinkRequested = false;
        if (component.libraryRef == Asset::AssetGuidUVE{}) {
            if (ImGui::Selectable("Link Library...")) {
                linkLibrary = true;
            }
        } else {
            const std::filesystem::path linkedPath = assetDatabase.ResolveUVE(component.libraryRef);
            const std::string unlinkLabel =
                linkedPath.empty() ? std::string{"Unlink library"} : "Unlink " + linkedPath.stem().string();
            if (ImGui::Selectable(unlinkLabel.c_str())) {
                unlinkRequested = true;
            }
        }
        bool exportLibrary = false;
        if (ImGui::Selectable("Export to Library...")) {
            exportLibrary = true;
        }
        ImGui::TextDisabled("Or drag a .uvanim from Content onto the Timeline.");
        ImGui::EndPopup();

        // ---- Apply what was picked, after the popup is closed -------------------------------
        if (choose.has_value()) {
            const Asset::AssetGuidUVE guid = *choose;
            static_cast<void>(EditAnimationSequencerUVE(player, [guid](Scene::AnimationSequencerComponentUVE& p) {
                if (std::ranges::find(p.library, guid) == p.library.end()) {
                    p.library.push_back(guid);
                }
                p.clip = guid;
                p.isPlaying = false;
                p.currentTimeSeconds = 0.0F;
                p.hasStartPose = false;
            }));
            m_timeline.playing = false;
            m_timeline.timeSeconds = 0.0;
            ImGui::CloseCurrentPopup();
        }
        if (quickAddPath.has_value()) {
            static_cast<void>(AddClipToAnimationSequencerUVE(player, *quickAddPath));
        }
        if (addFromProject) {
            FilePickerRequestUVE request;
            request.mode = FilePickerModeUVE::Open;
            request.title = "Add Animation";
            request.extensions = {".uvanim"};
            request.startDirectory = PickerStartDirectoryUVE(project, component.clip);
            request.onPick = [this, player](const std::filesystem::path& path) {
                static_cast<void>(AddClipToAnimationSequencerUVE(player, path));
            };
            OpenFilePickerUVE(std::move(request));
        }
        if (quickImportPath.has_value()) {
            static_cast<void>(ImportAnimationLibraryIntoSequencerUVE(player, *quickImportPath));
        }
        if (importLibrary) {
            FilePickerRequestUVE request;
            request.mode = FilePickerModeUVE::Open;
            request.title = "Import Library";
            request.extensions = {".uvanimlib"};
            request.startDirectory = PickerStartDirectoryUVE(project, component.clip);
            request.onPick = [this, player](const std::filesystem::path& path) {
                static_cast<void>(ImportAnimationLibraryIntoSequencerUVE(player, path));
            };
            OpenFilePickerUVE(std::move(request));
        }
        if (linkLibrary) {
            FilePickerRequestUVE request;
            request.mode = FilePickerModeUVE::Open;
            request.title = "Link Library";
            request.extensions = {".uvanimlib"};
            request.startDirectory = PickerStartDirectoryUVE(project, component.clip);
            request.onPick = [this, player](const std::filesystem::path& path) {
                static_cast<void>(LinkAnimationLibraryToSequencerUVE(player, path));
            };
            OpenFilePickerUVE(std::move(request));
        }
        if (unlinkRequested) {
            static_cast<void>(UnlinkAnimationLibraryFromSequencerUVE(player));
        }
        if (exportLibrary) {
            const std::string owner = entityManager.HasComponentUVE<Scene::NameComponentUVE>(player)
                                          ? entityManager.GetComponentUVE<Scene::NameComponentUVE>(player).name
                                          : std::string{"Animation"};
            FilePickerRequestUVE request;
            request.mode = FilePickerModeUVE::Save;
            request.title = "Export Library";
            request.extensions = {".uvanimlib"};
            request.startDirectory = PickerStartDirectoryUVE(project, component.clip);
            request.saveName = SafeLibraryStemUVE(owner) + " Library";
            request.onPick = [this, player](const std::filesystem::path& path) {
                static_cast<void>(ExportAnimationSequencerToLibraryUVE(player, path));
            };
            OpenFilePickerUVE(std::move(request));
        }
        if (removeGuid.has_value()) {
            const Asset::AssetGuidUVE guid = *removeGuid;
            static_cast<void>(EditAnimationSequencerUVE(player, [guid](Scene::AnimationSequencerComponentUVE& p) {
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
                    static_cast<void>(AddClipToAnimationSequencerUVE(player, copy));
                    m_timeline.status = "Duplicated to " + copy.filename().string();
                }
            }
        }
        if (renameGuid.has_value()) {
            if (*renameGuid != component.clip) {
                const Asset::AssetGuidUVE guid = *renameGuid;
                static_cast<void>(EditAnimationSequencerUVE(player, [guid](Scene::AnimationSequencerComponentUVE& p) {
                    p.clip = guid;
                    p.isPlaying = false;
                    p.currentTimeSeconds = 0.0F;
                    p.hasStartPose = false;
                }));
                m_timeline.playing = false;
                m_timeline.timeSeconds = 0.0;
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
                entityManager.HasComponentUVE<Scene::Skeleton3DComponentUVE>(skeletonEntity)) {
                for (const Scene::SkeletonBoneUVE& bone :
                     entityManager.GetComponentUVE<Scene::Skeleton3DComponentUVE>(skeletonEntity).bones) {
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
            if (Asset::SaveAnimationClipAssetUVE(clip, path) && AddClipToAnimationSequencerUVE(player, path)) {
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
        m_timeline.clipName = assetDatabase.ResolveUVE(m_timeline.clipGuid).stem().string();
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
            // The name is the file's: the .uvanim is renamed (keeping its GUID, so everything that
            // uses it still does) and the clip inside is written with the same name and any
            // unsaved key edits.
            const std::filesystem::path path = assetDatabase.ResolveUVE(m_timeline.clipGuid);
            const std::optional<std::filesystem::path> renamedPath =
                path.stem().string() == m_timeline.clipName ? std::optional<std::filesystem::path>{path}
                                                            : RenameContentAssetUVE(path, m_timeline.clipName);
            if (!renamedPath.has_value()) {
                m_timeline.status = "Cannot rename to \"" + m_timeline.clipName + "\": not a valid file name, or taken";
            } else {
                auto renamed = std::make_shared<Asset::AnimationClipAssetUVE>(*m_timeline.clip);
                renamed->clipId = renamedPath->stem().string();
                if (Asset::SaveAnimationClipAssetUVE(*renamed, *renamedPath)) {
                    m_timeline.retired = m_timeline.clip;
                    m_timeline.clip = std::move(renamed);
                    m_timeline.dirty = false;
                    m_timeline.clipCards.erase(m_timeline.clipGuid.value);
                    m_timeline.status = "Renamed to " + renamedPath->filename().string();
                }
            }
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

} // namespace UVE::Editor
