// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

// The parts of the Content Browser that are decisions rather than drawing: where it is looking,
// the back/forward history, the user's shelves, and which files a folder or a search shows. Kept
// free of ImGui and GL so the rules can be tested on their own, and so the editor bridge lists
// exactly what the panel shows.

#include <cstddef>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "uve/asset/i_project_file_index_uve.h"

namespace UVE::Editor {

/// What the item area shows: a folder of the project (empty is the content root), or, when
/// `shelf` is set, that shelf's items instead.
struct ContentLocationUVE final {
    std::filesystem::path directory;
    std::string shelf;

    [[nodiscard]] bool operator==(const ContentLocationUVE&) const = default;
};

/// Back and forward through the places the Content Browser has shown, like a file manager.
class ContentNavigationHistoryUVE final {
public:
    static constexpr std::size_t kMaxEntriesUVE = 64U;

    [[nodiscard]] const ContentLocationUVE& CurrentUVE() const noexcept { return m_current; }
    /// Moves to `location`, dropping the forward history. Moving to where it already is does
    /// nothing, so calling this every frame with the current place is safe.
    void GoUVE(const ContentLocationUVE& location);
    [[nodiscard]] bool CanGoBackUVE() const noexcept { return !m_back.empty(); }
    [[nodiscard]] bool CanGoForwardUVE() const noexcept { return !m_forward.empty(); }
    /// Returns false when there is nowhere to go.
    bool BackUVE();
    bool ForwardUVE();

private:
    std::vector<ContentLocationUVE> m_back;
    std::vector<ContentLocationUVE> m_forward;
    ContentLocationUVE m_current;
};

/// A named, hand-picked list of files and folders from anywhere in the project. Items are
/// project-relative paths; an item that is no longer on disk is not shown but is kept, the same
/// as a pinned path, so a file that has not been scanned yet is not lost.
struct ContentShelfUVE final {
    std::string name;
    std::vector<std::filesystem::path> items;
};

class ContentShelvesUVE final {
public:
    static constexpr std::size_t kMaxShelvesUVE = 64U;
    static constexpr std::size_t kMaxItemsPerShelfUVE = 512U;

    [[nodiscard]] std::span<const ContentShelfUVE> GetAllUVE() const noexcept { return m_shelves; }
    [[nodiscard]] const ContentShelfUVE* FindUVE(std::string_view name) const noexcept;
    /// Adds an empty shelf named `baseName`, or "baseName 2", "baseName 3"... when that is taken.
    /// Returns the name used, or an empty string when the limit is reached.
    std::string CreateUVE(std::string_view baseName);
    /// Refuses an empty name, a name another shelf has, or a shelf that does not exist.
    bool RenameUVE(std::string_view from, std::string_view to);
    bool RemoveUVE(std::string_view name);
    /// False when the shelf does not exist, already holds `item`, or is full.
    bool AddItemUVE(std::string_view shelf, const std::filesystem::path& item);
    bool RemoveItemUVE(std::string_view shelf, const std::filesystem::path& item);
    [[nodiscard]] bool ContainsUVE(std::string_view shelf, const std::filesystem::path& item) const;
    /// Follows a rename or move on disk: `from` itself and anything under it now lives at `to`.
    void MovePathUVE(const std::filesystem::path& from, const std::filesystem::path& to);
    void ClearUVE() noexcept { m_shelves.clear(); }

private:
    [[nodiscard]] ContentShelfUVE* FindMutableUVE(std::string_view name) noexcept;

    std::vector<ContentShelfUVE> m_shelves;
};

/// True when `path` is strictly inside `directory`, at any depth. Every path is inside the
/// content root (an empty `directory`) except the root itself.
[[nodiscard]] bool IsInsideContentDirectoryUVE(const std::filesystem::path& path,
                                               const std::filesystem::path& directory);

/// True when every space-separated word of `query` appears, ignoring case, in `name`. An empty
/// query matches everything.
[[nodiscard]] bool DoesContentNameMatchUVE(std::string_view name, std::string_view query) noexcept;

/// The entries the item area shows for a folder, as indices into `entries`: with no query, the
/// folder's direct children; with one, every file and folder anywhere under it whose name
/// matches. Folders come first, then everything by name, ignoring case.
[[nodiscard]] std::vector<std::size_t> ListContentFolderUVE(std::span<const Asset::ProjectFileEntryUVE> entries,
                                                            const std::filesystem::path& directory,
                                                            std::string_view query);

/// The same for a shelf: its items that exist in `entries` and match the query, in shelf order.
[[nodiscard]] std::vector<std::size_t> ListContentShelfUVE(std::span<const Asset::ProjectFileEntryUVE> entries,
                                                           const ContentShelfUVE& shelf, std::string_view query);

/// The folders the folder tree shows while its own search holds `query`: each folder whose name
/// matches, and every folder above one so it can be reached. Returned as generic path strings,
/// sorted. An empty query returns every folder.
[[nodiscard]] std::vector<std::string> CollectVisibleContentFoldersUVE(std::span<const Asset::ProjectFileEntryUVE> entries,
                                                                       std::string_view query);

} // namespace UVE::Editor
