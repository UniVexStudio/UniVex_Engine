// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
#include "editor_file_dialog.h"

#include "editor_chrome.h"
#include "editor_host.h"

#include "prism/Application.h"
#include "prism/Containers.h"
#include "prism/Controls.h"
#include "prism/Layouts.h"
#include "prism/Menu.h"
#include "prism/TableView.h"
#include "prism/TextInput.h"
#include "prism/Theme.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <string>
#include <system_error>
#include <utility>
#include <vector>
#include <cstddef>
#include <filesystem>

namespace UVE::Editor {
namespace {

constexpr int kWidthUVE = 940;
constexpr int kHeightUVE = 600;

[[nodiscard]] std::string LowerUVE(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(),
                   [](const unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return text;
}

[[nodiscard]] std::string SizeTextUVE(const std::uintmax_t bytes) {
    char buffer[32];
    if (bytes < 1024U) {
        std::snprintf(buffer, sizeof buffer, "%ju B", bytes);
    } else if (bytes < 1024U * 1024U) {
        std::snprintf(buffer, sizeof buffer, "%.1f KB", static_cast<double>(bytes) / 1024.0);
    } else {
        std::snprintf(buffer, sizeof buffer, "%.1f MB", static_cast<double>(bytes) / (1024.0 * 1024.0));
    }
    return buffer;
}

[[nodiscard]] std::string TimeTextUVE(const std::filesystem::file_time_type time) {
    if (time == std::filesystem::file_time_type{}) {
        return {};
    }
    const auto system = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
        time - std::filesystem::file_time_type::clock::now() + std::chrono::system_clock::now());
    const std::time_t t = std::chrono::system_clock::to_time_t(system);
    std::tm local{};
#if defined(_WIN32)
    localtime_s(&local, &t);
#else
    localtime_r(&t, &local);
#endif
    char buffer[32];
    std::strftime(buffer, sizeof buffer, "%Y-%m-%d %H:%M", &local);
    return buffer;
}

/// The folders a person usually picks from: the project's, then their own.
[[nodiscard]] std::vector<std::pair<std::string, std::filesystem::path>> PlacesUVE(const std::filesystem::path& contentRoot) {
    std::vector<std::pair<std::string, std::filesystem::path>> places;
    std::error_code error;
    std::filesystem::path content = contentRoot.lexically_normal();
    if (content.filename().empty()) {
        content = content.parent_path();   // "/project/Content/" names the folder before its slash
    }
    places.emplace_back("Content", content);
    if (!content.empty() && content.has_parent_path() && content.parent_path() != content) {
        places.emplace_back("Project", content.parent_path());
    }
#if defined(_WIN32)
    const char* home = std::getenv("USERPROFILE");
#else
    const char* home = std::getenv("HOME");
#endif
    if (home != nullptr && *home != '\0') {
        const std::filesystem::path h{home};
        places.emplace_back("Home", h);
        for (const char* name : {"Desktop", "Downloads", "Documents", "Pictures"}) {
            if (std::filesystem::is_directory(h / name, error)) {
                places.emplace_back(name, h / name);
            }
        }
    }
#if defined(_WIN32)
    for (char drive = 'C'; drive <= 'Z'; ++drive) {
        const std::filesystem::path root = std::string(1, drive) + ":\\";
        if (std::filesystem::is_directory(root, error)) {
            places.emplace_back(std::string(1, drive) + ":", root);
        }
    }
#else
    places.emplace_back("Computer", std::filesystem::path{"/"});
#endif
    return places;
}

} // namespace

EditorFileDialogUVE::EditorFileDialogUVE(EditorUVE& editor, EditorHostUVE& host)
    : m_editor(editor), m_host(host) {}

EditorFileDialogUVE::~EditorFileDialogUVE() {
    if (m_window != 0 && m_host.IsReadyUVE()) {
        m_host.CloseWindowUVE(m_window);
    }
}

std::filesystem::path EditorFileDialogUVE::ContentRootUVE() const {
    return m_editor.m_services->GetProjectFileIndexUVE().GetSnapshotUVE().contentRoot;
}

void EditorFileDialogUVE::OpenUVE(FilePickerRequestUVE request) {
    if (!m_host.IsReadyUVE()) {
        return;
    }
    if (m_window != 0) {
        m_host.CloseWindowUVE(m_window);
        m_window = 0;
    }
    m_request = std::move(request);
    m_closePending = false;
    m_allKinds = m_request.extensions.empty();
    const std::filesystem::path contentRoot = ContentRootUVE();
    m_importInto = m_editor.m_contentBrowserDirectory;
    std::error_code error;
    std::filesystem::path start = m_request.startDirectory;
    if (!start.empty() && start.is_relative()) {
        start = contentRoot / start;
    }
    if (start.empty() || !std::filesystem::is_directory(start, error)) {
        start = contentRoot;
    }
    m_window = m_host.OpenWindowUVE(BuildUVE(), m_request.title.empty() ? std::string("Open File") : m_request.title,
                                    kWidthUVE, kHeightUVE);
    if (m_window == 0) {
        return;
    }
    m_host.AppUVE().runInWindow(m_window, [this, start] { GoToUVE(start); });
    m_host.FocusWindowUVE(m_window);
}

void EditorFileDialogUVE::FrameUVE() {
    if (m_window == 0) {
        return;
    }
    if (m_host.WindowCloseRequestedUVE(m_window)) {
        m_host.ClearCloseRequestUVE(m_window);
        m_closePending = true;
    }
    if (m_closePending) {
        // Closed here, between frames: never from inside the window's own input.
        m_host.CloseWindowUVE(m_window);
        m_window = 0;
        m_closePending = false;
        m_request = FilePickerRequestUVE{};
        // The window's widgets go with it.
        m_pathInput.reset();
        m_search.reset();
        m_table.reset();
        m_fileName.reset();
        m_kind.reset();
        m_importTarget.reset();
        m_status.reset();
        m_entries.clear();
        return;
    }
    m_host.RenderWindowUVE(m_window, {});
}

// --------------------------------------------------------------------------------------- layout

prism::Ref<prism::Widget> EditorFileDialogUVE::BuildUVE() {
    const bool import = m_request.quickImport;
    const bool save = m_request.mode == FilePickerModeUVE::Save;
    auto window = prism::vbox();

    // Title bar: what is being picked, the drag strip, the window buttons.
    auto title = prism::hbox(6.0F);
    title->setPadding(prism::Margin(10.0F, 0.0F, 0.0F, 0.0F));
    auto titleText = prism::make<prism::Label>(m_request.title.empty() ? std::string("Open File") : m_request.title);
    titleText->setColor(prism::ThemeColor(&prism::Theme::textBright));
    title->add(titleText, prism::Slot{0.0F, {}, prism::HAlign::Left, prism::VAlign::Center});
    title->add(prism::make<DragStripUVE>(), prism::Slot{1.0F});
    AddWindowButtonsUVE(*title, [this] { CancelUVE(); });
    auto titleBox = prism::make<prism::SizeBox>();
    titleBox->setHeightOverride(kTitleRowUVE + 4.0F);
    titleBox->setContent(title);
    auto titleBar = prism::make<prism::Border>();
    titleBar->setBackground(prism::ThemeColor(&prism::Theme::header));
    titleBar->setBorder(prism::ThemeColor(&prism::Theme::border), prism::Margin(0.0F, 0.0F, 0.0F, 1.0F));
    titleBar->setChild(titleBox);
    window->add(titleBar);

    // Navigation: up, the path (type one and press Enter), search.
    auto nav = prism::hbox(4.0F);
    nav->setPadding(prism::Margin(6.0F, 4.0F, 6.0F, 4.0F));
    auto up = ToolButtonUVE(prism::Icon::ChevronUp, "Up one folder");
    up->onClick = [this] {
        if (m_directory.has_parent_path() && m_directory.parent_path() != m_directory) {
            GoToUVE(m_directory.parent_path());
        }
    };
    nav->add(up, prism::Slot{0.0F, {}, prism::HAlign::Left, prism::VAlign::Center});
    m_pathInput = prism::make<prism::TextInput>("", "Folder");
    m_pathInput->onCommit = [this](const std::string& text) {
        std::error_code error;
        const std::filesystem::path typed{text};
        if (std::filesystem::is_directory(typed, error)) {
            GoToUVE(typed);
        } else if (std::filesystem::is_regular_file(typed, error)) {
            GoToUVE(typed.parent_path());
            if (m_fileName) {
                m_fileName->setText(typed.filename().string());
            }
        } else if (m_status) {
            m_status->setText("There is no folder at " + text + ".");
        }
    };
    nav->add(m_pathInput, prism::Slot{1.0F, {}, prism::HAlign::Fill, prism::VAlign::Center});
    m_search = prism::make<prism::TextInput>("", "Search");
    m_search->onChanged = [this](const std::string&) { RebuildListUVE(); };
    auto searchBox = prism::make<prism::SizeBox>();
    searchBox->setWidthOverride(200.0F);
    searchBox->setContent(m_search);
    nav->add(searchBox, prism::Slot{0.0F, {}, prism::HAlign::Left, prism::VAlign::Center});
    window->add(nav);

    // Places | files.
    auto places = prism::vbox(1.0F);
    places->setPadding(prism::Margin(4.0F));
    for (const auto& [name, path] : PlacesUVE(ContentRootUVE())) {
        auto place = prism::make<prism::Button>(name, name == "Content" || name == "Project" ? prism::Icon::Folder
                                                                                            : prism::Icon::FolderOpen,
                                                prism::ButtonStyle::Flat);
        place->setTooltip(path.string());
        place->onClick = [this, p = path] { GoToUVE(p); };
        places->add(place, prism::Slot{0.0F, {}, prism::HAlign::Fill, prism::VAlign::Center});
    }
    places->add(prism::make<prism::Spacer>(), prism::Slot{1.0F});

    m_table = prism::make<prism::TableView>();
    m_table->addColumn(prism::TableColumn{"name", "Name", 360.0F});
    m_table->addColumn(prism::TableColumn{"type", "Type", 110.0F});
    m_table->addColumn(prism::TableColumn{"size", "Size", 90.0F, 40.0F, true, true, prism::HAlign::Right});
    m_table->addColumn(prism::TableColumn{"modified", "Modified", 140.0F});
    m_table->cellText = [this](const int row, const int col) -> std::string {
        if (row < 0 || static_cast<std::size_t>(row) >= m_entries.size()) {
            return {};
        }
        const EntryUVE& entry = m_entries[static_cast<std::size_t>(row)];
        switch (col) {
            case 0: return entry.path.filename().string();
            case 1: return entry.directory ? std::string("Folder") : LowerUVE(entry.path.extension().string());
            case 2: return entry.directory ? std::string() : SizeTextUVE(entry.size);
            default: return TimeTextUVE(entry.modified);
        }
    };
    m_table->lessThan = [this](const int a, const int b, const int col) {
        const EntryUVE& x = m_entries[static_cast<std::size_t>(a)];
        const EntryUVE& y = m_entries[static_cast<std::size_t>(b)];
        if (x.directory != y.directory) {
            return x.directory;   // folders stay first whichever way it is sorted
        }
        switch (col) {
            case 2: return x.size < y.size;
            case 3: return x.modified < y.modified;
            case 1: return LowerUVE(x.path.extension().string()) < LowerUVE(y.path.extension().string());
            default: return LowerUVE(x.path.filename().string()) < LowerUVE(y.path.filename().string());
        }
    };
    m_table->paintCell = [this](prism::Canvas& c, const int row, const int col, const prism::Rect r, const bool selected) {
        if (row < 0 || static_cast<std::size_t>(row) >= m_entries.size()) {
            return;
        }
        const prism::Theme& t = prism::theme();
        const EntryUVE& entry = m_entries[static_cast<std::size_t>(row)];
        std::string text = m_table->cellText(row, col);
        float x = r.x + 6.0F;
        if (col == 0) {
            const prism::Rect icon{x, std::floor(r.y + (r.h - 16.0F) * 0.5F), 16.0F, 16.0F};
            prism::drawIcon(c, entry.directory ? prism::Icon::Folder : prism::Icon::File, icon,
                            entry.directory ? prism::Color(214, 160, 72) : t.textDim);
            x += 22.0F;
        }
        c.textIn(*t.ui, prism::Rect{x, r.y, r.right() - x - 6.0F, r.h}, text, selected ? t.textBright : t.text,
                 col == 2 ? prism::HAlign::Right : prism::HAlign::Left, true);
    };
    m_table->onSelect = [this](const std::vector<int>& rows) {
        if (rows.empty() || rows.front() < 0 || static_cast<std::size_t>(rows.front()) >= m_entries.size()) {
            return;
        }
        const EntryUVE& entry = m_entries[static_cast<std::size_t>(rows.front())];
        if (!entry.directory && m_fileName) {
            m_fileName->setText(entry.path.filename().string());
        }
    };
    m_table->onActivate = [this](const int row) {
        if (row < 0 || static_cast<std::size_t>(row) >= m_entries.size()) {
            return;
        }
        const EntryUVE entry = m_entries[static_cast<std::size_t>(row)];
        if (entry.directory) {
            GoToUVE(entry.path);
        } else {
            if (m_fileName) {
                m_fileName->setText(entry.path.filename().string());
            }
            ConfirmUVE();
        }
    };

    auto body = prism::make<prism::Splitter>(prism::Orientation::Horizontal);
    body->add(PanelUVE(places), prism::Splitter::Pane{170.0F, 120.0F, 400.0F, false});
    body->add(PanelUVE(m_table), prism::Splitter::Pane{740.0F, 300.0F, 1e9F, true});
    window->add(body, prism::Slot{1.0F});

    // File name and kind.
    auto fileRow = prism::hbox(6.0F);
    fileRow->setPadding(prism::Margin(8.0F, 6.0F, 8.0F, 2.0F));
    auto fileLabel = prism::make<prism::Label>(save ? "Save as" : "File");
    fileLabel->setColor(prism::ThemeColor(&prism::Theme::textDim));
    auto fileLabelBox = prism::make<prism::SizeBox>();
    fileLabelBox->setWidthOverride(80.0F);
    fileLabelBox->setContent(fileLabel);
    fileRow->add(fileLabelBox, prism::Slot{0.0F, {}, prism::HAlign::Left, prism::VAlign::Center});
    m_fileName = prism::make<prism::TextInput>(m_request.saveName, save ? "Name" : "Pick a file, or type its name");
    m_fileName->onCommit = [this](const std::string&) { ConfirmUVE(); };
    fileRow->add(m_fileName, prism::Slot{1.0F, {}, prism::HAlign::Fill, prism::VAlign::Center});
    std::string kinds;
    for (const std::string& extension : m_request.extensions) {
        kinds += (kinds.empty() ? "*" : ", *") + extension;
    }
    // A slot's pick shows only what the slot takes; an open-ended pick can show everything.
    std::vector<std::string> filters;
    if (!kinds.empty()) {
        filters.push_back(kinds);
    } else {
        filters.emplace_back("All files (*.*)");
    }
    m_kind = prism::make<prism::ComboBox>(filters, 0);
    m_kind->onChange = [this](const int) { RebuildListUVE(); };
    auto kindBox = prism::make<prism::SizeBox>();
    kindBox->setWidthOverride(260.0F);
    kindBox->setContent(m_kind);
    fileRow->add(kindBox, prism::Slot{0.0F, {}, prism::HAlign::Left, prism::VAlign::Center});
    window->add(fileRow);

    // Import settings: where in Content the file goes.
    if (import) {
        auto importRow = prism::hbox(6.0F);
        importRow->setPadding(prism::Margin(8.0F, 2.0F, 8.0F, 2.0F));
        auto importLabel = prism::make<prism::Label>("Import into");
        importLabel->setColor(prism::ThemeColor(&prism::Theme::textDim));
        auto importLabelBox = prism::make<prism::SizeBox>();
        importLabelBox->setWidthOverride(80.0F);
        importLabelBox->setContent(importLabel);
        importRow->add(importLabelBox, prism::Slot{0.0F, {}, prism::HAlign::Left, prism::VAlign::Center});
        m_importTarget = prism::make<prism::Button>(
            m_importInto.empty() ? std::string("Content") : "Content/" + m_importInto.generic_string(), prism::Icon::Folder,
            prism::ButtonStyle::Default);
        m_importTarget->setTooltip("The Content folder the file is copied into");
        m_importTarget->onClick = [this] { ShowImportTargetMenuUVE(); };
        importRow->add(m_importTarget, prism::Slot{0.0F, {}, prism::HAlign::Left, prism::VAlign::Center});
        auto note = prism::make<prism::Label>("A copy is made there and used; the file you pick is left as it is.",
                                              prism::FontRole::Small);
        note->setColor(prism::ThemeColor(&prism::Theme::textDim));
        importRow->add(note, prism::Slot{1.0F, prism::Margin(8.0F, 0.0F, 0.0F, 0.0F), prism::HAlign::Left, prism::VAlign::Center});
        window->add(importRow);
    }

    // Status and the two buttons.
    auto buttons = prism::hbox(6.0F);
    buttons->setPadding(prism::Margin(8.0F, 6.0F, 8.0F, 8.0F));
    m_status = prism::make<prism::Label>("", prism::FontRole::Small);
    m_status->setColor(prism::ThemeColor(&prism::Theme::textDim));
    buttons->add(m_status, prism::Slot{1.0F, {}, prism::HAlign::Left, prism::VAlign::Center});
    auto confirm = prism::make<prism::Button>(import ? "Import" : save ? "Save" : "Load",
                                              import ? prism::Icon::Open : prism::Icon::Check, prism::ButtonStyle::Primary);
    confirm->onClick = [this] { ConfirmUVE(); };
    auto cancel = prism::make<prism::Button>("Cancel", prism::Icon::None, prism::ButtonStyle::Default);
    cancel->onClick = [this] { CancelUVE(); };
    buttons->add(confirm, prism::Slot{0.0F, {}, prism::HAlign::Left, prism::VAlign::Center});
    buttons->add(cancel, prism::Slot{0.0F, {}, prism::HAlign::Left, prism::VAlign::Center});
    auto footer = prism::make<prism::Border>();
    footer->setBackground(prism::ThemeColor(&prism::Theme::header));
    footer->setBorder(prism::ThemeColor(&prism::Theme::border), prism::Margin(0.0F, 1.0F, 0.0F, 0.0F));
    auto footerBox = prism::vbox();
    footerBox->add(buttons);
    footer->setChild(footerBox);
    window->add(footer);

    auto frame = prism::make<prism::Border>();
    frame->setBackground(prism::ThemeColor(&prism::Theme::window));
    frame->setBorder(prism::ThemeColor(&prism::Theme::border), prism::Margin(1.0F));
    frame->setChild(window);
    return frame;
}

// ------------------------------------------------------------------------------------ navigation

bool EditorFileDialogUVE::FolderHasKindUVE(const std::filesystem::path& folder) const {
    if (m_allKinds || m_request.extensions.empty()) {
        return true;
    }
    // Bounded: a folder this deep or this large is shown rather than searched to the end.
    std::error_code error;
    std::size_t visited = 0U;
    for (auto it = std::filesystem::recursive_directory_iterator(
             folder, std::filesystem::directory_options::skip_permission_denied, error);
         !error && it != std::filesystem::recursive_directory_iterator(); it.increment(error)) {
        if (++visited > 4000U || it.depth() > 8) {
            return true;
        }
        const std::string name = it->path().filename().string();
        if (!name.empty() && name.front() == '.') {
            if (it->is_directory(error)) {
                it.disable_recursion_pending();
            }
            continue;
        }
        std::error_code statError;
        if (it->is_regular_file(statError) && MatchesKindUVE(it->path())) {
            return true;
        }
    }
    return false;
}

bool EditorFileDialogUVE::MatchesKindUVE(const std::filesystem::path& file) const {
    if (m_allKinds || m_request.extensions.empty()) {
        return true;
    }
    const std::string extension = LowerUVE(file.extension().string());
    return std::any_of(m_request.extensions.begin(), m_request.extensions.end(),
                       [&](const std::string& kind) { return LowerUVE(kind) == extension; });
}

void EditorFileDialogUVE::GoToUVE(const std::filesystem::path& directory) {
    std::error_code error;
    const std::filesystem::path canonical = std::filesystem::weakly_canonical(directory, error);
    m_directory = error ? directory : canonical;
    if (m_pathInput) {
        m_pathInput->setText(m_directory.string());
    }
    if (m_search) {
        m_search->setText("");
    }
    RebuildListUVE();
}

void EditorFileDialogUVE::RebuildListUVE() {
    m_entries.clear();
    const std::string needle = m_search ? LowerUVE(m_search->text()) : std::string();
    std::error_code error;
    std::size_t hidden = 0U;
    for (auto it = std::filesystem::directory_iterator(m_directory, std::filesystem::directory_options::skip_permission_denied,
                                                       error);
         !error && it != std::filesystem::directory_iterator(); it.increment(error)) {
        const std::filesystem::path path = it->path();
        const std::string name = path.filename().string();
        if (name.empty() || name.front() == '.') {
            continue;   // hidden files and folders
        }
        std::error_code statError;
        const bool directory = it->is_directory(statError);
        if (!directory && !MatchesKindUVE(path)) {
            ++hidden;
            continue;
        }
        // A folder is worth opening only when something of the asked kinds is inside it.
        if (directory && !FolderHasKindUVE(path)) {
            continue;
        }
        if (!needle.empty() && LowerUVE(name).find(needle) == std::string::npos) {
            continue;
        }
        EntryUVE entry;
        entry.path = path;
        entry.directory = directory;
        entry.size = directory ? 0U : it->file_size(statError);
        entry.modified = it->last_write_time(statError);
        m_entries.push_back(std::move(entry));
    }
    std::stable_sort(m_entries.begin(), m_entries.end(), [](const EntryUVE& a, const EntryUVE& b) {
        if (a.directory != b.directory) {
            return a.directory;
        }
        return LowerUVE(a.path.filename().string()) < LowerUVE(b.path.filename().string());
    });
    if (m_table) {
        m_table->setRowCount(static_cast<int>(m_entries.size()));
        m_table->select({});
        m_table->setScrollY(0.0F);
    }
    if (m_status) {
        std::size_t files = 0U;
        for (const EntryUVE& entry : m_entries) {
            files += entry.directory ? 0U : 1U;
        }
        std::string text = std::to_string(files) + (files == 1U ? " file" : " files") + " here, " +
                           std::to_string(m_entries.size() - files) + " folders with more";
        if (hidden > 0U) {
            text += " (other kinds hidden)";
        }
        if (error) {
            text = "This folder cannot be read.";
        }
        m_status->setText(text);
    }
}

void EditorFileDialogUVE::ShowImportTargetMenuUVE() {
    std::vector<prism::MenuItem> items;
    items.push_back(prism::MenuItem::Item("Content", [this] {
        m_importInto.clear();
        if (m_importTarget) {
            m_importTarget->setText("Content");
        }
    }, "", prism::Icon::Folder));
    for (const Asset::ProjectFileEntryUVE& entry : m_editor.m_services->GetProjectFileIndexUVE().GetSnapshotUVE().entries) {
        if (entry.kind != Asset::ProjectFileEntryKindUVE::Directory) {
            continue;
        }
        const std::string name = "Content/" + entry.relativePath.generic_string();
        items.push_back(prism::MenuItem::Item(name, [this, rel = entry.relativePath, name] {
            m_importInto = rel;
            if (m_importTarget) {
                m_importTarget->setText(name);
            }
        }, "", prism::Icon::Folder));
    }
    prism::showMenu(std::move(items), m_importTarget ? m_importTarget->rect() : prism::Rect{}, prism::PopupPlacement::Below);
}

// ---------------------------------------------------------------------------------- confirmation

void EditorFileDialogUVE::CancelUVE() {
    m_closePending = true;
}

void EditorFileDialogUVE::ConfirmUVE() {
    const std::string name = m_fileName ? m_fileName->text() : std::string();
    if (name.empty()) {
        if (m_status) {
            m_status->setText(m_request.mode == FilePickerModeUVE::Save ? "Give the file a name." : "Pick a file first.");
        }
        return;
    }
    std::error_code error;
    std::filesystem::path chosen = std::filesystem::path{name}.is_absolute() ? std::filesystem::path{name} : m_directory / name;

    if (m_request.mode == FilePickerModeUVE::Save) {
        if (!m_request.extensions.empty() && LowerUVE(chosen.extension().string()) != LowerUVE(m_request.extensions.front())) {
            chosen += m_request.extensions.front();
        }
        if (std::filesystem::exists(chosen, error)) {
            chosen = EditorUVE::MakeUniqueContentPathUVE(chosen.parent_path(), chosen.stem().string(), chosen.extension().string());
        }
    } else if (!std::filesystem::is_regular_file(chosen, error)) {
        if (m_status) {
            m_status->setText("There is no file " + name + " here.");
        }
        return;
    }

    if (m_request.quickImport) {
        // A copy in the chosen Content folder, under a name of its own when that one is taken.
        const std::filesystem::path target = ContentRootUVE() / m_importInto;
        std::filesystem::create_directories(target, error);
        const bool alreadyThere = chosen.parent_path() == target;
        if (!alreadyThere) {
            const std::filesystem::path copy =
                EditorUVE::MakeUniqueContentPathUVE(target, chosen.stem().string(), chosen.extension().string());
            std::filesystem::copy_file(chosen, copy, error);
            if (error) {
                if (m_status) {
                    m_status->setText("Could not copy " + chosen.filename().string() + ": " + error.message());
                }
                return;
            }
            chosen = copy;
        }
        m_editor.RefreshProjectFileIndexUVE();
    }
    // The pick runs once; the window closes between frames.
    auto pick = std::move(m_request.onPick);
    m_request.onPick = {};
    m_closePending = true;
    if (pick) {
        pick(chosen);
    }
}

} // namespace UVE::Editor
