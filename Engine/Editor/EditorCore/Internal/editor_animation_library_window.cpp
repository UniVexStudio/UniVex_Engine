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

#include "animation_clip_asset.h"
#include "animation_library_asset.h"

namespace UVE::Editor {
namespace {

// The entry name the file keeps for `clipAbsolute`: the file's own stem, so the list reads like
// Content. Overlong stems are cut to what the file format holds; names need not be unique.
[[nodiscard]] std::string LibraryEntryName(const std::filesystem::path& clipAbsolute,
                                              const Asset::AnimationClipAsset& probe) {
    std::string name = clipAbsolute.stem().generic_string();
    if (name.empty()) {
        name = probe.clipId;
    }
    if (name.empty()) {
        name = "clip";
    }
    if (name.size() > Asset::kMaximumAnimationLibraryIdentifierBytes) {
        name.resize(Asset::kMaximumAnimationLibraryIdentifierBytes);
    }
    return name;
}

[[nodiscard]] bool LibraryHasGuid(const Asset::AnimationLibraryAsset& library, Asset::AssetGuid guid) {
    return std::ranges::any_of(library.entries, [guid](const Asset::AnimationLibraryEntry& entry) {
        return entry.clip == guid;
    });
}

} // namespace

bool Editor::OpenAnimationLibrary(const std::filesystem::path& assetPath) {
    // One window: opening another library switches to it, like Retarget. A file that does not
    // read as a library leaves whatever is open alone.
    Asset::AnimationLibraryAsset library;
    if (!Asset::LoadAnimationLibraryAsset(assetPath, library)) {
        return false;
    }
    AnimationLibraryWindowState window;
    window.assetPath = assetPath;
    window.library = std::move(library);
    m_animationLibraryWindow = std::move(window);
    return true;
}

void Editor::CloseAnimationLibraryWindow() {
    m_animationLibraryWindow.reset();
}

void Editor::RefreshAnimationLibraryWindow(const std::filesystem::path& libraryAbsolute) {
    if (!m_animationLibraryWindow.has_value() || m_animationLibraryWindow->assetPath != libraryAbsolute) {
        return;
    }
    AnimationLibraryWindowState& window = *m_animationLibraryWindow;
    Asset::AnimationLibraryAsset fresh;
    if (!Asset::LoadAnimationLibraryAsset(libraryAbsolute, fresh)) {
        window.status = "The file changed outside this window and no longer reads as a library.";
        window.statusIsError = true;
        return;
    }
    window.library = std::move(fresh);
    window.status = "Added from Content.";
    window.statusIsError = false;
}

std::size_t Editor::AppendClipsToAnimationLibrary(const std::filesystem::path& libraryAbsolute,
                                                        const std::vector<std::filesystem::path>& clipAbsolutePaths) {
    // The one funnel for drops: only `.uvanim` files that load, and whose GUIDs are not already
    // listed, are appended, and the file is written once. Anything else is skipped, never half
    // added.
    Asset::AnimationLibraryAsset library;
    if (!Asset::LoadAnimationLibraryAsset(libraryAbsolute, library)) {
        return 0U;
    }
    Asset::IAssetDatabase& assetDatabase = m_services->GetAssetDatabase();
    std::size_t added = 0U;
    for (const std::filesystem::path& clipAbsolute : clipAbsolutePaths) {
        if (clipAbsolute.extension() != ".uvanim") {
            continue;
        }
        Asset::AnimationClipAsset probe;
        if (!Asset::LoadAnimationClipAsset(clipAbsolute, probe)) {
            continue;
        }
        const Asset::AssetGuid guid = assetDatabase.Register(clipAbsolute);
        if (guid == Asset::kInvalidAssetGuid || LibraryHasGuid(library, guid)) {
            continue;
        }
        if (library.entries.size() >= Asset::kMaximumAnimationLibraryEntries) {
            break;
        }
        Asset::AnimationLibraryEntry entry;
        entry.clip = guid;
        entry.name = LibraryEntryName(clipAbsolute, probe);
        library.entries.push_back(std::move(entry));
        ++added;
    }
    if (added == 0U || !Asset::SaveAnimationLibraryAsset(library, libraryAbsolute)) {
        return 0U;
    }
    return added;
}

} // namespace UVE::Editor
