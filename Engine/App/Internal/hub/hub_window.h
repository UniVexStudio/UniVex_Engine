// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <chrono>
#include <filesystem>
#include <future>
#include <memory>
#include <optional>
#include <utility>
#include <string>
#include <string_view>
#include <vector>

#include "engine_install.h"
#include "engine_library.h"
#include "engine_release_feed.h"
#include "launcher_widgets.h"
#include "project_list.h"
#include "project_registry.h"
#include "project_template.h"
#include "prism/Containers.h"
#include "prism/Controls.h"
#include "prism/Layouts.h"
#include "prism/TextInput.h"

namespace UVE::Hub {

using LauncherUI::NavItemUVE;

inline constexpr std::string_view kHubWindowTitleUVE = "UniVex Hub";
/// The Hub's log, beside the project registry.
inline constexpr std::string_view kHubLogFileNameUVE = "uve_hub.log";

/// Where the Hub keeps its state and which engine it launches.
struct HubContextUVE final {
    /// An engine built from source beside the Hub (a development setup), if there is one. An
    /// installed Hub has none; its engines are downloaded into `enginesRoot`.
    std::optional<Pack::EngineInstallUVE> localEngine;
    /// The project templates the Hub ships.
    std::filesystem::path templatesDirectory;
    std::filesystem::path registryFile;
    /// True when `registryFile` was chosen on the command line rather than being the user's own;
    /// the editor is then pointed at the same file.
    bool registryOverridden = false;
    std::filesystem::path settingsFile;
    /// Downloaded engines live here, one folder per version.
    std::filesystem::path enginesRoot;
    /// Where engine releases are published; absent when the Hub ships without a feed.
    std::optional<Online::EngineFeedSourceUVE> feed;
    /// True on single-surface mobile screens: replaces the fixed left sidebar with a compact top
    /// navigation bar and stacks wide forms vertically.
    bool compactLayout = false;
};

enum class HubPageUVE : int {
    Projects = 0,
    NewProject,
    Engines,
    Settings,
};

/// The Hub's window contents: the project list, project creation from templates, and the Hub's
/// own settings. Opening or creating a project starts the engine's editor on it.
class HubWindowUVE final {
public:
    explicit HubWindowUVE(HubContextUVE context);
    ~HubWindowUVE();

    /// Builds the widget tree; call once.
    [[nodiscard]] prism::Ref<prism::Widget> BuildUVE();
    void ShowPageUVE(HubPageUVE page);
    /// Advances background work (release checks, downloads); call once a frame.
    void TickUVE();
    /// Shows a message in the status line.
    void ShowMessageUVE(const std::string& message, bool isError) { SetStatusUVE(message, isError); }

private:
    [[nodiscard]] prism::Ref<prism::Widget> BuildSidebarUVE();
    [[nodiscard]] prism::Ref<prism::Widget> BuildTopNavUVE();
    [[nodiscard]] prism::Ref<prism::Widget> BuildBrandHeaderUVE();
    [[nodiscard]] prism::Ref<prism::Widget> BuildProjectsPageUVE();
    [[nodiscard]] prism::Ref<prism::Widget> BuildNewProjectPageUVE();
    [[nodiscard]] prism::Ref<prism::Widget> BuildSettingsPageUVE();
    [[nodiscard]] prism::Ref<prism::Widget> BuildEnginesPageUVE();
    [[nodiscard]] prism::Ref<prism::Widget> BuildEngineRowUVE(const std::string& title, const std::string& detail,
                                                              prism::Ref<prism::Widget> action);
    [[nodiscard]] prism::Ref<prism::Widget> BuildProjectRowUVE(const Pack::ProjectListRowUVE& row);
    [[nodiscard]] prism::Ref<NavItemUVE> MakeNavItemUVE(const std::string& label, prism::Icon icon, HubPageUVE page);
    /// A path field with a Browse button that opens the folder picker.
    [[nodiscard]] prism::Ref<prism::Widget> FolderFieldUVE(const prism::Ref<prism::TextInput>& field,
                                                           const std::string& pickerTitle);

