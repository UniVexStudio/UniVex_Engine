// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "folder_picker.h"

#include <algorithm>
#include <cctype>
#include <memory>
#include <optional>
#include <system_error>
#include <utility>

#include "application_runtime.h"
#include "launcher_widgets.h"
#include "prism/Containers.h"
#include "prism/Controls.h"
#include "prism/Layouts.h"
#include "prism/TextInput.h"
#include "prism/Theme.h"
#include "prism/Views.h"

namespace UVE::LauncherUI {
namespace {

inline constexpr float kPickerWidthUVE = 560.0F;
inline constexpr float kPickerListHeightUVE = 320.0F;
inline constexpr float kPickerSpacingUVE = 8.0F;
/// Leading character of a hidden folder on the platforms that hide by name.
inline constexpr char kHiddenPrefixUVE = '.';

/// The picker's state, shared by its widgets' callbacks for as long as the window is open.
struct FolderPickerStateUVE final {
    std::filesystem::path current;
    prism::Ref<prism::TextInput> path;
    prism::Ref<prism::Box> folders;
    prism::Ref<prism::Label> problem;
    prism::Ref<prism::TextInput> newFolder;
    /// File mode: the name endings that are listed, and the file selected so far. Empty in folder mode.
    std::vector<std::string> fileSuffixes;
    std::optional<std::filesystem::path> selectedFile;
    std::vector<prism::Ref<NavItemUVE>> fileRows;
};

[[nodiscard]] std::string LowercaseUVE(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(),
                   [](const unsigned char character) { return static_cast<char>(std::tolower(character)); });
    return text;
}

[[nodiscard]] bool IsHiddenNameUVE(const std::string& name) {
    return name.empty() || name.front() == kHiddenPrefixUVE;
}

void SortByNameUVE(std::vector<std::filesystem::path>& paths) {
    std::sort(paths.begin(), paths.end(), [](const std::filesystem::path& lhs, const std::filesystem::path& rhs) {
        return lhs.filename().string() < rhs.filename().string();
    });
}

/// Where a picker opens: the nearest existing folder of `start`, else the home folder.
[[nodiscard]] std::filesystem::path FirstFolderUVE(const std::filesystem::path& start) {
    std::error_code error;
    std::filesystem::path first = start;
    // Start at the nearest folder that exists, so a location not created yet still opens nearby.
    while (!first.empty() && !std::filesystem::is_directory(first, error) && first.has_parent_path() &&
           first.parent_path() != first) {
        first = first.parent_path();
    }
    if (first.empty() || !std::filesystem::is_directory(first, error)) {
        const Platform::ApplicationUserDataDirectoryResultUVE home = Platform::ResolveUserHomeDirectoryUVE();
        first = home.success ? home.directory : std::filesystem::current_path(error);
    }
    return first;
}

void ShowFolderUVE(const std::shared_ptr<FolderPickerStateUVE>& state, const std::filesystem::path& directory) {
    std::error_code error;
    if (!std::filesystem::is_directory(directory, error)) {
        state->problem->setText("That folder does not exist.");
        return;
    }
    state->current = directory.lexically_normal();
    state->path->setText(state->current.string());
    state->problem->setText("");
    state->folders->clearChildren();
    state->selectedFile.reset();
    state->fileRows.clear();
    const bool pickFiles = !state->fileSuffixes.empty();
    const std::vector<std::filesystem::path> children = ListPickableFoldersUVE(state->current);
    const std::vector<std::filesystem::path> files =
        pickFiles ? ListPickableFilesUVE(state->current, state->fileSuffixes) : std::vector<std::filesystem::path>{};
    if (children.empty() && files.empty()) {
        auto empty = prism::make<prism::Label>(pickFiles ? "Nothing to choose here. Open another folder."
                                                         : "No folders here. Choose this one, or make a new folder.");
        empty->setColor(&prism::Theme::textDim);
        state->folders->add(empty);
    }
    for (const std::filesystem::path& child : children) {
        auto row = prism::make<NavItemUVE>(child.filename().string(), prism::Icon::Folder);
        const std::weak_ptr<FolderPickerStateUVE> weak = state;
        row->onActivate = [weak, child] {
            if (const std::shared_ptr<FolderPickerStateUVE> owner = weak.lock()) {
                ShowFolderUVE(owner, child);
            }
        };
        state->folders->add(row, prism::Slot{0.0F, {}, prism::HAlign::Fill});
    }
    for (const std::filesystem::path& file : files) {
        auto row = prism::make<NavItemUVE>(file.filename().string(), prism::Icon::File);
        const std::weak_ptr<FolderPickerStateUVE> weak = state;
        NavItemUVE* const self = row.get();
        row->onActivate = [weak, file, self] {
            if (const std::shared_ptr<FolderPickerStateUVE> owner = weak.lock()) {
                owner->selectedFile = file;
                for (const prism::Ref<NavItemUVE>& other : owner->fileRows) {
                    other->SetActiveUVE(other.get() == self);
                }
            }
        };
        state->fileRows.push_back(row);
        state->folders->add(row, prism::Slot{0.0F, {}, prism::HAlign::Fill});
    }
    state->folders->relayout();
}

/// The path field, the list and the problem line shared by both pickers; returns the dialog body.
[[nodiscard]] prism::Ref<prism::Box> BuildPickerBodyUVE(const std::shared_ptr<FolderPickerStateUVE>& state) {
    auto body = prism::vbox(kPickerSpacingUVE);
    auto navigation = prism::hbox(kPickerSpacingUVE);
    auto up = prism::make<prism::Button>("", prism::Icon::ChevronUp);
    up->setTooltip("Up one folder");
    up->onClick = [weak = std::weak_ptr<FolderPickerStateUVE>(state)] {
        const std::shared_ptr<FolderPickerStateUVE> owner = weak.lock();
        if (owner && owner->current.has_parent_path() && owner->current.parent_path() != owner->current) {
            ShowFolderUVE(owner, owner->current.parent_path());
        }
    };
    navigation->add(up);
    state->path = prism::make<prism::TextInput>();
    // The field belongs to the state, so its callback holds the state weakly.
    state->path->onCommit = [weak = std::weak_ptr<FolderPickerStateUVE>(state)](const std::string& typed) {
        if (const std::shared_ptr<FolderPickerStateUVE> owner = weak.lock()) {
            ShowFolderUVE(owner, typed);
        }
    };
    navigation->add(state->path, prism::Slot{1.0F});
    body->add(navigation);

    state->folders = prism::vbox();
    auto list = prism::make<prism::ScrollView>();
    list->setContent(state->folders);
    list->setFixedHeight(kPickerListHeightUVE);
    auto frame = prism::make<prism::Border>();
    frame->setBackground(&prism::Theme::field).setBorder(&prism::Theme::borderSoft).setChild(list);
    body->add(frame);

    state->problem = prism::make<prism::Label>("", prism::FontRole::Small);
    state->problem->setColor(&prism::Theme::error);
    return body;
}

} // namespace

