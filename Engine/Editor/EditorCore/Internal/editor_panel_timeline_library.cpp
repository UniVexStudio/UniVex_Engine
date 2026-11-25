// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

// The Animation Sequencer's animation picker actions: an AnimationSequencer's own list of animations, which one it
// plays, and the ways to grow the list - a new blank clip, any clip already in the project, or a
// .uvanim dragged in from Content. Libraries move whole lists at once: any .uvanimlib in the
// project imports in, and the player's list exports back out to one. Linking one instead keeps
// the player following the file. Every change to the player
// is one undo step; clip files are written only by New, Rename and Duplicate, which say so in the
// status line.

#include "editor.h"

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

#include "animation_clip_asset.h"
#include "animation_library_asset.h"
#include "animation_driver_component.h"
#include "animation_sequencer_component.h"
#include "hierarchy_component.h"
#include "name_component.h"
#include "skeleton_3d.h"
#include "type_metadata.h"
#include "scene_component_metadata.h"

namespace UVE::Editor {
namespace {

/// A name made safe for a `.uvanimlib` stem: letters, digits, spaces, `_` and `-` survive, the
/// rest become spaces, and the ends are trimmed. Empty in, "New" out.
[[nodiscard]] std::string SafeLibraryStem(std::string_view name) {
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
[[nodiscard]] std::filesystem::path PickerStartDirectory(const Asset::ProjectFileSnapshot& project,
                                                            Asset::AssetGuid clip) {
    if (clip != Asset::AssetGuid{}) {
        for (const Asset::ProjectFileEntry& entry : project.entries) {
            if (entry.registeredAssetGuid.has_value() && *entry.registeredAssetGuid == clip) {
                return entry.relativePath.parent_path();
            }
        }
    }
    const std::filesystem::path fallback{"Animations"};
    for (const Asset::ProjectFileEntry& entry : project.entries) {
        if (entry.kind == Asset::ProjectFileEntryKind::Directory && entry.relativePath == fallback) {
            return fallback;
        }
    }
    return std::filesystem::path{};
}

/// The library entry name for `guid`: the clip file's stem, or "(missing)" when the database no
/// longer resolves it. Cut to what the file format holds.
[[nodiscard]] std::string SequencerLibraryEntryName(Asset::IAssetDatabase& assetDatabase,
                                                       Asset::AssetGuid guid) {
    const std::filesystem::path path = assetDatabase.Resolve(guid);
    std::string name = path.empty() ? std::string{"(missing)"} : path.stem().generic_string();
    if (name.empty()) {
        name = "(missing)";
    }
    if (name.size() > Asset::kMaximumAnimationLibraryIdentifierBytes) {
        name.resize(Asset::kMaximumAnimationLibraryIdentifierBytes);
    }
    return name;
}

/// The linked library's clip GUIDs in file order, or empty when unlinked or unreadable. `name`
/// takes the file's stem for the section header ("(missing library)" when the link resolves to
/// nothing); `broken` is set when a link is set but unusable.
[[nodiscard]] std::vector<Asset::AssetGuid> LinkedSequencerClips(Asset::IAssetDatabase& assetDatabase,
                                                                      Asset::AssetGuid libraryRef,
                                                                      std::string& name, bool& broken) {
    std::vector<Asset::AssetGuid> guids;
    if (libraryRef == Asset::AssetGuid{}) {
        return guids;
    }
    const std::filesystem::path path = assetDatabase.Resolve(libraryRef);
    if (path.empty()) {
        name = "(missing library)";
        broken = true;
        return guids;
    }
    Asset::AnimationLibraryAsset library;
    if (!Asset::LoadAnimationLibraryAsset(path, library)) {
        name = path.stem().string();
        broken = true;
        return guids;
    }
    name = path.stem().string();
    guids.reserve(library.entries.size());
    for (const Asset::AnimationLibraryEntry& entry : library.entries) {
        guids.push_back(entry.clip);
    }
    return guids;
}

/// `directory/stem.uvanim`, or `stem_2`, `stem_3`... when that file exists.
[[nodiscard]] std::filesystem::path UniqueClipPath(const std::filesystem::path& directory, const std::string& stem) {
    std::error_code error;
    std::filesystem::path candidate = directory / (stem + ".uvanim");
    for (int suffix = 2; std::filesystem::exists(candidate, error) && suffix < 10000; ++suffix) {
        candidate = directory / (stem + "_" + std::to_string(suffix) + ".uvanim");
    }
    return candidate;
}

} // namespace

bool Editor::EditAnimationSequencer(const Scene::Entity player,
                                       const std::function<void(Scene::AnimationSequencerComponent&)>& change) {
    if (!IsAuthoringCommandAllowed()) {
        return false;
    }
    Scene::IEntityManager& entityManager = m_services->GetEntityManager();
    if (!entityManager.IsAlive(player) || !entityManager.HasComponent<Scene::AnimationSequencerComponent>(player)) {
        return false;
    }
    const Core::TypeMetadataEntry* const entry = Scene::GetSceneComponentMetadataRegistry().FindTypeByIndex(
        std::type_index(typeid(Scene::AnimationSequencerComponent)));
    if (entry == nullptr || !entry->HasFactory()) {
        return false;
    }
    auto& component = entityManager.GetComponent<Scene::AnimationSequencerComponent>(player);
    const Scene::AnimationSequencerComponent original = component;
    Core::TypeInstance before = Core::TypeInstance::Clone(*entry, &component);
    change(component);
    if (component.HasSameSettings(original) || !before.IsValid()) {
        return false;
    }
    Core::TypeInstance after = Core::TypeInstance::Clone(*entry, &component);
    if (!after.IsValid()) {
        component = original;
        return false;
    }
    // Same history entry as an Inspector edit: undo restores the whole player.
    const EditorSelectionSnapshot selection = CaptureSelectionSnapshot();
    const bool dirtyBefore = m_sceneDirty;
    m_sceneDirty = true;
    RecordHistory(ComponentPropertyHistoryEntry{player, entry, std::move(before), std::move(after), selection,
                                                      selection, dirtyBefore, true});
    return true;
}

bool Editor::AddClipToAnimationSequencer(const Scene::Entity player, const std::filesystem::path& absoluteClip) {
    Asset::AnimationClipAsset probe;
    if (!Asset::LoadAnimationClipAsset(absoluteClip, probe)) {
        m_timeline.status = "Not an animation: " + absoluteClip.filename().string();
        return false;
    }
    const Asset::AssetGuid guid = m_services->GetAssetDatabase().Register(absoluteClip);
    const bool changed = EditAnimationSequencer(player, [guid](Scene::AnimationSequencerComponent& component) {
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

bool Editor::ImportAnimationLibraryIntoSequencer(const Scene::Entity player,
                                                       const std::filesystem::path& absoluteLibrary) {
    if (!IsAuthoringCommandAllowed()) {
        m_timeline.status = "Stop the game to change the player's animations.";
        return false;
    }
    Scene::IEntityManager& entityManager = m_services->GetEntityManager();
    if (!entityManager.IsAlive(player) ||
        !entityManager.HasComponent<Scene::AnimationSequencerComponent>(player)) {
        return false;
    }
    Asset::AnimationLibraryAsset library;
    if (!Asset::LoadAnimationLibraryAsset(absoluteLibrary, library)) {
        m_timeline.status = "Not a library: " + absoluteLibrary.filename().string();
        return false;
    }
    const Scene::AnimationSequencerComponent& before =
        entityManager.GetComponent<Scene::AnimationSequencerComponent>(player);
    std::vector<Asset::AssetGuid> fresh;
    for (const Asset::AnimationLibraryEntry& entry : library.entries) {
        if (entry.clip != Asset::AssetGuid{} &&
            std::ranges::find(before.library, entry.clip) == before.library.end() &&
            std::ranges::find(fresh, entry.clip) == fresh.end()) {
            fresh.push_back(entry.clip);
        }
    }
    if (fresh.empty()) {
        m_timeline.status = absoluteLibrary.stem().string() + " adds nothing new to this player.";
        return false;
    }
    const bool changed = EditAnimationSequencer(player, [&fresh](Scene::AnimationSequencerComponent& component) {
        for (const Asset::AssetGuid guid : fresh) {
            component.library.push_back(guid);
        }
        if (component.clip == Asset::AssetGuid{}) {
            component.clip = fresh.front();
        }
    });
    if (changed) {
        for (const Asset::AssetGuid guid : fresh) {
            m_timeline.clipCards.erase(guid.value);
        }
        m_timeline.status = "Imported " + std::to_string(fresh.size()) + " from " + absoluteLibrary.stem().string();
    }
    return changed;
}

bool Editor::ExportAnimationSequencerToLibrary(const Scene::Entity player,
                                                     const std::filesystem::path& absoluteLibrary) {
    Scene::IEntityManager& entityManager = m_services->GetEntityManager();
    if (!entityManager.IsAlive(player) ||
        !entityManager.HasComponent<Scene::AnimationSequencerComponent>(player)) {
        return false;
    }
    const Scene::AnimationSequencerComponent& component =
        entityManager.GetComponent<Scene::AnimationSequencerComponent>(player);
    // What the picker offers: the owned list, the clip, and the linked library's entries.
    Asset::IAssetDatabase& assetDatabase = m_services->GetAssetDatabase();
    std::string linkedName;
    bool linkedBroken = false;
    const std::vector<Asset::AssetGuid> linked =
        LinkedSequencerClips(assetDatabase, component.libraryRef, linkedName, linkedBroken);
    const std::vector<Asset::AssetGuid> guids =
        MergeSequencerClipLists(component.library, component.clip, linked);
    if (guids.empty()) {
        m_timeline.status = "Nothing to export: this player has no animations.";
        return false;
    }
    Asset::AnimationLibraryAsset library;
    library.libraryId = absoluteLibrary.stem().generic_string();
    if (library.libraryId.empty()) {
        library.libraryId = "library";
    }
    // The file rejects invalid and duplicate GUIDs, so they are skipped and counted, never written.
    std::size_t skipped = 0U;
    for (const Asset::AssetGuid guid : guids) {
        const bool duplicate = std::ranges::any_of(
            library.entries, [guid](const Asset::AnimationLibraryEntry& entry) { return entry.clip == guid; });
        if (guid == Asset::AssetGuid{} || duplicate ||
            library.entries.size() >= Asset::kMaximumAnimationLibraryEntries) {
            ++skipped;
            continue;
        }
        Asset::AnimationLibraryEntry entry;
        entry.clip = guid;
        entry.name = SequencerLibraryEntryName(assetDatabase, guid);
        library.entries.push_back(std::move(entry));
    }
    if (library.entries.empty()) {
        m_timeline.status = "Nothing to export: none of the player's clips can go in a library.";
        return false;
    }
    if (!Asset::SaveAnimationLibraryAsset(library, absoluteLibrary)) {
        m_timeline.status = "Could not write " + absoluteLibrary.filename().string();
        return false;
    }
    m_timeline.status = "Exported " + std::to_string(library.entries.size()) + " to " + absoluteLibrary.filename().string() +
                        (skipped == 0U ? std::string{} : " (" + std::to_string(skipped) + " skipped)");
    return true;
}

std::vector<Asset::AssetGuid> Editor::MergeSequencerClipLists(const std::vector<Asset::AssetGuid>& owned,
                                                                       Asset::AssetGuid clip,
                                                                       const std::vector<Asset::AssetGuid>& linked) {
    std::vector<Asset::AssetGuid> guids = owned;
    if (clip != Asset::AssetGuid{} && std::ranges::find(guids, clip) == guids.end()) {
        guids.insert(guids.begin(), clip);
    }
    for (const Asset::AssetGuid guid : linked) {
        if (guid != Asset::AssetGuid{} && std::ranges::find(guids, guid) == guids.end()) {
            guids.push_back(guid);
        }
    }
    return guids;
}

void Editor::RefreshSequencerLinkedClips(const Scene::Entity player) {
    m_timeline.linkedLibraryRef = Asset::AssetGuid{};
    m_timeline.linkedLibraryName.clear();
    m_timeline.linkedLibraryClips.clear();
    m_timeline.linkedLibraryBroken = false;
    Scene::IEntityManager& entityManager = m_services->GetEntityManager();
    if (!entityManager.IsAlive(player) ||
        !entityManager.HasComponent<Scene::AnimationSequencerComponent>(player)) {
        return;
    }
    const Asset::AssetGuid libraryRef =
        entityManager.GetComponent<Scene::AnimationSequencerComponent>(player).libraryRef;
    if (libraryRef == Asset::AssetGuid{}) {
        return;
    }
    m_timeline.linkedLibraryRef = libraryRef;
    m_timeline.linkedLibraryClips = LinkedSequencerClips(m_services->GetAssetDatabase(), libraryRef,
                                                            m_timeline.linkedLibraryName, m_timeline.linkedLibraryBroken);
}

bool Editor::LinkAnimationLibraryToSequencer(const Scene::Entity player,
                                                   const std::filesystem::path& absoluteLibrary) {
    if (!IsAuthoringCommandAllowed()) {
        m_timeline.status = "Stop the game to change the player's animations.";
        return false;
    }
    Scene::IEntityManager& entityManager = m_services->GetEntityManager();
    if (!entityManager.IsAlive(player) ||
        !entityManager.HasComponent<Scene::AnimationSequencerComponent>(player)) {
        return false;
    }
    Asset::AnimationLibraryAsset probe;
    if (!Asset::LoadAnimationLibraryAsset(absoluteLibrary, probe)) {
        m_timeline.status = "Not a library: " + absoluteLibrary.filename().string();
        return false;
    }
    const Asset::AssetGuid guid = m_services->GetAssetDatabase().Register(absoluteLibrary);
    if (guid == Asset::AssetGuid{}) {
        m_timeline.status = "Could not use " + absoluteLibrary.filename().string() + " as a library.";
        return false;
    }
    const bool changed = EditAnimationSequencer(
        player, [guid](Scene::AnimationSequencerComponent& component) { component.libraryRef = guid; });
    if (!changed) {
        m_timeline.status = absoluteLibrary.stem().string() + " is already this player's library.";
        return false;
    }
    RefreshSequencerLinkedClips(player);
    m_timeline.status = "Linked " + absoluteLibrary.stem().string() + ": " +
                        std::to_string(m_timeline.linkedLibraryClips.size()) + " clips offered.";
    return true;
}

bool Editor::UnlinkAnimationLibraryFromSequencer(const Scene::Entity player) {
    if (!IsAuthoringCommandAllowed()) {
        m_timeline.status = "Stop the game to change the player's animations.";
        return false;
    }
    Scene::IEntityManager& entityManager = m_services->GetEntityManager();
    if (!entityManager.IsAlive(player) ||
        !entityManager.HasComponent<Scene::AnimationSequencerComponent>(player)) {
        return false;
    }
    const Asset::AssetGuid before =
        entityManager.GetComponent<Scene::AnimationSequencerComponent>(player).libraryRef;
    if (before == Asset::AssetGuid{}) {
        m_timeline.status = "No library linked.";
        return false;
    }
    const std::filesystem::path where = m_services->GetAssetDatabase().Resolve(before);
    const bool changed = EditAnimationSequencer(player, [](Scene::AnimationSequencerComponent& component) {
        component.libraryRef = Asset::AssetGuid{};
    });
    if (changed) {
        RefreshSequencerLinkedClips(player);
        m_timeline.status = "Unlinked " + (where.empty() ? std::string{"library"} : where.stem().string()) + ".";
    }
    return changed;
}

const Editor::AnimationTimelineState::ClipCard& Editor::AnimationTimelineClipCard(const Asset::AssetGuid guid) {
    auto found = m_timeline.clipCards.find(guid.value);
    if (found == m_timeline.clipCards.end()) {
        AnimationTimelineState::ClipCard card;
        card.path = m_services->GetAssetDatabase().Resolve(guid);
        Asset::AnimationClipAsset clip;
        if (!card.path.empty() && Asset::LoadAnimationClipAsset(card.path, clip)) {
            card.readable = true;
            card.name = card.path.stem().string();   // the file's name is the animation's name
            card.durationSeconds = clip.durationSeconds;
            card.skeletal = clip.IsSkeletal();
        } else {
            card.name = card.path.empty() ? std::string{"(missing clip)"} : card.path.stem().string() + " (unreadable)";
        }
        found = m_timeline.clipCards.emplace(guid.value, std::move(card)).first;
    }
    return found->second;
}

std::vector<Asset::AssetGuid> Editor::AnimationTimelineOwnClips(const Scene::Entity player) {
    Scene::IEntityManager& entityManager = m_services->GetEntityManager();
    if (!entityManager.IsAlive(player) || !entityManager.HasComponent<Scene::AnimationSequencerComponent>(player)) {
        return {};
    }
    const auto& component = entityManager.GetComponent<Scene::AnimationSequencerComponent>(player);
    // Its library, and its clip even when an older save never listed it.
    std::vector<Asset::AssetGuid> list = component.library;
    if (component.clip != Asset::AssetGuid{} && std::ranges::find(list, component.clip) == list.end()) {
        list.insert(list.begin(), component.clip);
    }
    return list;
}

std::vector<Asset::AssetGuid> Editor::AnimationTimelineLinkedClips(const Scene::Entity player) {
    Scene::IEntityManager& entityManager = m_services->GetEntityManager();
    if (!entityManager.IsAlive(player) || !entityManager.HasComponent<Scene::AnimationSequencerComponent>(player)) {
        return {};
    }
    // The link is live: when it changed (Details, undo), re-read the file.
    const auto& component = entityManager.GetComponent<Scene::AnimationSequencerComponent>(player);
    if (component.libraryRef != m_timeline.linkedLibraryRef) {
        RefreshSequencerLinkedClips(player);
    }
    const std::vector<Asset::AssetGuid> own = AnimationTimelineOwnClips(player);
    std::vector<Asset::AssetGuid> linkedOnly;
    for (const Asset::AssetGuid guid : m_timeline.linkedLibraryClips) {
        if (std::ranges::find(own, guid) == own.end()) {
            linkedOnly.push_back(guid);
        }
    }
    return linkedOnly;
}

void Editor::ChooseAnimationTimelineClip(const Scene::Entity player, const Asset::AssetGuid guid) {
    static_cast<void>(EditAnimationSequencer(player, [guid](Scene::AnimationSequencerComponent& p) {
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
}

void Editor::RemoveAnimationTimelineClip(const Scene::Entity player, const Asset::AssetGuid guid) {
    if (EditAnimationSequencer(player, [guid](Scene::AnimationSequencerComponent& p) {
            std::erase(p.library, guid);
            if (p.clip == guid) {
                p.clip = p.library.empty() ? Asset::AssetGuid{} : p.library.front();
            }
        })) {
        m_timeline.status = "Removed from the player (the file is still in the project)";
    }
}

void Editor::DuplicateAnimationTimelineClip(const Scene::Entity player, const Asset::AssetGuid guid) {
    const AnimationTimelineState::ClipCard card = AnimationTimelineClipCard(guid);
    Asset::AnimationClipAsset clip;
    if (!Asset::LoadAnimationClipAsset(card.path, clip)) {
        m_timeline.status = "Could not read " + card.name;
        return;
    }
    const std::filesystem::path copy = UniqueClipPath(card.path.parent_path(), card.path.stem().string() + "_copy");
    clip.clipId = copy.stem().string();
    if (Asset::SaveAnimationClipAsset(clip, copy)) {
        static_cast<void>(AddClipToAnimationSequencer(player, copy));
        m_timeline.status = "Duplicated to " + copy.filename().string();
    }
}

bool Editor::CreateAnimationTimelineClip(const Scene::Entity player, const Scene::Entity skeletonEntity) {
    Scene::IEntityManager& entityManager = m_services->GetEntityManager();
    if (!entityManager.IsAlive(player) || !entityManager.HasComponent<Scene::AnimationSequencerComponent>(player)) {
        return false;
    }
    const auto& component = entityManager.GetComponent<Scene::AnimationSequencerComponent>(player);
    // A one-second clip beside the current one (or in Content/Animations), keyed at both ends with
    // the skeleton's rest pose, so it plays and is ready to edit.
    const Asset::ProjectFileSnapshot project = m_services->GetProjectFileIndex().GetSnapshot();
    std::filesystem::path directory = project.contentRoot / "Animations";
    if (component.clip != Asset::AssetGuid{}) {
        const std::filesystem::path currentPath = m_services->GetAssetDatabase().Resolve(component.clip);
        if (!currentPath.empty()) {
            directory = currentPath.parent_path();
        }
    }
    std::error_code error;
    std::filesystem::create_directories(directory, error);
    const std::string owner = entityManager.HasComponent<Scene::NameComponent>(player)
                                  ? entityManager.GetComponent<Scene::NameComponent>(player).name
                                  : std::string{"Animation"};
    const std::filesystem::path path = UniqueClipPath(directory, "New_Animation");
    Asset::AnimationClipAsset clip;
    clip.clipId = path.stem().string();
    clip.durationSeconds = 1.0;
    if (skeletonEntity != Scene::kInvalidEntity && entityManager.IsAlive(skeletonEntity) &&
        entityManager.HasComponent<Scene::Skeleton3DComponent>(skeletonEntity)) {
        for (const Scene::SkeletonBone& bone : entityManager.GetComponent<Scene::Skeleton3DComponent>(skeletonEntity).bones) {
            Asset::AnimationAssetSample rest;
            rest.pose.position = bone.localPosition;
            rest.pose.rotation = bone.localRotation;
            rest.pose.scale = bone.localScale;
            Asset::AnimationAssetSample end = rest;
            end.timeSeconds = 1.0;
            clip.bones.push_back(Asset::AnimationAssetBoneTrack{bone.name, {rest, end}});
        }
    }
    if (clip.bones.empty()) {
        Asset::AnimationAssetSample start;
        Asset::AnimationAssetSample end;
        end.timeSeconds = 1.0;
        clip.samples = {start, end};
    }
    if (Asset::SaveAnimationClipAsset(clip, path) && AddClipToAnimationSequencer(player, path)) {
        m_timeline.status = "New animation for " + owner + ": " + path.filename().string();
        return true;
    }
    m_timeline.status = "Could not create " + path.filename().string();
    return false;
}

bool Editor::RenameAnimationTimelineClip(const std::string& name) {
    if (name.empty() || m_timeline.clip == nullptr) {
        return false;
    }
    // The name is the file's: the .uvanim is renamed (keeping its GUID, so everything that uses it
    // still does) and the clip inside is written with the same name and any unsaved key edits.
    const std::filesystem::path path = m_services->GetAssetDatabase().Resolve(m_timeline.clipGuid);
    const std::optional<std::filesystem::path> renamedPath =
        path.stem().string() == name ? std::optional<std::filesystem::path>{path} : RenameContentAsset(path, name);
    if (!renamedPath.has_value()) {
        m_timeline.status = "Cannot rename to \"" + name + "\": not a valid file name, or taken";
        return false;
    }
    auto renamed = std::make_shared<Asset::AnimationClipAsset>(*m_timeline.clip);
    renamed->clipId = renamedPath->stem().string();
    if (!Asset::SaveAnimationClipAsset(*renamed, *renamedPath)) {
        m_timeline.status = "Could not write " + renamedPath->filename().string();
        return false;
    }
    m_timeline.retired = m_timeline.clip;
    m_timeline.clip = std::move(renamed);
    m_timeline.dirty = false;
    m_timeline.clipCards.erase(m_timeline.clipGuid.value);
    m_timeline.status = "Renamed to " + renamedPath->filename().string();
    return true;
}

void Editor::BrowseAnimationTimeline(const Scene::Entity player, const AnimationTimelineBrowse what) {
    Scene::IEntityManager& entityManager = m_services->GetEntityManager();
    if (!entityManager.IsAlive(player) || !entityManager.HasComponent<Scene::AnimationSequencerComponent>(player)) {
        return;
    }
    const Asset::AssetGuid clip = entityManager.GetComponent<Scene::AnimationSequencerComponent>(player).clip;
    const Asset::ProjectFileSnapshot project = m_services->GetProjectFileIndex().GetSnapshot();
    FilePickerRequest request;
    request.mode = FilePickerMode::Open;
    request.startDirectory = PickerStartDirectory(project, clip);
    switch (what) {
        case AnimationTimelineBrowse::AddClip:
            request.title = "Add Animation";
            request.extensions = {".uvanim"};
            request.onPick = [this, player](const std::filesystem::path& path) {
                static_cast<void>(AddClipToAnimationSequencer(player, path));
            };
            break;
        case AnimationTimelineBrowse::ImportLibrary:
            request.title = "Import Library";
            request.extensions = {".uvanimlib"};
            request.onPick = [this, player](const std::filesystem::path& path) {
                static_cast<void>(ImportAnimationLibraryIntoSequencer(player, path));
            };
            break;
        case AnimationTimelineBrowse::LinkLibrary:
            request.title = "Link Library";
            request.extensions = {".uvanimlib"};
            request.onPick = [this, player](const std::filesystem::path& path) {
                static_cast<void>(LinkAnimationLibraryToSequencer(player, path));
            };
            break;
        case AnimationTimelineBrowse::ExportLibrary: {
            const std::string owner = entityManager.HasComponent<Scene::NameComponent>(player)
                                          ? entityManager.GetComponent<Scene::NameComponent>(player).name
                                          : std::string{"Animation"};
            request.mode = FilePickerMode::Save;
            request.title = "Export Library";
            request.extensions = {".uvanimlib"};
            request.saveName = SafeLibraryStem(owner) + " Library";
            request.onPick = [this, player](const std::filesystem::path& path) {
                static_cast<void>(ExportAnimationSequencerToLibrary(player, path));
            };
            break;
        }
    }
    OpenFilePicker(std::move(request));
}

} // namespace UVE::Editor
