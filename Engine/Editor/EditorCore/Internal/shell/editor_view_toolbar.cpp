// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
#include "editor_view_toolbar.h"

#include "editor.h"
#include "editor_chrome.h"
#include "editor_touch_controls.h"

#include "prism/Application.h"
#include "prism/Containers.h"
#include "prism/Controls.h"
#include "prism/Layouts.h"
#include "prism/Menu.h"
#include "prism/Theme.h"
#include "prism/Views.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>
#include <cstddef>
#include <utility>

namespace UVE::Editor {
namespace {

[[nodiscard]] prism::Color ToPrismColor(const float r, const float g, const float b) {
    const auto c = [](const float v) { return static_cast<std::uint8_t>(std::lround(std::clamp(v, 0.0F, 1.0F) * 255.0F)); };
    return prism::Color(c(r), c(g), c(b), 255);
}

} // namespace

EditorViewToolbar::EditorViewToolbar(Editor& editor) : m_editor(editor) {}

prism::Ref<prism::Widget> EditorViewToolbar::Build(const bool gizmoOnly, prism::Ref<prism::Widget> extra) {
    // Sub row 2 (full width): gizmo mode, snapping, grid, projection - the viewport settings row.
    auto settings = prism::hbox(4.0F);
    settings->setPadding(prism::Margin(6.0F, 1.0F, 6.0F, 1.0F));
    m_gizmoModes = prism::make<prism::Segmented>();
    m_gizmoModes->add("", prism::Icon::Select, "Universal")
        .add("", prism::Icon::Move, "Move")
        .add("", prism::Icon::Rotate, "Rotate")
        .add("", prism::Icon::Scale, "Scale");
    m_gizmoModes->onSelect = [this](const int index) {
        using Mode = Editor::ViewportGizmoMode;
        static constexpr std::array<Mode, 4> kModes{Mode::Universal, Mode::Move, Mode::Rotate, Mode::Scale};
        m_editor.m_viewportOverlayState.gizmoMode = kModes[static_cast<std::size_t>(std::clamp(index, 0, 3))];
    };
    settings->add(m_gizmoModes, prism::Slot{0.0F, {}, prism::HAlign::Left, prism::VAlign::Center});
    settings->add(HSeparator());
    m_snapButton = ToolButton(prism::Icon::Magnet, "Snap moves to the grid and rotations to their step");
    m_snapButton->setCheckable(true);
    m_snapButton->onToggle = [this](const bool on) {
        EditorTransformSnappingSettings snap = m_editor.m_transformSnappingSettings;
        snap.enabled = on;
        static_cast<void>(m_editor.SetTransformSnappingSettings(snap));
    };
    m_gridButton = ToolButton(prism::Icon::Grid, "Show grid");
    m_gridButton->setCheckable(true);
    m_gridButton->onToggle = [this](const bool on) { m_editor.m_viewportOverlayState.gridVisible = on; };
    settings->add(m_snapButton, prism::Slot{0.0F, {}, prism::HAlign::Left, prism::VAlign::Center});
    // How far a snap steps: the grid for moves, the angle for rotations, and the scale step (whose
    // own magnet sits beside Scale in Details).
    m_snapLevelButton = ToolButton(prism::Icon::None, "Snap levels: the grid step for moves, the angle for rotations, the scale step", "1");
    m_snapLevelButton->onClick = [this] { ShowSnapLevels(); };
    settings->add(m_snapLevelButton, prism::Slot{0.0F, {}, prism::HAlign::Left, prism::VAlign::Center});
    settings->add(m_gridButton, prism::Slot{0.0F, {}, prism::HAlign::Left, prism::VAlign::Center});
    if (prism::Ref<prism::Widget> controls = MakeTouchControlsButton()) {   // touch screens only
        settings->add(controls, prism::Slot{0.0F, {}, prism::HAlign::Left, prism::VAlign::Center});
    }
    if (gizmoOnly) {
        // The Entity Editor's view: the gizmo and its snapping only; the camera, shading and preview
        // lighting are the studio's own.
        if (extra) {
            settings->add(HSeparator());
            settings->add(extra, prism::Slot{0.0F, {}, prism::HAlign::Left, prism::VAlign::Center});
        }
        return FinishBar(settings);
    }
    settings->add(HSeparator());

    // Projection and facing: the engine's own setters (SetViewportOrthographic /
    // RequestViewportView), the same entries the previous toolbar had.
    using View = Editor::ViewportView;
    m_projectionButton = prism::make<prism::Button>("Perspective", prism::Icon::Projection, prism::ButtonStyle::Toolbar);
    m_projectionButton->setTooltip("Projection and facing");
    m_projectionButton->onClick = [this] {
        Editor::ViewportOverlayState& state = m_editor.m_viewportOverlayState;
        std::vector<prism::MenuItem> items;
        items.push_back(prism::MenuItem::Check("Perspective", !state.orthographic && state.view == View::User, [this] {
            m_editor.SetViewportOrthographic(false);
            m_editor.RequestViewportView(View::User);
        }));
        items.push_back(prism::MenuItem::Check("Orthographic", state.orthographic && state.view == View::User, [this] {
            m_editor.SetViewportOrthographic(true);
            m_editor.RequestViewportView(View::User);
        }));
        items.push_back(prism::MenuItem::Sep());
        for (const View view : {View::Top, View::Bottom, View::Front, View::Back, View::Right, View::Left}) {
            items.push_back(prism::MenuItem::Check(Editor::GetViewportViewName(view), state.view == view,
                                                   [this, view] { m_editor.RequestViewportView(view); }));
        }
        prism::showMenu(std::move(items), m_projectionButton->rect(), prism::PopupPlacement::Below);
    };
    settings->add(m_projectionButton, prism::Slot{0.0F, {}, prism::HAlign::Left, prism::VAlign::Center});

    // Lit: the viewport shading modes.
    m_shadingButton = prism::make<prism::Button>("Lit", prism::Icon::Shaded, prism::ButtonStyle::Toolbar);
    m_shadingButton->setTooltip("Shading");
    m_shadingButton->onClick = [this] {
        std::vector<prism::MenuItem> items;
        for (std::size_t i = 0; i < kShadingNames.size(); ++i) {
            items.push_back(prism::MenuItem::Check(kShadingNames[i],
                                                   m_editor.m_viewportOverlayState.shadingMode == static_cast<int>(i),
                                                   [this, i] { m_editor.m_viewportOverlayState.shadingMode = static_cast<int>(i); }));
        }
        prism::showMenu(std::move(items), m_shadingButton->rect(), prism::PopupPlacement::Below);
    };
    settings->add(m_shadingButton, prism::Slot{0.0F, {}, prism::HAlign::Left, prism::VAlign::Center});
    settings->add(HSeparator());

    // Preview: the editor-only sun and sky, and their settings.
    m_sunButton = ToolButton(prism::Icon::DirectionalLight, "Preview sun");
    m_sunButton->setCheckable(true);
    m_sunButton->onToggle = [this](const bool on) { m_editor.m_viewportOverlayState.previewSun = on; };
    m_skyButton = ToolButton(prism::Icon::Environment, "Preview sky");
    m_skyButton->setCheckable(true);
    m_skyButton->onToggle = [this](const bool on) { m_editor.m_viewportOverlayState.previewEnvironment = on; };
    auto previewSettings = prism::make<prism::Button>("Preview", prism::Icon::Settings, prism::ButtonStyle::Toolbar);
    previewSettings->setTooltip("Preview lighting & sky");
    std::weak_ptr<prism::Button> weakPreview = previewSettings;
    previewSettings->onClick = [this, weakPreview] {
        if (auto anchor = weakPreview.lock()) {
            ShowPreviewSettings(anchor->rect());
        }
    };
    settings->add(m_sunButton, prism::Slot{0.0F, {}, prism::HAlign::Left, prism::VAlign::Center});
    settings->add(m_skyButton, prism::Slot{0.0F, {}, prism::HAlign::Left, prism::VAlign::Center});
    settings->add(previewSettings, prism::Slot{0.0F, {}, prism::HAlign::Left, prism::VAlign::Center});
    return FinishBar(settings);
}

prism::Ref<prism::Widget> EditorViewToolbar::FinishBar(const prism::Ref<prism::Box>& settings) {
    const bool compact = prism::Application::exists() && prism::Application::get().platform().singleSurface();
    settings->add(prism::make<prism::Spacer>(), prism::Slot{1.0F});
    auto bar = prism::make<prism::Border>();
    bar->setBackground(prism::ThemeColor(&prism::Theme::panel));
    bar->setBorder(prism::ThemeColor(&prism::Theme::border), prism::Margin(0.0F, 0.0F, 0.0F, 1.0F));
    auto sized = prism::make<prism::SizeBox>();
    sized->setHeightOverride(compact ? kCompactSubRow2 : kSubRow2);
    sized->setContent(settings);
    bar->setChild(sized);
    return bar;
}

void EditorViewToolbar::ShowPreviewSettings(const prism::Rect anchor) {
    // Preview lighting & sky: the editor-only sun and sky the viewport uses while the scene has no
    // DirectionalLight3D / WorldEnvironment3D of its own - the same fields the previous popup edited.
    Editor::ViewportOverlayState& state = m_editor.m_viewportOverlayState;
    const auto color3 = [](const std::array<float, 3>& c) { return ToPrismColor(c[0], c[1], c[2]); };
    const auto store3 = [](std::array<float, 3>& out, const prism::Color c) {
        out = {static_cast<float>(c.r) / 255.0F, static_cast<float>(c.g) / 255.0F, static_cast<float>(c.b) / 255.0F};
    };

    auto grid = prism::make<prism::PropertyGrid>();
    const int sun = grid->addCategory("Preview Sun", true);
    auto sunOn = prism::make<prism::CheckBox>("", state.previewSun);
    sunOn->onChange = [this](const bool on) { m_editor.m_viewportOverlayState.previewSun = on; };
    grid->addRow(sun, "Enabled", sunOn);
    auto sunColor = prism::make<prism::ColorField>(color3(state.previewSunColor));
    sunColor->onChange = [this, store3](const prism::Color c) { store3(m_editor.m_viewportOverlayState.previewSunColor, c); };
    grid->addRow(sun, "Color", sunColor);
    const auto slider = [](const float lo, const float hi, const float value, const int decimals, const char* suffix) {
        auto s = prism::make<prism::Slider>(lo, hi, value);
        s->setDecimals(decimals);
        if (suffix != nullptr) {
            s->setSuffix(suffix);
        }
        return s;
    };
    auto energy = slider(0.0F, 8.0F, state.previewSunEnergy, 2, nullptr);
    energy->onChange = [this](const float v) { m_editor.m_viewportOverlayState.previewSunEnergy = v; };
    grid->addRow(sun, "Energy", energy);
    auto azimuth = slider(-180.0F, 180.0F, state.previewSunAzimuth, 0, " deg");
    azimuth->onChange = [this](const float v) { m_editor.m_viewportOverlayState.previewSunAzimuth = v; };
    grid->addRow(sun, "Azimuth", azimuth, "Horizontal compass bearing around the world Y axis.");
    auto altitude = slider(-90.0F, 90.0F, state.previewSunAltitude, 0, " deg");
    altitude->onChange = [this](const float v) { m_editor.m_viewportOverlayState.previewSunAltitude = v; };
    grid->addRow(sun, "Altitude", altitude, "Elevation above the horizon. Higher values cast shorter shadows.");
    auto addSun = prism::make<prism::Button>("Add Sun to Scene", prism::Icon::Plus, prism::ButtonStyle::Primary);
    addSun->setEnabled(m_editor.IsAuthoringCommandAllowed());
    addSun->onClick = [this] {
        static_cast<void>(m_editor.AddPreviewSunToScene());
        prism::Application::get().closeAllPopups();
    };
    grid->addRow(sun, "", addSun, "Creates a DirectionalLight3D with these sun settings and shadows on.");

    const int sky = grid->addCategory("Preview Sky", true);
    auto skyOn = prism::make<prism::CheckBox>("", state.previewEnvironment);
    skyOn->onChange = [this](const bool on) { m_editor.m_viewportOverlayState.previewEnvironment = on; };
    grid->addRow(sky, "Enabled", skyOn);
    auto skyColor = prism::make<prism::ColorField>(color3(state.previewSkyColor));
    skyColor->onChange = [this, store3](const prism::Color c) { store3(m_editor.m_viewportOverlayState.previewSkyColor, c); };
    grid->addRow(sky, "Sky", skyColor);
    auto horizon = prism::make<prism::ColorField>(color3(state.previewHorizonColor));
    horizon->onChange = [this, store3](const prism::Color c) { store3(m_editor.m_viewportOverlayState.previewHorizonColor, c); };
    grid->addRow(sky, "Horizon", horizon);
    auto ground = prism::make<prism::ColorField>(color3(state.previewGroundColor));
    ground->onChange = [this, store3](const prism::Color c) { store3(m_editor.m_viewportOverlayState.previewGroundColor, c); };
    grid->addRow(sky, "Ground", ground);
    auto ambient = slider(0.0F, 4.0F, state.previewAmbientEnergy, 2, nullptr);
    ambient->onChange = [this](const float v) { m_editor.m_viewportOverlayState.previewAmbientEnergy = v; };
    grid->addRow(sky, "Ambient", ambient, "Sky and ground ambient light intensity across the scene.");
    auto addEnv = prism::make<prism::Button>("Add Environment to Scene", prism::Icon::Plus, prism::ButtonStyle::Primary);
    addEnv->setEnabled(m_editor.IsAuthoringCommandAllowed());
    addEnv->onClick = [this] {
        static_cast<void>(m_editor.AddPreviewEnvironmentToScene());
        prism::Application::get().closeAllPopups();
    };
    grid->addRow(sky, "", addEnv, "Creates a WorldEnvironment3D carrying these sky and ambient settings.");

    auto header = prism::hbox(4.0F);
    header->add(prism::make<prism::Label>("Preview Lighting & Sky", prism::FontRole::Semibold),
                prism::Slot{1.0F, {}, prism::HAlign::Fill, prism::VAlign::Center});
    auto reset = prism::make<prism::Button>("Reset", prism::Icon::Refresh, prism::ButtonStyle::Flat);
    reset->onClick = [this, anchor] {
        Editor::ViewportOverlayState& s = m_editor.m_viewportOverlayState;
        const Editor::ViewportOverlayState defaults{};
        s.previewSun = defaults.previewSun;
        s.previewSunColor = defaults.previewSunColor;
        s.previewSunEnergy = defaults.previewSunEnergy;
        s.previewSunAzimuth = defaults.previewSunAzimuth;
        s.previewSunAltitude = defaults.previewSunAltitude;
        s.previewEnvironment = defaults.previewEnvironment;
        s.previewSkyColor = defaults.previewSkyColor;
        s.previewHorizonColor = defaults.previewHorizonColor;
        s.previewGroundColor = defaults.previewGroundColor;
        s.previewAmbientEnergy = defaults.previewAmbientEnergy;
        prism::Application::get().closeAllPopups();
        ShowPreviewSettings(anchor); // reopen showing the defaults
    };
    header->add(reset, prism::Slot{0.0F, {}, prism::HAlign::Right, prism::VAlign::Center});

    auto scroll = prism::make<prism::ScrollView>();
    scroll->setContent(grid);

    auto body = prism::vbox(6.0F);
    body->setPadding(prism::Margin(8.0F));
    body->add(header);
    body->add(scroll, prism::Slot{1.0F});

    const prism::Vec2 win = prism::Application::get().windowSize();
    const float maxW = win.x > 64.0F ? std::max(220.0F, win.x - 24.0F) : 340.0F;
    const float maxH = win.y > 64.0F ? std::max(180.0F, win.y - 44.0F) : 430.0F;
    auto size = prism::make<prism::SizeBox>();
    size->setWidthOverride(std::min(340.0F, maxW));
    size->setHeightOverride(std::min(430.0F, maxH));
    size->setContent(body);
    auto panel = prism::make<prism::Border>();
    panel->setBackground(prism::ThemeColor(&prism::Theme::panel));
    panel->setBorder(prism::ThemeColor(&prism::Theme::border), prism::Margin(1.0F));
    panel->setChild(size);
    prism::PopupOptions options{};
    options.placement = prism::PopupPlacement::Below;
    options.anchor = anchor;
    prism::Application::get().showPopup(panel, options);
}

void EditorViewToolbar::ShowSnapLevels() {
    const EditorTransformSnappingSettings now = m_editor.m_transformSnappingSettings;
    const auto pick = [this](const int kind, const float value) {
        EditorTransformSnappingSettings snap = m_editor.m_transformSnappingSettings;
        if (kind == 0) {
            snap.translateStep = value;
            snap.enabled = true;
        } else if (kind == 1) {
            snap.rotateStepDegrees = value;
            snap.enabled = true;
        } else {
            snap.scaleStep = value;
            snap.scaleEnabled = true;
        }
        static_cast<void>(m_editor.SetTransformSnappingSettings(snap));
    };
    const auto section = [&](const int kind, const float current, const std::initializer_list<float> values, const char* unit) {
        std::vector<prism::MenuItem> items;
        for (const float value : values) {
            char label[24];
            std::snprintf(label, sizeof(label), "%g%s", static_cast<double>(value), unit);
            items.push_back(prism::MenuItem::Check(label, std::abs(current - value) < 1e-5F, [pick, kind, value] { pick(kind, value); }));
        }
        return items;
    };
    std::vector<prism::MenuItem> items;
    items.push_back(prism::MenuItem::Check("Snap Move and Rotate", now.enabled, [this] {
        EditorTransformSnappingSettings snap = m_editor.m_transformSnappingSettings;
        snap.enabled = !snap.enabled;
        static_cast<void>(m_editor.SetTransformSnappingSettings(snap));
    }));
    items.push_back(prism::MenuItem::Check("Snap Scale", now.scaleEnabled, [this] {
        EditorTransformSnappingSettings snap = m_editor.m_transformSnappingSettings;
        snap.scaleEnabled = !snap.scaleEnabled;
        static_cast<void>(m_editor.SetTransformSnappingSettings(snap));
    }));
    items.push_back(prism::MenuItem::Sep());
    items.push_back(prism::MenuItem::Sub("Move (Grid)", section(0, now.translateStep, {0.01F, 0.05F, 0.1F, 0.25F, 0.5F, 1.0F, 2.0F, 5.0F, 10.0F}, " m"),
                                         prism::Icon::Move));
    items.push_back(prism::MenuItem::Sub("Rotate", section(1, now.rotateStepDegrees, {1.0F, 5.0F, 10.0F, 15.0F, 22.5F, 30.0F, 45.0F, 90.0F}, "\xC2\xB0"),
                                         prism::Icon::Rotate));
    items.push_back(prism::MenuItem::Sub("Scale", section(2, now.scaleStep, {0.01F, 0.05F, 0.1F, 0.25F, 0.5F, 1.0F}, ""), prism::Icon::Scale));
    if (m_snapLevelButton) {
        const prism::Rect r = m_snapLevelButton->rect();
        prism::showContextMenu(std::move(items), prism::Vec2{r.x, r.bottom()});
    }
}

void EditorViewToolbar::Refresh() {
    if (m_snapButton) {
        m_snapButton->setChecked(m_editor.m_transformSnappingSettings.enabled);
    }
    if (m_snapLevelButton) {
        // The levels at a glance: the grid step and the angle.
        char levels[32];
        std::snprintf(levels, sizeof(levels), "%g  %g\xC2\xB0", static_cast<double>(m_editor.m_transformSnappingSettings.translateStep),
                      static_cast<double>(m_editor.m_transformSnappingSettings.rotateStepDegrees));
        if (m_shownSnapLevels != levels) {
            m_shownSnapLevels = levels;
            m_snapLevelButton->setText(levels);
        }
    }
    if (m_gridButton) {
        m_gridButton->setChecked(m_editor.m_viewportOverlayState.gridVisible);
    }
    {
        const Editor::ViewportOverlayState& state = m_editor.m_viewportOverlayState;
        if (m_projectionButton) {
            const std::string text = state.view != Editor::ViewportView::User
                                         ? std::string(Editor::GetViewportViewName(state.view))
                                         : (state.orthographic ? "Orthographic" : "Perspective");
            if (m_projectionButton->text() != text) {
                m_projectionButton->setText(text);
            }
        }
        if (m_shadingButton) {
            const std::string text =
                kShadingNames[static_cast<std::size_t>(std::clamp(state.shadingMode, 0, static_cast<int>(kShadingNames.size()) - 1))];
            if (m_shadingButton->text() != text) {
                m_shadingButton->setText(text);
            }
        }
        if (m_sunButton) {
            m_sunButton->setChecked(state.previewSun);
        }
        if (m_skyButton) {
            m_skyButton->setChecked(state.previewEnvironment);
        }
    }
    using Mode = Editor::ViewportGizmoMode;
    int mode = 0;
    switch (m_editor.m_viewportOverlayState.gizmoMode) {
        case Mode::Universal: mode = 0; break;
        case Mode::Move: mode = 1; break;
        case Mode::Rotate: mode = 2; break;
        case Mode::Scale: mode = 3; break;
    }
    if (m_gizmoModes && m_gizmoModes->selected() != mode) {
        m_gizmoModes->setSelected(mode);
    }
}

} // namespace UVE::Editor
