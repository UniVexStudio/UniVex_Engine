// The file picker: a floating window that picks one project file. UniVex's own answer to the
// engine file dialog - Content-rooted, with breadcrumb navigation, an extension filter, a file
// search, and a status line that narrates. Open mode picks an existing file; Save mode names a
// new one, collisions taking the house-style " 2" suffix like +Add creates. One pick at a time;
// the request's onPick fires once with the absolute choice, or never when cancelled.
#include "uve/editor/editor_uve.h"

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

#include <imgui.h>

namespace UVE::Editor {
namespace {

[[nodiscard]] std::string LowerUVE(std::string text) {
    std::ranges::transform(text, text.begin(), [](const unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return text;
}

/// ".uvanimlib", or ".uvaudio, .wav" - the FILES header for the picker's kinds.
[[nodiscard]] std::string JoinExtensionsUVE(const std::vector<std::string>& extensions) {
    std::string joined;
    for (const std::string& kind : extensions) {
        if (!joined.empty()) {
            joined += ", ";
        }
        joined += kind;
    }
    return joined;
}

/// The save target for the picker's name, or nullopt with the picker's status explaining why.
[[nodiscard]] std::optional<std::filesystem::path> SaveTargetUVE(FilePickerStateUVE& picker,
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
    return EditorUVE::MakeUniqueContentPathUVE(directory, picker.saveName, picker.request.extensions.front());
}

} // namespace

FilePickerListingUVE EditorUVE::BuildFilePickerListingUVE(const std::vector<Asset::ProjectFileEntryUVE>& entries,
                                                           const std::filesystem::path& directory,
                                                           const std::vector<std::string>& extensions,
                                                           std::string_view search) {
    FilePickerListingUVE listing;
    const std::string needle = LowerUVE(std::string{search});
    for (const Asset::ProjectFileEntryUVE& entry : entries) {
        if (entry.relativePath.parent_path() != directory) {
            continue;
        }
        if (entry.kind == Asset::ProjectFileEntryKindUVE::Directory) {
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
        if (!needle.empty() && LowerUVE(entry.relativePath.stem().string()).find(needle) == std::string::npos) {
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

void EditorUVE::OpenFilePickerUVE(FilePickerRequestUVE request) {
    // One picker: opening another replaces the pending one, and the replaced onPick never fires.
    FilePickerStateUVE picker;
    picker.request = std::move(request);
    picker.directory = picker.request.startDirectory;
    picker.saveName = picker.request.saveName;
    m_filePicker = std::move(picker);
}

void EditorUVE::CloseFilePickerUVE() {
    m_filePicker.reset();
}

void EditorUVE::DrawFilePickerUVE() {
    FilePickerStateUVE& picker = *m_filePicker;
    const Asset::ProjectFileSnapshotUVE project = m_services->GetProjectFileIndexUVE().GetSnapshotUVE();
    const bool saveMode = picker.request.mode == FilePickerModeUVE::Save;

    // The folder may be gone (deleted while the picker stood open): climb to what exists.
    const auto folderKnown = [&](const std::filesystem::path& directory) {
        return directory.empty() || std::ranges::any_of(project.entries, [&](const Asset::ProjectFileEntryUVE& entry) {
                   return entry.kind == Asset::ProjectFileEntryKindUVE::Directory && entry.relativePath == directory;
               });
    };
    while (!folderKnown(picker.directory)) {
        picker.directory = picker.directory.parent_path();
        picker.selectedFile.reset();
        picker.status = "That folder is gone; back to where it was.";
        picker.statusIsError = false;
    }

    const ImGuiViewport* const mainViewport = ImGui::GetMainViewport();
    // Its own OS window, like the library window: never merged into the main one, never docked.
    ImGuiWindowClass windowClass;
    windowClass.ViewportFlagsOverrideSet = ImGuiViewportFlags_NoAutoMerge;
    ImGui::SetNextWindowClass(&windowClass);
    ImGui::SetNextWindowPos(ImVec2{mainViewport->GetCenter().x, mainViewport->GetCenter().y}, ImGuiCond_FirstUseEver,
                            ImVec2{0.5F, 0.5F});
    ImGui::SetNextWindowSize(ImVec2{520.0F, 460.0F}, ImGuiCond_FirstUseEver);
    bool open = true;
    const std::string title = picker.request.title + "###file-picker";
    ImGui::Begin(title.c_str(), &open, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoDocking);

    // Breadcrumb: Content / Animations / ... - every crumb jumps there.
    if (ImGui::SmallButton("Content")) {
        picker.directory.clear();
        picker.selectedFile.reset();
    }
    {
        std::filesystem::path crumb;
        for (const std::filesystem::path& part : picker.directory) {
            crumb /= part;
            ImGui::SameLine();
            ImGui::TextDisabled("/");
            ImGui::SameLine();
            const std::string label = part.generic_string() + "##crumb-" + crumb.generic_string();
            if (ImGui::SmallButton(label.c_str())) {
                picker.directory = crumb;
                picker.selectedFile.reset();
            }
        }
    }

    // Search: files only, so navigation never hides behind the filter.
    {
        char buffer[129];
        std::snprintf(buffer, sizeof(buffer), "%s", picker.search.c_str());
        if (ImGui::IsWindowAppearing() && !saveMode) {
            ImGui::SetKeyboardFocusHere();
        }
        ImGui::SetNextItemWidth(-1.0F);
        if (ImGui::InputTextWithHint("##file-picker-search", "Search files", buffer, sizeof(buffer))) {
            picker.search = buffer;
            picker.selectedFile.reset();
        }
    }

    const FilePickerListingUVE listing =
        BuildFilePickerListingUVE(project.entries, picker.directory, picker.request.extensions, picker.search);
    std::optional<std::filesystem::path> confirmed;

    ImGui::Spacing();
    ImGui::TextDisabled("FOLDERS");
    if (saveMode) {
        ImGui::SameLine();
        if (ImGui::SmallButton("+ Folder")) {
            const std::filesystem::path created =
                MakeUniqueContentPathUVE(project.contentRoot / picker.directory, "New Folder", "");
            std::error_code error;
            std::filesystem::create_directories(created, error);
            if (error) {
                picker.status = "Could not create a folder here.";
                picker.statusIsError = true;
            } else {
                picker.status = "Created " + created.filename().generic_string() + ".";
                picker.statusIsError = false;
            }
        }
    }
    if (listing.folders.empty()) {
        ImGui::TextDisabled("  No folders here.");
    }
    for (const std::filesystem::path& folder : listing.folders) {
        const std::string label = folder.filename().generic_string() + "/";
        if (ImGui::Selectable(label.c_str(), false, ImGuiSelectableFlags_AllowDoubleClick)) {
            if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                picker.directory = folder;
                picker.selectedFile.reset();
            }
        }
    }

    ImGui::Spacing();
    const std::string kinds = JoinExtensionsUVE(picker.request.extensions);
    ImGui::TextDisabled("FILES (%s)", picker.request.extensions.empty() ? "every file" : kinds.c_str());
    if (listing.files.empty()) {
        if (picker.request.extensions.empty()) {
            ImGui::TextDisabled("  No files here.");
        } else {
            ImGui::TextDisabled("  No %s here.", kinds.c_str());
        }
    }
    for (const std::filesystem::path& file : listing.files) {
        const std::string name = file.filename().generic_string();
        const bool isSelected = picker.selectedFile.has_value() && *picker.selectedFile == file;
        if (ImGui::Selectable(name.c_str(), isSelected, ImGuiSelectableFlags_AllowDoubleClick)) {
            picker.selectedFile = file;
            if (saveMode) {
                picker.saveName = file.stem().string();
            } else if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                confirmed = project.contentRoot / file;
            }
        }
    }

    if (saveMode) {
        ImGui::Spacing();
        ImGui::TextDisabled("FILE NAME");
        char buffer[129];
        std::snprintf(buffer, sizeof(buffer), "%s", picker.saveName.c_str());
        if (ImGui::IsWindowAppearing()) {
            ImGui::SetKeyboardFocusHere();
        }
        ImGui::SetNextItemWidth(-1.0F);
        const bool nameEntered = ImGui::InputText("##file-picker-name", buffer, sizeof(buffer),
                                                  ImGuiInputTextFlags_EnterReturnsTrue);
        picker.saveName = buffer;
        const std::string stem = picker.saveName.empty() ? std::string{"New File"} : picker.saveName;
        const std::string saveKind = picker.request.extensions.empty() ? std::string{} : picker.request.extensions.front();
        const std::filesystem::path preview =
            MakeUniqueContentPathUVE(project.contentRoot / picker.directory, stem, saveKind);
        ImGui::TextDisabled("Saves to %s", preview.filename().string().c_str());
        if (nameEntered) {
            confirmed = SaveTargetUVE(picker, project.contentRoot);
        }
    }

    if (!picker.status.empty()) {
        if (picker.statusIsError) {
            ImGui::TextColored(ImVec4{1.0F, 0.45F, 0.45F, 1.0F}, "%s", picker.status.c_str());
        } else {
            ImGui::TextDisabled("%s", picker.status.c_str());
        }
    }

    const char* const confirmLabel = saveMode ? "Save" : "Open";
    const bool canConfirm = saveMode ? !picker.saveName.empty() : picker.selectedFile.has_value();
    ImGui::BeginDisabled(!canConfirm);
    const bool confirmPressed = ImGui::Button(confirmLabel);
    ImGui::EndDisabled();
    ImGui::SameLine();
    const bool cancelPressed = ImGui::Button("Cancel");
    if (confirmPressed) {
        if (saveMode) {
            confirmed = SaveTargetUVE(picker, project.contentRoot);
        } else if (picker.selectedFile.has_value()) {
            confirmed = project.contentRoot / *picker.selectedFile;
        } else {
            picker.status = "Pick a file first.";
            picker.statusIsError = true;
        }
    }
    // Enter confirms too in Open mode (the name box handles its own Enter in Save mode).
    if (!saveMode && !confirmed.has_value() && picker.selectedFile.has_value() &&
        ImGui::IsKeyPressed(ImGuiKey_Enter)) {
        confirmed = project.contentRoot / *picker.selectedFile;
    }

    ImGui::End();
    // The callback runs after End with the state already cleared, so whatever it opens (or
    // re-opens) never meets a half-closed picker.
    if (confirmed.has_value()) {
        auto callback = std::move(picker.request.onPick);
        const std::filesystem::path picked = *confirmed;
        m_filePicker.reset();
        if (callback) {
            callback(picked);
        }
        return;
    }
    if (!open || cancelPressed) {
        CloseFilePickerUVE();
    }
}

} // namespace UVE::Editor
