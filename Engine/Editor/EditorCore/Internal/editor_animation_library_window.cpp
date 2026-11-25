// The Animation Library window's state: a `.uvanimlib` opened from Content. The window itself is
// PRISM's (shell/anim_library/editor_animation_library_editor). Direct edits (remove, reorder,
// rename) write the file at once; bulk clip additions run one-by-one through the paced queue so
// adding dozens or hundreds of animations never stalls the UI or spikes RAM.
#include "editor.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstddef>
#include <cstring>
#include <filesystem>
#include <optional>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#include "animation_clip_asset.h"
#include "animation_library_asset.h"

namespace UVE::Editor {
namespace {

[[nodiscard]] std::string Utf8String(const std::filesystem::path& p) {
    const std::u8string u8 = p.u8string();
    return std::string(reinterpret_cast<const char*>(u8.data()), u8.size());
}

[[nodiscard]] std::string Utf8GenericString(const std::filesystem::path& p) {
    const std::u8string u8 = p.generic_u8string();
    return std::string(reinterpret_cast<const char*>(u8.data()), u8.size());
}

[[nodiscard]] std::string LowerAscii(std::string text) {
    std::ranges::transform(text, text.begin(),
                           [](const unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return text;
}

[[nodiscard]] bool IsAnimClipPath(const std::filesystem::path& path) {
    return LowerAscii(Utf8GenericString(path.extension())) == ".uvanim";
}

[[nodiscard]] std::string NormalizeRelativeFolderKey(const std::filesystem::path& folder) {
    std::string normalized = Utf8GenericString(folder.lexically_normal());
    while (!normalized.empty() && normalized.back() == '/') {
        normalized.pop_back();
    }
    if (normalized == ".") {
        normalized.clear();
    }
    return normalized;
}

// The entry name the file keeps for `clipAbsolute`: the file's own stem, so the list reads like
// Content. Overlong stems are cut to what the file format holds; names need not be unique.
[[nodiscard]] std::string LibraryEntryName(const std::filesystem::path& clipAbsolute,
                                           const std::string& fallbackClipId) {
    std::string name = Utf8GenericString(clipAbsolute.stem());
    if (name.empty()) {
        name = fallbackClipId;
    }
    if (name.empty()) {
        name = "clip";
    }
    std::erase(name, '\0');
    if (name.empty()) {
        name = "clip";
    }
    if (name.size() > Asset::kMaximumAnimationLibraryIdentifierBytes) {
        name.resize(Asset::kMaximumAnimationLibraryIdentifierBytes);
    }
    return name;
}

[[nodiscard]] std::string SafeLibraryId(std::string stem) {
    std::erase(stem, '\0');
    if (stem.empty()) {
        stem = "Animations";
    }
    if (stem.size() > Asset::kMaximumAnimationLibraryIdentifierBytes) {
        stem.resize(Asset::kMaximumAnimationLibraryIdentifierBytes);
    }
    return stem;
}

[[nodiscard]] bool LibraryHasGuid(const Asset::AnimationLibraryAsset& library, const Asset::AssetGuid guid) {
    return std::ranges::any_of(library.entries, [guid](const Asset::AnimationLibraryEntry& entry) {
        return entry.clip == guid;
    });
}

} // namespace

bool Editor::IsAnimationLibraryAddQueueActive() const noexcept {
    return m_animLibraryAddQueue.has_value() && !m_animLibraryAddQueue->pendingClips.empty();
}

bool Editor::OpenAnimationLibrary(const std::filesystem::path& assetPath) {
    // One window: opening another library switches to it, like Retarget. A file that does not
    // read as a library leaves whatever is open alone.
    Asset::AnimationLibraryAsset library;
    if (m_animLibraryAddQueue.has_value() && m_animLibraryAddQueue->libraryPath == assetPath) {
        library = m_animLibraryAddQueue->library;
    } else if (!Asset::LoadAnimationLibraryAsset(assetPath, library)) {
        return false;
    }
    AnimationLibraryWindowState window;
    window.assetPath = assetPath;
    window.library = std::move(library);
    m_animationLibraryWindow = std::move(window);
    return true;
}

void Editor::CloseAnimationLibraryWindow() {
    if (m_animLibraryAddQueue.has_value() && m_animationLibraryWindow.has_value() &&
        m_animLibraryAddQueue->libraryPath == m_animationLibraryWindow->assetPath) {
        m_animLibraryAddQueue->library = m_animationLibraryWindow->library;
    }
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

std::size_t Editor::AppendClipsToAnimationLibrary(
    const std::filesystem::path& libraryAbsolute,
    const std::vector<std::filesystem::path>& clipAbsolutePaths) {
    // Direct synchronous funnel (e.g. single-file drop onto a closed `.uvanimlib` in Content):
    // validates via streaming SAX summary so keyframe curves are never allocated in RAM.
    Asset::AnimationLibraryAsset library;
    if (!Asset::LoadAnimationLibraryAsset(libraryAbsolute, library)) {
        return 0U;
    }
    Asset::IAssetDatabase& assetDatabase = m_services->GetAssetDatabase();
    std::size_t added = 0U;
    for (const std::filesystem::path& clipAbsolute : clipAbsolutePaths) {
        if (!IsAnimClipPath(clipAbsolute)) {
            continue;
        }
        const Asset::AnimationClipSummary summary = Asset::InspectAnimationClipFileSummary(clipAbsolute);
        if (!summary.valid) {
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
        entry.name = LibraryEntryName(clipAbsolute, summary.clipId);
        library.entries.push_back(std::move(entry));
        ++added;
    }
    if (added == 0U || !Asset::SaveAnimationLibraryAsset(library, libraryAbsolute)) {
        return 0U;
    }
    return added;
}

void Editor::QueueClipsForAnimationLibrary(const std::filesystem::path& libraryAbsolute,
                                           const std::vector<std::filesystem::path>& clipAbsolutePaths) {
    if (clipAbsolutePaths.empty()) {
        if (m_animationLibraryWindow.has_value() && m_animationLibraryWindow->assetPath == libraryAbsolute) {
            m_animationLibraryWindow->status = "Nothing new to add: only .uvanim clips not already listed join.";
            m_animationLibraryWindow->statusIsError = false;
        }
        return;
    }

    const std::filesystem::path normalizedLib = libraryAbsolute.lexically_normal();
    if (m_animLibraryAddQueue.has_value()) {
        if (m_animLibraryAddQueue->libraryPath.lexically_normal() == normalizedLib) {
            for (const std::filesystem::path& clip : clipAbsolutePaths) {
                m_animLibraryAddQueue->pendingClips.push_back(clip);
            }
            m_animLibraryAddQueue->totalCount += clipAbsolutePaths.size();
            return;
        }
        // Finish and flush any previous library's queue before starting another.
        while (PollAnimationLibraryAddQueue(128U, 1.0)) {
        }
    }

    Asset::AnimationLibraryAsset library;
    if (m_animationLibraryWindow.has_value() &&
        m_animationLibraryWindow->assetPath.lexically_normal() == normalizedLib) {
        library = m_animationLibraryWindow->library;
    } else if (!Asset::LoadAnimationLibraryAsset(libraryAbsolute, library)) {
        return;
    }

    AnimationLibraryAddQueueState queue;
    queue.libraryPath = libraryAbsolute;
    queue.library = std::move(library);
    for (const std::filesystem::path& clip : clipAbsolutePaths) {
        queue.pendingClips.push_back(clip);
    }
    queue.totalCount = queue.pendingClips.size();
    m_animLibraryAddQueue = std::move(queue);

    if (m_animationLibraryWindow.has_value() &&
        m_animationLibraryWindow->assetPath.lexically_normal() == normalizedLib) {
        m_animationLibraryWindow->status =
            "Loading animations (0 / " + std::to_string(clipAbsolutePaths.size()) + ")...";
        m_animationLibraryWindow->statusIsError = false;
    }
}

bool Editor::PollAnimationLibraryAddQueue(const std::size_t maxClips, const double maxBudgetSeconds) {
    if (!m_animLibraryAddQueue.has_value()) {
        return false;
    }
    AnimationLibraryAddQueueState& queue = *m_animLibraryAddQueue;
    const bool windowMatches =
        m_animationLibraryWindow.has_value() &&
        m_animationLibraryWindow->assetPath.lexically_normal() == queue.libraryPath.lexically_normal();
    Asset::AnimationLibraryAsset& targetLibrary =
        windowMatches ? m_animationLibraryWindow->library : queue.library;

    Asset::IAssetDatabase& assetDatabase = m_services->GetAssetDatabase();
    const auto start = std::chrono::steady_clock::now();
    const std::size_t clipCap = std::max<std::size_t>(1U, maxClips);
    std::size_t processedThisTick = 0U;
    std::string lastClipLabel;

    while (!queue.pendingClips.empty() && processedThisTick < clipCap) {
        const std::filesystem::path clipAbsolute = std::move(queue.pendingClips.front());
        queue.pendingClips.pop_front();
        ++queue.processedCount;
        ++processedThisTick;
        lastClipLabel = Utf8String(clipAbsolute.stem());

        if (IsAnimClipPath(clipAbsolute) &&
            targetLibrary.entries.size() < Asset::kMaximumAnimationLibraryEntries) {
            const Asset::AnimationClipSummary summary = Asset::InspectAnimationClipFileSummary(clipAbsolute);
            if (summary.valid) {
                const Asset::AssetGuid guid = assetDatabase.Register(clipAbsolute);
                if (guid != Asset::kInvalidAssetGuid && !LibraryHasGuid(targetLibrary, guid)) {
                    Asset::AnimationLibraryEntry entry;
                    entry.clip = guid;
                    entry.name = LibraryEntryName(clipAbsolute, summary.clipId);
                    targetLibrary.entries.push_back(std::move(entry));
                    ++queue.addedCount;
                    ++queue.dirtyUnsavedCount;
                    queue.databaseTouched = true;
                }
            }
        }

        if (queue.dirtyUnsavedCount >= 8U) {
            if (Asset::SaveAnimationLibraryAsset(targetLibrary, queue.libraryPath)) {
                queue.dirtyUnsavedCount = 0U;
            }
        }

        const double elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
        if (elapsed >= maxBudgetSeconds) {
            break;
        }
    }

    if (windowMatches) {
        queue.library = targetLibrary;
    }

    if (!queue.pendingClips.empty()) {
        if (windowMatches) {
            m_animationLibraryWindow->status =
                "Loading animations (" + std::to_string(queue.processedCount) + " / " +
                std::to_string(queue.totalCount) + ")" +
                (lastClipLabel.empty() ? std::string{"..."} : ": " + lastClipLabel);
            m_animationLibraryWindow->statusIsError = false;
        }
        return true;
    }

    bool writeOk = true;
    if (queue.dirtyUnsavedCount > 0U) {
        writeOk = Asset::SaveAnimationLibraryAsset(targetLibrary, queue.libraryPath);
        queue.dirtyUnsavedCount = 0U;
    }
    if (queue.databaseTouched) {
        static_cast<void>(assetDatabase.Save());
        queue.databaseTouched = false;
    }
    if (windowMatches) {
        if (!writeOk) {
            m_animationLibraryWindow->status =
                "Could not write " + Utf8String(queue.libraryPath.filename()) + ".";
            m_animationLibraryWindow->statusIsError = true;
        } else {
            m_animationLibraryWindow->status =
                queue.addedCount > 0U
                    ? (queue.addedCount == 1U ? "Added 1 clip."
                                              : "Added " + std::to_string(queue.addedCount) + " clips.")
                    : "Nothing new to add: only .uvanim clips not already listed join.";
            m_animationLibraryWindow->statusIsError = false;
        }
    }
    m_animLibraryAddQueue.reset();
    return false;
}

std::vector<std::filesystem::path> Editor::CollectAnimationClipsInFolder(
    const std::filesystem::path& relativeFolder) const {
    if (m_services == nullptr) {
        return {};
    }
    const Asset::ProjectFileSnapshot snapshot = m_services->GetProjectFileIndex().GetSnapshot();
    const std::string targetFolder = NormalizeRelativeFolderKey(relativeFolder);
    std::vector<std::filesystem::path> clips;

    if (!snapshot.entries.empty()) {
        for (const Asset::ProjectFileEntry& entry : snapshot.entries) {
            if (entry.kind != Asset::ProjectFileEntryKind::File || !IsAnimClipPath(entry.relativePath)) {
                continue;
            }
            // Strictly direct `.uvanim` files inside `relativeFolder` — nested subfolders are excluded.
            if (NormalizeRelativeFolderKey(entry.relativePath.parent_path()) != targetFolder) {
                continue;
            }
            clips.push_back((snapshot.contentRoot / entry.relativePath).lexically_normal());
        }
    } else if (!snapshot.contentRoot.empty()) {
        const std::filesystem::path folderAbs =
            targetFolder.empty() ? snapshot.contentRoot : (snapshot.contentRoot / relativeFolder);
        std::error_code error;
        for (const std::filesystem::directory_entry& dirEntry :
             std::filesystem::directory_iterator(folderAbs, error)) {
            if (error) {
                break;
            }
            std::error_code fileErr;
            if (dirEntry.is_regular_file(fileErr) && !fileErr && IsAnimClipPath(dirEntry.path())) {
                clips.push_back(dirEntry.path().lexically_normal());
            }
        }
    }

    std::ranges::sort(clips, [](const std::filesystem::path& left, const std::filesystem::path& right) {
        return Utf8GenericString(left) < Utf8GenericString(right);
    });
    return clips;
}

std::optional<std::filesystem::path> Editor::CreateAnimationLibraryFromFolder(
    const std::filesystem::path& relativeFolder) {
    if (m_services == nullptr || !IsAuthoringCommandAllowed()) {
        return std::nullopt;
    }
    const std::vector<std::filesystem::path> clips = CollectAnimationClipsInFolder(relativeFolder);
    if (clips.empty()) {
        return std::nullopt;
    }

    const Asset::ProjectFileSnapshot snapshot = m_services->GetProjectFileIndex().GetSnapshot();
    const std::string normalizedFolder = NormalizeRelativeFolderKey(relativeFolder);
    const std::filesystem::path folderAbs =
        normalizedFolder.empty() ? snapshot.contentRoot : (snapshot.contentRoot / relativeFolder);
    std::error_code error;
    std::filesystem::create_directories(folderAbs, error);

    const std::string rawFolderName =
        normalizedFolder.empty() ? std::string{"Animations"} : Utf8String(relativeFolder.lexically_normal().filename());
    const std::string baseStem = SafeLibraryId(rawFolderName.empty() || rawFolderName == "." ? "Animations" : rawFolderName);
    const std::filesystem::path libraryPath = MakeUniqueContentPath(folderAbs, baseStem, ".uvanimlib");

    Asset::AnimationLibraryAsset library;
    library.libraryId = SafeLibraryId(Utf8String(libraryPath.stem()));
    if (!Asset::SaveAnimationLibraryAsset(library, libraryPath)) {
        m_contentStatusMessage = "Could not create Animation Library in folder.";
        return std::nullopt;
    }

    Asset::IAssetDatabase& assetDatabase = m_services->GetAssetDatabase();
    static_cast<void>(assetDatabase.Register(libraryPath));
    static_cast<void>(assetDatabase.Save());
    RefreshProjectFileIndex();

    static_cast<void>(OpenAnimationLibrary(libraryPath));
    QueueClipsForAnimationLibrary(libraryPath, clips);
    m_contentStatusMessage = "Created " + Utf8String(libraryPath.filename()) + " (" +
                             std::to_string(clips.size()) +
                             (clips.size() == 1U ? " animation)." : " animations).");
    return libraryPath;
}

} // namespace UVE::Editor