    void ReloadRegistryUVE();
    void RefreshInstalledEnginesUVE();
    /// The engine versions a new project can be made for: installed ones, then published ones.
    void RefreshNewProjectEnginesUVE();
    void RefreshEngineListUVE();
    void CheckForEnginesUVE();
    void DownloadEngineUVE(const Online::EngineReleaseUVE& release);
    /// Asks for a package file and installs it in the background.
    void ImportEnginePackageUVE();
    /// True while a download or an import is running; only one engine installs at a time.
    [[nodiscard]] bool IsInstallingEngineUVE() const noexcept { return m_download != nullptr || m_import.valid(); }
    [[nodiscard]] bool IsInstalledUVE(const Core::VersionUVE& version) const;
    void RefreshProjectListUVE();
    void RefreshTemplateListUVE();
    void SelectTemplateUVE(std::size_t index);
    /// Applies `change` to a freshly loaded registry and saves it, so other tools' updates survive.
    template <class ChangeUVE> void UpdateRegistryUVE(ChangeUVE&& change);

    void OpenProjectUVE(const std::filesystem::path& projectFile);
    /// Opens the project whose manifest sits in `folder`.
    void OpenProjectFolderUVE(const std::filesystem::path& folder);
    void CreateProjectUVE();
    void SaveSettingsUVE();
    void SetStatusUVE(const std::string& message, bool isError);
    void StepStartupLoadingUVE();

    HubContextUVE m_context;
    Pack::ProjectRegistryUVE m_registry;
    Pack::LauncherSettingsUVE m_settings;
    std::vector<Pack::ProjectTemplateUVE> m_templates;
    std::size_t m_selectedTemplate = 0U;
    Pack::ProjectListSortUVE m_sort = Pack::ProjectListSortUVE::RecentlyOpened;

    std::vector<Online::InstalledEngineUVE> m_engines;
    Online::EngineFeedResultUVE m_feedResult;
    std::future<Online::EngineFeedResultUVE> m_feedRequest;
    std::unique_ptr<Online::EngineDownloadUVE> m_download;
    std::future<Online::EngineInstallOutcomeUVE> m_import;
    /// Where the import picker opens next: the folder of the last imported package.
    std::filesystem::path m_lastImportFolder;

    class HubLoadingOverlayUVE;
    prism::Ref<HubLoadingOverlayUVE> m_loadingOverlay;
    int m_startupStep = 0;
    std::chrono::steady_clock::time_point m_startupBegan{};
    std::chrono::steady_clock::time_point m_startupFadeBegan{};

    prism::Ref<prism::StackSwitcher> m_pages;
    std::vector<std::pair<HubPageUVE, prism::Ref<NavItemUVE>>> m_navigation;
    prism::Ref<prism::Label> m_status;

    prism::Ref<prism::TextInput> m_search;
    prism::Ref<prism::Box> m_projectRows;
    prism::Ref<prism::TextInput> m_openPath;

    prism::Ref<prism::Box> m_templateRows;
    std::vector<prism::Ref<NavItemUVE>> m_templateButtons;
    prism::Ref<prism::Label> m_templateDescription;
    prism::Ref<prism::TextInput> m_projectName;
    prism::Ref<prism::TextInput> m_projectLocation;
    prism::Ref<prism::ComboBox> m_newProjectEngine;
    std::vector<Core::VersionUVE> m_newProjectEngineVersions;
    prism::Ref<prism::ComboBox> m_newProjectRenderer;
    prism::Ref<prism::Label> m_rendererDescription;

    prism::Ref<prism::Box> m_engineRows;
    prism::Ref<prism::Label> m_feedStatus;
    prism::Ref<prism::ProgressBar> m_downloadBar;
    prism::Ref<prism::Label> m_downloadLabel;
    prism::Ref<prism::Button> m_importButton;

    prism::Ref<prism::TextInput> m_settingsLocation;
    prism::Ref<prism::TextInput> m_settingsRecentLimit;
};

} // namespace UVE::Hub
