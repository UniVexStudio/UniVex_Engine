// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "setup_window.h"

#include <array>
#include <chrono>
#include <system_error>
#include <utility>

#include "folder_picker.h"
#include "launcher_files.h"
#include "launcher_window.h"
#include "process_launcher.h"
#include "tar_gz_archive.h"
#include "prism/Theme.h"

namespace UVE::Setup {
namespace {

inline constexpr float kRailWidthUVE = 248.0F;
inline constexpr float kRailPaddingUVE = 24.0F;
inline constexpr float kLogoSizeUVE = 64.0F;
inline constexpr float kBrandTopSpaceUVE = 12.0F;
inline constexpr float kBrandBottomSpaceUVE = 16.0F;
inline constexpr float kBrandLineSpacingUVE = 2.0F;
inline constexpr float kPagePaddingUVE = 32.0F;
inline constexpr float kSectionSpacingUVE = 14.0F;
inline constexpr float kStepSpacingUVE = 2.0F;
inline constexpr float kControlSpacingUVE = 8.0F;
inline constexpr float kActionBarPaddingUVE = 16.0F;
inline constexpr float kListIndentUVE = 12.0F;
/// Share of the bar each stage has reached when it starts; unpacking takes most of the time.
inline constexpr std::array<float, 4U> kStageProgressUVE{0.05F, 0.70F, 0.90F, 1.0F};

/// Each step's name in the rail, per mode; an empty name is a step that mode skips. Steps that are
/// shown are numbered in order.
inline constexpr std::array<std::string_view, kSetupStepCountUVE> kInstallStepNamesUVE{"Welcome", "Location", "Install",
                                                                                         "Done"};
inline constexpr std::array<std::string_view, kSetupStepCountUVE> kUninstallStepNamesUVE{"Confirm", "", "Remove", "Done"};
/// Space between a step's number and its name.
inline constexpr std::string_view kStepNumberSeparatorUVE = "  ";
/// Temporary copy of the payload while it is unpacked.
inline constexpr std::string_view kPayloadScratchPrefixUVE = "univex-hub-setup-";

[[nodiscard]] prism::Ref<prism::Label> HeadingUVE(const std::string& text) {
    return prism::make<prism::Label>(text, prism::FontRole::Semibold);
}

[[nodiscard]] prism::Ref<prism::Label> ParagraphUVE(const std::string& text) {
    auto label = prism::make<prism::Label>(text);
    label->setColor(&prism::Theme::textDim);
    return label;
}

[[nodiscard]] std::string StageTextUVE(const SetupJobUVE::StageUVE stage, const SetupModeUVE mode) {
    if (mode == SetupModeUVE::Uninstall) {
        return "Removing UniVex Hub...";
    }
    switch (stage) {
    case SetupJobUVE::StageUVE::Unpacking:
        return "Unpacking UniVex Hub...";
    case SetupJobUVE::StageUVE::Installing:
        return "Putting the files in place...";
    case SetupJobUVE::StageUVE::Registering:
        return "Adding UniVex Hub to your applications...";
    case SetupJobUVE::StageUVE::Finished:
        return "Finishing...";
    }
    return {};
}

} // namespace

// --- job ----------------------------------------------------------------------------------------

SetupJobUVE::SetupJobUVE(SetupContextUVE context, const bool addToApplications) {
    m_future = std::async(std::launch::async, [this, context = std::move(context), addToApplications]() mutable {
        return RunUVE(std::move(context), addToApplications);
    });
}

bool SetupJobUVE::IsFinishedUVE() const {
    return m_result.has_value() || m_future.wait_for(std::chrono::seconds::zero()) == std::future_status::ready;
}

Online::HubSetupResultUVE SetupJobUVE::ResultUVE() {
    if (!m_result.has_value()) {
        m_result = m_future.get();
    }
    return *m_result;
}

Online::HubSetupResultUVE SetupJobUVE::RunUVE(SetupContextUVE context, const bool addToApplications) {
    if (context.mode == SetupModeUVE::Uninstall) {
        m_stage.store(StageUVE::Installing);
        Online::HubSetupResultUVE result = Online::UninstallHubUVE(context.installDirectory, context.entriesRoot);
        m_stage.store(StageUVE::Finished);
        return result;
    }
    if (!context.payload.has_value()) {
        return {false, "This setup program carries no files to install."};
    }
    const std::filesystem::path archive =
        LauncherUI::UniqueTemporaryPathUVE(kPayloadScratchPrefixUVE, Online::kTarGzFileExtensionUVE);
    const auto removeArchive = [&archive] {
        std::error_code ignored;
        std::filesystem::remove(archive, ignored);
    };
    if (archive.empty() || !Online::ExtractSetupPayloadUVE(context.setupExecutable, *context.payload, archive)) {
        removeArchive();
        return {false, "This setup program is damaged; download it again."};
    }
    m_stage.store(StageUVE::Installing);
    Online::HubSetupResultUVE installed =
        Online::InstallHubUVE(archive, context.installDirectory, context.setupExecutable);
    removeArchive();
    if (!installed.succeeded) {
        return installed;
    }
    if (addToApplications) {
        m_stage.store(StageUVE::Registering);
        const Online::HubSetupResultUVE registered =
            Online::RegisterHubWithDesktopUVE(context.installDirectory, context.version, context.entriesRoot);
        if (!registered.succeeded) {
            installed.message += " " + registered.message;
        }
    }
    m_stage.store(StageUVE::Finished);
    return installed;
}

// --- window -------------------------------------------------------------------------------------

SetupWindowUVE::SetupWindowUVE(SetupContextUVE context) : m_context(std::move(context)) {
    std::error_code error;
    m_existingInstall = std::filesystem::is_regular_file(Online::HubExecutableInUVE(m_context.installDirectory), error);
}

prism::Ref<prism::Widget> SetupWindowUVE::BuildUVE() {
    m_pages = prism::make<prism::StackSwitcher>();
    m_pages->addPage(BuildWelcomePageUVE());
    m_pages->addPage(BuildLocationPageUVE());
    m_pages->addPage(BuildInstallingPageUVE());
    m_pages->addPage(BuildDonePageUVE());

    auto actions = prism::hbox(kControlSpacingUVE);
    actions->setPadding(prism::Margin(kActionBarPaddingUVE));
    m_cancel = prism::make<prism::Button>("Cancel");
    m_cancel->onClick = [] { LauncherUI::RequestLauncherCloseUVE(); };
    actions->add(m_cancel);
    actions->add(prism::make<prism::Spacer>(), prism::Slot{1.0F});
    m_back = prism::make<prism::Button>("Back");
    m_back->onClick = [this] { ShowStepUVE(SetupStepUVE::Welcome); };
    actions->add(m_back);
    m_next = prism::make<prism::Button>("Next", prism::Icon::None, prism::ButtonStyle::Primary);
    m_next->onClick = [this] {
        if (m_step == SetupStepUVE::Welcome && m_context.mode == SetupModeUVE::Install) {
            ShowStepUVE(SetupStepUVE::Location);
        } else if (m_step == SetupStepUVE::Done) {
            if (m_outcome.has_value() && m_outcome->succeeded && m_context.mode == SetupModeUVE::Install) {
                LaunchHubUVE();
            }
            LauncherUI::RequestLauncherCloseUVE();
        } else {
            StartUVE();
        }
    };
    actions->add(m_next);

    auto content = prism::vbox();
    content->add(m_pages, prism::Slot{1.0F});
    content->add(prism::make<prism::Separator>(prism::Orientation::Horizontal));
    content->add(actions);

    auto root = prism::hbox();
    root->add(BuildRailUVE());
    root->add(prism::make<prism::Separator>(prism::Orientation::Vertical));
    root->add(content, prism::Slot{1.0F});
    auto window = prism::make<prism::Border>();
    window->setBackground(&prism::Theme::window).setChild(root);

    ShowStepUVE(SetupStepUVE::Welcome);
    return window;
}

prism::Ref<prism::Widget> SetupWindowUVE::BuildRailUVE() {
    auto rail = prism::vbox(kSectionSpacingUVE);
    rail->setPadding(prism::Margin(kRailPaddingUVE));
    // The brand block sits centred in the rail, above the steps.
    rail->add(prism::make<prism::Spacer>(0.0F, kBrandTopSpaceUVE));
    rail->add(prism::make<LauncherUI::BrandMarkUVE>(kLogoSizeUVE), prism::Slot{0.0F, {}, prism::HAlign::Center});
    auto name = prism::vbox(kBrandLineSpacingUVE);
    auto product = prism::make<prism::Label>("UniVex Hub", prism::FontRole::Semibold);
    product->setAlign(prism::HAlign::Center);
    name->add(product, prism::Slot{0.0F, {}, prism::HAlign::Fill});
    auto version = prism::make<prism::Label>("Hub " + m_context.version, prism::FontRole::Small);
    version->setColor(&prism::Theme::textDim);
    version->setAlign(prism::HAlign::Center);
    name->add(version, prism::Slot{0.0F, {}, prism::HAlign::Fill});
    rail->add(name, prism::Slot{0.0F, {}, prism::HAlign::Fill});
    rail->add(prism::make<prism::Spacer>(0.0F, kBrandBottomSpaceUVE));

    // The steps are a real sequence, so they are numbered.
    auto steps = prism::vbox(kStepSpacingUVE);
    const auto& names = m_context.mode == SetupModeUVE::Install ? kInstallStepNamesUVE : kUninstallStepNamesUVE;
    std::size_t number = 0U;
    for (const std::string_view name : names) {
        const std::string label =
            name.empty() ? std::string{} : std::to_string(++number) + std::string(kStepNumberSeparatorUVE) + std::string(name);
        auto item = prism::make<LauncherUI::NavItemUVE>(label, prism::Icon::None);
        item->setEnabled(false);
        item->setVisible(!name.empty());
        m_stepItems.push_back(item);
        steps->add(item, prism::Slot{0.0F, {}, prism::HAlign::Fill});
    }
    rail->add(steps);

    auto frame = prism::make<prism::Border>();
    frame->setBackground(&prism::Theme::panel).setChild(rail);
    frame->setFixedWidth(kRailWidthUVE);
    return frame;
}

prism::Ref<prism::Widget> SetupWindowUVE::BuildWelcomePageUVE() {
    auto page = prism::vbox(kSectionSpacingUVE);
    page->setPadding(prism::Margin(kPagePaddingUVE));
    if (m_context.mode == SetupModeUVE::Uninstall) {
        page->add(HeadingUVE("Remove UniVex Hub"));
        page->add(ParagraphUVE("UniVex Hub will be removed from " + m_context.installDirectory.string() + "."));
        page->add(ParagraphUVE("Your projects, your project list and the engines you downloaded stay."));
        return page;
    }
    page->add(HeadingUVE(m_existingInstall ? "Update UniVex Hub" : "Install UniVex Hub"));
    page->add(ParagraphUVE("The Hub creates and opens your projects and downloads the engine versions they use."));
    page->add(prism::make<prism::Spacer>(0.0F, kControlSpacingUVE));
    page->add(prism::make<prism::Label>("What is included", prism::FontRole::Medium));
    for (const std::string& item : {"UniVex Hub " + m_context.version, std::string("The project templates"),
                                    std::string("Engines are downloaded from the Hub when you need them")}) {
        auto line = prism::make<prism::Label>(item);
        line->setPadding(prism::Margin(kListIndentUVE, 0.0F, 0.0F, 0.0F));
        page->add(line);
    }
    page->add(prism::make<prism::Spacer>(0.0F, kControlSpacingUVE));
    page->add(ParagraphUVE("It installs for you only; no administrator rights are needed."));
    if (m_existingInstall) {
        page->add(ParagraphUVE("The Hub already installed in " + m_context.installDirectory.string() +
                               " is replaced. Your projects are not touched."));
    }
    if (!m_context.payload.has_value()) {
        auto problem = prism::make<prism::Label>("This setup program carries no files to install; download it again.");
        problem->setColor(&prism::Theme::error);
        page->add(problem);
    }
    return page;
}

prism::Ref<prism::Widget> SetupWindowUVE::BuildLocationPageUVE() {
    auto page = prism::vbox(kSectionSpacingUVE);
    page->setPadding(prism::Margin(kPagePaddingUVE));
    page->add(HeadingUVE("Where should UniVex Hub go?"));
    page->add(ParagraphUVE("The default folder is yours alone. Projects are saved wherever you choose later."));

    auto row = prism::hbox(kControlSpacingUVE);
    m_location = prism::make<prism::TextInput>(m_context.installDirectory.string());
    m_location->onChanged = [this](const std::string&) { m_locationProblem->setText(""); };
    row->add(m_location, prism::Slot{1.0F});
    auto browse = prism::make<prism::Button>("Browse", prism::Icon::FolderOpen);
    browse->onClick = [this] {
        LauncherUI::ShowFolderPickerUVE("Install UniVex Hub in", std::filesystem::path(m_location->text()).parent_path(),
                                        [this](const std::filesystem::path& chosen) {
                                            m_location->setText((chosen / Online::kHubInstallFolderNameUVE).string());
                                        });
    };
    row->add(browse);
    page->add(row);

#if defined(_WIN32)
    m_addToApplications = prism::make<prism::CheckBox>("Add UniVex Hub to the Start menu", true);
#else
    m_addToApplications = prism::make<prism::CheckBox>("Add UniVex Hub to the applications menu", true);
#endif
    page->add(m_addToApplications, prism::Slot{0.0F, {}, prism::HAlign::Left});
    if (m_context.payload.has_value()) {
        page->add(ParagraphUVE("Download size " + LauncherUI::FormatByteSizeUVE(m_context.payload->size) +
                               "; the installed files take a few times that."));
    }
    m_locationProblem = prism::make<prism::Label>("", prism::FontRole::Small);
    m_locationProblem->setColor(&prism::Theme::error);
    page->add(m_locationProblem);
    return page;
}

prism::Ref<prism::Widget> SetupWindowUVE::BuildInstallingPageUVE() {
    auto page = prism::vbox(kSectionSpacingUVE);
    page->setPadding(prism::Margin(kPagePaddingUVE));
    page->add(HeadingUVE(m_context.mode == SetupModeUVE::Install ? "Installing" : "Removing"));
    m_stageLabel = ParagraphUVE("");
    page->add(m_stageLabel);
    m_progress = prism::make<prism::ProgressBar>();
    page->add(m_progress, prism::Slot{0.0F, {}, prism::HAlign::Fill});
    return page;
}

prism::Ref<prism::Widget> SetupWindowUVE::BuildDonePageUVE() {
    auto page = prism::vbox(kSectionSpacingUVE);
    page->setPadding(prism::Margin(kPagePaddingUVE));
    m_doneHeading = HeadingUVE("");
    page->add(m_doneHeading);
    m_doneMessage = ParagraphUVE("");
    page->add(m_doneMessage);
    return page;
}

void SetupWindowUVE::ShowStepUVE(const SetupStepUVE step) {
    m_step = step;
    m_pages->setActiveIndex(static_cast<int>(step));
    for (std::size_t index = 0U; index < m_stepItems.size(); ++index) {
        m_stepItems[index]->SetActiveUVE(static_cast<int>(index) == static_cast<int>(step));
    }
    RefreshActionsUVE();
}

void SetupWindowUVE::RefreshActionsUVE() {
    const bool install = m_context.mode == SetupModeUVE::Install;
    m_back->setVisible(install && m_step == SetupStepUVE::Location);
    m_cancel->setVisible(m_step == SetupStepUVE::Welcome || m_step == SetupStepUVE::Location);
    m_next->setVisible(m_step != SetupStepUVE::Installing);
    switch (m_step) {
    case SetupStepUVE::Welcome:
        m_next->setText(install ? "Next" : "Remove");
        m_next->setEnabled(!install || m_context.payload.has_value());
        break;
    case SetupStepUVE::Location:
        m_next->setText(m_existingInstall ? "Update" : "Install");
        m_next->setEnabled(true);
        break;
    case SetupStepUVE::Installing:
        break;
    case SetupStepUVE::Done:
        m_next->setText(install && m_outcome.has_value() && m_outcome->succeeded ? "Open UniVex Hub" : "Close");
        m_next->setEnabled(true);
        break;
    }
}

void SetupWindowUVE::StartUVE() {
    bool addToApplications = true;
    if (m_context.mode == SetupModeUVE::Install) {
        const std::filesystem::path chosen = m_location->text();
        if (!chosen.is_absolute() || chosen.filename().empty()) {
            m_locationProblem->setText("Enter a full folder path, for example " +
                                       m_context.installDirectory.string() + ".");
            return;
        }
        m_context.installDirectory = chosen;
        addToApplications = m_addToApplications->checked();
    }
    m_job = std::make_unique<SetupJobUVE>(m_context, addToApplications);
    ShowStepUVE(SetupStepUVE::Installing);
}

void SetupWindowUVE::TickUVE() {
    if (m_job == nullptr) {
        return;
    }
    const SetupJobUVE::StageUVE stage = m_job->CurrentStageUVE();
    m_progress->setValue(kStageProgressUVE[static_cast<std::size_t>(stage)]);
    m_stageLabel->setText(StageTextUVE(stage, m_context.mode));
    if (!m_job->IsFinishedUVE()) {
        return;
    }
    m_outcome = m_job->ResultUVE();
    m_job.reset();
    const bool install = m_context.mode == SetupModeUVE::Install;
    if (m_outcome->succeeded) {
        m_doneHeading->setText(install ? "UniVex Hub is ready" : "UniVex Hub was removed");
        m_doneMessage->setText(m_outcome->message);
    } else {
        m_doneHeading->setText(install ? "UniVex Hub was not installed" : "UniVex Hub was not removed");
        m_doneMessage->setText(m_outcome->message);
        m_doneMessage->setColor(&prism::Theme::error);
    }
    ShowStepUVE(SetupStepUVE::Done);
}

void SetupWindowUVE::LaunchHubUVE() {
    static_cast<void>(Platform::LaunchDetachedProcessUVE(
        {Online::HubExecutableInUVE(m_context.installDirectory), {}, m_context.installDirectory}));
}

} // namespace UVE::Setup
