// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <filesystem>
#include <utility>
#include <string>
#include <vector>

#include "engine_install.h"
#include "hub_widgets.h"
#include "project_list.h"
#include "project_registry.h"
#include "project_template.h"
#include "prism/Containers.h"
#include "prism/Controls.h"
#include "prism/Layouts.h"
#include "prism/TextInput.h"

namespace UVE::Hub {

/// Where the Hub keeps its state and which engine it launches.
struct HubContextUVE final {
    Pack::EngineInstallUVE engine;
    std::filesystem::path registryFile;
    /// True when `registryFile` was chosen on the command line rather than being the user's own;
    /// the editor is then pointed at the same file.
    bool registryOverridden = false;
    std::filesystem::path settingsFile;
};

enum class HubPageUVE : int {
    Projects = 0,
    NewProject,
    Settings,
};

/// The Hub's window contents: the project list, project creation from templates, and the Hub's
/// own settings. Opening or creating a project starts the engine's editor on it.
class HubWindowUVE final {
public:
    explicit HubWindowUVE(HubContextUVE context);

    /// Builds the widget tree; call once.
    [[nodiscard]] prism::Ref<prism::Widget> BuildUVE();
    void ShowPageUVE(HubPageUVE page);

private:
    [[nodiscard]] prism::Ref<prism::Widget> BuildSidebarUVE();
    [[nodiscard]] prism::Ref<prism::Widget> BuildProjectsPageUVE();
    [[nodiscard]] prism::Ref<prism::Widget> BuildNewProjectPageUVE();
    [[nodiscard]] prism::Ref<prism::Widget> BuildSettingsPageUVE();
    [[nodiscard]] prism::Ref<prism::Widget> BuildProjectRowUVE(const Pack::ProjectListRowUVE& row);
    [[nodiscard]] prism::Ref<NavItemUVE> MakeNavItemUVE(const std::string& label, prism::Icon icon, HubPageUVE page);
    /// A path field with a Browse button that opens the folder picker.
    [[nodiscard]] prism::Ref<prism::Widget> FolderFieldUVE(const prism::Ref<prism::TextInput>& field,
                                                           const std::string& pickerTitle);

    void ReloadRegistryUVE();
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

    HubContextUVE m_context;
    Pack::ProjectRegistryUVE m_registry;
    Pack::LauncherSettingsUVE m_settings;
    std::vector<Pack::ProjectTemplateUVE> m_templates;
    std::size_t m_selectedTemplate = 0U;
    Pack::ProjectListSortUVE m_sort = Pack::ProjectListSortUVE::RecentlyOpened;

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

    prism::Ref<prism::TextInput> m_settingsLocation;
    prism::Ref<prism::TextInput> m_settingsRecentLimit;
};

} // namespace UVE::Hub
