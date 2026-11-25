// The file picker: a floating window that picks one project file. UniVex's own answer to the
// engine file dialog - Content-rooted, with breadcrumb navigation, an extension filter, a file
// search, and a status line that narrates. Open mode picks an existing file; Save mode names a
// new one, collisions taking the house-style " 2" suffix like +Add creates. One pick at a time;
// the request's onPick fires once with the absolute choice, or never when cancelled.
#include "editor.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace UVE::Editor {
namespace {

[[nodiscard]] std::string Lower(std::string text) {
    std::ranges::transform(text, text.begin(), [](const unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return text;
}

/// The save target for the picker's name, or nullopt with the picker's status explaining why.
[[nodiscard]] std::optional<std::filesystem::path> SaveTarget(FilePickerState& picker,
                                                                 const std::filesystem::path& contentRoot) {
    if (picker.saveName.empty() || picker.saveName.find('/') != std::string::npos ||
        picker.saveName.find('\\') != std::string::npos) {
        picker.status = "Give the file a name (no slashes).";
        picker.statusIsError = true;
        return std::nullopt;
    }
    if (picker.request.extensions.empty()) {
        picker.status = "This picker names no file kind.";
        picker.statusIsError = true;
        return std::nullopt;
    }
    const std::filesystem::path directory = contentRoot / picker.directory;
    std::error_code error;
    std::filesystem::create_directories(directory, error);
    return Editor::MakeUniqueContentPath(directory, picker.saveName, picker.request.extensions.front());
}

} // namespace

FilePickerListing Editor::BuildFilePickerListing(const std::vector<Asset::ProjectFileEntry>& entries,
                                                           const std::filesystem::path& directory,
                                                           const std::vector<std::string>& extensions,
                                                           std::string_view search) {
    FilePickerListing listing;
    const std::string needle = Lower(std::string{search});
    for (const Asset::ProjectFileEntry& entry : entries) {
        if (entry.relativePath.parent_path() != directory) {
            continue;
        }
        if (entry.kind == Asset::ProjectFileEntryKind::Directory) {
            listing.folders.push_back(entry.relativePath);
            continue;
        }
        // Empty matches every file; otherwise one of the kinds must match.
        const std::filesystem::path fileExtension = entry.relativePath.extension();
        const bool kindMatches = extensions.empty() || std::ranges::any_of(extensions, [&](const std::string& kind) {
            return fileExtension == kind;
        });
        if (!kindMatches) {
            continue;
        }
        if (!needle.empty() && Lower(entry.relativePath.stem().string()).find(needle) == std::string::npos) {
            continue;
        }
        listing.files.push_back(entry.relativePath);
    }
    const auto byName = [](const std::filesystem::path& left, const std::filesystem::path& right) {
        return left.filename().generic_string() < right.filename().generic_string();
    };
    std::ranges::sort(listing.folders, byName);
    std::ranges::sort(listing.files, byName);
    return listing;
}

void Editor::OpenFilePicker(FilePickerRequest request) {
    if (m_filePickerHost) {
        // A pick made for the open entity lands on the entity, whatever is active when it fires.
        if (m_entityDocumentActive && request.onPick) {
            request.onPick = [this, pick = std::move(request.onPick)](const std::filesystem::path& chosen) {
                const EntityDocumentScope scope(*this);
                pick(chosen);
            };
        }
        if (m_entityDocumentActive && request.onPickMany) {
            request.onPickMany = [this, pickMany = std::move(request.onPickMany)](
                                     const std::vector<std::filesystem::path>& chosen) {
                const EntityDocumentScope scope(*this);
                pickMany(chosen);
            };
        }
        m_filePickerHost(std::move(request));
        return;
    }
    // One picker: opening another replaces the pending one, and the replaced onPick never fires.
    FilePickerState picker;
    picker.request = std::move(request);
    picker.directory = picker.request.startDirectory;
    picker.saveName = picker.request.saveName;
    m_filePicker = std::move(picker);
}

void Editor::CloseFilePicker() {
    m_filePicker.reset();
}

} // namespace UVE::Editor