std::vector<std::filesystem::path> ListPickableFoldersUVE(const std::filesystem::path& directory) {
    std::vector<std::filesystem::path> folders;
    std::error_code error;
    std::filesystem::directory_iterator iterator(directory, std::filesystem::directory_options::skip_permission_denied,
                                                 error);
    if (error) {
        return folders;
    }
    for (const std::filesystem::directory_entry& entry : iterator) {
        if (IsHiddenNameUVE(entry.path().filename().string()) || !entry.is_directory(error)) {
            continue;
        }
        folders.push_back(entry.path());
    }
    SortByNameUVE(folders);
    return folders;
}

std::vector<std::filesystem::path> ListPickableFilesUVE(const std::filesystem::path& directory,
                                                        const std::vector<std::string>& suffixes) {
    std::vector<std::filesystem::path> files;
    std::error_code error;
    std::filesystem::directory_iterator iterator(directory, std::filesystem::directory_options::skip_permission_denied,
                                                 error);
    if (error) {
        return files;
    }
    for (const std::filesystem::directory_entry& entry : iterator) {
        const std::string name = LowercaseUVE(entry.path().filename().string());
        if (IsHiddenNameUVE(name) || !entry.is_regular_file(error)) {
            continue;
        }
        const bool matches = std::any_of(suffixes.begin(), suffixes.end(), [&name](const std::string& suffix) {
            const std::string wanted = LowercaseUVE(suffix);
            return name.size() > wanted.size() && name.compare(name.size() - wanted.size(), wanted.size(), wanted) == 0;
        });
        if (matches) {
            files.push_back(entry.path());
        }
    }
    SortByNameUVE(files);
    return files;
}

