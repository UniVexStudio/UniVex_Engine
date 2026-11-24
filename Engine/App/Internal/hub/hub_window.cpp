// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "hub_window.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <charconv>
#include <cmath>
#include <string_view>
#include <system_error>
#include <utility>

#include "folder_picker.h"
#include "hub_project_opener.h"
#include "hub_setup.h"
#include "launcher_files.h"
#include "process_launcher.h"
#include "application_runtime.h"
#include "project_compatibility.h"
#include "prism/Icons.h"
#include "prism/Theme.h"

namespace UVE::Hub {

using LauncherUI::ShowFilePickerUVE;
using LauncherUI::ShowFolderPickerUVE;

namespace {

// Layout, in PRISM layout units.
inline constexpr float kSidebarWidthUVE = 224.0F;
inline constexpr float kCompactSidebarWidthUVE = 136.0F;
inline constexpr float kSidebarInsetUVE = 12.0F;
inline constexpr float kCompactSidebarInsetUVE = 6.0F;
inline constexpr float kNavSpacingUVE = 2.0F;
inline constexpr float kNavInsetTopUVE = 10.0F;
inline constexpr float kFooterInsetUVE = 6.0F;
inline constexpr float kBrandMarkSizeUVE = 34.0F;
inline constexpr float kBrandGapUVE = 10.0F;
inline constexpr float kBrandInsetUVE = 4.0F;
inline constexpr float kBrandLineSpacingUVE = 1.0F;
inline constexpr float kPagePaddingUVE = 22.0F;
inline constexpr float kCompactPagePaddingUVE = 10.0F;
inline constexpr float kSectionSpacingUVE = 14.0F;
inline constexpr float kCompactSectionSpacingUVE = 8.0F;
inline constexpr float kCompactBrandMarkSizeUVE = 22.0F;
inline constexpr float kRowSpacingUVE = 6.0F;
inline constexpr float kRowPaddingUVE = 10.0F;
inline constexpr float kRowRadiusUVE = 6.0F;
inline constexpr float kControlSpacingUVE = 6.0F;
inline constexpr float kFieldLabelWidthUVE = 116.0F;
inline constexpr float kTemplateListWidthUVE = 240.0F;
inline constexpr double kHubSplashMinSecondsUVE = 0.80;
inline constexpr double kHubSplashFadeSecondsUVE = 0.25;

[[nodiscard]] float SmoothStepUVE(const float x) noexcept {
    const float t = std::clamp(x, 0.0F, 1.0F);
    return t * t * (3.0F - 2.0F * t);
}

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
    case Pack::ProjectListStatusUVE::EngineNotInstalled:
        return &prism::Theme::textDim;
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
    case Pack::ProjectListStatusUVE::EngineNotInstalled:
        return "Engine not downloaded";
    }
    return {};
}

