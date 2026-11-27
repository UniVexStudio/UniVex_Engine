// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
#include "editor_file_dialog.h"
#include "editor_layout_store.h"

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

constexpr int kWidth = 940;
constexpr int kHeight = 600;

// UTF-8 on every platform: path::string() throws on Windows for names outside the ANSI code page, so a
// folder holding one crashed the dialog when opened.
[[nodiscard]] std::string Utf8(const std::filesystem::path& path) {
    const std::u8string text = path.u8string();
    return std::string(text.begin(), text.end());
}

[[nodiscard]] std::string Lower(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(),
                   [](const unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return text;
}

[[nodiscard]] std::string SizeText(const std::uintmax_t bytes) {
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

[[nodiscard]] std::string TimeText(const std::filesystem::file_time_type time) {
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
[[nodiscard]] std::vector<std::pair<std::string, std::filesystem::path>> Places(const std::filesystem::path& contentRoot) {
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
#if defined(__ANDROID__)
    const auto addPlace = [&](const std::string& label, const std::filesystem::path& path) {
        if (!path.empty() && std::filesystem::is_directory(path, error)) {
            places.emplace_back(label, path);
        }
    };
    const char* const pubStorage = std::getenv("UNIVEX_ANDROID_PUBLIC_STORAGE");
    const std::filesystem::path storageRoot =
        (pubStorage != nullptr && *pubStorage != '\0') ? std::filesystem::path{pubStorage}
                                                       : std::filesystem::path{"/storage/emulated/0"};
    addPlace("Storage", storageRoot);
    if (const char* const pubDocs = std::getenv("UNIVEX_ANDROID_PUBLIC_DOCS"); pubDocs != nullptr && *pubDocs != '\0') {
        addPlace("Documents", std::filesystem::path{pubDocs});
    } else {
        addPlace("Documents", storageRoot / "Documents");
    }
    addPlace("Downloads", storageRoot / "Download");
    addPlace("Pictures", storageRoot / "Pictures");
    if (const char* const appDocs = std::getenv("UNIVEX_ANDROID_APP_EXT_DOCS"); appDocs != nullptr && *appDocs != '\0') {
        addPlace("App Files", std::filesystem::path{appDocs});
    }
    std::filesystem::directory_iterator storageIt(
        "/storage", std::filesystem::directory_options::skip_permission_denied, error);
    // increment(error), not a range-for: its operator++ throws when /storage can't be read further.
    for (; !error && storageIt != std::filesystem::directory_iterator(); storageIt.increment(error)) {
        const std::filesystem::directory_entry& entry = *storageIt;
        const std::string vol = Utf8(entry.path().filename());
        if (vol.empty() || vol == "self" || vol == "emulated" || vol.front() == '.') {
            continue;
        }
        std::error_code statError;
        if (entry.is_directory(statError)) {
            places.emplace_back("SD: " + vol, entry.path());
        }
    }
#else
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
#endif
    return places;
}

} // namespace

EditorFileDialog::EditorFileDialog(Editor& editor, EditorHost& host)
    : m_editor(editor), m_host(host) {}

EditorFileDialog::~EditorFileDialog() {
    if (m_window != 0 && m_host.IsReady()) {
        m_host.CloseWindow(m_window);
    }
}

std::filesystem::path EditorFileDialog::ContentRoot() const {
    return m_editor.m_services->GetProjectFileIndex().GetSnapshot().contentRoot;
}

void EditorFileDialog::Open(FilePickerRequest request) {
    if (!m_host.IsReady()) {
        return;
    }
    if (m_window != 0) {
        m_host.CloseWindow(m_window);
        m_window = 0;
    }
    m_request = std::move(request);
    m_closePending = false;
    m_allKinds = m_request.extensions.empty();
    const std::filesystem::path contentRoot = ContentRoot();
    m_importInto = m_editor.m_assetTableDirectory;
    std::error_code error;
    std::filesystem::path start = m_request.startDirectory;
    if (!start.empty() && start.is_relative()) {
        start = contentRoot / start;
    }
    if (start.empty() || !std::filesystem::is_directory(start, error)) {
        start = contentRoot;
    }
    m_window = m_host.OpenWindow(Build(), m_request.title.empty() ? std::string("Open File") : m_request.title,
                                    kWidth, kHeight);
    if (m_window == 0) {
        return;
    }
    m_host.App().runInWindow(m_window, [this, start] { GoTo(start); });
    m_host.FocusWindow(m_window);
}

void EditorFileDialog::Frame() {
    if (m_window == 0) {
        return;
    }
    if (m_host.WindowCloseRequested(m_window)) {
        m_host.ClearCloseRequest(m_window);
        m_closePending = true;
    }
    if (m_closePending) {
        // Closed here, between frames: never from inside the window's own input.
        m_host.CloseWindow(m_window);
        m_window = 0;
        m_closePending = false;
        m_request = FilePickerRequest{};
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
    m_host.RenderWindow(m_window, {});
}

// --------------------------------------------------------------------------------------- layout

prism::Ref<prism::Widget> EditorFileDialog::Build() {
    const bool import = m_request.quickImport;
    const bool save = m_request.mode == FilePickerMode::Save;
    const bool openFolder = m_request.mode == FilePickerMode::OpenFolder;
    auto window = prism::vbox();

    // Title bar: shared PRISM tool window chrome with icon, DragStrip, Floating/Full toggle, and Close.
    window->add(BuildToolWindowTitleBar(
        m_request.title.empty() ? std::string(openFolder ? "Select Folder" : "Open File") : m_request.title,
        prism::Icon::FolderOpen, [this] { Cancel(); }));

    // Navigation: up, the path (type one and press Enter), search.
    auto nav = prism::hbox(4.0F);
    nav->setPadding(prism::Margin(6.0F, 4.0F, 6.0F, 4.0F));
    auto up = ToolButton(prism::Icon::ChevronUp, "Up one folder");
    up->onClick = [this] {
        if (m_directory.has_parent_path() && m_directory.parent_path() != m_directory) {
            GoTo(m_directory.parent_path());
        }
    };
    nav->add(up, prism::Slot{0.0F, {}, prism::HAlign::Left, prism::VAlign::Center});
    m_pathInput = prism::make<prism::TextInput>("", "Folder");
    m_pathInput->onCommit = [this](const std::string& text) {
        std::error_code error;
        const std::filesystem::path typed{text};
        if (std::filesystem::is_directory(typed, error)) {
            GoTo(typed);
        } else if (std::filesystem::is_regular_file(typed, error)) {
            GoTo(typed.parent_path());
            if (m_fileName) {
                m_fileName->setText(Utf8(typed.filename()));
            }
        } else if (m_status) {
            m_status->setText("There is no folder at " + text + ".");
        }
    };
    nav->add(m_pathInput, prism::Slot{1.0F, {}, prism::HAlign::Fill, prism::VAlign::Center});
    m_search = prism::make<prism::TextInput>("", "Search");
    m_search->onChanged = [this](const std::string&) { RebuildList(); };
    auto searchBox = prism::make<prism::SizeBox>();
    searchBox->setWidthOverride(200.0F);
    searchBox->setContent(m_search);
    nav->add(searchBox, prism::Slot{0.0F, {}, prism::HAlign::Left, prism::VAlign::Center});
    window->add(nav);

    // Places | files.
    auto places = prism::vbox(1.0F);
    places->setPadding(prism::Margin(4.0F));
    for (const auto& [name, path] : Places(ContentRoot())) {
        auto place = prism::make<prism::Button>(name, name == "Content" || name == "Project" ? prism::Icon::Folder
                                                                                            : prism::Icon::FolderOpen,
                                                prism::ButtonStyle::Flat);
        place->setTooltip(Utf8(path));
        place->onClick = [this, p = path] { GoTo(p); };
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
        const Entry& entry = m_entries[static_cast<std::size_t>(row)];
        switch (col) {
            case 0: return Utf8(entry.path.filename());
            case 1: return entry.directory ? std::string("Folder") : Lower(Utf8(entry.path.extension()));
            case 2: return entry.directory ? std::string() : SizeText(entry.size);
            default: return TimeText(entry.modified);
        }
    };
    m_table->lessThan = [this](const int a, const int b, const int col) {
        const Entry& x = m_entries[static_cast<std::size_t>(a)];
        const Entry& y = m_entries[static_cast<std::size_t>(b)];
        if (x.directory != y.directory) {
            return x.directory;   // folders stay first whichever way it is sorted
        }
        switch (col) {
            case 2: return x.size < y.size;
            case 3: return x.modified < y.modified;
            case 1: return Lower(Utf8(x.path.extension())) < Lower(Utf8(y.path.extension()));
            default: return Lower(Utf8(x.path.filename())) < Lower(Utf8(y.path.filename()));
        }
    };
    m_table->paintCell = [this](prism::Canvas& c, const int row, const int col, const prism::Rect r, const bool selected) {
        if (row < 0 || static_cast<std::size_t>(row) >= m_entries.size()) {
            return;
        }
        const prism::Theme& t = prism::theme();
        const Entry& entry = m_entries[static_cast<std::size_t>(row)];
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
        if (!m_fileName) {
            return;
        }
        if (m_request.mode == FilePickerMode::OpenFolder) {
            std::vector<std::string> dirNames;
            for (const int row : rows) {
                if (row >= 0 && static_cast<std::size_t>(row) < m_entries.size() &&
                    m_entries[static_cast<std::size_t>(row)].directory) {
                    dirNames.push_back(Utf8(m_entries[static_cast<std::size_t>(row)].path.filename()));
                }
            }
            if (dirNames.size() == 1U) {
                m_fileName->setText(dirNames.front());
            } else if (dirNames.size() > 1U) {
                m_fileName->setText(std::to_string(dirNames.size()) + " folders selected");
            } else {
                m_fileName->setText("");
            }
            return;
        }
        if (m_request.allowMultiSelect) {
            std::vector<std::string> fileNames;
            for (const int row : rows) {
                if (row >= 0 && static_cast<std::size_t>(row) < m_entries.size() &&
                    !m_entries[static_cast<std::size_t>(row)].directory) {
                    fileNames.push_back(Utf8(m_entries[static_cast<std::size_t>(row)].path.filename()));
                }
            }
            if (fileNames.size() == 1U) {
                m_fileName->setText(fileNames.front());
            } else if (fileNames.size() > 1U) {
                m_fileName->setText(std::to_string(fileNames.size()) + " files selected");
            }
            return;
        }
        if (rows.empty() || rows.front() < 0 || static_cast<std::size_t>(rows.front()) >= m_entries.size()) {
            return;
        }
        const Entry& entry = m_entries[static_cast<std::size_t>(rows.front())];
        if (!entry.directory) {
            m_fileName->setText(Utf8(entry.path.filename()));
        }
    };
    m_table->onActivate = [this](const int row) {
        if (row < 0 || static_cast<std::size_t>(row) >= m_entries.size()) {
            return;
        }
        const Entry entry = m_entries[static_cast<std::size_t>(row)];
        if (entry.directory) {
            GoTo(entry.path);
        } else if (m_request.mode != FilePickerMode::OpenFolder) {
            if (m_fileName) {
                m_fileName->setText(Utf8(entry.path.filename()));
            }
            Confirm();
        }
    };

    auto body = prism::make<prism::Splitter>(prism::Orientation::Horizontal);
    body->add(Panel(places), prism::Splitter::Pane{170.0F, 120.0F, 400.0F, false});
    body->add(Panel(m_table), prism::Splitter::Pane{740.0F, 300.0F, 1e9F, true});
    EditorLayoutStore::Instance().BindSplitter("fileDialog.body", *body);
    window->add(body, prism::Slot{1.0F});

    // File name and kind.
    auto fileRow = prism::hbox(6.0F);
    fileRow->setPadding(prism::Margin(8.0F, 6.0F, 8.0F, 2.0F));
    auto fileLabel = prism::make<prism::Label>(openFolder ? "Folder" : save ? "Save as" : "File");
    fileLabel->setColor(prism::ThemeColor(&prism::Theme::textDim));
    auto fileLabelBox = prism::make<prism::SizeBox>();
    fileLabelBox->setWidthOverride(80.0F);
    fileLabelBox->setContent(fileLabel);
    fileRow->add(fileLabelBox, prism::Slot{0.0F, {}, prism::HAlign::Left, prism::VAlign::Center});
    const std::string placeholder = openFolder
                                        ? "Current folder (or select subfolder)"
                                        : save ? "Name"
                                               : (m_request.allowMultiSelect ? "Pick one or more files (Ctrl/Shift+click)"
                                                                             : "Pick a file, or type its name");
    m_fileName = prism::make<prism::TextInput>(m_request.saveName, placeholder);
    m_fileName->onCommit = [this](const std::string&) { Confirm(); };
    fileRow->add(m_fileName, prism::Slot{1.0F, {}, prism::HAlign::Fill, prism::VAlign::Center});
    std::string kinds;
    for (const std::string& extension : m_request.extensions) {
        kinds += (kinds.empty() ? "*" : ", *") + extension;
    }
    // A slot's pick shows only what the slot takes; an open-ended pick can show everything.
    std::vector<std::string> filters;
    if (openFolder) {
        filters.emplace_back("Folders (preserves subfolders)");
    } else if (!kinds.empty()) {
        filters.push_back(kinds);
    } else {
        filters.emplace_back("All files (*.*)");
    }
    m_kind = prism::make<prism::ComboBox>(filters, 0);
    m_kind->onChange = [this](const int) { RebuildList(); };
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
        m_importTarget->onClick = [this] { ShowImportTargetMenu(); };
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
    const char* const confirmLabel = openFolder ? "Import Folder" : (import || m_request.allowMultiSelect) ? "Import" : save ? "Save" : "Load";
    auto confirm = prism::make<prism::Button>(confirmLabel,
                                              (openFolder || import || m_request.allowMultiSelect) ? prism::Icon::Open : prism::Icon::Check,
                                              prism::ButtonStyle::Primary);
    confirm->onClick = [this] { Confirm(); };
    auto cancel = prism::make<prism::Button>("Cancel", prism::Icon::None, prism::ButtonStyle::Default);
    cancel->onClick = [this] { Cancel(); };
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

bool EditorFileDialog::MatchesKind(const std::filesystem::path& file) const {
    if (m_allKinds || m_request.extensions.empty()) {
        return true;
    }
    const std::string extension = Lower(Utf8(file.extension()));
    return std::any_of(m_request.extensions.begin(), m_request.extensions.end(),
                       [&](const std::string& kind) { return Lower(kind) == extension; });
}

void EditorFileDialog::GoTo(const std::filesystem::path& directory) {
    std::error_code error;
    const std::filesystem::path canonical = std::filesystem::weakly_canonical(directory, error);
    m_directory = error ? directory : canonical;
    if (m_pathInput) {
        m_pathInput->setText(Utf8(m_directory));
    }
    if (m_search) {
        m_search->setText("");
    }
    if (m_request.mode == FilePickerMode::OpenFolder && m_fileName) {
        m_fileName->setText("");
    }
    RebuildList();
}

void EditorFileDialog::RebuildList() {
    m_entries.clear();
    const std::string needle = m_search ? Lower(m_search->text()) : std::string();
    std::error_code error;
    std::size_t hidden = 0U;
    for (auto it = std::filesystem::directory_iterator(m_directory, std::filesystem::directory_options::skip_permission_denied,
                                                       error);
         !error && it != std::filesystem::directory_iterator(); it.increment(error)) {
        const std::filesystem::path path = it->path();
        const std::string name = Utf8(path.filename());
        if (name.empty() || name.front() == '.') {
            continue;   // hidden files and folders
        }
        std::error_code statError;
        const bool directory = it->is_directory(statError);
        if (!directory && !MatchesKind(path)) {
            ++hidden;
            continue;
        }
        if (!needle.empty() && Lower(name).find(needle) == std::string::npos) {
            continue;
        }
        Entry entry;
        entry.path = path;
        entry.directory = directory;
        entry.size = directory ? 0U : it->file_size(statError);
        entry.modified = it->last_write_time(statError);
        m_entries.push_back(std::move(entry));
    }
    std::stable_sort(m_entries.begin(), m_entries.end(), [](const Entry& a, const Entry& b) {
        if (a.directory != b.directory) {
            return a.directory;
        }
        return Lower(Utf8(a.path.filename())) < Lower(Utf8(b.path.filename()));
    });
    if (m_table) {
        m_table->setRowCount(static_cast<int>(m_entries.size()));
        m_table->select({});
        m_table->setScrollY(0.0F);
    }
    if (m_status) {
        std::size_t files = 0U;
        for (const Entry& entry : m_entries) {
            files += entry.directory ? 0U : 1U;
        }
        std::string text = std::to_string(files) + (files == 1U ? " file" : " files") + " here, " +
                           std::to_string(m_entries.size() - files) + " folders";
        if (hidden > 0U) {
            text += " (other kinds hidden)";
        }
        if (error) {
            text = "This folder cannot be read.";
        }
        m_status->setText(text);
    }
}

void EditorFileDialog::ShowImportTargetMenu() {
    std::vector<prism::MenuItem> items;
    items.push_back(prism::MenuItem::Item("Content", [this] {
        m_importInto.clear();
        if (m_importTarget) {
            m_importTarget->setText("Content");
        }
    }, "", prism::Icon::Folder));
    for (const Asset::ProjectFileEntry& entry : m_editor.m_services->GetProjectFileIndex().GetSnapshot().entries) {
        if (entry.kind != Asset::ProjectFileEntryKind::Directory) {
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

void EditorFileDialog::Cancel() {
    m_closePending = true;
}

void EditorFileDialog::Confirm() {
    std::error_code error;
    if (m_request.mode == FilePickerMode::OpenFolder) {
        std::vector<std::filesystem::path> chosenDirs;
        if (m_table) {
            for (const int row : m_table->selection()) {
                if (row >= 0 && static_cast<std::size_t>(row) < m_entries.size() &&
                    m_entries[static_cast<std::size_t>(row)].directory) {
                    chosenDirs.push_back(m_entries[static_cast<std::size_t>(row)].path);
                }
            }
        }
        if (chosenDirs.empty()) {
            const std::string name = m_fileName ? m_fileName->text() : std::string();
            if (!name.empty()) {
                const std::filesystem::path typed =
                    std::filesystem::path{name}.is_absolute() ? std::filesystem::path{name} : (m_directory / name);
                if (std::filesystem::is_directory(typed, error)) {
                    chosenDirs.push_back(typed);
                }
            }
        }
        if (chosenDirs.empty() && std::filesystem::is_directory(m_directory, error)) {
            chosenDirs.push_back(m_directory);
        }
        if (chosenDirs.empty()) {
            if (m_status) {
                m_status->setText("Select a valid folder first.");
            }
            return;
        }
        auto pickMany = std::move(m_request.onPickMany);
        auto pick = std::move(m_request.onPick);
        m_request.onPickMany = {};
        m_request.onPick = {};
        m_closePending = true;
        if (pickMany) {
            pickMany(chosenDirs);
        } else if (pick) {
            pick(chosenDirs.front());
        }
        return;
    }

    if (m_request.allowMultiSelect && m_request.onPickMany && m_table && !m_table->selection().empty()) {
        std::vector<std::filesystem::path> chosenFiles;
        for (const int row : m_table->selection()) {
            if (row >= 0 && static_cast<std::size_t>(row) < m_entries.size() &&
                !m_entries[static_cast<std::size_t>(row)].directory) {
                chosenFiles.push_back(m_entries[static_cast<std::size_t>(row)].path);
            }
        }
        if (!chosenFiles.empty()) {
            auto pickMany = std::move(m_request.onPickMany);
            m_request.onPickMany = {};
            m_request.onPick = {};
            m_closePending = true;
            pickMany(chosenFiles);
            return;
        }
    }

    const std::string name = m_fileName ? m_fileName->text() : std::string();
    if (name.empty()) {
        if (m_status) {
            m_status->setText(m_request.mode == FilePickerMode::Save ? "Give the file a name." : "Pick a file first.");
        }
        return;
    }
    std::filesystem::path chosen = std::filesystem::path{name}.is_absolute() ? std::filesystem::path{name} : m_directory / name;

    if (m_request.mode == FilePickerMode::Save) {
        if (!m_request.extensions.empty() && Lower(Utf8(chosen.extension())) != Lower(m_request.extensions.front())) {
            chosen += m_request.extensions.front();
        }
        if (std::filesystem::exists(chosen, error)) {
            chosen = Editor::MakeUniqueContentPath(chosen.parent_path(), chosen.stem().string(), chosen.extension().string());
        }
    } else if (!std::filesystem::is_regular_file(chosen, error)) {
        if (m_status) {
            m_status->setText("There is no file " + name + " here.");
        }
        return;
    }

    if (m_request.quickImport) {
        // A copy in the chosen Content folder, under a name of its own when that one is taken.
        const std::filesystem::path target = ContentRoot() / m_importInto;
        std::filesystem::create_directories(target, error);
        const bool alreadyThere = chosen.parent_path() == target;
        if (!alreadyThere) {
            const std::filesystem::path copy =
                Editor::MakeUniqueContentPath(target, chosen.stem().string(), chosen.extension().string());
            std::filesystem::copy_file(chosen, copy, error);
            if (error) {
                if (m_status) {
                    m_status->setText("Could not copy " + Utf8(chosen.filename()) + ": " + error.message());
                }
                return;
            }
            chosen = copy;
        }
        m_editor.RefreshProjectFileIndex();
    }
    // The pick runs once; the window closes between frames.
    auto pickMany = std::move(m_request.onPickMany);
    auto pick = std::move(m_request.onPick);
    m_request.onPickMany = {};
    m_request.onPick = {};
    m_closePending = true;
    if (m_request.allowMultiSelect && pickMany) {
        pickMany({chosen});
    } else if (pick) {
        pick(chosen);
    }
}

} // namespace UVE::Editor
