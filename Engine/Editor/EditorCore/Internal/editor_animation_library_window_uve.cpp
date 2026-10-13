// The Animation Library window: a `.uvanimlib` opened from Content, in its own OS window like
// Retarget. Every mutation (add, remove, reorder, drop) writes the file at once, so there is no
// dirty flag and nothing to lose on close.
#include "uve/editor/editor_uve.h"

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <filesystem>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <imgui.h>
#include <imgui_internal.h>

#include "uve/asset/animation_clip_asset_uve.h"
#include "uve/asset/animation_library_asset_uve.h"
#include "editor_chrome_layout_uve.h"

namespace UVE::Editor {
namespace {

// The entry name the file keeps for `clipAbsolute`: the file's own stem, so the list reads like
// Content. Overlong stems are cut to what the file format holds; names need not be unique.
[[nodiscard]] std::string LibraryEntryNameUVE(const std::filesystem::path& clipAbsolute,
                                              const Asset::AnimationClipAssetUVE& probe) {
    std::string name = clipAbsolute.stem().generic_string();
    if (name.empty()) {
        name = probe.clipId;
    }
    if (name.empty()) {
        name = "clip";
    }
    if (name.size() > Asset::kMaximumAnimationLibraryIdentifierBytesUVE) {
        name.resize(Asset::kMaximumAnimationLibraryIdentifierBytesUVE);
    }
    return name;
}

[[nodiscard]] bool LibraryHasGuidUVE(const Asset::AnimationLibraryAssetUVE& library, Asset::AssetGuidUVE guid) {
    return std::ranges::any_of(library.entries, [guid](const Asset::AnimationLibraryEntryUVE& entry) {
        return entry.clip == guid;
    });
}

} // namespace

bool EditorUVE::OpenAnimationLibraryUVE(const std::filesystem::path& assetPath) {
    // One window: opening another library switches to it, like Retarget. A file that does not
    // read as a library leaves whatever is open alone.
    Asset::AnimationLibraryAssetUVE library;
    if (!Asset::LoadAnimationLibraryAssetUVE(assetPath, library)) {
        return false;
    }
    AnimationLibraryWindowStateUVE window;
    window.assetPath = assetPath;
    window.library = std::move(library);
    m_animationLibraryWindow = std::move(window);
    return true;
}

void EditorUVE::CloseAnimationLibraryWindowUVE() {
    m_animationLibraryWindow.reset();
}

void EditorUVE::RefreshAnimationLibraryWindowUVE(const std::filesystem::path& libraryAbsolute) {
    if (!m_animationLibraryWindow.has_value() || m_animationLibraryWindow->assetPath != libraryAbsolute) {
        return;
    }
    AnimationLibraryWindowStateUVE& window = *m_animationLibraryWindow;
    Asset::AnimationLibraryAssetUVE fresh;
    if (!Asset::LoadAnimationLibraryAssetUVE(libraryAbsolute, fresh)) {
        window.status = "The file changed outside this window and no longer reads as a library.";
        window.statusIsError = true;
        return;
    }
    window.library = std::move(fresh);
    window.status = "Added from Content.";
    window.statusIsError = false;
}

std::vector<std::filesystem::path> EditorUVE::AcceptAnimationLibraryDropPathsUVE() {
    // Both Content drag shapes: one path NUL-terminated, or many NUL-joined. Only one fires per
    // drop; both come back content-relative.
    std::vector<std::filesystem::path> paths;
    if (const ImGuiPayload* const single = ImGui::AcceptDragDropPayload(kContentItemPayloadUVE);
        single != nullptr && single->Data != nullptr && single->DataSize > 1) {
        paths.emplace_back(
            std::string(static_cast<const char*>(single->Data), static_cast<std::size_t>(single->DataSize - 1)));
    }
    if (const ImGuiPayload* const multi = ImGui::AcceptDragDropPayload(kContentItemsPayloadUVE);
        multi != nullptr && multi->Data != nullptr && multi->DataSize > 0) {
        const char* const bytes = static_cast<const char*>(multi->Data);
        const std::size_t byteCount = static_cast<std::size_t>(multi->DataSize);
        std::size_t at = 0U;
        while (at < byteCount) {
            const void* const found = std::memchr(bytes + at, '\0', byteCount - at);
            if (found == nullptr) {
                break;
            }
            const char* const end = static_cast<const char*>(found);
            if (end != bytes + at) {
                paths.emplace_back(std::string(bytes + at, end));
            }
            at += static_cast<std::size_t>(end - (bytes + at)) + 1U;
        }
    }
    return paths;
}

std::size_t EditorUVE::AppendClipsToAnimationLibraryUVE(const std::filesystem::path& libraryAbsolute,
                                                        const std::vector<std::filesystem::path>& clipAbsolutePaths) {
    // The one funnel for drops: only `.uvanim` files that load, and whose GUIDs are not already
    // listed, are appended, and the file is written once. Anything else is skipped, never half
    // added.
    Asset::AnimationLibraryAssetUVE library;
    if (!Asset::LoadAnimationLibraryAssetUVE(libraryAbsolute, library)) {
        return 0U;
    }
    Asset::IAssetDatabaseUVE& assetDatabase = m_services->GetAssetDatabaseUVE();
    std::size_t added = 0U;
    for (const std::filesystem::path& clipAbsolute : clipAbsolutePaths) {
        if (clipAbsolute.extension() != ".uvanim") {
            continue;
        }
        Asset::AnimationClipAssetUVE probe;
        if (!Asset::LoadAnimationClipAssetUVE(clipAbsolute, probe)) {
            continue;
        }
        const Asset::AssetGuidUVE guid = assetDatabase.RegisterUVE(clipAbsolute);
        if (guid == Asset::kInvalidAssetGuidUVE || LibraryHasGuidUVE(library, guid)) {
            continue;
        }
        if (library.entries.size() >= Asset::kMaximumAnimationLibraryEntriesUVE) {
            break;
        }
        Asset::AnimationLibraryEntryUVE entry;
        entry.clip = guid;
        entry.name = LibraryEntryNameUVE(clipAbsolute, probe);
        library.entries.push_back(std::move(entry));
        ++added;
    }
    if (added == 0U || !Asset::SaveAnimationLibraryAssetUVE(library, libraryAbsolute)) {
        return 0U;
    }
    return added;
}

void EditorUVE::DrawAnimationLibraryWindowUVE() {
    AnimationLibraryWindowStateUVE& window = *m_animationLibraryWindow;
    Asset::IAssetDatabaseUVE& assetDatabase = m_services->GetAssetDatabaseUVE();

    const ImGuiViewport* const mainViewport = ImGui::GetMainViewport();
    // Its own OS window, like Retarget: never merged into the main one, never docked.
    ImGuiWindowClass windowClass;
    windowClass.ViewportFlagsOverrideSet = ImGuiViewportFlags_NoAutoMerge;
    ImGui::SetNextWindowClass(&windowClass);
    ImGui::SetNextWindowPos(ImVec2{mainViewport->GetCenter().x, mainViewport->GetCenter().y}, ImGuiCond_FirstUseEver,
                            ImVec2{0.5F, 0.5F});
    ImGui::SetNextWindowSize(ImVec2{560.0F, 420.0F}, ImGuiCond_FirstUseEver);
    bool open = true;
    const std::string title = window.assetPath.filename().generic_string() + "###animation-library-window";
    ImGui::Begin(title.c_str(), &open, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoDocking);

    // Every edit below writes the file through here. A failed write rolls the window back to what
    // the file holds, so the list never shows entries that are not on disk.
    const auto saveNow = [&](const char* doneLine) {
        if (Asset::SaveAnimationLibraryAssetUVE(window.library, window.assetPath)) {
            window.status = doneLine;
            window.statusIsError = false;
            return true;
        }
        window.status = "Could not write " + window.assetPath.filename().generic_string() + ".";
        window.statusIsError = true;
        Asset::AnimationLibraryAssetUVE rollback;
        if (Asset::LoadAnimationLibraryAssetUVE(window.assetPath, rollback)) {
            window.library = std::move(rollback);
        }
        return false;
    };

    // The entries, in list order. A GUID the database no longer resolves stays in the file (the
    // player skips it) but is marked, so a rename or a delete is visible instead of silent.
    std::optional<std::size_t> removeAt;
    std::optional<std::size_t> moveUpAt;
    std::optional<std::size_t> moveDownAt;
    std::optional<std::size_t> renameAt;
    for (std::size_t index = 0U; index < window.library.entries.size(); ++index) {
        const Asset::AnimationLibraryEntryUVE& entry = window.library.entries[index];
        ImGui::PushID(static_cast<int>(index));
        if (assetDatabase.ResolveUVE(entry.clip).empty()) {
            ImGui::TextDisabled("%s (missing)", entry.name.c_str());
        } else {
            ImGui::TextUnformatted(entry.name.c_str());
        }
        ImGui::SameLine();
        ImGui::BeginDisabled(index == 0U);
        if (ImGui::SmallButton("Up")) {
            moveUpAt = index;
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::BeginDisabled(index + 1U >= window.library.entries.size());
        if (ImGui::SmallButton("Down")) {
            moveDownAt = index;
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::SmallButton("Remove")) {
            removeAt = index;
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("Rename")) {
            renameAt = index;
        }
        ImGui::PopID();
    }
    if (window.library.entries.empty()) {
        ImGui::TextDisabled("No clips yet: pick one below, or drop .uvanim files here.");
    }
    if (removeAt.has_value()) {
        const std::string name = window.library.entries[*removeAt].name;
        window.library.entries.erase(window.library.entries.begin() + static_cast<std::ptrdiff_t>(*removeAt));
        static_cast<void>(saveNow(("Removed '" + name + "'.").c_str()));
    } else if (moveUpAt.has_value()) {
        std::swap(window.library.entries[*moveUpAt], window.library.entries[*moveUpAt - 1U]);
        static_cast<void>(saveNow("Moved up."));
    } else if (moveDownAt.has_value()) {
        std::swap(window.library.entries[*moveDownAt], window.library.entries[*moveDownAt + 1U]);
        static_cast<void>(saveNow("Moved down."));
    }
    if (renameAt.has_value()) {
        window.renameEntryRequested = renameAt;
        window.entryName = window.library.entries[*renameAt].name;
        ImGui::OpenPopup("##lib-rename-entry");
    }

    ImGui::Separator();
    const bool full = window.library.entries.size() >= Asset::kMaximumAnimationLibraryEntriesUVE;
    if (full) {
        ImGui::TextDisabled("The library is full (%d entries).", static_cast<int>(window.library.entries.size()));
    } else {
        if (ImGui::Button("Add Animation...")) {
            const Asset::ProjectFileSnapshotUVE project = m_services->GetProjectFileIndexUVE().GetSnapshotUVE();
            const std::filesystem::path relative = window.assetPath.lexically_relative(project.contentRoot);
            FilePickerRequestUVE request;
            request.mode = FilePickerModeUVE::Open;
            request.title = "Add Animation";
            request.extensions = {".uvanim"};
            request.startDirectory = (relative.empty() || *relative.begin() == "..") ? std::filesystem::path{}
                                                                                     : relative.parent_path();
            const std::filesystem::path libraryAbsolute = window.assetPath;
            request.onPick = [this, libraryAbsolute](const std::filesystem::path& picked) {
                const std::size_t added = AppendClipsToAnimationLibraryUVE(libraryAbsolute, {picked});
                RefreshAnimationLibraryWindowUVE(libraryAbsolute);
                if (m_animationLibraryWindow.has_value() && m_animationLibraryWindow->assetPath == libraryAbsolute) {
                    m_animationLibraryWindow->status =
                        added > 0U ? "Added '" + picked.stem().string() + "'." : "Nothing new to add.";
                    m_animationLibraryWindow->statusIsError = false;
                }
            };
            OpenFilePickerUVE(std::move(request));
        }
    }

    if (!window.status.empty()) {
        if (window.statusIsError) {
            ImGui::TextColored(ImVec4{1.0F, 0.45F, 0.45F, 1.0F}, "%s", window.status.c_str());
        } else {
            ImGui::TextDisabled("%s", window.status.c_str());
        }
    }

    // Renaming an entry: the list's own label for the clip, checked on Enter. The 128-byte box
    // is the format's whole name budget, so anything typed already fits.
    if (ImGui::BeginPopup("##lib-rename-entry")) {
        ImGui::TextDisabled("Entry name");
        char buffer[129];
        std::snprintf(buffer, sizeof(buffer), "%s", window.entryName.c_str());
        if (ImGui::IsWindowAppearing()) {
            ImGui::SetKeyboardFocusHere();
        }
        ImGui::SetNextItemWidth(240.0F);
        const bool entered = ImGui::InputText("##lib-rename-entry-field", buffer, sizeof(buffer),
                                              ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);
        window.entryName = buffer;
        if (entered) {
            const std::size_t at = window.renameEntryRequested.value_or(window.library.entries.size());
            if (window.entryName.empty()) {
                window.status = "Give the entry a name.";
                window.statusIsError = true;
            } else if (at >= window.library.entries.size()) {
                window.status = "That entry is gone; nothing was renamed.";
                window.statusIsError = true;
            } else {
                window.library.entries[at].name = window.entryName;
                static_cast<void>(saveNow(("Renamed to '" + window.entryName + "'.").c_str()));
            }
            window.renameEntryRequested.reset();
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    // A clip dropped anywhere on the window joins the list, like the Timeline's own drop.
    const ImGuiWindow* const self = ImGui::GetCurrentWindow();
    if (self != nullptr && ImGui::BeginDragDropTargetCustom(self->Rect(), self->ID)) {
        const std::vector<std::filesystem::path> dropped = AcceptAnimationLibraryDropPathsUVE();
        if (!dropped.empty()) {
            const std::filesystem::path root = m_services->GetProjectFileIndexUVE().GetSnapshotUVE().contentRoot;
            std::vector<std::filesystem::path> absolute;
            absolute.reserve(dropped.size());
            for (const std::filesystem::path& relative : dropped) {
                absolute.push_back(root / relative);
            }
            const std::size_t added = AppendClipsToAnimationLibraryUVE(window.assetPath, absolute);
            RefreshAnimationLibraryWindowUVE(window.assetPath);
            if (added > 0U) {
                window.status =
                    added == 1U ? "Added 1 clip." : "Added " + std::to_string(added) + " clips.";
                window.statusIsError = false;
            } else {
                window.status = "Nothing new to add: only .uvanim clips not already listed join.";
                window.statusIsError = false;
            }
        }
        ImGui::EndDragDropTarget();
    }

    ImGui::End();
    if (!open) {
        CloseAnimationLibraryWindowUVE();
    }
}

} // namespace UVE::Editor
