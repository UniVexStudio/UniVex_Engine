// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
#include "editor_retarget_editor.h"

#include "editor.h"
#include "editor_chrome.h"
#include "editor_host.h"
#include "editor_retarget_plan.h"
#include "editor_retarget_diagram.h"

#include "animation_clip_asset.h"
#include "retarget_skeleton.h"

#include "i_project_file_index.h"
#include "retarget_humanoid.h"

#include "prism/Application.h"
#include "prism/Containers.h"
#include "prism/Controls.h"
#include "prism/EngineView.h"
#include "prism/Layouts.h"
#include "prism/Menu.h"
#include "prism/TableView.h"
#include "prism/TextInput.h"
#include "prism/Theme.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <mutex>
#include <string>
#include <system_error>
#include <unordered_map>
#include <utility>
#include <vector>
#include <cstddef>
#include <cstdint>
#include <filesystem>

namespace UVE::Editor {
namespace {

constexpr int kWindowWidthUVE = 1320;
constexpr int kWindowHeightUVE = 800;

[[nodiscard]] prism::Slot CentreUVE() {
    return prism::Slot{0.0F, {}, prism::HAlign::Left, prism::VAlign::Center};
}

[[nodiscard]] std::string LowerUVE(std::string text) {
    std::ranges::transform(text, text.begin(), [](const unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return text;
}

[[nodiscard]] prism::Color StatusColourUVE(const Retarget::JointStatusUVE status) {
    const std::array<float, 3> c = GetRetargetStatusColourUVE(status);
    return prism::Color(static_cast<std::uint8_t>(std::lround(c[0] * 255.0F)), static_cast<std::uint8_t>(std::lround(c[1] * 255.0F)),
                        static_cast<std::uint8_t>(std::lround(c[2] * 255.0F)));
}

[[nodiscard]] const char* AnimationStateLabelUVE(const Retarget::RetargetAnimationStateUVE state) {
    switch (state) {
        case Retarget::RetargetAnimationStateUVE::Ready: return "Ready";
        case Retarget::RetargetAnimationStateUVE::AlreadyConformed: return "Already conformed";
        case Retarget::RetargetAnimationStateUVE::NoSkeleton: return "No skeleton";
        case Retarget::RetargetAnimationStateUVE::NoBones: return "Moves no bones";
        case Retarget::RetargetAnimationStateUVE::Unreadable: return "Cannot read";
    }
    return "";
}

/// A status dot and the text beside it, in a table cell.
void PaintDotCellUVE(prism::Canvas& c, const prism::Rect r, const prism::Color dot, const std::string& text, const bool dim) {
    const prism::Theme& t = prism::theme();
    c.fillCircle(prism::Vec2{r.x + 9.0F, r.y + r.h * 0.5F}, 4.0F, dot);
    c.textIn(*t.ui, prism::Rect{r.x + 20.0F, r.y, r.w - 22.0F, r.h}, text, dim ? t.textDim : t.text, prism::HAlign::Left, true);
}

} // namespace

EditorRetargetWindowUVE::EditorRetargetWindowUVE(EditorUVE& editor, EditorHostUVE& host) : m_editor(editor), m_host(host) {}

EditorRetargetWindowUVE::~EditorRetargetWindowUVE() {
    if (m_window != 0 && m_host.IsReadyUVE()) {
        m_host.CloseWindowUVE(m_window);
    }
}

// ------------------------------------------------------------------------------------- lifetime

void EditorRetargetWindowUVE::SyncUVE() {
    if (!m_host.IsReadyUVE()) {
        return;
    }
    const bool open = m_editor.m_retargetWindow.has_value();
    if (m_window != 0 && !open) {
        m_host.CloseWindowUVE(m_window);
        m_window = 0;
        m_shownPlan = static_cast<std::size_t>(-1);
        m_viewport.reset();
        m_bones.reset();
        m_animations.reset();
        m_kept.reset();
        m_source.reset();
        m_sourceMode.reset();
        m_sourceCaption.reset();
        m_shownSourceMode = -1;
        m_pickedReference = -1;
    }
    if (!open) {
        return;
    }
    if (m_window == 0) {
        m_shownPlan = static_cast<std::size_t>(-1);
        m_window = m_host.OpenWindowUVE(BuildUVE(), "Retarget", kWindowWidthUVE, kWindowHeightUVE);
        if (m_window == 0) {
            m_editor.CloseRetargetWindowUVE();
            return;
        }
        m_host.FocusWindowUVE(m_window);
    }
    RetargetWindowStateUVE& window = *m_editor.m_retargetWindow;
    if (window.focusPending) {
        window.focusPending = false;
        m_host.FocusWindowUVE(m_window);
    }
    if (m_host.WindowCloseRequestedUVE(m_window)) {
        m_host.ClearCloseRequestUVE(m_window);
        m_editor.CloseRetargetWindowUVE();
        return;
    }
    m_editor.TickRetargetWindowUVE();
    m_host.AppUVE().runInWindow(m_window, [this] { RefreshUVE(); });
}

// --------------------------------------------------------------------------------------- layout

prism::Ref<prism::Widget> EditorRetargetWindowUVE::BuildUVE() {
    auto window = prism::vbox();
    window->add(BuildTitleBarUVE());
    window->add(BuildToolbarUVE());

    // Left: the animations, and the character's bones the humanoid has no place for.
    m_animations = prism::make<prism::TableView>();
    m_animations->addColumn(prism::TableColumn{"file", "Animation", 170.0F});
    m_animations->addColumn(prism::TableColumn{"state", "State", 110.0F});
    m_animations->cellText = [this](const int row, const int col) -> std::string {
        if (!m_editor.m_retargetWindow.has_value()) {
            return {};
        }
        const std::vector<RetargetAnimationRowUVE>& rows = m_editor.m_retargetWindow->plan.animations;
        if (row < 0 || static_cast<std::size_t>(row) >= rows.size()) {
            return {};
        }
        const RetargetAnimationRowUVE& r = rows[static_cast<std::size_t>(row)];
        return col == 0 ? r.file.stem().string() : std::string(AnimationStateLabelUVE(r.state));
    };
    m_animations->paintCell = [this](prism::Canvas& c, const int row, const int col, const prism::Rect r, bool) {
        if (!m_editor.m_retargetWindow.has_value() || col != 0) {
            const std::string text = m_animations->cellText(row, col);
            c.textIn(*prism::theme().ui, prism::Rect{r.x + 6.0F, r.y, r.w - 8.0F, r.h}, text, prism::theme().textDim, prism::HAlign::Left, true);
            return;
        }
        const std::vector<RetargetAnimationRowUVE>& rows = m_editor.m_retargetWindow->plan.animations;
        if (row < 0 || static_cast<std::size_t>(row) >= rows.size()) {
            return;
        }
        const bool ready = rows[static_cast<std::size_t>(row)].state == Retarget::RetargetAnimationStateUVE::Ready;
        PaintDotCellUVE(c, r, StatusColourUVE(ready ? Retarget::JointStatusUVE::Good : Retarget::JointStatusUVE::Missing),
                        m_animations->cellText(row, col), false);
    };
    m_kept = prism::make<prism::TableView>();
    m_kept->addColumn(prism::TableColumn{"bone", "Kept as they are", 260.0F});
    m_kept->cellText = [this](const int row, int) -> std::string {
        if (!m_editor.m_retargetWindow.has_value()) {
            return {};
        }
        const std::vector<std::string>& rows = m_editor.m_retargetWindow->plan.keptBones;
        return row >= 0 && static_cast<std::size_t>(row) < rows.size() ? rows[static_cast<std::size_t>(row)] : std::string{};
    };
    // Left, on top: the source, drawn flat in a box of its own (not in the preview).
    m_sourceMode = prism::make<prism::ComboBox>(std::vector<std::string>{"Humanoid skeleton", "Animation's skeleton"}, 0);
    m_sourceMode->setTooltip("Which skeleton the box draws: the humanoid every character is matched to, or the animations' own");
    m_sourceMode->onChange = [this](int) { m_shownSourceMode = -1; };
    auto sourceTitle = prism::make<prism::Label>("SOURCE", prism::FontRole::Small);
    sourceTitle->setColor(prism::ThemeColor(&prism::Theme::textBright));
    m_sourceCaption = prism::make<prism::Label>("", prism::FontRole::Small);
    m_sourceCaption->setColor(prism::ThemeColor(&prism::Theme::textDim));
    auto sourceModeBox = prism::make<prism::SizeBox>();
    sourceModeBox->setWidthOverride(160.0F);
    sourceModeBox->setContent(m_sourceMode);
    auto sourceHeader = prism::hbox(8.0F);
    sourceHeader->setPadding(prism::Margin(8.0F, 5.0F, 6.0F, 5.0F));
    sourceHeader->add(sourceTitle, CentreUVE());
    sourceHeader->add(m_sourceCaption, prism::Slot{1.0F, {}, prism::HAlign::Fill, prism::VAlign::Center});
    sourceHeader->add(sourceModeBox, CentreUVE());
    auto sourceHeaderBar = prism::make<prism::Border>();
    sourceHeaderBar->setBackground(prism::ThemeColor(&prism::Theme::header));
    sourceHeaderBar->setBorder(prism::ThemeColor(&prism::Theme::border), prism::Margin(0.0F, 0.0F, 0.0F, 1.0F));
    sourceHeaderBar->setChild(sourceHeader);
    m_source = prism::make<RetargetSkeletonDiagramUVE>();
    m_source->onPicked = [this](const int joint) {
        int reference = joint;
        if (m_shownSourceMode == 1) {
            reference = joint >= 0 && static_cast<std::size_t>(joint) < m_sourceReference.size() ? m_sourceReference[static_cast<std::size_t>(joint)] : -1;
        }
        PickReferenceBoneUVE(reference, false);
    };
    auto sourceBox = prism::vbox();
    sourceBox->add(sourceHeaderBar);
    sourceBox->add(m_source, prism::Slot{1.0F});

    auto left = prism::make<prism::Splitter>(prism::Orientation::Vertical);
    left->add(PanelUVE(sourceBox), prism::Splitter::Pane{470.0F, 220.0F, 1e9F, true});
    left->add(PanelUVE(m_animations), prism::Splitter::Pane{120.0F, 60.0F, 1e9F, false});
    left->add(PanelUVE(m_kept), prism::Splitter::Pane{130.0F, 60.0F, 1e9F, false});

    // Centre: the character alone, in the preview's own world (the level is not touched).
    m_viewport = prism::make<prism::EngineView>();
    m_viewport->mode = prism::EngineView::Mode::Hole;
    m_viewport->onInput = [](const prism::EngineInput&) {};

    // Right: the humanoid's bones and what answers each.
    m_filter = prism::make<prism::TextInput>("", "Filter bones");
    m_problemsOnly = prism::make<prism::CheckBox>("Problems only", false);
    auto filterRow = prism::hbox(6.0F);
    filterRow->setPadding(prism::Margin(6.0F, 4.0F, 6.0F, 4.0F));
    filterRow->add(m_filter, prism::Slot{1.0F, {}, prism::HAlign::Fill, prism::VAlign::Center});
    filterRow->add(m_problemsOnly, CentreUVE());
    m_bones = prism::make<prism::TableView>();
    m_bones->addColumn(prism::TableColumn{"humanoid", "Humanoid bone", 170.0F});
    m_bones->addColumn(prism::TableColumn{"character", "Character bone", 170.0F});
    m_bones->addColumn(prism::TableColumn{"found", "Found by", 90.0F});
    m_bones->cellText = [this](const int row, const int col) -> std::string {
        if (!m_editor.m_retargetWindow.has_value() || row < 0 || static_cast<std::size_t>(row) >= m_boneRows.size()) {
            return {};
        }
        const RetargetPlanUVE& plan = m_editor.m_retargetWindow->plan;
        const std::size_t index = m_boneRows[static_cast<std::size_t>(row)];
        if (index >= plan.joints.size()) {
            return {};
        }
        const RetargetJointRowUVE& joint = plan.joints[index];
        const Retarget::HumanoidReferenceUVE& reference = Retarget::GetHumanoidReferenceUVE();
        if (col == 0) {
            // One column, all names lined up (no hierarchy indent).
            return reference.skeleton.bones[static_cast<std::size_t>(joint.referenceBone)].name;
        }
        if (col == 1) {
            if (!joint.characterName.empty()) {
                return joint.characterName;
            }
            return joint.optional ? std::string("(optional, not made)") : joint.willMake ? std::string("(generated)") : std::string("(none - renamed only)");
        }
        if (joint.characterName.empty()) {
            return joint.optional ? std::string{} : std::string("Rig Brain");
        }
        if (joint.byBrain) {
            char text[32];
            std::snprintf(text, sizeof(text), "Brain %d%%", static_cast<int>(std::lround(joint.confidence * 100.0F)));
            return text;
        }
        return "Name";
    };
    m_bones->paintCell = [this](prism::Canvas& c, const int row, const int col, const prism::Rect r, bool) {
        const std::string text = m_bones->cellText(row, col);
        if (!m_editor.m_retargetWindow.has_value() || row < 0 || static_cast<std::size_t>(row) >= m_boneRows.size()) {
            return;
        }
        const RetargetJointRowUVE& joint = m_editor.m_retargetWindow->plan.joints[m_boneRows[static_cast<std::size_t>(row)]];
        if (col == 0) {
            PaintDotCellUVE(c, r, StatusColourUVE(joint.status), text, joint.optional && joint.characterName.empty());
            return;
        }
        const prism::Theme& t = prism::theme();
        c.textIn(*t.ui, prism::Rect{r.x + 6.0F, r.y, r.w - 8.0F, r.h}, text, joint.characterName.empty() ? t.textDim : t.text,
                 prism::HAlign::Left, true);
    };
    m_bones->onSelect = [this](const std::vector<int>& rows) {
        if (rows.empty() || !m_editor.m_retargetWindow.has_value()) {
            return;
        }
        const std::size_t row = static_cast<std::size_t>(rows.front());
        const RetargetPlanUVE& plan = m_editor.m_retargetWindow->plan;
        if (row < m_boneRows.size() && m_boneRows[row] < plan.joints.size()) {
            PickReferenceBoneUVE(plan.joints[m_boneRows[row]].referenceBone, true);
        }
    };
    auto right = prism::vbox();
    right->add(filterRow);
    right->add(m_bones, prism::Slot{1.0F});

    auto columns = prism::make<prism::Splitter>(prism::Orientation::Horizontal);
    columns->add(left, prism::Splitter::Pane{330.0F, 220.0F, 700.0F, false});
    columns->add(m_viewport, prism::Splitter::Pane{640.0F, 300.0F, 1e9F, true});
    columns->add(PanelUVE(right), prism::Splitter::Pane{420.0F, 260.0F, 900.0F, false});
    window->add(columns, prism::Slot{1.0F});

    // Footer: what happened, or how far Generate is.
    auto footer = prism::hbox(8.0F);
    footer->setPadding(prism::Margin(8.0F, 4.0F, 8.0F, 4.0F));
    m_progress = prism::make<prism::ProgressBar>();
    auto progressBox = prism::make<prism::SizeBox>();
    progressBox->setWidthOverride(220.0F);
    progressBox->setContent(m_progress);
    footer->add(progressBox, CentreUVE());
    m_status = prism::make<prism::Label>("", prism::FontRole::Small);
    m_status->setColor(prism::ThemeColor(&prism::Theme::textDim));
    footer->add(m_status, prism::Slot{1.0F, {}, prism::HAlign::Fill, prism::VAlign::Center});
    auto footerBar = prism::make<prism::Border>();
    footerBar->setBackground(prism::ThemeColor(&prism::Theme::statusBar));
    footerBar->setBorder(prism::ThemeColor(&prism::Theme::border), prism::Margin(0.0F, 1.0F, 0.0F, 0.0F));
    footerBar->setChild(footer);
    window->add(footerBar);

    auto frame = prism::make<prism::Border>();
    frame->setBackground(prism::ThemeColor(&prism::Theme::window));
    frame->setBorder(prism::ThemeColor(&prism::Theme::border), prism::Margin(1.0F));
    frame->setChild(window);
    return frame;
}

prism::Ref<prism::Widget> EditorRetargetWindowUVE::BuildTitleBarUVE() {
    auto row = prism::hbox();
    auto menus = prism::make<prism::MenuBar>();
    menus->setShowLogo(false);
    menus->setItemPadding(12.0F);
    menus->addMenu("File", [this] {
        std::vector<prism::MenuItem> items;
        prism::MenuItem generate = prism::MenuItem::Item("Generate Retarget", [this] { static_cast<void>(m_editor.StartRetargetUVE()); }, "",
                                                         prism::Icon::Play);
        generate.enabled = m_editor.m_retargetWindow.has_value() && m_editor.m_retargetWindow->plan.CanGenerateUVE();
        items.push_back(std::move(generate));
        prism::MenuItem undo = prism::MenuItem::Item("Undo Last", [this] { static_cast<void>(m_editor.UndoLastRetargetUVE()); }, "",
                                                     prism::Icon::Undo);
        undo.enabled = m_editor.m_retargetWindow.has_value() && !m_editor.m_retargetWindow->lastBackup.empty();
        items.push_back(std::move(undo));
        items.push_back(prism::MenuItem::Sep());
        items.push_back(prism::MenuItem::Item("Close", [this] { m_editor.CloseRetargetWindowUVE(); }, "", prism::Icon::Close));
        return items;
    });
    menus->addMenu("View", [this] {
        std::vector<prism::MenuItem> items;
        items.push_back(prism::MenuItem::Check("Problems only", m_problemsOnly && m_problemsOnly->checked(), [this] {
            if (m_problemsOnly) {
                m_problemsOnly->setChecked(!m_problemsOnly->checked());
            }
        }));
        return items;
    });
    menus->addMenu("Help", [] {
        return std::vector<prism::MenuItem>{
            prism::MenuItem::Item("Green: found and fine. Yellow: found, look at it. Red: broken. Grey: missing.", {}).disabled(),
            prism::MenuItem::Item("The Source box draws the skeleton the character answers; click a joint to find it everywhere.", {}).disabled(),
            prism::MenuItem::Item("Generate keeps the character's own rest pose: no vertex moves.", {}).disabled(),
            prism::MenuItem::Item("Missing core bones (root, IK, sockets ...) are made; correctives are optional.", {}).disabled(),
            prism::MenuItem::Item("The Rig Brain finds bones whose names say nothing; every bone takes the engine's name.", {}).disabled()};
    });
    row->add(menus, CentreUVE());
    row->add(prism::make<DragStripUVE>(), prism::Slot{1.0F});
    AddWindowButtonsUVE(*row, [this] { m_editor.CloseRetargetWindowUVE(); });

    auto tabRow = prism::hbox();
    m_tab = prism::make<DocumentTabUVE>();
    m_tab->setFallbackIcon(prism::Icon::Skeleton);
    m_tab->setName("Retarget");
    tabRow->add(m_tab, prism::Slot{0.0F, {}, prism::HAlign::Left, prism::VAlign::Fill});
    tabRow->add(prism::make<DragStripUVE>(true), prism::Slot{1.0F});
    auto titleRow = prism::make<prism::SizeBox>();
    titleRow->setHeightOverride(kTitleRowUVE);
    titleRow->setContent(row);
    auto tabRowBox = prism::make<prism::SizeBox>();
    tabRowBox->setHeightOverride(kTabRowUVE);
    tabRowBox->setContent(tabRow);
    auto rightColumn = prism::vbox();
    rightColumn->add(titleRow);
    rightColumn->add(tabRowBox);
    auto top = prism::hbox();
    top->add(prism::make<LogoBoxUVE>());
    top->add(rightColumn, prism::Slot{1.0F});
    auto bar = prism::make<prism::Border>();
    bar->setBackground(prism::ThemeColor(&prism::Theme::header));
    bar->setBorder(prism::ThemeColor(&prism::Theme::border), prism::Margin(0.0F, 0.0F, 0.0F, 1.0F));
    bar->setChild(top);
    return bar;
}

prism::Ref<prism::Widget> EditorRetargetWindowUVE::BuildToolbarUVE() {
    auto row = prism::hbox(6.0F);
    row->setPadding(prism::Margin(8.0F, 4.0F, 8.0F, 4.0F));
    auto label = prism::make<prism::Label>("Character");
    row->add(label, CentreUVE());
    m_character = prism::make<prism::ComboBox>(std::vector<std::string>{"Animation skeleton only (no mesh)"}, 0);
    m_character->setTooltip("The character the animations are for: its bones take the engine's names, its rest pose is kept");
    m_character->onChange = [this](const int index) {
        if (!m_editor.m_retargetWindow.has_value() || m_editor.m_retargetWindow->job.valid()) {
            return;
        }
        RetargetWindowStateUVE& window = *m_editor.m_retargetWindow;
        const std::filesystem::path target =
            index <= 0 || static_cast<std::size_t>(index) > m_candidates.size() ? std::filesystem::path{} : m_candidates[static_cast<std::size_t>(index) - 1U];
        if (target != window.target) {
            window.target = target;
            window.planStale = true;
        }
    };
    auto characterBox = prism::make<prism::SizeBox>();
    characterBox->setWidthOverride(320.0F);
    characterBox->setContent(m_character);
    row->add(characterBox, CentreUVE());
    row->add(HSeparatorUVE(), CentreUVE());
    m_legend = prism::make<prism::Label>("", prism::FontRole::Small);
    m_legend->setColor(prism::ThemeColor(&prism::Theme::textDim));
    row->add(m_legend, prism::Slot{1.0F, {}, prism::HAlign::Fill, prism::VAlign::Center});
    m_generate = prism::make<prism::Button>("Generate Retarget", prism::Icon::Play, prism::ButtonStyle::Primary);
    m_generate->setTooltip("Conform the character and the animations in place (the originals are saved first; Undo Last puts them back)");
    m_generate->onClick = [this] { static_cast<void>(m_editor.StartRetargetUVE()); };
    row->add(m_generate, CentreUVE());
    m_undo = prism::make<prism::Button>("Undo Last", prism::Icon::Undo, prism::ButtonStyle::Toolbar);
    m_undo->setTooltip("Put the last run's originals back");
    m_undo->onClick = [this] { static_cast<void>(m_editor.UndoLastRetargetUVE()); };
    row->add(m_undo, CentreUVE());
    auto bar = prism::make<prism::Border>();
    bar->setBackground(prism::ThemeColor(&prism::Theme::header));
    bar->setBorder(prism::ThemeColor(&prism::Theme::border), prism::Margin(0.0F, 0.0F, 0.0F, 1.0F));
    bar->setChild(row);
    return bar;
}

// --------------------------------------------------------------------------------------- refresh

void EditorRetargetWindowUVE::RebuildRowsUVE() {
    m_boneRows.clear();
    if (!m_editor.m_retargetWindow.has_value()) {
        return;
    }
    const RetargetPlanUVE& plan = m_editor.m_retargetWindow->plan;
    const Retarget::HumanoidReferenceUVE& reference = Retarget::GetHumanoidReferenceUVE();
    const std::string filter = m_filter ? LowerUVE(m_filter->text()) : std::string{};
    const bool problemsOnly = m_problemsOnly && m_problemsOnly->checked();
    for (std::size_t index = 0U; index < plan.joints.size(); ++index) {
        const RetargetJointRowUVE& joint = plan.joints[index];
        const bool problem = joint.status == Retarget::JointStatusUVE::Warning || joint.status == Retarget::JointStatusUVE::Broken;
        if (problemsOnly && !problem) {
            continue;
        }
        if (!filter.empty() && LowerUVE(reference.skeleton.bones[static_cast<std::size_t>(joint.referenceBone)].name).find(filter) == std::string::npos &&
            LowerUVE(joint.characterName).find(filter) == std::string::npos) {
            continue;
        }
        m_boneRows.push_back(index);
    }
    if (m_bones) {
        m_bones->setRowCount(static_cast<int>(m_boneRows.size()));
    }
}

void EditorRetargetWindowUVE::RefreshUVE() {
    if (!m_editor.m_retargetWindow.has_value()) {
        return;
    }
    RetargetWindowStateUVE& window = *m_editor.m_retargetWindow;
    const RetargetPlanUVE& plan = window.plan;
    const bool running = window.job.valid();

    // The character picker follows the project's rigged characters.
    const std::vector<std::filesystem::path> candidates = m_editor.RetargetCandidatesUVE();
    if (candidates != m_candidates && m_character) {
        m_candidates = candidates;
        std::vector<std::string> items{"Animation skeleton only (no mesh)"};
        for (const std::filesystem::path& candidate : m_candidates) {
            items.push_back(candidate.generic_string());
        }
        m_character->setItems(std::move(items));
    }
    if (m_character) {
        int selected = 0;
        for (std::size_t i = 0U; i < m_candidates.size(); ++i) {
            if (m_candidates[i] == window.target) {
                selected = static_cast<int>(i) + 1;
            }
        }
        if (m_character->selected() != selected) {
            m_character->setSelected(selected);
        }
        m_character->setEnabled(!running);
    }

    const bool filterChanged = (m_filter && m_filter->text() != m_shownFilter) ||
                               (m_problemsOnly && m_problemsOnly->checked() != m_shownProblemsOnly);
    if (window.planSerial != m_shownPlan || filterChanged) {
        m_shownPlan = window.planSerial;
        m_shownFilter = m_filter ? m_filter->text() : std::string{};
        m_shownProblemsOnly = m_problemsOnly && m_problemsOnly->checked();
        RebuildRowsUVE();
        if (m_animations) {
            m_animations->setRowCount(static_cast<int>(plan.animations.size()));
        }
        if (m_kept) {
            m_kept->setRowCount(static_cast<int>(plan.keptBones.size()));
        }
        RebuildSourceUVE(true);
        if (m_legend) {
            char text[256];
            if (plan.modelReadable) {
                std::snprintf(text, sizeof(text), "%zu good    %zu check    %zu broken    %zu missing (%zu made, the rest optional)%s",
                              plan.CountUVE(Retarget::JointStatusUVE::Good), plan.CountUVE(Retarget::JointStatusUVE::Warning),
                              plan.CountUVE(Retarget::JointStatusUVE::Broken), plan.CountUVE(Retarget::JointStatusUVE::Missing),
                              plan.missingCore, plan.usedBrain ? "    Rig Brain on" : "    Rig Brain learning...");
            } else {
                std::snprintf(text, sizeof(text), "%s",
                              window.target.empty() && plan.animations.empty() ? "Pick a character or an animation." : plan.modelError.c_str());
            }
            m_legend->setText(text);
        }
    }
    if (m_sourceMode && m_sourceMode->selected() != m_shownSourceMode) {
        RebuildSourceUVE(true);
    }
    if (m_generate) {
        m_generate->setEnabled(!running && plan.CanGenerateUVE() && m_editor.IsAuthoringCommandAllowedUVE());
        const std::string label = plan.modelReadable && plan.CharacterDoneUVE()
                                      ? (plan.ReadyAnimationsUVE() > 0U ? "Retarget Animations" : "Already Retargeted")
                                      : plan.renameOnly ? "Generate (Rename Only)" : "Generate Retarget";
        if (m_generate->text() != label) {
            m_generate->setText(label);
        }
        m_generate->setTooltip(plan.CharacterDoneUVE() ? "This character is retargeted already: Undo Last, or import it again, to retarget it anew"
                               : plan.renameOnly       ? "The character has every bone, helper and IK target: its bones only take the engine's names"
                                                       : "Adds the bones the character lacks, then names every bone as the engine's");
    }
    if (m_undo) {
        m_undo->setEnabled(!running && !window.lastBackup.empty());
    }
    if (m_progress && m_status) {
        if (running) {
            const std::size_t done = window.progress->done;
            const std::size_t total = std::max<std::size_t>(1U, window.progress->total);
            std::string what;
            {
                const std::scoped_lock lock(window.progress->mutex);
                what = window.progress->what;
            }
            m_progress->setVisible(true);
            m_progress->setValue(static_cast<float>(done) / static_cast<float>(total));
            m_status->setText("Retargeting " + what + " ...");
        } else {
            m_progress->setVisible(false);
            std::string text = window.status;
            if (text.empty() && plan.modelReadable) {
                char line[192];
                if (plan.CharacterDoneUVE()) {
                    std::snprintf(line, sizeof(line), "The character is retargeted already (Undo Last or import it again to redo it); %zu of %zu animations will be conformed.",
                                  plan.ReadyAnimationsUVE(), plan.animations.size());
                } else if (plan.renameOnly) {
                    std::snprintf(line, sizeof(line), "Rename only: every bone, helper and IK target is there, no bone is added; %zu of %zu animations will be conformed.",
                                  plan.ReadyAnimationsUVE(), plan.animations.size());
                } else {
                    std::snprintf(line, sizeof(line), "%zu bones will be added; %zu of %zu animations will be conformed; the mesh keeps its rest pose.",
                                  plan.bonesToAdd, plan.ReadyAnimationsUVE(), plan.animations.size());
                }
                text = line;
            }
            m_status->setText(text);
            m_status->setColor(window.statusIsError ? prism::ThemeColor(&prism::Theme::error) : prism::ThemeColor(&prism::Theme::textDim));
        }
    }
}

// ----------------------------------------------------------------------------------------- source

void EditorRetargetWindowUVE::RebuildSourceUVE(const bool refit) {
    if (!m_source || !m_editor.m_retargetWindow.has_value()) {
        return;
    }
    const RetargetWindowStateUVE& window = *m_editor.m_retargetWindow;
    const RetargetPlanUVE& plan = window.plan;
    const Retarget::HumanoidReferenceUVE& reference = Retarget::GetHumanoidReferenceUVE();
    m_shownSourceMode = m_sourceMode ? m_sourceMode->selected() : 0;
    m_sourceReference.clear();
    const auto detailOf = [&plan](const std::size_t ref) -> std::string {
        if (ref >= plan.joints.size()) {
            return {};
        }
        const RetargetJointRowUVE& joint = plan.joints[ref];
        if (joint.characterName.empty()) {
            return joint.optional   ? "optional - the character has none"
                   : joint.willMake ? "the character has none - Generate makes it"
                                    : "the character has none - left out (the rig is all but complete: renamed only)";
        }
        std::string text = "character: " + joint.characterName;
        if (joint.byBrain) {
            char sure[48];
            std::snprintf(sure, sizeof(sure), "  (Rig Brain, %d%% sure)", static_cast<int>(std::lround(joint.confidence * 100.0F)));
            text += sure;
        }
        if (!joint.reason.empty() && joint.status != Retarget::JointStatusUVE::Good) {
            text += " - " + joint.reason;
        }
        return text;
    };
    const auto colourOf = [&plan](const std::size_t ref) {
        return GetRetargetStatusColourUVE(ref < plan.joints.size() ? plan.joints[ref].status : Retarget::JointStatusUVE::Missing);
    };
    std::vector<RetargetSkeletonDiagramUVE::JointUVE> joints;
    std::string caption;
    if (m_shownSourceMode == 0) {
        // The humanoid in its rest pose (the A-pose), as the engine knows it.
        const std::vector<Retarget::WorldTransformUVE> world = Retarget::ComputeWorldTransformsUVE(reference.skeleton);
        for (std::size_t bone = 0U; bone < reference.skeleton.bones.size() && bone < world.size(); ++bone) {
            RetargetSkeletonDiagramUVE::JointUVE joint;
            joint.name = reference.skeleton.bones[bone].name;
            joint.parent = reference.skeleton.bones[bone].parent;
            joint.x = world[bone].position.x;
            joint.y = world[bone].position.y;
            joint.colour = colourOf(bone);
            const bool answered = bone < plan.joints.size() && !plan.joints[bone].characterName.empty();
            joint.faint = !answered;
            joint.hidden = bone < reference.info.size() && reference.info[bone].optional && !answered;
            if (bone < reference.info.size()) {
                const Retarget::HumanoidBoneKindUVE kind = reference.info[bone].kind;
                joint.helper = kind == Retarget::HumanoidBoneKindUVE::Root || kind == Retarget::HumanoidBoneKindUVE::IK ||
                               kind == Retarget::HumanoidBoneKindUVE::Socket;
            }
            joint.detail = detailOf(bone);
            joints.push_back(std::move(joint));
        }
        caption = "Humanoid - " + std::to_string(reference.skeleton.bones.size()) + " bones";
    } else {
        // The animations' own skeleton: the first one that carries its rest pose.
        std::unordered_map<std::string, std::size_t> referenceOf;
        for (std::size_t ref = 0U; ref < plan.joints.size(); ++ref) {
            if (!plan.joints[ref].characterName.empty()) {
                referenceOf.emplace(plan.joints[ref].characterName, ref);
            }
        }
        for (std::size_t ref = 0U; ref < reference.skeleton.bones.size(); ++ref) {
            referenceOf.emplace(reference.skeleton.bones[ref].name, ref);
        }
        for (const std::filesystem::path& file : window.animations) {
            Asset::AnimationClipAssetUVE clip;
            if (!Asset::LoadAnimationClipAssetUVE(file, clip) || clip.rest.empty()) {
                continue;
            }
            Retarget::RetargetSkeletonUVE rig;
            for (const Asset::AnimationAssetRestBoneUVE& rest : clip.rest) {
                Retarget::RetargetBoneUVE bone;
                bone.name = rest.bone;
                bone.parent = rest.parent;
                bone.position = rest.position;
                bone.rotation = rest.rotation;
                bone.scale = rest.scale.x;
                rig.bones.push_back(std::move(bone));
            }
            const std::vector<Retarget::WorldTransformUVE> world = Retarget::ComputeWorldTransformsUVE(rig);
            // A rig made standing on Z is drawn standing up all the same.
            float spanY = 0.0F;
            float spanZ = 0.0F;
            {
                float lowY = 1e30F, highY = -1e30F, lowZ = 1e30F, highZ = -1e30F;
                for (const Retarget::WorldTransformUVE& w : world) {
                    lowY = std::min(lowY, w.position.y);
                    highY = std::max(highY, w.position.y);
                    lowZ = std::min(lowZ, w.position.z);
                    highZ = std::max(highZ, w.position.z);
                }
                spanY = highY - lowY;
                spanZ = highZ - lowZ;
            }
            const bool zUp = spanZ > spanY * 1.2F;
            for (std::size_t bone = 0U; bone < rig.bones.size() && bone < world.size(); ++bone) {
                RetargetSkeletonDiagramUVE::JointUVE joint;
                joint.name = rig.bones[bone].name;
                joint.parent = rig.bones[bone].parent;
                joint.x = world[bone].position.x;
                joint.y = zUp ? world[bone].position.z : world[bone].position.y;
                const auto found = referenceOf.find(joint.name);
                const int ref = found == referenceOf.end() ? -1 : static_cast<int>(found->second);
                m_sourceReference.push_back(ref);
                if (ref >= 0) {
                    const Retarget::HumanoidBoneKindUVE kind = reference.info[static_cast<std::size_t>(ref)].kind;
                    joint.helper = kind == Retarget::HumanoidBoneKindUVE::Root || kind == Retarget::HumanoidBoneKindUVE::IK ||
                                   kind == Retarget::HumanoidBoneKindUVE::Socket;
                    joint.colour = colourOf(static_cast<std::size_t>(ref));
                    joint.detail = "humanoid: " + reference.skeleton.bones[static_cast<std::size_t>(ref)].name;
                } else {
                    joint.colour = {0.42F, 0.46F, 0.55F};
                    joint.detail = "kept as it is";
                }
                joints.push_back(std::move(joint));
            }
            caption = file.stem().string() + " - " + std::to_string(rig.bones.size()) + " bones";
            break;
        }
    }
    m_source->SetEmptyTextUVE(m_shownSourceMode == 0 ? "No humanoid reference." : "No animation with its rest pose to draw.");
    m_source->SetJointsUVE(std::move(joints), refit);
    if (m_sourceCaption) {
        m_sourceCaption->setText(caption);
    }
    std::vector<std::pair<std::array<float, 3>, std::string>> legend;
    const std::array<std::pair<Retarget::JointStatusUVE, const char*>, 4> keys{
        std::pair{Retarget::JointStatusUVE::Good, "good"}, std::pair{Retarget::JointStatusUVE::Warning, "check"},
        std::pair{Retarget::JointStatusUVE::Broken, "broken"}, std::pair{Retarget::JointStatusUVE::Missing, "missing"}};
    for (const auto& [status, label] : keys) {
        legend.emplace_back(GetRetargetStatusColourUVE(status), std::to_string(plan.CountUVE(status)) + " " + label);
    }
    m_source->SetLegendUVE(std::move(legend));
    // Keep the pick across a rebuild.
    PickReferenceBoneUVE(m_pickedReference, true);
}

void EditorRetargetWindowUVE::PickReferenceBoneUVE(const int reference, const bool fromList) {
    if (!m_editor.m_retargetWindow.has_value()) {
        return;
    }
    const RetargetPlanUVE& plan = m_editor.m_retargetWindow->plan;
    m_pickedReference = reference;
    if (m_source) {
        int joint = -1;
        if (m_shownSourceMode == 1) {
            for (std::size_t i = 0U; i < m_sourceReference.size(); ++i) {
                if (reference >= 0 && m_sourceReference[i] == reference) {
                    joint = static_cast<int>(i);
                    break;
                }
            }
        } else {
            joint = reference;
        }
        m_source->SetSelectedUVE(joint);
    }
    if (!fromList && m_bones) {
        for (std::size_t row = 0U; row < m_boneRows.size(); ++row) {
            if (m_boneRows[row] < plan.joints.size() && plan.joints[m_boneRows[row]].referenceBone == reference) {
                m_bones->select({static_cast<int>(row)});
                m_bones->ensureVisible(static_cast<int>(row));
                break;
            }
        }
    }
    // The preview lights the character's bone that answers it.
    if (m_editor.m_retargetPreview.has_value()) {
        m_editor.m_retargetPreview->highlightBone =
            reference >= 0 && static_cast<std::size_t>(reference) < plan.joints.size() ? plan.joints[static_cast<std::size_t>(reference)].characterName
                                                                                       : std::string{};
    }
}

// ---------------------------------------------------------------------------------------- render

void EditorRetargetWindowUVE::RenderUVE() {
    if (m_window == 0 || !m_editor.m_retargetWindow.has_value()) {
        return;
    }
    std::vector<EditorHostUVE::ViewImageUVE> views;
    if (m_viewport && m_viewport->visible()) {
        const prism::Rect rect = m_viewport->rect();
        if (rect.w >= 1.0F && rect.h >= 1.0F) {
            const EditorViewportInputUVE input = m_host.CaptureViewInputUVE(m_window, rect);
            Math::Vector2UVE used{0.0F, 0.0F};
            const std::uint64_t texture = m_editor.RenderRetargetPreviewUVE(Math::Vector2UVE{std::floor(rect.w), std::floor(rect.h)}, used, input);
            m_host.ApplyViewCursorUVE(m_window, input);
            if (texture != 0U && used.x > 0.0F && used.y > 0.0F) {
                EditorHostUVE::ViewImageUVE view{};
                view.rect = prism::Rect{std::floor(rect.x), std::floor(rect.y), used.x, used.y};
                view.texture = static_cast<std::uint32_t>(texture);
                view.usedWidth = used.x;
                view.usedHeight = used.y;
                view.textureWidth = used.x;
                view.textureHeight = used.y;
                views.push_back(view);
            }
        }
    }
    m_host.RenderWindowUVE(m_window, views);
}

} // namespace UVE::Editor
