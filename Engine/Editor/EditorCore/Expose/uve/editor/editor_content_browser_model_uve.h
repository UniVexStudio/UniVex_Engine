// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

// The parts of the Content Browser that are decisions rather than drawing: where it is looking,
// the back/forward history, the user's shelves, and which files a folder or a search shows. Kept
// free of ImGui and GL so the rules can be tested on their own, and so the editor bridge lists
// exactly what the panel shows.

#include <chrono>
#include <cstddef>
#include <cstdint>
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
/// as a pinned path, so a file that has not been scanned yet is not lost. A shared shelf belongs
/// to the team and lives in the project (project.uvshelves); the rest are one person's.
struct ContentShelfUVE final {
    std::string name;
    std::vector<std::filesystem::path> items;
    bool shared = false;
};

class ContentShelvesUVE final {
public:
    static constexpr std::size_t kMaxShelvesUVE = 64U;
    static constexpr std::size_t kMaxItemsPerShelfUVE = 512U;

    [[nodiscard]] std::span<const ContentShelfUVE> GetAllUVE() const noexcept { return m_shelves; }
    [[nodiscard]] const ContentShelfUVE* FindUVE(std::string_view name) const noexcept;
    /// Adds an empty shelf named `baseName`, or "baseName 2", "baseName 3"... when that is taken.
    /// Returns the name used, or an empty string when the limit is reached.
    std::string CreateUVE(std::string_view baseName, bool shared = false);
    /// Moves a shelf between the team's and one person's. False when it does not exist.
    bool SetSharedUVE(std::string_view name, bool shared);
    /// Removes every shared (or every personal) shelf, leaving the other kind alone.
    void RemoveAllUVE(bool shared);
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

/// The team's shelves as the text of project.uvshelves (JSON); personal shelves are left out.
[[nodiscard]] std::string WriteSharedShelvesTextUVE(const ContentShelvesUVE& shelves);
/// Replaces the shared shelves in `shelves` with those in `text`. The file comes from someone
/// else, so an item that is absolute or climbs out with ".." is skipped, and a shelf whose name
/// a personal shelf already has keeps it while the personal one is renamed. False, with `error`
/// set and `shelves` untouched, when the text is not a shelves file.
bool ReadSharedShelvesTextUVE(std::string_view text, ContentShelvesUVE& shelves, std::string& error);

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

/// Everything anywhere under `directory` (files and folders), matching `query` when given, in
/// the same order. What the Board and Recent modes start from.
[[nodiscard]] std::vector<std::size_t> ListContentTreeUVE(std::span<const Asset::ProjectFileEntryUVE> entries,
                                                          const std::filesystem::path& directory, std::string_view query);

/// The same for a shelf: its items that exist in `entries` and match the query, in shelf order.
[[nodiscard]] std::vector<std::size_t> ListContentShelfUVE(std::span<const Asset::ProjectFileEntryUVE> entries,
                                                           const ContentShelfUVE& shelf, std::string_view query);

/// The folders the folder tree shows while its own search holds `query`: each folder whose name
/// matches, and every folder above one so it can be reached. Returned as generic path strings,
/// sorted. An empty query returns every folder.
[[nodiscard]] std::vector<std::string> CollectVisibleContentFoldersUVE(std::span<const Asset::ProjectFileEntryUVE> entries,
                                                                       std::string_view query);

/// The five ways the Content Browser shows things. Each is its own way of working, not only a look:
/// Tiles browses one folder by picture; Columns walks down folder levels side by side with a
/// preview of the file picked; Details is a table to sort by size or date; Recent lists what
/// changed lately across everything below; Board lays everything below out by kind.
enum class ContentBrowserModeUVE : std::uint8_t {
    Tiles = 0,
    Columns,
    Details,
    Recent,
    Board,
};

/// What the disk says about one file: its size and when it last changed (seconds since the
/// epoch; 0 when unknown).
struct ContentFileFactsUVE final {
    std::uintmax_t size = 0U;
    std::int64_t modified = 0;
};

/// What a sorting or grouping mode needs to know about one item.
struct ContentItemFactsUVE final {
    std::string name;
    std::string type;
    std::string folder;
    std::uintmax_t size = 0U;
    /// Seconds since the epoch of the last change; 0 when not known.
    std::int64_t modified = 0;
    bool isFolder = false;
};

enum class ContentSortKeyUVE : std::uint8_t {
    Name = 0,
    Type,
    Size,
    Modified,
    Folder,
};

/// Orders `order` (indices into `facts`) by `key`. Folders stay ahead of files either way; ties
/// fall back to the name so the order never jumps between frames.
void SortContentFactsUVE(std::vector<std::size_t>& order, std::span<const ContentItemFactsUVE> facts,
                         ContentSortKeyUVE key, bool ascending);

/// Recent's buckets, newest first.
enum class ContentAgeUVE : std::uint8_t {
    Today = 0,
    Yesterday,
    ThisWeek,
    Earlier,
};
[[nodiscard]] const char* GetContentAgeLabelUVE(ContentAgeUVE age) noexcept;
/// Which bucket a change at `modified` falls in, given when today began (local midnight), both in
/// seconds since the epoch. A time in the future counts as today.
[[nodiscard]] ContentAgeUVE ClassifyContentAgeUVE(std::int64_t modified, std::int64_t startOfToday) noexcept;

/// Board's lanes: the items of `order` grouped by type, lanes by type name, items keeping their
/// order within a lane. Folders are left out; the Board lays out files.
[[nodiscard]] std::vector<std::pair<std::string, std::vector<std::size_t>>> GroupContentByTypeUVE(
    const std::vector<std::size_t>& order, std::span<const ContentItemFactsUVE> facts);

/// The items picked in the Content browser, by content-relative path. A click picks one; Ctrl+click
/// adds or removes one; Shift+click picks the run from the last plain or Ctrl click to this one, in
/// the order the items are shown (with Ctrl as well, the run is added to what is picked).
class ContentSelectionUVE final {
public:
    /// Applies a click on `item`. `visibleOrder` is every item as currently shown, in order.
    void ClickUVE(std::span<const std::string> visibleOrder, const std::string& item, bool control, bool shift);

    [[nodiscard]] bool ContainsUVE(const std::string& item) const noexcept;
    /// The picked items, in the order they were picked.
    [[nodiscard]] const std::vector<std::string>& ItemsUVE() const noexcept { return m_items; }
    [[nodiscard]] bool IsEmptyUVE() const noexcept { return m_items.empty(); }
    void ClearUVE() noexcept;
    /// Drops every picked item that is not among `existing` (files deleted or renamed since).
    void KeepOnlyUVE(std::span<const std::string> existing);

private:
    std::vector<std::string> m_items;
    std::string m_anchor;
};

} // namespace UVE::Editor