void ShowFolderPickerUVE(const std::string& title, const std::filesystem::path& start,
                         std::function<void(const std::filesystem::path&)> onPicked) {
    auto state = std::make_shared<FolderPickerStateUVE>();
    const prism::Ref<prism::Box> body = BuildPickerBodyUVE(state);

    auto creation = prism::hbox(kPickerSpacingUVE);
    state->newFolder = prism::make<prism::TextInput>("", "New folder name");
    creation->add(state->newFolder, prism::Slot{1.0F});
    auto make = prism::make<prism::Button>("Make folder", prism::Icon::Plus);
    make->onClick = [state] {
        const std::string name = state->newFolder->text();
        const std::filesystem::path created = state->current / name;
        std::error_code makeError;
        if (name.empty() || std::filesystem::path(name).has_parent_path() ||
            !std::filesystem::create_directory(created, makeError)) {
            state->problem->setText(makeError ? "Unable to make that folder: " + makeError.message()
                                              : "Enter a new folder name without slashes.");
            return;
        }
        state->newFolder->setText("");
        ShowFolderUVE(state, created);
    };
    creation->add(make);
    body->add(creation);
    body->add(state->problem);

    ShowFolderUVE(state, FirstFolderUVE(start));
    prism::showDialog(title, body,
                      {{"Cancel", false, [state] { state->folders->clearChildren(); }},
                       {"Choose this folder", true,
                        [state, onPicked = std::move(onPicked)] {
                            const std::filesystem::path chosen = state->current;
                            state->folders->clearChildren();
                            if (onPicked) {
                                onPicked(chosen);
                            }
                        }}},
                      kPickerWidthUVE);
}

namespace {
SystemFilePickerUVE g_systemFilePickerUVE;
} // namespace

void SetSystemFilePickerUVE(SystemFilePickerUVE picker) {
    g_systemFilePickerUVE = std::move(picker);
}

void ShowFilePickerUVE(const std::string& title, const std::filesystem::path& start, std::vector<std::string> suffixes,
                       const std::string& confirmLabel, std::function<void(const std::filesystem::path&)> onPicked) {
    if (g_systemFilePickerUVE && g_systemFilePickerUVE(suffixes, onPicked)) {
        return;
    }
    auto state = std::make_shared<FolderPickerStateUVE>();
    state->fileSuffixes = std::move(suffixes);
    const prism::Ref<prism::Box> body = BuildPickerBodyUVE(state);
    body->add(state->problem);

    ShowFolderUVE(state, FirstFolderUVE(start));
    // The rows hold the state weakly; clearing them on close releases everything.
    const auto release = [state] {
        state->fileRows.clear();
        state->folders->clearChildren();
    };
    prism::showDialog(title, body,
                      {{"Cancel", false, release},
                       {confirmLabel, true,
                        [state, release, onPicked = std::move(onPicked)] {
                            const std::optional<std::filesystem::path> chosen = state->selectedFile;
                            release();
                            if (chosen.has_value() && onPicked) {
                                onPicked(*chosen);
                            }
                        }}},
                      kPickerWidthUVE);
}

} // namespace UVE::LauncherUI
