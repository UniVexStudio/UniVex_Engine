// The Animation Library window's state: a `.uvanimlib` opened from Content. The window itself is
// PRISM's (shell/anim_library/editor_animation_library_editor). Every mutation (add, remove,
// reorder, drop) writes the file at once, so there is no dirty flag and nothing to lose on close.
#include "editor.h"

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

#include "animation_clip_asset.h"
#include "animation_library_asset.h"
#include "editor_chrome_layout.h"

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

} // namespace UVE::Editor