[[nodiscard]] prism::Ref<prism::Widget> FieldRowUVE(const std::string& label, prism::Ref<prism::Widget> field,
                                                    const bool stacked = false) {
    if (stacked) {
        auto column = prism::vbox(4.0F);
        auto caption = prism::make<prism::Label>(label, prism::FontRole::Small);
        caption->setColor(&prism::Theme::textDim);
        column->add(caption);
        column->add(std::move(field));
        return column;
    }
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

/// Branded UniVex Hub startup splash using the real `univex_logo.png` mark (`prism::drawBrandMark`),
/// version label, live startup step text, sleek progress bar, and smooth fade-out.
class HubWindowUVE::HubLoadingOverlayUVE final : public prism::Widget {
public:
    explicit HubLoadingOverlayUVE(const bool compact)
        : m_compact(compact),
          m_version("v" + Online::FormatDisplayVersionUVE(Online::kHubVersionUVE)) {}

    void SetProgressUVE(std::string step, const float targetProgress, const float alpha) {
        m_step = std::move(step);
        m_targetProgress = std::clamp(targetProgress, 0.0F, 1.0F);
        m_shownProgress += (m_targetProgress - m_shownProgress) * 0.28F;
        m_alpha = std::clamp(alpha, 0.0F, 1.0F);
        if (m_alpha <= 0.005F) {
            if (visible()) {
                setVisible(false);
            }
            return;
        }
        if (!visible()) {
            setVisible(true);
        }
        invalidate();
    }

    prism::Vec2 measure(const prism::Vec2 avail) override {
        return m_desired = constrain(avail);
    }

    bool hitSelf() const override {
        return visible() && m_alpha >= 0.99F;
    }

    void paint(prism::Canvas& c) override {
        if (m_alpha <= 0.005F) {
            return;
        }
        const prism::Theme& t = prism::theme();
        const prism::Rect r = rect();
        const float a = m_alpha;
        const prism::Color accent = t.accent;

        c.gradientV(r, prism::Color(17, 19, 24).alpha(a), prism::Color(11, 12, 16).alpha(a));
        c.fillRect(prism::Rect{r.x, r.y, r.w, 2.0F}, accent.alpha(a * 0.85F));

        const bool compact = m_compact || r.h < 420.0F || r.w < 500.0F;
        const float logoSide = compact ? 72.0F : 96.0F;
        const float barWidth = std::min(300.0F, std::max(168.0F, r.w - 56.0F));
        const float blockHeight = logoSide + (compact ? 104.0F : 122.0F);
        const float topY = std::floor(r.y + (r.h - blockHeight) * 0.5F);
        const float centerX = r.x + r.w * 0.5F;

        const prism::Rect mark{std::floor(centerX - logoSide * 0.5F), topY, logoSide, logoSide};
        if (prism::hasBrandMark()) {
            prism::drawBrandMark(c, mark, a);
        } else {
            c.fillRoundRect(mark, logoSide * 0.22F, accent.alpha(a * 0.22F));
            c.strokeRoundRect(mark, logoSide * 0.22F, accent.alpha(a), 2);
        }

        float y = mark.bottom() + (compact ? 14.0F : 18.0F);
        c.textIn(*t.semibold, prism::Rect{r.x + 24.0F, y, r.w - 48.0F, 24.0F}, "UniVex Hub",
                 t.textBright.alpha(a), prism::HAlign::Center);
        y += 22.0F;
        c.textIn(*t.small, prism::Rect{r.x + 24.0F, y, r.w - 48.0F, 18.0F}, m_version,
                 t.textDim.alpha(a), prism::HAlign::Center);
        y += 24.0F;

        const prism::Rect bar{std::floor(centerX - barWidth * 0.5F), y, barWidth, 5.0F};
        c.fillRoundRect(bar, 2.5F, prism::Color(30, 33, 42).alpha(a));
        c.fillRoundRect(prism::Rect{bar.x, bar.y, std::clamp(bar.w * m_shownProgress, 6.0F, bar.w), bar.h}, 2.5F,
                        accent.alpha(a));
        y += 14.0F;

        const int percent = static_cast<int>(std::round(std::clamp(m_shownProgress, 0.0F, 1.0F) * 100.0F));
        c.textIn(*t.small, prism::Rect{bar.x, y, bar.w - 40.0F, 16.0F}, m_step, t.text.alpha(a), prism::HAlign::Left);
        c.textIn(*t.small, prism::Rect{bar.x, y, bar.w, 16.0F}, std::to_string(percent) + "%",
                 t.textDim.alpha(a), prism::HAlign::Right);
    }

private:
    bool m_compact = false;
    std::string m_version;
    std::string m_step = "Starting UniVex Hub...";
    float m_targetProgress = 0.12F;
    float m_shownProgress = 0.08F;
    float m_alpha = 1.0F;
};

HubWindowUVE::HubWindowUVE(HubContextUVE context) : m_context(std::move(context)) {
    m_settings = Pack::LoadLauncherSettingsUVE(m_context.settingsFile);
}

HubWindowUVE::~HubWindowUVE() = default;

prism::Ref<prism::Widget> HubWindowUVE::BuildUVE() {
    const float pagePadding = m_context.compactLayout ? kCompactPagePaddingUVE : kPagePaddingUVE;
    m_status = prism::make<prism::Label>("", prism::FontRole::Small);
    m_status->setPadding(prism::Margin(pagePadding, kRowSpacingUVE));

    m_pages = prism::make<prism::StackSwitcher>();
    m_pages->addPage(BuildProjectsPageUVE());
    m_pages->addPage(BuildNewProjectPageUVE());
    m_pages->addPage(BuildEnginesPageUVE());
    m_pages->addPage(BuildSettingsPageUVE());

    auto content = prism::vbox();
    content->add(m_pages, prism::Slot{1.0F});
    content->add(prism::make<prism::Separator>(prism::Orientation::Horizontal));
    content->add(m_status);

    auto split = prism::hbox();
    split->add(BuildSidebarUVE());
    split->add(prism::make<prism::Separator>(prism::Orientation::Vertical));
    split->add(content, prism::Slot{1.0F});
    prism::Ref<prism::Widget> root = split;

    auto window = prism::make<prism::Border>();
    window->setBackground(&prism::Theme::window).setChild(root);

    ShowPageUVE(HubPageUVE::Projects);

    // Stepwise initialization runs across the first frames in TickUVE() under the branded loading
    // screen so Frame 1 renders immediately and never appears abruptly half-populated.
    m_startupStep = 0;
    m_startupBegan = std::chrono::steady_clock::now();
    m_startupFadeBegan = {};
    m_loadingOverlay = prism::make<HubLoadingOverlayUVE>(m_context.compactLayout);

    auto overlay = prism::make<prism::Overlay>();
    overlay->add(window);
    overlay->add(m_loadingOverlay);
    return overlay;
}

prism::Ref<NavItemUVE> HubWindowUVE::MakeNavItemUVE(const std::string& label, const prism::Icon icon,
                                                    const HubPageUVE page) {
    auto item = prism::make<NavItemUVE>(label, icon);
    item->SetCompactUVE(m_context.compactLayout);
    item->onActivate = [this, page] { ShowPageUVE(page); };
    m_navigation.emplace_back(page, item);
    return item;
}

prism::Ref<prism::Widget> HubWindowUVE::BuildBrandHeaderUVE() {
    const bool compact = m_context.compactLayout;
    auto names = prism::vbox(kBrandLineSpacingUVE);
    names->add(prism::make<prism::Label>("UniVex Hub", prism::FontRole::Semibold));
    auto version = prism::make<prism::Label>(
        (compact ? "v" : "Hub ") + Online::FormatDisplayVersionUVE(Online::kHubVersionUVE), prism::FontRole::Small);
    version->setColor(&prism::Theme::textDim);
    names->add(version);

    auto header = prism::hbox(compact ? kRowSpacingUVE : kBrandGapUVE);
    const float inset = compact ? kCompactSidebarInsetUVE : kSidebarInsetUVE;
    header->setPadding(prism::Margin(inset + kBrandInsetUVE, compact ? kCompactSectionSpacingUVE : kSectionSpacingUVE));
    header->add(prism::make<LauncherUI::BrandMarkUVE>(compact ? kCompactBrandMarkSizeUVE : kBrandMarkSizeUVE),
                prism::Slot{0.0F, {}, prism::HAlign::Left, prism::VAlign::Center});
    header->add(names, prism::Slot{1.0F, {}, prism::HAlign::Left, prism::VAlign::Center});
    return header;
}

prism::Ref<prism::Widget> HubWindowUVE::BuildSidebarUVE() {
    const bool compact = m_context.compactLayout;
    const float inset = compact ? kCompactSidebarInsetUVE : kSidebarInsetUVE;
    auto navigation = prism::vbox(kNavSpacingUVE);
    navigation->setPadding(prism::Margin(inset, compact ? kRowSpacingUVE : kNavInsetTopUVE, inset, 0.0F));
    navigation->add(MakeNavItemUVE("Projects", prism::Icon::Folder, HubPageUVE::Projects),
                    prism::Slot{0.0F, {}, prism::HAlign::Fill});
    navigation->add(MakeNavItemUVE("New Project", prism::Icon::Plus, HubPageUVE::NewProject),
                    prism::Slot{0.0F, {}, prism::HAlign::Fill});
    navigation->add(MakeNavItemUVE("Engines", prism::Icon::Layers, HubPageUVE::Engines),
                    prism::Slot{0.0F, {}, prism::HAlign::Fill});

    // Settings is about the Hub itself, not a place to work in, so it sits apart at the bottom.
    auto footer = prism::vbox(kNavSpacingUVE);
    footer->setPadding(prism::Margin(inset, kFooterInsetUVE));
    footer->add(MakeNavItemUVE("Settings", prism::Icon::Settings, HubPageUVE::Settings),
                prism::Slot{0.0F, {}, prism::HAlign::Fill});

    auto sidebar = prism::vbox();
    sidebar->add(BuildBrandHeaderUVE());
    sidebar->add(prism::make<prism::Separator>(prism::Orientation::Horizontal));
    sidebar->add(navigation);
    sidebar->add(prism::make<prism::Spacer>(), prism::Slot{1.0F});
    sidebar->add(prism::make<prism::Separator>(prism::Orientation::Horizontal));
    sidebar->add(footer);

    auto frame = prism::make<prism::Border>();
    frame->setBackground(&prism::Theme::panel).setChild(sidebar);
    frame->setFixedWidth(compact ? kCompactSidebarWidthUVE : kSidebarWidthUVE);
    return frame;
}

prism::Ref<prism::Widget> HubWindowUVE::BuildTopNavUVE() {
    auto brand = prism::hbox(kRowSpacingUVE);
    brand->setPadding(prism::Margin(kCompactPagePaddingUVE, 8.0F, kCompactPagePaddingUVE, 4.0F));
    brand->add(prism::make<LauncherUI::BrandMarkUVE>(kCompactBrandMarkSizeUVE),
               prism::Slot{0.0F, {}, prism::HAlign::Left, prism::VAlign::Center});
    brand->add(prism::make<prism::Label>("UniVex Hub", prism::FontRole::Semibold),
               prism::Slot{1.0F, {}, prism::HAlign::Left, prism::VAlign::Center});
    auto version = prism::make<prism::Label>("v" + Online::FormatDisplayVersionUVE(Online::kHubVersionUVE),
                                             prism::FontRole::Small);
    version->setColor(&prism::Theme::textDim);
    brand->add(version, prism::Slot{0.0F, {}, prism::HAlign::Right, prism::VAlign::Center});

    auto tabs = prism::hbox(4.0F);
    tabs->setPadding(prism::Margin(8.0F, 2.0F, 8.0F, 6.0F));
    tabs->add(MakeNavItemUVE("Projects", prism::Icon::Folder, HubPageUVE::Projects), prism::Slot{1.0F});
    tabs->add(MakeNavItemUVE("New", prism::Icon::Plus, HubPageUVE::NewProject), prism::Slot{1.0F});
    tabs->add(MakeNavItemUVE("Engines", prism::Icon::Layers, HubPageUVE::Engines), prism::Slot{1.0F});
    tabs->add(MakeNavItemUVE("Settings", prism::Icon::Settings, HubPageUVE::Settings), prism::Slot{1.0F});

    auto bar = prism::vbox();
    bar->add(brand);
    bar->add(tabs);

    auto frame = prism::make<prism::Border>();
    frame->setBackground(&prism::Theme::panel).setChild(bar);
    return frame;
}

prism::Ref<prism::Widget> HubWindowUVE::FolderFieldUVE(const prism::Ref<prism::TextInput>& field,
                                                       const std::string& pickerTitle) {
    auto row = prism::hbox(kControlSpacingUVE);
    row->add(field, prism::Slot{1.0F});
    auto browse = prism::make<prism::Button>("Browse", prism::Icon::FolderOpen);
    browse->onClick = [field, pickerTitle] {
        ShowFolderPickerUVE(pickerTitle, field->text(),
                            [field](const std::filesystem::path& chosen) { field->setText(chosen.string(), true); });
    };
    row->add(browse);
    return row;
}

prism::Ref<prism::Widget> HubWindowUVE::BuildProjectsPageUVE() {
    const float pagePadding = m_context.compactLayout ? kCompactPagePaddingUVE : kPagePaddingUVE;
    const float sectionSpacing = m_context.compactLayout ? kCompactSectionSpacingUVE : kSectionSpacingUVE;
    auto page = prism::vbox(sectionSpacing);
    page->setPadding(prism::Margin(pagePadding));

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
    auto refresh =
        prism::make<prism::Button>(m_context.compactLayout ? "" : "Refresh", prism::Icon::Refresh);
    refresh->setTooltip("Refresh projects");
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
    auto browse = prism::make<prism::Button>(m_context.compactLayout ? "Folder" : "Open project folder",
                                             prism::Icon::FolderOpen);
    browse->setTooltip("Browse for a project folder");
    browse->onClick = [this] {
        ShowFolderPickerUVE("Open a project folder", m_settings.defaultProjectLocation,
                            [this](const std::filesystem::path& folder) { OpenProjectFolderUVE(folder); });
    };
    openRow->add(browse);
    page->add(openRow);

    m_projectRows = prism::vbox(kRowSpacingUVE);
    auto scroll = prism::make<prism::ScrollView>();
    scroll->setContent(m_projectRows);
    page->add(scroll, prism::Slot{1.0F});
    return page;
}

prism::Ref<prism::Widget> HubWindowUVE::BuildNewProjectPageUVE() {
    const float pagePadding = m_context.compactLayout ? kCompactPagePaddingUVE : kPagePaddingUVE;
    const float sectionSpacing = m_context.compactLayout ? kCompactSectionSpacingUVE : kSectionSpacingUVE;
    auto page = prism::vbox(sectionSpacing);
    page->setPadding(prism::Margin(pagePadding));
    page->add(HeadingUVE("New Project"));

    m_templateRows = prism::vbox(kRowSpacingUVE);
    m_templateDescription = prism::make<prism::Label>();
    m_templateDescription->setColor(&prism::Theme::textDim);
    m_projectName = prism::make<prism::TextInput>("", "My Game");
    m_projectLocation = prism::make<prism::TextInput>(m_settings.defaultProjectLocation.string());
    m_newProjectEngine = prism::make<prism::ComboBox>();
    const Platform::RendererProfileUVE defaultProfile =
        m_context.compactLayout ? Platform::RendererProfileUVE::Compatibility : Platform::DefaultRendererProfileForHostUVE();
    const std::vector<std::string> rendererLabels{
        std::string(Platform::GetRendererProfileDisplayNameUVE(Platform::RendererProfileUVE::Compatibility)),
        std::string(Platform::GetRendererProfileDisplayNameUVE(Platform::RendererProfileUVE::Mobile)),
        std::string(Platform::GetRendererProfileDisplayNameUVE(Platform::RendererProfileUVE::ForwardPlus)),
    };
    m_newProjectRenderer = prism::make<prism::ComboBox>(rendererLabels, static_cast<int>(defaultProfile));
    m_rendererDescription = prism::make<prism::Label>(
        std::string(Platform::GetRendererProfileDescriptionUVE(defaultProfile)), prism::FontRole::Small);
    m_rendererDescription->setColor(&prism::Theme::textDim);
    m_newProjectRenderer->onChange = [this](const int index) {
        const auto profile = static_cast<Platform::RendererProfileUVE>(std::clamp(index, 0, 2));
        m_rendererDescription->setText(std::string(Platform::GetRendererProfileDescriptionUVE(profile)));
    };
    auto create = prism::make<prism::Button>("Create and Open", prism::Icon::Plus, prism::ButtonStyle::Primary);
    create->onClick = [this] { CreateProjectUVE(); };

    if (m_context.compactLayout) {
        auto stacked = prism::vbox(sectionSpacing);
        stacked->add(m_templateRows);
        stacked->add(m_templateDescription);
        stacked->add(FieldRowUVE("Name", m_projectName, true));
        stacked->add(FieldRowUVE("Location", FolderFieldUVE(m_projectLocation, "Where to create the project"), true));
        stacked->add(FieldRowUVE("Engine", m_newProjectEngine, true));
        stacked->add(FieldRowUVE("Renderer", m_newProjectRenderer, true));
        stacked->add(m_rendererDescription);
        stacked->add(create, prism::Slot{0.0F, {}, prism::HAlign::Fill});
        auto scroll = prism::make<prism::ScrollView>();
        scroll->setContent(stacked);
        page->add(scroll, prism::Slot{1.0F});
        return page;
    }

    auto body = prism::hbox(kSectionSpacingUVE);
    auto templates = prism::make<prism::ScrollView>();
    templates->setContent(m_templateRows);
    templates->setFixedWidth(kTemplateListWidthUVE);
    body->add(templates);

    auto details = prism::vbox(kSectionSpacingUVE);
    details->add(m_templateDescription);
    details->add(FieldRowUVE("Name", m_projectName));
    details->add(FieldRowUVE("Location", FolderFieldUVE(m_projectLocation, "Where to create the project")));
    details->add(FieldRowUVE("Engine", m_newProjectEngine));
    details->add(FieldRowUVE("Renderer", m_newProjectRenderer));
    details->add(m_rendererDescription);
    details->add(create, prism::Slot{0.0F, {}, prism::HAlign::Left});
    body->add(details, prism::Slot{1.0F});

    page->add(body, prism::Slot{1.0F});
    return page;
}

prism::Ref<prism::Widget> HubWindowUVE::BuildSettingsPageUVE() {
    const float pagePadding = m_context.compactLayout ? kCompactPagePaddingUVE : kPagePaddingUVE;
    const float sectionSpacing = m_context.compactLayout ? kCompactSectionSpacingUVE : kSectionSpacingUVE;
    auto page = prism::vbox(sectionSpacing);
    page->setPadding(prism::Margin(pagePadding));
    page->add(HeadingUVE("Settings"));
    m_settingsLocation = prism::make<prism::TextInput>(m_settings.defaultProjectLocation.string());
    m_settingsRecentLimit = prism::make<prism::TextInput>();
    auto hint = prism::make<prism::Label>("Favorites are always kept. Between " +
                                              std::to_string(Pack::kMinimumRecentProjectLimitUVE) + " and " +
                                              std::to_string(Pack::kMaximumRecentProjectLimitUVE) + ".",
                                          prism::FontRole::Small);
    hint->setColor(&prism::Theme::textDim);
    auto save = prism::make<prism::Button>("Save", prism::Icon::Save, prism::ButtonStyle::Primary);
    save->onClick = [this] { SaveSettingsUVE(); };

    if (m_context.compactLayout) {
        auto fields = prism::vbox(sectionSpacing);
        fields->add(FieldRowUVE("New projects in", FolderFieldUVE(m_settingsLocation, "Where new projects go"), true));
        fields->add(FieldRowUVE("Recent projects", m_settingsRecentLimit, true));
        fields->add(hint);
        fields->add(save, prism::Slot{0.0F, {}, prism::HAlign::Left});
        auto scroll = prism::make<prism::ScrollView>();
        scroll->setContent(fields);
        page->add(scroll, prism::Slot{1.0F});
        return page;
    }

    page->add(FieldRowUVE("New projects in", FolderFieldUVE(m_settingsLocation, "Where new projects go")));
    page->add(FieldRowUVE("Recent projects", m_settingsRecentLimit));
    page->add(hint);
    page->add(save, prism::Slot{0.0F, {}, prism::HAlign::Left});

    return page;
}

void HubWindowUVE::ShowPageUVE(const HubPageUVE page) {
    m_pages->setActiveIndex(static_cast<int>(page));
    for (const auto& [itemPage, item] : m_navigation) {
        item->SetActiveUVE(itemPage == page);
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
        Pack::BuildProjectListUVE(m_registry.EntriesUVE(), m_search->text(), m_sort, Core::VersionUVE{});
    if (rows.empty()) {
        auto empty = prism::make<prism::Label>(m_registry.EntriesUVE().empty()
                                                   ? "No projects yet. Create one, or open a .uvproject file."
                                                   : "No project matches the search.");
        empty->setColor(&prism::Theme::textDim);
        m_projectRows->add(empty);
    }
    for (Pack::ProjectListRowUVE row : rows) {
        // The badge describes the engine that would actually open the project.
        if (row.status != Pack::ProjectListStatusUVE::Missing) {
            const Online::InstalledEngineUVE* const engine =
                Online::ChooseEngineForProjectUVE(m_engines, row.entry.engineVersion, row.entry.engineChoice);
            row.status = engine == nullptr
                             ? Pack::ProjectListStatusUVE::EngineNotInstalled
                             : Pack::ProjectListStatusForUVE(row.entry.engineVersion, engine->install.version);
        }
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

    const bool openable = row.status != Pack::ProjectListStatusUVE::Missing &&
                          row.status != Pack::ProjectListStatusUVE::Incompatible &&
                          row.status != Pack::ProjectListStatusUVE::EngineNotInstalled;
    prism::Ref<prism::Widget> primaryAction;
    if (row.status == Pack::ProjectListStatusUVE::EngineNotInstalled) {
        auto getEngine = prism::make<prism::Button>("Get engine", prism::Icon::Open);
        getEngine->setTooltip("Download engine " + Pack::FormatProjectVersionUVE(row.entry.engineVersion) +
                              " on the Engines page");
        getEngine->onClick = [this] { ShowPageUVE(HubPageUVE::Engines); };
        primaryAction = getEngine;
    } else if (openable) {
        auto open = prism::make<prism::Button>("Open", prism::Icon::Play, prism::ButtonStyle::Primary);
        open->onClick = [this, projectFile] { OpenProjectUVE(projectFile); };
        primaryAction = open;
    }

    prism::Ref<prism::Widget> engineChoice;
    if (row.status != Pack::ProjectListStatusUVE::Missing) {
        // "Automatic" lets the Hub pick; otherwise the exact installed version the user chose.
        std::vector<std::string> choices{"Automatic"};
        int selected = 0;
        for (const Online::InstalledEngineUVE& engine : m_engines) {
            choices.push_back("Engine " + engine.install.version.ToStringUVE());
            if (engine.install.version.ToStringUVE() == row.entry.engineChoice) {
                selected = static_cast<int>(choices.size()) - 1;
            }
        }
        auto engine = prism::make<prism::ComboBox>(choices, selected);
        engine->setTooltip("Engine used to open this project");
        engine->onChange = [this, projectFile](const int index) {
            const std::string choice =
                index <= 0 ? std::string{}
                           : m_engines[static_cast<std::size_t>(index - 1)].install.version.ToStringUVE();
            UpdateRegistryUVE([&](Pack::ProjectRegistryUVE& registry) {
                static_cast<void>(registry.SetEngineChoiceUVE(projectFile, choice));
            });
        };
        engineChoice = engine;
    }

    const std::string favoriteLabel =
        m_context.compactLayout ? (row.entry.favorite ? "Starred" : "Star")
                                : (row.entry.favorite ? "Favorite" : "Add to favorites");
    auto favorite = prism::make<prism::Button>(favoriteLabel, prism::Icon::Check, prism::ButtonStyle::Flat);
    favorite->setCheckable(true);
    favorite->setChecked(row.entry.favorite);
    favorite->onClick = [this, projectFile, wasFavorite = row.entry.favorite] {
        UpdateRegistryUVE([&](Pack::ProjectRegistryUVE& registry) {
            static_cast<void>(registry.SetFavoriteUVE(projectFile, !wasFavorite));
        });
    };

    auto remove = prism::make<prism::Button>("", prism::Icon::Trash, prism::ButtonStyle::Flat);
    remove->setTooltip("Remove from the list (the project stays on disk)");
    remove->onClick = [this, projectFile] {
        UpdateRegistryUVE([&](Pack::ProjectRegistryUVE& registry) { static_cast<void>(registry.RemoveUVE(projectFile)); });
    };

    prism::Ref<prism::Widget> content;
    if (m_context.compactLayout) {
        auto top = prism::hbox(kControlSpacingUVE);
        top->add(text, prism::Slot{1.0F});
        if (primaryAction != nullptr) {
            top->add(primaryAction, prism::Slot{0.0F, {}, prism::HAlign::Fill, prism::VAlign::Center});
        }
        auto controls = prism::hbox(kControlSpacingUVE);
        if (engineChoice != nullptr) {
            controls->add(engineChoice, prism::Slot{1.0F, {}, prism::HAlign::Fill, prism::VAlign::Center});
        } else {
            controls->add(prism::make<prism::Spacer>(), prism::Slot{1.0F});
        }
        controls->add(favorite, prism::Slot{0.0F, {}, prism::HAlign::Fill, prism::VAlign::Center});
        controls->add(remove, prism::Slot{0.0F, {}, prism::HAlign::Fill, prism::VAlign::Center});

        auto stacked = prism::vbox(kRowSpacingUVE);
        stacked->add(top);
        stacked->add(controls);
        content = stacked;
    } else {
        auto line = prism::hbox(kControlSpacingUVE);
        line->add(text, prism::Slot{1.0F});
        if (primaryAction != nullptr) {
            line->add(primaryAction, prism::Slot{0.0F, {}, prism::HAlign::Fill, prism::VAlign::Center});
        }
        if (engineChoice != nullptr) {
            line->add(engineChoice, prism::Slot{0.0F, {}, prism::HAlign::Fill, prism::VAlign::Center});
        }
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
        line->add(remove, prism::Slot{0.0F, {}, prism::HAlign::Fill, prism::VAlign::Center});
        content = line;
    }

    auto card = prism::make<prism::Border>();
    card->setBackground(&prism::Theme::panel)
        .setBorder(&prism::Theme::borderSoft)
        .setRadius(kRowRadiusUVE)
        .setPadding(prism::Margin(kRowPaddingUVE))
        .setChild(content);
    return card;
}

void HubWindowUVE::RefreshTemplateListUVE() {
    const Pack::ProjectTemplateDiscoveryUVE discovery =
        Pack::DiscoverProjectTemplatesUVE(m_context.templatesDirectory);
    m_templates = discovery.templates;
    m_templateRows->clearChildren();
    m_templateButtons.clear();
    for (std::size_t index = 0U; index < m_templates.size(); ++index) {
        auto button = prism::make<NavItemUVE>(m_templates[index].displayName, prism::Icon::Cube);
        button->onActivate = [this, index] { SelectTemplateUVE(index); };
        m_templateButtons.push_back(button);
        m_templateRows->add(button, prism::Slot{0.0F, {}, prism::HAlign::Fill});
    }
    if (m_templates.empty()) {
        m_templateDescription->setText("No project templates were found in " +
                                       m_context.templatesDirectory.string() + ".");
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
        m_templateButtons[button]->SetActiveUVE(button == index);
    }
    m_templateDescription->setText(m_templates[index].description);
}

void HubWindowUVE::OpenProjectUVE(const std::filesystem::path& projectFile) {
    const ProjectOpenResultUVE opened = OpenProjectInEngineUVE(
        projectFile, m_engines, m_context.registryFile,
        m_context.registryOverridden ? m_context.registryFile : std::filesystem::path{});
    ReloadRegistryUVE();
    SetStatusUVE(opened.message, !opened.launched);
}

void HubWindowUVE::OpenProjectFolderUVE(const std::filesystem::path& folder) {
    std::vector<std::filesystem::path> manifests;
    std::error_code error;
    for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator(folder, error)) {
        if (entry.path().extension() == Platform::kEditorProjectFileExtensionUVE && entry.is_regular_file(error)) {
            manifests.push_back(entry.path());
        }
    }
    if (manifests.size() != 1U) {
        SetStatusUVE(manifests.empty() ? "That folder has no project file (" +
                                             std::string(Platform::kEditorProjectFileExtensionUVE) + ")."
                                       : "That folder has more than one project file; open one by its path.",
                     true);
        return;
    }
    OpenProjectUVE(manifests.front());
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
    const int engineIndex = m_newProjectEngine->selected();
    if (engineIndex < 0 || static_cast<std::size_t>(engineIndex) >= m_newProjectEngineVersions.size()) {
        SetStatusUVE("No engine version is known yet. Go online once, or download an engine on the Engines page.",
                     true);
        return;
    }
    const Core::VersionUVE engineVersion = m_newProjectEngineVersions[static_cast<std::size_t>(engineIndex)];
    const int rendererIndex = m_newProjectRenderer ? std::clamp(m_newProjectRenderer->selected(), 0, 2)
                                                   : static_cast<int>(Platform::DefaultRendererProfileForHostUVE());
    const auto rendererProfile = static_cast<Platform::RendererProfileUVE>(rendererIndex);
    const std::string displayName = m_projectName->text();
    const Pack::ProjectCreationResultUVE created = Pack::CreateProjectFromTemplateUVE(
        m_templates[m_selectedTemplate], location, m_projectName->text(), engineVersion, rendererProfile);
    if (!created.IsCreatedUVE()) {
        SetStatusUVE(created.message, true);
        return;
    }
    m_projectName->setText("");
    ShowPageUVE(HubPageUVE::Projects);
    if (IsInstalledUVE(engineVersion)) {
        OpenProjectUVE(created.projectFile);
        return;
    }
    // The project is ready; it opens once its engine is downloaded.
    UpdateRegistryUVE([&](Pack::ProjectRegistryUVE& registry) {
        const auto now = std::chrono::duration_cast<std::chrono::seconds>(
                             std::chrono::system_clock::now().time_since_epoch())
                             .count();
        registry.RecordOpenedUVE(created.projectFile, displayName,
                                 {engineVersion.major, engineVersion.minor, engineVersion.patch, engineVersion.build}, now);
    });
    SetStatusUVE("Project created. Download engine " + engineVersion.ToStringUVE() +
                     " on the Engines page to open it.",
                 false);
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

prism::Ref<prism::Widget> HubWindowUVE::BuildEnginesPageUVE() {
    const float pagePadding = m_context.compactLayout ? kCompactPagePaddingUVE : kPagePaddingUVE;
    const float sectionSpacing = m_context.compactLayout ? kCompactSectionSpacingUVE : kSectionSpacingUVE;
    auto page = prism::vbox(sectionSpacing);
    page->setPadding(prism::Margin(pagePadding));

    auto header = prism::hbox(kControlSpacingUVE);
    header->add(HeadingUVE("Engines"), prism::Slot{1.0F, {}, prism::HAlign::Left, prism::VAlign::Center});
    auto check = prism::make<prism::Button>(m_context.compactLayout ? "Check" : "Check for new versions",
                                            prism::Icon::Refresh);
    check->setTooltip("Check for new engine versions");
    check->onClick = [this] { CheckForEnginesUVE(); };
    header->add(check);
    m_importButton =
        prism::make<prism::Button>(m_context.compactLayout ? "Import..." : "Import package...", prism::Icon::Open);
    m_importButton->setTooltip("Install an engine package file you already have, such as an offline copy");
    m_importButton->onClick = [this] { ImportEnginePackageUVE(); };
    header->add(m_importButton);
    page->add(header);

    m_feedStatus = prism::make<prism::Label>("", prism::FontRole::Small);
    m_feedStatus->setColor(&prism::Theme::textDim);
    page->add(m_feedStatus);

    auto progress = prism::hbox(kControlSpacingUVE);
    m_downloadLabel = prism::make<prism::Label>("", prism::FontRole::Small);
    progress->add(m_downloadLabel, prism::Slot{0.0F, {}, prism::HAlign::Left, prism::VAlign::Center});
    m_downloadBar = prism::make<prism::ProgressBar>();
    progress->add(m_downloadBar, prism::Slot{1.0F, {}, prism::HAlign::Fill, prism::VAlign::Center});
    progress->setVisible(false);
    page->add(progress);

    m_engineRows = prism::vbox(kRowSpacingUVE);
    auto scroll = prism::make<prism::ScrollView>();
    scroll->setContent(m_engineRows);
    page->add(scroll, prism::Slot{1.0F});
    return page;
}

prism::Ref<prism::Widget> HubWindowUVE::BuildEngineRowUVE(const std::string& title, const std::string& detail,
                                                          prism::Ref<prism::Widget> action) {
    auto text = prism::vbox();
    text->add(prism::make<prism::Label>(title, prism::FontRole::Semibold));
    auto detailLabel = prism::make<prism::Label>(detail, prism::FontRole::Small);
    detailLabel->setColor(&prism::Theme::textDim);
    text->add(detailLabel);
    auto line = prism::hbox(kControlSpacingUVE);
    line->add(text, prism::Slot{1.0F});
    if (action != nullptr) {
        line->add(std::move(action), prism::Slot{0.0F, {}, prism::HAlign::Fill, prism::VAlign::Center});
    }
    auto card = prism::make<prism::Border>();
    card->setBackground(&prism::Theme::panel)
        .setBorder(&prism::Theme::borderSoft)
        .setRadius(kRowRadiusUVE)
        .setPadding(prism::Margin(kRowPaddingUVE))
        .setChild(line);
    return card;
}

void HubWindowUVE::RefreshInstalledEnginesUVE() {
    std::optional<Online::InstalledEngineUVE> local;
    if (m_context.localEngine.has_value()) {
        local = Online::InstalledEngineUVE{*m_context.localEngine, Online::EngineChannelUVE::Stable, true};
    }
    m_engines = Online::ListInstalledEnginesUVE(m_context.enginesRoot, local);
}

bool HubWindowUVE::IsInstalledUVE(const Core::VersionUVE& version) const {
    return std::any_of(m_engines.begin(), m_engines.end(),
                       [&version](const Online::InstalledEngineUVE& engine) { return engine.install.version == version; });
}

void HubWindowUVE::RefreshNewProjectEnginesUVE() {
    std::vector<std::string> labels;
    m_newProjectEngineVersions.clear();
    for (const Online::InstalledEngineUVE& engine : m_engines) {
        m_newProjectEngineVersions.push_back(engine.install.version);
        labels.push_back("Engine " + engine.install.version.ToStringUVE() + " (installed)");
    }
    for (const Online::EngineReleaseUVE& release : m_feedResult.releases) {
        if (IsInstalledUVE(release.version)) {
            continue;
        }
        m_newProjectEngineVersions.push_back(release.version);
        labels.push_back("Engine " + release.version.ToStringUVE() + " (" +
                         std::string(Online::EngineChannelNameUVE(release.channel)) + ", not downloaded)");
    }
    m_newProjectEngine->setItems(labels);
    m_newProjectEngine->setSelected(labels.empty() ? -1 : 0);
}

void HubWindowUVE::RefreshEngineListUVE() {
    RefreshNewProjectEnginesUVE();
    m_engineRows->clearChildren();
    m_engineRows->add(HeadingUVE("Installed"));
    if (m_engines.empty()) {
        auto none = prism::make<prism::Label>(
            "No engine is downloaded yet. Projects can be created now and opened once their engine is here.");
        none->setColor(&prism::Theme::textDim);
        m_engineRows->add(none);
    }
    for (const Online::InstalledEngineUVE& engine : m_engines) {
        const std::string channel(Online::EngineChannelNameUVE(engine.channel));
        std::string detail = engine.localBuild ? "Local build: " + engine.install.root.string() : engine.install.root.string();
        prism::Ref<prism::Widget> action;
        if (!engine.localBuild) {
            auto remove = prism::make<prism::Button>("Remove", prism::Icon::Trash, prism::ButtonStyle::Flat);
            const Core::VersionUVE version = engine.install.version;
            remove->onClick = [this, version] {
                if (!Online::UninstallEngineUVE(m_context.enginesRoot, version)) {
                    SetStatusUVE("Unable to remove engine " + version.ToStringUVE() + ".", true);
                }
                RefreshInstalledEnginesUVE();
                RefreshEngineListUVE();
                RefreshProjectListUVE();
            };
            action = remove;
        }
        m_engineRows->add(
            BuildEngineRowUVE("Engine " + engine.install.version.ToStringUVE() + "  (" + channel + ")", detail, action));
    }

    if (m_feedResult.state != Online::EngineFeedStateUVE::Online) {
        return;
    }
    const Online::EngineReleaseGroupsUVE groups = Online::GroupEngineReleasesUVE(m_feedResult.releases);
    const auto addRelease = [this](const Online::EngineReleaseUVE& release, const std::string& label) {
        const std::string size = LauncherUI::FormatByteSizeUVE(release.packageBytes);
        prism::Ref<prism::Widget> action;
        if (IsInstalledUVE(release.version)) {
            auto installed = prism::make<prism::Label>("Installed", prism::FontRole::Small);
            installed->setColor(&prism::Theme::success);
            action = installed;
        } else {
            auto download = prism::make<prism::Button>("Download", prism::Icon::Open, prism::ButtonStyle::Primary);
            download->setEnabled(!IsInstallingEngineUVE());
            download->onClick = [this, release] { DownloadEngineUVE(release); };
            action = download;
        }
        m_engineRows->add(BuildEngineRowUVE(label + "  Engine " + release.version.ToStringUVE(),
                                            release.title + " - " + size, action));
    };
    if (groups.stable.has_value() || groups.beta.has_value()) {
        m_engineRows->add(prism::make<prism::Spacer>(0.0F, kRowSpacingUVE));
        m_engineRows->add(HeadingUVE("Latest"));
    }
    if (groups.stable.has_value()) {
        addRelease(*groups.stable, "Stable");
    }
    if (groups.beta.has_value()) {
        addRelease(*groups.beta, "Beta");
    }
    if (!groups.previous.empty()) {
        m_engineRows->add(prism::make<prism::Spacer>(0.0F, kRowSpacingUVE));
        m_engineRows->add(HeadingUVE("Previous versions"));
        for (const Online::EngineReleaseUVE& release : groups.previous) {
            addRelease(release, "Stable");
        }
    }
    m_engineRows->relayout();
}

void HubWindowUVE::CheckForEnginesUVE() {
    if (!m_context.feed.has_value()) {
        m_feedStatus->setText("This Hub has no engine release source configured; installed engines are shown.");
        RefreshEngineListUVE();
        return;
    }
    if (m_feedRequest.valid()) {
        return;
    }
    m_feedStatus->setText("Checking for engine versions...");
    const Online::EngineFeedSourceUVE source = *m_context.feed;
    const std::string target(Platform::GetEnginePackageTargetUVE());
    m_feedRequest = std::async(std::launch::async, [source, target] {
        const std::unique_ptr<Online::IHttpClientUVE> client = Online::CreatePlatformHttpClientUVE();
        return Online::FetchEngineReleasesUVE(*client, source, target);
    });
    RefreshEngineListUVE();
}

void HubWindowUVE::DownloadEngineUVE(const Online::EngineReleaseUVE& release) {
    if (IsInstallingEngineUVE()) {
        return;
    }
    m_download = std::make_unique<Online::EngineDownloadUVE>(Online::CreatePlatformHttpClientUVE(), release,
                                                             m_context.enginesRoot);
    m_downloadLabel->setText("Downloading engine " + release.version.ToStringUVE());
    m_downloadBar->setVisible(true);
    m_downloadBar->parent()->setVisible(true);
    RefreshEngineListUVE();
}

void HubWindowUVE::ImportEnginePackageUVE() {
    if (IsInstallingEngineUVE()) {
        return;
    }
    ShowFilePickerUVE(
        "Import an engine package", m_lastImportFolder, {std::string(Online::kEnginePackageExtensionUVE)}, "Import",
        [this](const std::filesystem::path& package) {
            if (IsInstallingEngineUVE()) {
                return;
            }
            const bool isAndroidTempCopy =
#if defined(__ANDROID__)
                package.parent_path().filename() == "picked";
#else
                false;
#endif
            if (!isAndroidTempCopy) {
                m_lastImportFolder = package.parent_path();
            }
            m_import =
                std::async(std::launch::async, [package, enginesRoot = m_context.enginesRoot, isAndroidTempCopy] {
                    const Online::EngineInstallOutcomeUVE outcome =
                        Online::InstallLocalEnginePackageUVE(package, enginesRoot);
                    if (isAndroidTempCopy) {
                        std::error_code cleanupError;
                        std::filesystem::remove(package, cleanupError);
                    }
                    return outcome;
                });
            m_importButton->setEnabled(false);
            // An import has no measurable progress; the line shows only what is installing.
            m_downloadLabel->setText("Installing " + package.filename().string() + "...");
            m_downloadBar->setVisible(false);
            m_downloadBar->parent()->setVisible(true);
            RefreshEngineListUVE();
        },
        [this](const std::string& message) { SetStatusUVE(message, true); });
}

void HubWindowUVE::StepStartupLoadingUVE() {
    if (!m_loadingOverlay || !m_loadingOverlay->visible()) {
        return;
    }
    const auto now = std::chrono::steady_clock::now();
    if (m_startupBegan == std::chrono::steady_clock::time_point{}) {
        m_startupBegan = now;
    }
    switch (m_startupStep) {
    case 0:
        m_loadingOverlay->SetProgressUVE("Loading installed engines...", 0.30F, 1.0F);
        RefreshInstalledEnginesUVE();
        m_startupStep = 1;
        return;
    case 1:
        m_loadingOverlay->SetProgressUVE("Loading projects...", 0.65F, 1.0F);
        ReloadRegistryUVE();
        m_startupStep = 2;
        return;
    case 2:
        m_loadingOverlay->SetProgressUVE("Preparing templates...", 0.94F, 1.0F);
        RefreshTemplateListUVE();
        // New engine versions only appear when the feed can be reached; offline, nothing is fetched.
        CheckForEnginesUVE();
        m_startupStep = 3;
        return;
    case 3: {
        const double shownSeconds = std::chrono::duration<double>(now - m_startupBegan).count();
        if (shownSeconds < kHubSplashMinSecondsUVE) {
            m_loadingOverlay->SetProgressUVE("Ready", 1.0F, 1.0F);
            return;
        }
        if (m_startupFadeBegan == std::chrono::steady_clock::time_point{}) {
            m_startupFadeBegan = now;
        }
        const double fadeElapsed = std::chrono::duration<double>(now - m_startupFadeBegan).count();
        const float t = static_cast<float>(std::clamp(fadeElapsed / kHubSplashFadeSecondsUVE, 0.0, 1.0));
        const float alpha = 1.0F - SmoothStepUVE(t);
        m_loadingOverlay->SetProgressUVE("Ready", 1.0F, alpha);
        if (t >= 1.0F) {
            m_startupStep = 4;
        }
        return;
    }
    default:
        m_loadingOverlay->SetProgressUVE("Ready", 1.0F, 0.0F);
        return;
    }
}

void HubWindowUVE::TickUVE() {
    StepStartupLoadingUVE();
    if (m_feedRequest.valid() &&
        m_feedRequest.wait_for(std::chrono::seconds::zero()) == std::future_status::ready) {
        m_feedResult = m_feedRequest.get();
        switch (m_feedResult.state) {
        case Online::EngineFeedStateUVE::Online:
            m_feedStatus->setText(m_feedResult.message);
            break;
        case Online::EngineFeedStateUVE::Offline:
            m_feedStatus->setText("You are offline. Installed engines still work; new versions appear when you "
                                  "are back online.");
            break;
        case Online::EngineFeedStateUVE::NotFound:
        case Online::EngineFeedStateUVE::Failed:
            m_feedStatus->setText(m_feedResult.message);
            break;
        }
        RefreshEngineListUVE();
    }
    if (m_download != nullptr) {
        m_downloadBar->setValue(m_download->ProgressUVE());
        const Online::EngineDownloadUVE::StateUVE state = m_download->GetStateUVE();
        if (state != Online::EngineDownloadUVE::StateUVE::Running) {
            const bool installed = state == Online::EngineDownloadUVE::StateUVE::Installed;
            SetStatusUVE(m_download->MessageUVE(), !installed);
            m_download.reset();
            m_downloadBar->parent()->setVisible(false);
            RefreshInstalledEnginesUVE();
            RefreshEngineListUVE();
            RefreshProjectListUVE();
        }
    }
    if (m_import.valid() && m_import.wait_for(std::chrono::seconds::zero()) == std::future_status::ready) {
        const Online::EngineInstallOutcomeUVE outcome = m_import.get();
        SetStatusUVE(outcome.message, !outcome.installed);
        m_importButton->setEnabled(true);
        m_downloadBar->parent()->setVisible(false);
        RefreshInstalledEnginesUVE();
        RefreshEngineListUVE();
        RefreshProjectListUVE();
    }
}

} // namespace UVE::Hub
