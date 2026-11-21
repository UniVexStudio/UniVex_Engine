// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <future>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "hub_setup.h"
#include "launcher_widgets.h"
#include "prism/Containers.h"
#include "prism/Controls.h"
#include "prism/Layouts.h"
#include "prism/TextInput.h"

namespace UVE::Setup {

/// The steps of an installation, in order.
enum class SetupStepUVE : int {
    Welcome = 0,
    Location,
    Installing,
    Done,
};
inline constexpr std::size_t kSetupStepCountUVE = static_cast<std::size_t>(SetupStepUVE::Done) + 1U;

/// What the setup program was started to do.
enum class SetupModeUVE : std::uint8_t {
    Install = 0,
    Uninstall,
};

struct SetupContextUVE final {
    SetupModeUVE mode = SetupModeUVE::Install;
    /// The setup program itself, which carries the payload and becomes the uninstaller.
    std::filesystem::path setupExecutable;
    std::optional<Online::SetupPayloadLocationUVE> payload;
    std::filesystem::path installDirectory;
    /// Where the applications-menu entry goes; empty uses the platform's own place.
    std::filesystem::path entriesRoot;
    std::string version;
};

/// Runs the whole installation off the interface thread: unpack, install, add to the applications
/// menu. Shared by the window and by silent installs.
class SetupJobUVE final {
public:
    enum class StageUVE : std::uint8_t { Unpacking, Installing, Registering, Finished };

    SetupJobUVE(SetupContextUVE context, bool addToApplications);
    SetupJobUVE(const SetupJobUVE&) = delete;
    SetupJobUVE& operator=(const SetupJobUVE&) = delete;

    [[nodiscard]] StageUVE CurrentStageUVE() const noexcept { return m_stage.load(); }
    [[nodiscard]] bool IsFinishedUVE() const;
    /// Valid once finished.
    [[nodiscard]] Online::HubSetupResultUVE ResultUVE();

private:
    [[nodiscard]] Online::HubSetupResultUVE RunUVE(SetupContextUVE context, bool addToApplications);

    std::atomic<StageUVE> m_stage{StageUVE::Unpacking};
    std::future<Online::HubSetupResultUVE> m_future;
    std::optional<Online::HubSetupResultUVE> m_result;
};

/// The installer's window: a rail naming the steps beside the page for the current step.
class SetupWindowUVE final {
public:
    explicit SetupWindowUVE(SetupContextUVE context);

    [[nodiscard]] prism::Ref<prism::Widget> BuildUVE();
    void ShowStepUVE(SetupStepUVE step);
    void TickUVE();

private:
    [[nodiscard]] prism::Ref<prism::Widget> BuildRailUVE();
    [[nodiscard]] prism::Ref<prism::Widget> BuildWelcomePageUVE();
    [[nodiscard]] prism::Ref<prism::Widget> BuildLocationPageUVE();
    [[nodiscard]] prism::Ref<prism::Widget> BuildInstallingPageUVE();
    [[nodiscard]] prism::Ref<prism::Widget> BuildDonePageUVE();
    void RefreshActionsUVE();
    void StartUVE();
    void LaunchHubUVE();

    SetupContextUVE m_context;
    SetupStepUVE m_step = SetupStepUVE::Welcome;
    bool m_existingInstall = false;
    std::unique_ptr<SetupJobUVE> m_job;
    std::optional<Online::HubSetupResultUVE> m_outcome;

    prism::Ref<prism::StackSwitcher> m_pages;
    std::vector<prism::Ref<LauncherUI::NavItemUVE>> m_stepItems;
    prism::Ref<prism::TextInput> m_location;
    prism::Ref<prism::CheckBox> m_addToApplications;
    prism::Ref<prism::Label> m_locationProblem;
    prism::Ref<prism::ProgressBar> m_progress;
    prism::Ref<prism::Label> m_stageLabel;
    prism::Ref<prism::Label> m_doneHeading;
    prism::Ref<prism::Label> m_doneMessage;
    prism::Ref<prism::Button> m_back;
    prism::Ref<prism::Button> m_next;
    prism::Ref<prism::Button> m_cancel;
};

} // namespace UVE::Setup
