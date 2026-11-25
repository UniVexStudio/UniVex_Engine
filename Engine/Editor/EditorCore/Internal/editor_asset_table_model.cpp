// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "editor_asset_table_model.h"

#include <algorithm>
#include <cctype>
#include <iterator>
#include <set>

#include <nlohmann/json.hpp>

#include "editor_text_search.h"

namespace UVE::Editor {
namespace {

[[nodiscard]] std::string Lower(std::string_view text) {
    std::string lower(text);
    std::ranges::transform(lower, lower.begin(),
                           [](const unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return lower;
}

[[nodiscard]] bool IsDirectory(const Asset::ProjectFileEntry& entry) noexcept {
    return entry.kind == Asset::ProjectFileEntryKind::Directory;
}

/// Folders first, then by file name ignoring case, then by full path so the order is total.
void SortForDisplay(std::vector<std::size_t>& indices, const std::span<const Asset::ProjectFileEntry> entries) {
    std::ranges::stable_sort(indices, [&entries](const std::size_t a, const std::size_t b) {
        const Asset::ProjectFileEntry& x = entries[a];
        const Asset::ProjectFileEntry& y = entries[b];
        if (IsDirectory(x) != IsDirectory(y)) {
            return IsDirectory(x);
        }
        const std::string xName = Lower(x.relativePath.filename().generic_string());
        const std::string yName = Lower(y.relativePath.filename().generic_string());
        if (xName != yName) {
            return xName < yName;
        }
        return x.relativePath.generic_string() < y.relativePath.generic_string();
    });
}

} // namespace

void ContentNavigationHistory::Go(const ContentLocation& location) {
    if (location == m_current) {
        return;
    }
    m_back.push_back(std::move(m_current));
    if (m_back.size() > kMaxEntries) {
        m_back.erase(m_back.begin());
    }
    m_forward.clear();
    m_current = location;
}

bool ContentNavigationHistory::Back() {
    if (m_back.empty()) {
        return false;
    }
    m_forward.push_back(std::move(m_current));
    m_current = std::move(m_back.back());
    m_back.pop_back();
    return true;
}

bool ContentNavigationHistory::Forward() {
    if (m_forward.empty()) {
        return false;
    }
    m_back.push_back(std::move(m_current));
    m_current = std::move(m_forward.back());
    m_forward.pop_back();
    return true;
}

const ContentShelf* ContentShelves::Find(const std::string_view name) const noexcept {
    const auto it = std::ranges::find(m_shelves, name, &ContentShelf::name);
    return it == m_shelves.end() ? nullptr : &*it;
}

ContentShelf* ContentShelves::FindMutable(const std::string_view name) noexcept {
    const auto it = std::ranges::find(m_shelves, name, &ContentShelf::name);
    return it == m_shelves.end() ? nullptr : &*it;
}

std::string ContentShelves::Create(const std::string_view baseName, const bool shared) {
    if (m_shelves.size() >= kMaxShelves) {
        return {};
    }
    const std::string base = baseName.empty() ? std::string{"Shelf"} : std::string{baseName};
    std::string name = base;
    for (int suffix = 2; Find(name) != nullptr; ++suffix) {
        name = base + " " + std::to_string(suffix);
    }
    m_shelves.push_back(ContentShelf{name, {}, shared});
    return name;
}

bool ContentShelves::SetShared(const std::string_view name, const bool shared) {
    ContentShelf* const shelf = FindMutable(name);
    if (shelf == nullptr) {
        return false;
    }
    shelf->shared = shared;
    return true;
}

void ContentShelves::RemoveAll(const bool shared) {
    std::erase_if(m_shelves, [shared](const ContentShelf& shelf) { return shelf.shared == shared; });
}

bool ContentShelves::Rename(const std::string_view from, const std::string_view to) {
    ContentShelf* const shelf = FindMutable(from);
    if (shelf == nullptr || to.empty()) {
        return false;
    }
    if (from == to) {
        return true;
    }
    if (Find(to) != nullptr) {
        return false;
    }
    shelf->name = std::string{to};
    return true;
}

bool ContentShelves::Remove(const std::string_view name) {
    return std::erase_if(m_shelves, [name](const ContentShelf& shelf) { return shelf.name == name; }) > 0U;
}

bool ContentShelves::AddItem(const std::string_view shelfName, const std::filesystem::path& item) {
    ContentShelf* const shelf = FindMutable(shelfName);
    if (shelf == nullptr || item.empty() || shelf->items.size() >= kMaxItemsPerShelf ||
        std::ranges::find(shelf->items, item) != shelf->items.end()) {
        return false;
    }
    shelf->items.push_back(item);
    return true;
}

bool ContentShelves::RemoveItem(const std::string_view shelfName, const std::filesystem::path& item) {
    ContentShelf* const shelf = FindMutable(shelfName);
    return shelf != nullptr && std::erase(shelf->items, item) > 0U;
}

bool ContentShelves::Contains(const std::string_view shelfName, const std::filesystem::path& item) const {
    const ContentShelf* const shelf = Find(shelfName);
    return shelf != nullptr && std::ranges::find(shelf->items, item) != shelf->items.end();
}

void ContentShelves::MovePath(const std::filesystem::path& from, const std::filesystem::path& to) {
    for (ContentShelf& shelf : m_shelves) {
        for (std::filesystem::path& item : shelf.items) {
            if (item == from) {
                item = to;
            } else if (IsInsideContentDirectory(item, from)) {
                item = to / item.lexically_relative(from);
            }
        }
    }
}

std::string WriteSharedShelvesText(const ContentShelves& shelves) {
    nlohmann::json list = nlohmann::json::array();
    for (const ContentShelf& shelf : shelves.GetAll()) {
        if (!shelf.shared) {
            continue;
        }
        nlohmann::json items = nlohmann::json::array();
        for (const std::filesystem::path& item : shelf.items) {
            items.push_back(item.generic_string());
        }
        list.push_back({{"name", shelf.name}, {"items", std::move(items)}});
    }
    return nlohmann::json{{"version", 1}, {"shelves", std::move(list)}}.dump(2) + "\n";
}

bool ReadSharedShelvesText(const std::string_view text, ContentShelves& shelves, std::string& error) {
    const nlohmann::json document = nlohmann::json::parse(text, nullptr, false);
    if (document.is_discarded() || !document.is_object() || !document.contains("shelves") ||
        !document["shelves"].is_array()) {
        error = "not a shelves file";
        return false;
    }
    if (document.value("version", 0) != 1) {
        error = "written by a newer editor";
        return false;
    }
    // Personal shelves are set aside and put back after, so a team shelf keeps its name.
    std::vector<ContentShelf> personal;
    for (const ContentShelf& shelf : shelves.GetAll()) {
        if (!shelf.shared) {
            personal.push_back(shelf);
        }
    }
    shelves.Clear();
    for (const nlohmann::json& entry : document["shelves"]) {
        if (!entry.is_object() || !entry.contains("name") || !entry["name"].is_string()) {
            continue;
        }
        const std::string wanted = entry["name"].get<std::string>();
        if (wanted.empty() || shelves.Find(wanted) != nullptr) {
            continue; // a repeated team shelf is one too many, not a rename
        }
        const std::string name = shelves.Create(wanted, true);
        if (name.empty() || !entry.contains("items") || !entry["items"].is_array()) {
            continue;
        }
        for (const nlohmann::json& item : entry["items"]) {
            if (!item.is_string()) {
                continue;
            }
            const std::filesystem::path path = std::filesystem::path{item.get<std::string>()}.lexically_normal();
            const bool escapes = path.empty() || path.is_absolute() || path.has_root_name() ||
                                 path.has_root_directory() ||
                                 (path.begin() != path.end() && *path.begin() == "..");
            if (!escapes) {
                static_cast<void>(shelves.AddItem(name, path));
            }
        }
    }
    for (ContentShelf& shelf : personal) {
        const std::string name = shelves.Create(shelf.name, false);
        for (const std::filesystem::path& item : shelf.items) {
            static_cast<void>(shelves.AddItem(name, item));
        }
    }
    error.clear();
    return true;
}

bool IsInsideContentDirectory(const std::filesystem::path& path, const std::filesystem::path& directory) {
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

bool DoesContentNameMatch(const std::string_view name, const std::string_view query) noexcept {
    std::size_t position = 0U;
    while (position < query.size()) {
        const std::size_t start = query.find_first_not_of(' ', position);
        if (start == std::string_view::npos) {
            break;
        }
        const std::size_t end = std::min(query.find(' ', start), query.size());
        if (!ContainsCaseInsensitive(name, query.substr(start, end - start))) {
            return false;
        }
        position = end;
    }
    return true;
}

std::vector<std::size_t> ListContentFolder(const std::span<const Asset::ProjectFileEntry> entries,
                                              const std::filesystem::path& directory, const std::string_view query) {
    const bool searching = query.find_first_not_of(' ') != std::string_view::npos;
    std::vector<std::size_t> indices;
    for (std::size_t index = 0U; index < entries.size(); ++index) {
        const std::filesystem::path& path = entries[index].relativePath;
        const bool shown = searching ? IsInsideContentDirectory(path, directory) &&
                                           DoesContentNameMatch(path.filename().generic_string(), query)
                                     : path.parent_path() == directory;
        if (shown) {
            indices.push_back(index);
        }
    }
    SortForDisplay(indices, entries);
    return indices;
}

std::vector<std::size_t> ListContentTree(const std::span<const Asset::ProjectFileEntry> entries,
                                            const std::filesystem::path& directory, const std::string_view query) {
    std::vector<std::size_t> indices;
    for (std::size_t index = 0U; index < entries.size(); ++index) {
        const std::filesystem::path& path = entries[index].relativePath;
        if (IsInsideContentDirectory(path, directory) &&
            DoesContentNameMatch(path.filename().generic_string(), query)) {
            indices.push_back(index);
        }
    }
    SortForDisplay(indices, entries);
    return indices;
}

std::vector<std::size_t> ListContentShelf(const std::span<const Asset::ProjectFileEntry> entries,
                                             const ContentShelf& shelf, const std::string_view query) {
    std::vector<std::size_t> indices;
    for (const std::filesystem::path& item : shelf.items) {
        const auto it = std::ranges::find(entries, item, &Asset::ProjectFileEntry::relativePath);
        if (it != entries.end() && DoesContentNameMatch(item.filename().generic_string(), query)) {
            indices.push_back(static_cast<std::size_t>(std::distance(entries.begin(), it)));
        }
    }
    return indices;
}

std::vector<std::string> CollectVisibleContentFolders(const std::span<const Asset::ProjectFileEntry> entries,
                                                         const std::string_view query) {
    std::set<std::string> visible;
    for (const Asset::ProjectFileEntry& entry : entries) {
        if (!IsDirectory(entry) || !DoesContentNameMatch(entry.relativePath.filename().generic_string(), query)) {
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

void SortContentFacts(std::vector<std::size_t>& order, const std::span<const ContentItemFacts> facts,
                         const ContentSortKey key, const bool ascending) {
    std::ranges::stable_sort(order, [&](const std::size_t a, const std::size_t b) {
        const ContentItemFacts& x = facts[a];
        const ContentItemFacts& y = facts[b];
        if (x.isFolder != y.isFolder) {
            return x.isFolder;
        }
        const std::string xName = Lower(x.name);
        const std::string yName = Lower(y.name);
        int compare = 0;
        switch (key) {
            case ContentSortKey::Name: break;
            case ContentSortKey::Type: compare = Lower(x.type).compare(Lower(y.type)); break;
            case ContentSortKey::Size: compare = x.size < y.size ? -1 : (x.size > y.size ? 1 : 0); break;
            case ContentSortKey::Modified:
                compare = x.modified < y.modified ? -1 : (x.modified > y.modified ? 1 : 0);
                break;
            case ContentSortKey::Folder: compare = Lower(x.folder).compare(Lower(y.folder)); break;
        }
        if (compare == 0) {
            compare = xName.compare(yName);
        }
        return ascending ? compare < 0 : compare > 0;
    });
}

const char* GetContentAgeLabel(const ContentAge age) noexcept {
    switch (age) {
        case ContentAge::Today: return "Today";
        case ContentAge::Yesterday: return "Yesterday";
        case ContentAge::ThisWeek: return "Earlier this week";
        case ContentAge::Earlier: return "Before that";
    }
    return "Before that";
}

ContentAge ClassifyContentAge(const std::int64_t modified, const std::int64_t startOfToday) noexcept {
    constexpr std::int64_t kDay = 24 * 60 * 60;
    if (modified >= startOfToday) {
        return ContentAge::Today;
    }
    if (modified >= startOfToday - kDay) {
        return ContentAge::Yesterday;
    }
    if (modified >= startOfToday - 6 * kDay) {
        return ContentAge::ThisWeek;
    }
    return ContentAge::Earlier;
}

std::vector<std::pair<std::string, std::vector<std::size_t>>> GroupContentByType(
    const std::vector<std::size_t>& order, const std::span<const ContentItemFacts> facts) {
    std::vector<std::pair<std::string, std::vector<std::size_t>>> lanes;
    for (const std::size_t index : order) {
        if (facts[index].isFolder) {
            continue;
        }
        auto lane = std::ranges::find(lanes, facts[index].type, &std::pair<std::string, std::vector<std::size_t>>::first);
        if (lane == lanes.end()) {
            lanes.emplace_back(facts[index].type, std::vector<std::size_t>{});
            lane = std::prev(lanes.end());
        }
        lane->second.push_back(index);
    }
    std::ranges::sort(lanes, [](const auto& a, const auto& b) { return Lower(a.first) < Lower(b.first); });
    return lanes;
}

void ContentSelection::Click(const std::span<const std::string> visibleOrder, const std::string& item,
                                   const bool control, const bool shift) {
    const auto position = [&visibleOrder](const std::string& name) {
        return std::ranges::find(visibleOrder, name);
    };
    const auto clicked = position(item);
    const auto anchored = m_anchor.empty() ? visibleOrder.end() : position(m_anchor);
    if (shift && clicked != visibleOrder.end() && anchored != visibleOrder.end()) {
        if (!control) {
            m_items.clear();
        }
        const auto from = std::min(clicked, anchored);
        const auto to = std::max(clicked, anchored);
        for (auto at = from; at <= to; ++at) {
            if (!Contains(*at)) {
                m_items.push_back(*at);
            }
        }
        return; // the anchor stays, so the run can be stretched or shrunk from the same end
    }
    if (control) {
        if (const auto found = std::ranges::find(m_items, item); found != m_items.end()) {
            m_items.erase(found);
        } else {
            m_items.push_back(item);
        }
    } else {
        m_items.assign(1U, item);
    }
    m_anchor = item;
}

bool ContentSelection::Contains(const std::string& item) const noexcept {
    return std::ranges::find(m_items, item) != m_items.end();
}

void ContentSelection::Clear() noexcept {
    m_items.clear();
    m_anchor.clear();
}

void ContentSelection::KeepOnly(const std::span<const std::string> existing) {
    std::erase_if(m_items, [&existing](const std::string& item) { return std::ranges::find(existing, item) == existing.end(); });
    if (!m_anchor.empty() && std::ranges::find(existing, m_anchor) == existing.end()) {
        m_anchor.clear();
    }
}

} // namespace UVE::Editor
