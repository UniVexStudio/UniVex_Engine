// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/editor/editor_content_browser_model_uve.h"

#include <algorithm>
#include <cctype>
#include <iterator>
#include <set>

#include "editor_text_search_uve.h"

namespace UVE::Editor {
namespace {

[[nodiscard]] std::string LowerUVE(std::string_view text) {
    std::string lower(text);
    std::ranges::transform(lower, lower.begin(),
                           [](const unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return lower;
}

[[nodiscard]] bool IsDirectoryUVE(const Asset::ProjectFileEntryUVE& entry) noexcept {
    return entry.kind == Asset::ProjectFileEntryKindUVE::Directory;
}

/// Folders first, then by file name ignoring case, then by full path so the order is total.
void SortForDisplayUVE(std::vector<std::size_t>& indices, const std::span<const Asset::ProjectFileEntryUVE> entries) {
    std::ranges::stable_sort(indices, [&entries](const std::size_t a, const std::size_t b) {
        const Asset::ProjectFileEntryUVE& x = entries[a];
        const Asset::ProjectFileEntryUVE& y = entries[b];
        if (IsDirectoryUVE(x) != IsDirectoryUVE(y)) {
            return IsDirectoryUVE(x);
        }
        const std::string xName = LowerUVE(x.relativePath.filename().generic_string());
        const std::string yName = LowerUVE(y.relativePath.filename().generic_string());
        if (xName != yName) {
            return xName < yName;
        }
        return x.relativePath.generic_string() < y.relativePath.generic_string();
    });
}

} // namespace

void ContentNavigationHistoryUVE::GoUVE(const ContentLocationUVE& location) {
    if (location == m_current) {
        return;
    }
    m_back.push_back(std::move(m_current));
    if (m_back.size() > kMaxEntriesUVE) {
        m_back.erase(m_back.begin());
    }
    m_forward.clear();
    m_current = location;
}

bool ContentNavigationHistoryUVE::BackUVE() {
    if (m_back.empty()) {
        return false;
    }
    m_forward.push_back(std::move(m_current));
    m_current = std::move(m_back.back());
    m_back.pop_back();
    return true;
}

bool ContentNavigationHistoryUVE::ForwardUVE() {
    if (m_forward.empty()) {
        return false;
    }
    m_back.push_back(std::move(m_current));
    m_current = std::move(m_forward.back());
    m_forward.pop_back();
    return true;
}

const ContentShelfUVE* ContentShelvesUVE::FindUVE(const std::string_view name) const noexcept {
    const auto it = std::ranges::find(m_shelves, name, &ContentShelfUVE::name);
    return it == m_shelves.end() ? nullptr : &*it;
}

ContentShelfUVE* ContentShelvesUVE::FindMutableUVE(const std::string_view name) noexcept {
    const auto it = std::ranges::find(m_shelves, name, &ContentShelfUVE::name);
    return it == m_shelves.end() ? nullptr : &*it;
}

std::string ContentShelvesUVE::CreateUVE(const std::string_view baseName) {
    if (m_shelves.size() >= kMaxShelvesUVE) {
        return {};
    }
    const std::string base = baseName.empty() ? std::string{"Shelf"} : std::string{baseName};
    std::string name = base;
    for (int suffix = 2; FindUVE(name) != nullptr; ++suffix) {
        name = base + " " + std::to_string(suffix);
    }
    m_shelves.push_back(ContentShelfUVE{name, {}});
    return name;
}

bool ContentShelvesUVE::RenameUVE(const std::string_view from, const std::string_view to) {
    ContentShelfUVE* const shelf = FindMutableUVE(from);
    if (shelf == nullptr || to.empty()) {
        return false;
    }
    if (from == to) {
        return true;
    }
    if (FindUVE(to) != nullptr) {
        return false;
    }
    shelf->name = std::string{to};
    return true;
}

bool ContentShelvesUVE::RemoveUVE(const std::string_view name) {
    return std::erase_if(m_shelves, [name](const ContentShelfUVE& shelf) { return shelf.name == name; }) > 0U;
}

bool ContentShelvesUVE::AddItemUVE(const std::string_view shelfName, const std::filesystem::path& item) {
    ContentShelfUVE* const shelf = FindMutableUVE(shelfName);
    if (shelf == nullptr || item.empty() || shelf->items.size() >= kMaxItemsPerShelfUVE ||
        std::ranges::find(shelf->items, item) != shelf->items.end()) {
        return false;
    }
    shelf->items.push_back(item);
    return true;
}

bool ContentShelvesUVE::RemoveItemUVE(const std::string_view shelfName, const std::filesystem::path& item) {
    ContentShelfUVE* const shelf = FindMutableUVE(shelfName);
    return shelf != nullptr && std::erase(shelf->items, item) > 0U;
}

bool ContentShelvesUVE::ContainsUVE(const std::string_view shelfName, const std::filesystem::path& item) const {
    const ContentShelfUVE* const shelf = FindUVE(shelfName);
    return shelf != nullptr && std::ranges::find(shelf->items, item) != shelf->items.end();
}

void ContentShelvesUVE::MovePathUVE(const std::filesystem::path& from, const std::filesystem::path& to) {
    for (ContentShelfUVE& shelf : m_shelves) {
        for (std::filesystem::path& item : shelf.items) {
            if (item == from) {
                item = to;
            } else if (IsInsideContentDirectoryUVE(item, from)) {
                item = to / item.lexically_relative(from);
            }
        }
    }
}

bool IsInsideContentDirectoryUVE(const std::filesystem::path& path, const std::filesystem::path& directory) {
    if (path.empty()) {
        return false;
    }
    if (directory.empty()) {
        return true;
    }
    auto pathIt = path.begin();
    for (auto dirIt = directory.begin(); dirIt != directory.end(); ++dirIt, ++pathIt) {
        if (pathIt == path.end() || *pathIt != *dirIt) {
            return false;
        }
    }
    return pathIt != path.end();
}

bool DoesContentNameMatchUVE(const std::string_view name, const std::string_view query) noexcept {
    std::size_t position = 0U;
    while (position < query.size()) {
        const std::size_t start = query.find_first_not_of(' ', position);
        if (start == std::string_view::npos) {
            break;
        }
        const std::size_t end = std::min(query.find(' ', start), query.size());
        if (!ContainsCaseInsensitiveUVE(name, query.substr(start, end - start))) {
            return false;
        }
        position = end;
    }
    return true;
}

std::vector<std::size_t> ListContentFolderUVE(const std::span<const Asset::ProjectFileEntryUVE> entries,
                                              const std::filesystem::path& directory, const std::string_view query) {
    const bool searching = query.find_first_not_of(' ') != std::string_view::npos;
    std::vector<std::size_t> indices;
    for (std::size_t index = 0U; index < entries.size(); ++index) {
        const std::filesystem::path& path = entries[index].relativePath;
        const bool shown = searching ? IsInsideContentDirectoryUVE(path, directory) &&
                                           DoesContentNameMatchUVE(path.filename().generic_string(), query)
                                     : path.parent_path() == directory;
        if (shown) {
            indices.push_back(index);
        }
    }
    SortForDisplayUVE(indices, entries);
    return indices;
}

std::vector<std::size_t> ListContentShelfUVE(const std::span<const Asset::ProjectFileEntryUVE> entries,
                                             const ContentShelfUVE& shelf, const std::string_view query) {
    std::vector<std::size_t> indices;
    for (const std::filesystem::path& item : shelf.items) {
        const auto it = std::ranges::find(entries, item, &Asset::ProjectFileEntryUVE::relativePath);
        if (it != entries.end() && DoesContentNameMatchUVE(item.filename().generic_string(), query)) {
            indices.push_back(static_cast<std::size_t>(std::distance(entries.begin(), it)));
        }
    }
    return indices;
}

std::vector<std::string> CollectVisibleContentFoldersUVE(const std::span<const Asset::ProjectFileEntryUVE> entries,
                                                         const std::string_view query) {
    std::set<std::string> visible;
    for (const Asset::ProjectFileEntryUVE& entry : entries) {
        if (!IsDirectoryUVE(entry) || !DoesContentNameMatchUVE(entry.relativePath.filename().generic_string(), query)) {
            continue;
        }
        for (std::filesystem::path folder = entry.relativePath; !folder.empty(); folder = folder.parent_path()) {
            if (!visible.insert(folder.generic_string()).second) {
                break; // everything above was added with it
            }
        }
    }
    return {visible.begin(), visible.end()};
}

} // namespace UVE::Editor
