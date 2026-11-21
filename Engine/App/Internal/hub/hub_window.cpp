// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "hub_window.h"

#include <array>
#include <chrono>
#include <charconv>
#include <string_view>
#include <system_error>
#include <utility>

#include "process_launcher.h"
#include "project_compatibility.h"
#include "prism/Theme.h"

namespace UVE::Hub {
namespace {

// Layout, in PRISM layout units.
inline constexpr float kSidebarWidthUVE = 220.0F;
inline constexpr float kPagePaddingUVE = 24.0F;
inline constexpr float kSectionSpacingUVE = 16.0F;
inline constexpr float kRowSpacingUVE = 8.0F;
inline constexpr float kRowPaddingUVE = 12.0F;
inline constexpr float kRowRadiusUVE = 6.0F;
inline constexpr float kControlSpacingUVE = 8.0F;
inline constexpr float kFieldLabelWidthUVE = 120.0F;
inline constexpr float kTemplateListWidthUVE = 260.0F;

struct SortChoiceUVE final {
    const char* label;
    Pack::ProjectListSortUVE sort;
};
inline constexpr std::array<SortChoiceUVE, 2U> kSortChoicesUVE{{
    {"Recently opened", Pack::ProjectListSortUVE::RecentlyOpened},
    {"Name", Pack::ProjectListSortUVE::Name},
}};

[[nodiscard]] prism::ThemeColor StatusColorUVE(const Pack::ProjectListStatusUVE status) {
    switch (status) {
    case Pack::ProjectListStatusUVE::Ready:
        return &prism::Theme::success;
    case Pack::ProjectListStatusUVE::NeedsUpgrade:
    case Pack::ProjectListStatusUVE::OpensWithWarning:
        return &prism::Theme::warning;
    case Pack::ProjectListStatusUVE::Incompatible:
    case Pack::ProjectListStatusUVE::Missing:
        return &prism::Theme::error;
    }
    return &prism::Theme::textDim;
}

[[nodiscard]] std::string StatusTextUVE(const Pack::ProjectListStatusUVE status) {
    switch (status) {
    case Pack::ProjectListStatusUVE::Ready:
        return "Ready";
    case Pack::ProjectListStatusUVE::NeedsUpgrade:
        return "Asks to upgrade";
    case Pack::ProjectListStatusUVE::OpensWithWarning:
        return "Newer engine";
    case Pack::ProjectListStatusUVE::Incompatible:
        return "Needs a newer engine";
    case Pack::ProjectListStatusUVE::Missing:
        return "Missing";
    }
    return {};
}

[[nodiscard]] prism::Ref<prism::Widget> FieldRowUVE(const std::string& label, prism::Ref<prism::Widget> field) {
    auto row = prism::hbox(kControlSpacingUVE);
    auto caption = prism::make<prism::Label>(label);
    caption->setFixedWidth(kFieldLabelWidthUVE);
    row->add(caption, prism::Slot{0.0F, {}, prism::HAlign::Left, prism::VAlign::Center});
    row->add(std::move(field), prism::Slot{1.0F});
    return row;
}

[[nodiscard]] prism::Ref<prism::Label> HeadingUVE(const std::string& text) {
    return prism::make<prism::Label>(text, prism::FontRole::Semibold);
}

} // namespace

HubWindowUVE::HubWindowUVE(HubContextUVE context) : m_context(std::move(context)) {
    m_settings = Pack::LoadLauncherSettingsUVE(m_context.settingsFile);
}

prism::Ref<prism::Widget> HubWindowUVE::BuildUVE() {
    m_status = prism::make<prism::Label>("", prism::FontRole::Small);
    m_status->setPadding(prism::Margin(kPagePaddingUVE, kRowSpacingUVE));

    m_pages = prism::make<prism::StackSwitcher>();
    m_pages->addPage(BuildProjectsPageUVE());
    m_pages->addPage(BuildNewProjectPageUVE());
    m_pages->addPage(BuildSettingsPageUVE());

    auto content = prism::vbox();
    content->add(m_pages, prism::Slot{1.0F});
    content->add(prism::make<prism::Separator>(prism::Orientation::Horizontal));
    content->add(m_status);

    auto root = prism::hbox();
    root->add(BuildSidebarUVE());
    root->add(prism::make<prism::Separator>(prism::Orientation::Vertical));
    root->add(content, prism::Slot{1.0F});

    auto window = prism::make<prism::Border>();
    window->setBackground(&prism::Theme::window).setChild(root);

    ReloadRegistryUVE();
    RefreshTemplateListUVE();
    ShowPageUVE(HubPageUVE::Projects);
    return window;
}

prism::Ref<prism::Widget> HubWindowUVE::BuildSidebarUVE() {
    auto sidebar = prism::vbox(kRowSpacingUVE);
    sidebar->setPadding(prism::Margin(kSectionSpacingUVE));
    sidebar->add(prism::make<prism::Label>("UniVex Hub", prism::FontRole::Semibold));
    sidebar->add(prism::make<prism::Label>("Engine " + m_context.engine.version.ToStringUVE(), prism::FontRole::Small));
    sidebar->add(prism::make<prism::Spacer>(0.0F, kSectionSpacingUVE));

    const std::array<std::pair<const char*, prism::Icon>, 3U> pages{{
        {"Projects", prism::Icon::Folder},
        {"New Project", prism::Icon::Plus},
        {"Settings", prism::Icon::Settings},
    }};
    for (std::size_t index = 0U; index < pages.size(); ++index) {
        auto button = prism::make<prism::Button>(pages[index].first, pages[index].second, prism::ButtonStyle::Toolbar);
        button->setCheckable(true);
        button->onClick = [this, index] { ShowPageUVE(static_cast<HubPageUVE>(index)); };
        m_navigation.push_back(button);
        sidebar->add(button, prism::Slot{0.0F, {}, prism::HAlign::Fill});
    }

    auto frame = prism::make<prism::Border>();
    frame->setBackground(&prism::Theme::panel).setChild(sidebar);
    frame->setFixedWidth(kSidebarWidthUVE);
    return frame;
}

prism::Ref<prism::Widget> HubWindowUVE::BuildProjectsPageUVE() {
    auto page = prism::vbox(kSectionSpacingUVE);
    page->setPadding(prism::Margin(kPagePaddingUVE));

    auto header = prism::hbox(kControlSpacingUVE);
    header->add(HeadingUVE("Projects"), prism::Slot{1.0F, {}, prism::HAlign::Left, prism::VAlign::Center});
    std::vector<std::string> sortLabels;
    for (const SortChoiceUVE& choice : kSortChoicesUVE) {
        sortLabels.emplace_back(choice.label);
    }
    auto sort = prism::make<prism::ComboBox>(sortLabels, 0);
    sort->onChange = [this](const int index) {
        m_sort = kSortChoicesUVE[static_cast<std::size_t>(index)].sort;
        RefreshProjectListUVE();
    };
    header->add(sort);
    auto refresh = prism::make<prism::Button>("Refresh", prism::Icon::Refresh);
    refresh->onClick = [this] { ReloadRegistryUVE(); };
    header->add(refresh);
    page->add(header);

    m_search = prism::make<prism::TextInput>("", "Search projects");
    m_search->setLeadingIcon(prism::Icon::Search);
    m_search->setClearable(true);
    m_search->onChanged = [this](const std::string&) { RefreshProjectListUVE(); };
    page->add(m_search);

    auto openRow = prism::hbox(kControlSpacingUVE);
    m_openPath = prism::make<prism::TextInput>("", "Path to a .uvproject file");
    m_openPath->onCommit = [this](const std::string& path) { OpenProjectUVE(path); };
    openRow->add(m_openPath, prism::Slot{1.0F});
    auto open = prism::make<prism::Button>("Open", prism::Icon::Open);
    open->onClick = [this] { OpenProjectUVE(m_openPath->text()); };
    openRow->add(open);
    page->add(openRow);

    m_projectRows = prism::vbox(kRowSpacingUVE);
    auto scroll = prism::make<prism::ScrollView>();
    scroll->setContent(m_projectRows);
    page->add(scroll, prism::Slot{1.0F});
    return page;
}

prism::Ref<prism::Widget> HubWindowUVE::BuildNewProjectPageUVE() {
    auto page = prism::vbox(kSectionSpacingUVE);
    page->setPadding(prism::Margin(kPagePaddingUVE));
    page->add(HeadingUVE("New Project"));

    auto body = prism::hbox(kSectionSpacingUVE);
    m_templateRows = prism::vbox(kRowSpacingUVE);
    auto templates = prism::make<prism::ScrollView>();
    templates->setContent(m_templateRows);
    templates->setFixedWidth(kTemplateListWidthUVE);
    body->add(templates);

    auto details = prism::vbox(kSectionSpacingUVE);
    m_templateDescription = prism::make<prism::Label>();
    m_templateDescription->setColor(&prism::Theme::textDim);
    details->add(m_templateDescription);
    m_projectName = prism::make<prism::TextInput>("", "My Game");
    details->add(FieldRowUVE("Name", m_projectName));
    m_projectLocation = prism::make<prism::TextInput>(m_settings.defaultProjectLocation.string());
    details->add(FieldRowUVE("Location", m_projectLocation));
    auto create = prism::make<prism::Button>("Create and Open", prism::Icon::Plus, prism::ButtonStyle::Primary);
    create->onClick = [this] { CreateProjectUVE(); };
    details->add(create, prism::Slot{0.0F, {}, prism::HAlign::Left});
    body->add(details, prism::Slot{1.0F});

    page->add(body, prism::Slot{1.0F});
    return page;
}

prism::Ref<prism::Widget> HubWindowUVE::BuildSettingsPageUVE() {
    auto page = prism::vbox(kSectionSpacingUVE);
    page->setPadding(prism::Margin(kPagePaddingUVE));
    page->add(HeadingUVE("Settings"));
    m_settingsLocation = prism::make<prism::TextInput>(m_settings.defaultProjectLocation.string());
    page->add(FieldRowUVE("New projects in", m_settingsLocation));
    m_settingsRecentLimit = prism::make<prism::TextInput>();
    page->add(FieldRowUVE("Recent projects", m_settingsRecentLimit));
    page->add(prism::make<prism::Label>("Favorites are always kept. Between " +
                                            std::to_string(Pack::kMinimumRecentProjectLimitUVE) + " and " +
                                            std::to_string(Pack::kMaximumRecentProjectLimitUVE) + ".",
                                        prism::FontRole::Small));
    auto save = prism::make<prism::Button>("Save", prism::Icon::Save, prism::ButtonStyle::Primary);
    save->onClick = [this] { SaveSettingsUVE(); };
    page->add(save, prism::Slot{0.0F, {}, prism::HAlign::Left});
    return page;
}

void HubWindowUVE::ShowPageUVE(const HubPageUVE page) {
    const int index = static_cast<int>(page);
    m_pages->setActiveIndex(index);
    for (std::size_t button = 0U; button < m_navigation.size(); ++button) {
        m_navigation[button]->setChecked(static_cast<int>(button) == index);
    }
}

template <class ChangeUVE> void HubWindowUVE::UpdateRegistryUVE(ChangeUVE&& change) {
    const Pack::ProjectRegistryResultUVE loaded = m_registry.LoadUVE(m_context.registryFile);
    if (!loaded.IsAppliedUVE()) {
        SetStatusUVE(loaded.message, true);
        return;
    }
    change(m_registry);
    const Pack::ProjectRegistryResultUVE saved = m_registry.SaveUVE(m_context.registryFile);
    if (!saved.IsAppliedUVE()) {
        SetStatusUVE(saved.message, true);
    }
    RefreshProjectListUVE();
}

void HubWindowUVE::ReloadRegistryUVE() {
    const Pack::ProjectRegistryResultUVE loaded = m_registry.LoadUVE(m_context.registryFile);
    if (!loaded.IsAppliedUVE()) {
        SetStatusUVE(loaded.message, true);
    }
    m_settingsRecentLimit->setText(std::to_string(m_registry.RecentLimitUVE()));
    RefreshProjectListUVE();
}

void HubWindowUVE::RefreshProjectListUVE() {
    m_projectRows->clearChildren();
    const std::vector<Pack::ProjectListRowUVE> rows =
        Pack::BuildProjectListUVE(m_registry.EntriesUVE(), m_search->text(), m_sort, m_context.engine.version);
    if (rows.empty()) {
        auto empty = prism::make<prism::Label>(m_registry.EntriesUVE().empty()
                                                   ? "No projects yet. Create one, or open a .uvproject file."
                                                   : "No project matches the search.");
        empty->setColor(&prism::Theme::textDim);
        m_projectRows->add(empty);
    }
    for (const Pack::ProjectListRowUVE& row : rows) {
        m_projectRows->add(BuildProjectRowUVE(row));
    }
    m_projectRows->relayout();
}

prism::Ref<prism::Widget> HubWindowUVE::BuildProjectRowUVE(const Pack::ProjectListRowUVE& row) {
    const std::filesystem::path projectFile = row.entry.projectFile;
    auto text = prism::vbox();
    text->add(prism::make<prism::Label>(row.entry.displayName, prism::FontRole::Semibold));
    auto path = prism::make<prism::Label>(projectFile.parent_path().string(), prism::FontRole::Small);
    path->setColor(&prism::Theme::textDim);
    text->add(path);
    auto badge = prism::make<prism::Label>(row.badge + " - " + StatusTextUVE(row.status), prism::FontRole::Small);
    badge->setColor(StatusColorUVE(row.status));
    text->add(badge);

    auto line = prism::hbox(kControlSpacingUVE);
    line->add(text, prism::Slot{1.0F});

    const bool openable = row.status != Pack::ProjectListStatusUVE::Missing &&
                          row.status != Pack::ProjectListStatusUVE::Incompatible;
    if (openable) {
        auto open = prism::make<prism::Button>("Open", prism::Icon::Play, prism::ButtonStyle::Primary);
        open->onClick = [this, projectFile] { OpenProjectUVE(projectFile); };
        line->add(open, prism::Slot{0.0F, {}, prism::HAlign::Fill, prism::VAlign::Center});
    }
    auto favorite = prism::make<prism::Button>(row.entry.favorite ? "Favorite" : "Add to favorites", prism::Icon::Check,
                                               prism::ButtonStyle::Flat);
    favorite->setCheckable(true);
    favorite->setChecked(row.entry.favorite);
    favorite->onClick = [this, projectFile, wasFavorite = row.entry.favorite] {
        UpdateRegistryUVE([&](Pack::ProjectRegistryUVE& registry) {
            static_cast<void>(registry.SetFavoriteUVE(projectFile, !wasFavorite));
        });
    };
    line->add(favorite, prism::Slot{0.0F, {}, prism::HAlign::Fill, prism::VAlign::Center});
    if (row.status != Pack::ProjectListStatusUVE::Missing) {
        auto reveal = prism::make<prism::Button>("Show in folder", prism::Icon::FolderOpen, prism::ButtonStyle::Flat);
        reveal->onClick = [this, projectFile] {
            const Platform::ProcessLaunchResultUVE revealed =
                Platform::RevealInFileManagerUVE(projectFile.parent_path());
            if (!revealed.launched) {
                SetStatusUVE(revealed.message, true);
            }
        };
        line->add(reveal, prism::Slot{0.0F, {}, prism::HAlign::Fill, prism::VAlign::Center});
    }
    auto remove = prism::make<prism::Button>("", prism::Icon::Trash, prism::ButtonStyle::Flat);
    remove->setTooltip("Remove from the list (the project stays on disk)");
    remove->onClick = [this, projectFile] {
        UpdateRegistryUVE([&](Pack::ProjectRegistryUVE& registry) { static_cast<void>(registry.RemoveUVE(projectFile)); });
    };
    line->add(remove, prism::Slot{0.0F, {}, prism::HAlign::Fill, prism::VAlign::Center});

    auto card = prism::make<prism::Border>();
    card->setBackground(&prism::Theme::panel)
        .setBorder(&prism::Theme::borderSoft)
        .setRadius(kRowRadiusUVE)
        .setPadding(prism::Margin(kRowPaddingUVE))
        .setChild(line);
    return card;
}

void HubWindowUVE::RefreshTemplateListUVE() {
    const Pack::ProjectTemplateDiscoveryUVE discovery =
        Pack::DiscoverProjectTemplatesUVE(m_context.engine.templatesDirectory);
    m_templates = discovery.templates;
    m_templateRows->clearChildren();
    m_templateButtons.clear();
    for (std::size_t index = 0U; index < m_templates.size(); ++index) {
        auto button = prism::make<prism::Button>(m_templates[index].displayName, prism::Icon::Cube, prism::ButtonStyle::Toolbar);
        button->setCheckable(true);
        button->onClick = [this, index] { SelectTemplateUVE(index); };
        m_templateButtons.push_back(button);
        m_templateRows->add(button, prism::Slot{0.0F, {}, prism::HAlign::Fill});
    }
    if (m_templates.empty()) {
        m_templateDescription->setText("No project templates were found in " +
                                       m_context.engine.templatesDirectory.string() + ".");
    } else {
        SelectTemplateUVE(0U);
    }
    if (!discovery.problems.empty()) {
        SetStatusUVE(discovery.problems.front(), true);
    }
}

void HubWindowUVE::SelectTemplateUVE(const std::size_t index) {
    if (index >= m_templates.size()) {
        return;
    }
    m_selectedTemplate = index;
    for (std::size_t button = 0U; button < m_templateButtons.size(); ++button) {
        m_templateButtons[button]->setChecked(button == index);
    }
    m_templateDescription->setText(m_templates[index].description);
}

void HubWindowUVE::OpenProjectUVE(const std::filesystem::path& projectFile) {
    if (projectFile.empty()) {
        SetStatusUVE("Enter the path of a .uvproject file to open.", true);
        return;
    }
    const Platform::EditorProjectPackageLoadResultUVE manifest =
        Platform::EditorProjectPackageCodecUVE::LoadUVE(projectFile);
    if (!manifest.IsAcceptedUVE()) {
        SetStatusUVE("Cannot open " + projectFile.string() + ": " + manifest.result.message, true);
        return;
    }
    const Pack::ProjectCompatibilityUVE compatibility =
        Pack::EvaluateProjectCompatibilityUVE(manifest.package->engineVersion, m_context.engine.version);
    if (!compatibility.CanOpenUVE()) {
        SetStatusUVE(compatibility.message, true);
        return;
    }
    const Platform::ProcessLaunchResultUVE launched = Platform::LaunchDetachedProcessUVE(
        {m_context.engine.editorExecutable,
         Pack::MakeEditorLaunchArgumentsUVE(Pack::ProjectRegistryUVE::NormalizeProjectFileUVE(projectFile),
                                            m_context.registryOverridden ? m_context.registryFile
                                                                         : std::filesystem::path{}),
         projectFile.parent_path()});
    if (!launched.launched) {
        SetStatusUVE(launched.message, true);
        return;
    }
    // The editor records the project itself; listing it now shows it at once.
    UpdateRegistryUVE([&](Pack::ProjectRegistryUVE& registry) {
        const auto now = std::chrono::duration_cast<std::chrono::seconds>(
                             std::chrono::system_clock::now().time_since_epoch())
                             .count();
        registry.RecordOpenedUVE(projectFile, manifest.package->displayName, manifest.package->engineVersion, now);
    });
    SetStatusUVE("Opening " + manifest.package->displayName + "...", false);
}

void HubWindowUVE::CreateProjectUVE() {
    if (m_selectedTemplate >= m_templates.size()) {
        SetStatusUVE("Choose a template first.", true);
        return;
    }
    const std::filesystem::path location = m_projectLocation->text();
    std::error_code error;
    if (location.is_absolute()) {
        std::filesystem::create_directories(location, error);
    }
    const Pack::ProjectCreationResultUVE created = Pack::CreateProjectFromTemplateUVE(
        m_templates[m_selectedTemplate], location, m_projectName->text(), m_context.engine.version);
    if (!created.IsCreatedUVE()) {
        SetStatusUVE(created.message, true);
        return;
    }
    m_projectName->setText("");
    ShowPageUVE(HubPageUVE::Projects);
    OpenProjectUVE(created.projectFile);
}

void HubWindowUVE::SaveSettingsUVE() {
    const std::filesystem::path location = m_settingsLocation->text();
    if (!location.is_absolute()) {
        SetStatusUVE("The project folder must be a full path.", true);
        return;
    }
    const std::string& limitText = m_settingsRecentLimit->text();
    std::size_t limit = 0U;
    const auto [end, parseError] = std::from_chars(limitText.data(), limitText.data() + limitText.size(), limit);
    if (parseError != std::errc{} || end != limitText.data() + limitText.size()) {
        SetStatusUVE("The number of recent projects must be a whole number.", true);
        return;
    }
    m_settings.defaultProjectLocation = location;
    if (!Pack::SaveLauncherSettingsUVE(m_context.settingsFile, m_settings)) {
        SetStatusUVE("Unable to save the Hub settings to " + m_context.settingsFile.string() + ".", true);
        return;
    }
    m_projectLocation->setText(location.string());
    UpdateRegistryUVE([limit](Pack::ProjectRegistryUVE& registry) { registry.SetRecentLimitUVE(limit); });
    m_settingsRecentLimit->setText(std::to_string(m_registry.RecentLimitUVE()));
    SetStatusUVE("Settings saved.", false);
}

void HubWindowUVE::SetStatusUVE(const std::string& message, const bool isError) {
    m_status->setText(message);
    m_status->setColor(isError ? prism::ThemeColor(&prism::Theme::error) : prism::ThemeColor(&prism::Theme::textDim));
}

} // namespace UVE::Hub
