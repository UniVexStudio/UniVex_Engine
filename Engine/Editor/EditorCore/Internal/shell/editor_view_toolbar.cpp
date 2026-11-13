// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
#include "editor_view_toolbar.h"

#include "editor.h"
#include "editor_chrome.h"

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
#include <string>
#include <vector>
#include <cstddef>
#include <utility>

namespace UVE::Editor {
namespace {

[[nodiscard]] prism::Color ToPrismColorUVE(const float r, const float g, const float b) {
    const auto c = [](const float v) { return static_cast<std::uint8_t>(std::lround(std::clamp(v, 0.0F, 1.0F) * 255.0F)); };
    return prism::Color(c(r), c(g), c(b), 255);
}

} // namespace

EditorViewToolbarUVE::EditorViewToolbarUVE(EditorUVE& editor) : m_editor(editor) {}

prism::Ref<prism::Widget> EditorViewToolbarUVE::BuildUVE(const bool gizmoOnly, prism::Ref<prism::Widget> extra) {
    // Sub row 2 (full width): gizmo mode, snapping, grid, projection - the viewport settings row.
    auto settings = prism::hbox(4.0F);
    settings->setPadding(prism::Margin(6.0F, 1.0F, 6.0F, 1.0F));
    m_gizmoModes = prism::make<prism::Segmented>();
    m_gizmoModes->add("", prism::Icon::Select, "Universal")
        .add("", prism::Icon::Move, "Move")
        .add("", prism::Icon::Rotate, "Rotate")
        .add("", prism::Icon::Scale, "Scale");
    m_gizmoModes->onSelect = [this](const int index) {
        using Mode = EditorUVE::ViewportGizmoModeUVE;
        static constexpr std::array<Mode, 4> kModes{Mode::Universal, Mode::Move, Mode::Rotate, Mode::Scale};
        m_editor.m_viewportOverlayState.gizmoMode = kModes[static_cast<std::size_t>(std::clamp(index, 0, 3))];
    };
    settings->add(m_gizmoModes, prism::Slot{0.0F, {}, prism::HAlign::Left, prism::VAlign::Center});
    settings->add(HSeparatorUVE());
    m_snapButton = ToolButtonUVE(prism::Icon::Magnet, "Snap to grid");
    m_snapButton->setCheckable(true);
    m_snapButton->onToggle = [this](const bool on) {
        EditorTransformSnappingSettingsUVE snap = m_editor.m_transformSnappingSettings;
        snap.enabled = on;
        static_cast<void>(m_editor.SetTransformSnappingSettingsUVE(snap));
    };
    m_gridButton = ToolButtonUVE(prism::Icon::Grid, "Show grid");
    m_gridButton->setCheckable(true);
    m_gridButton->onToggle = [this](const bool on) { m_editor.m_viewportOverlayState.gridVisible = on; };
    settings->add(m_snapButton, prism::Slot{0.0F, {}, prism::HAlign::Left, prism::VAlign::Center});
    settings->add(m_gridButton, prism::Slot{0.0F, {}, prism::HAlign::Left, prism::VAlign::Center});
    if (gizmoOnly) {
        // The Entity Editor's view: the gizmo and its snapping only; the camera, shading and preview
        // lighting are the studio's own.
        if (extra) {
            settings->add(HSeparatorUVE());
            settings->add(extra, prism::Slot{0.0F, {}, prism::HAlign::Left, prism::VAlign::Center});
        }
        return FinishBarUVE(settings);
    }
    settings->add(HSeparatorUVE());

    // Projection and facing: the engine's own setters (SetViewportOrthographicUVE /
    // RequestViewportViewUVE), the same entries the previous toolbar had.
    using ViewUVE = EditorUVE::ViewportViewUVE;
    m_projectionButton = prism::make<prism::Button>("Perspective", prism::Icon::Projection, prism::ButtonStyle::Toolbar);
    m_projectionButton->setTooltip("Projection and facing");
    m_projectionButton->onClick = [this] {
        EditorUVE::ViewportOverlayStateUVE& state = m_editor.m_viewportOverlayState;
        std::vector<prism::MenuItem> items;
        items.push_back(prism::MenuItem::Check("Perspective", !state.orthographic && state.view == ViewUVE::User, [this] {
            m_editor.SetViewportOrthographicUVE(false);
            m_editor.RequestViewportViewUVE(ViewUVE::User);
        }));
        items.push_back(prism::MenuItem::Check("Orthographic", state.orthographic && state.view == ViewUVE::User, [this] {
            m_editor.SetViewportOrthographicUVE(true);
            m_editor.RequestViewportViewUVE(ViewUVE::User);
        }));
        items.push_back(prism::MenuItem::Sep());
        for (const ViewUVE view : {ViewUVE::Top, ViewUVE::Bottom, ViewUVE::Front, ViewUVE::Back, ViewUVE::Right, ViewUVE::Left}) {
            items.push_back(prism::MenuItem::Check(EditorUVE::GetViewportViewNameUVE(view), state.view == view,
                                                   [this, view] { m_editor.RequestViewportViewUVE(view); }));
        }
        prism::showMenu(std::move(items), m_projectionButton->rect(), prism::PopupPlacement::Below);
    };
    settings->add(m_projectionButton, prism::Slot{0.0F, {}, prism::HAlign::Left, prism::VAlign::Center});

    // Lit: the viewport shading modes.
    m_shadingButton = prism::make<prism::Button>("Lit", prism::Icon::Shaded, prism::ButtonStyle::Toolbar);
    m_shadingButton->setTooltip("Shading");
    m_shadingButton->onClick = [this] {
        std::vector<prism::MenuItem> items;
        for (std::size_t i = 0; i < kShadingNamesUVE.size(); ++i) {
            items.push_back(prism::MenuItem::Check(kShadingNamesUVE[i],
                                                   m_editor.m_viewportOverlayState.shadingMode == static_cast<int>(i),
                                                   [this, i] { m_editor.m_viewportOverlayState.shadingMode = static_cast<int>(i); }));
        }
        prism::showMenu(std::move(items), m_shadingButton->rect(), prism::PopupPlacement::Below);
    };
    settings->add(m_shadingButton, prism::Slot{0.0F, {}, prism::HAlign::Left, prism::VAlign::Center});
    settings->add(HSeparatorUVE());

    // Preview: the editor-only sun and sky, and their settings.
    m_sunButton = ToolButtonUVE(prism::Icon::DirectionalLight, "Preview sun");
    m_sunButton->setCheckable(true);
    m_sunButton->onToggle = [this](const bool on) { m_editor.m_viewportOverlayState.previewSun = on; };
    m_skyButton = ToolButtonUVE(prism::Icon::Environment, "Preview sky");
    m_skyButton->setCheckable(true);
    m_skyButton->onToggle = [this](const bool on) { m_editor.m_viewportOverlayState.previewEnvironment = on; };
    auto previewSettings = prism::make<prism::Button>("Preview", prism::Icon::Settings, prism::ButtonStyle::Toolbar);
    previewSettings->setTooltip("Preview lighting & sky");
    std::weak_ptr<prism::Button> weakPreview = previewSettings;
    previewSettings->onClick = [this, weakPreview] {
        if (auto anchor = weakPreview.lock()) {
            ShowPreviewSettingsUVE(anchor->rect());
        }
    };
    settings->add(m_sunButton, prism::Slot{0.0F, {}, prism::HAlign::Left, prism::VAlign::Center});
    settings->add(m_skyButton, prism::Slot{0.0F, {}, prism::HAlign::Left, prism::VAlign::Center});
    settings->add(previewSettings, prism::Slot{0.0F, {}, prism::HAlign::Left, prism::VAlign::Center});
    return FinishBarUVE(settings);
}

prism::Ref<prism::Widget> EditorViewToolbarUVE::FinishBarUVE(const prism::Ref<prism::Box>& settings) {
    settings->add(prism::make<prism::Spacer>(), prism::Slot{1.0F});
    auto bar = prism::make<prism::Border>();
    bar->setBackground(prism::ThemeColor(&prism::Theme::panel));
    bar->setBorder(prism::ThemeColor(&prism::Theme::border), prism::Margin(0.0F, 0.0F, 0.0F, 1.0F));
    auto sized = prism::make<prism::SizeBox>();
    sized->setHeightOverride(kSubRow2UVE);
    sized->setContent(settings);
    bar->setChild(sized);
    return bar;
}

void EditorViewToolbarUVE::ShowPreviewSettingsUVE(const prism::Rect anchor) {
    // Preview lighting & sky: the editor-only sun and sky the viewport uses while the scene has no
    // DirectionalLight3D / WorldEnvironment3D of its own - the same fields the previous popup edited.
    EditorUVE::ViewportOverlayStateUVE& state = m_editor.m_viewportOverlayState;
    const auto color3 = [](const std::array<float, 3>& c) { return ToPrismColorUVE(c[0], c[1], c[2]); };
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
    addSun->setEnabled(m_editor.IsAuthoringCommandAllowedUVE());
    addSun->onClick = [this] {
        static_cast<void>(m_editor.AddPreviewSunToSceneUVE());
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
    addEnv->setEnabled(m_editor.IsAuthoringCommandAllowedUVE());
    addEnv->onClick = [this] {
        static_cast<void>(m_editor.AddPreviewEnvironmentToSceneUVE());
        prism::Application::get().closeAllPopups();
    };
    grid->addRow(sky, "", addEnv, "Creates a WorldEnvironment3D carrying these sky and ambient settings.");

    auto header = prism::hbox(4.0F);
    header->add(prism::make<prism::Label>("Preview Lighting & Sky", prism::FontRole::Semibold),
                prism::Slot{1.0F, {}, prism::HAlign::Fill, prism::VAlign::Center});
    auto reset = prism::make<prism::Button>("Reset", prism::Icon::Refresh, prism::ButtonStyle::Flat);
    reset->onClick = [this, anchor] {
        EditorUVE::ViewportOverlayStateUVE& s = m_editor.m_viewportOverlayState;
        const EditorUVE::ViewportOverlayStateUVE defaults{};
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
        ShowPreviewSettingsUVE(anchor); // reopen showing the defaults
    };
    header->add(reset, prism::Slot{0.0F, {}, prism::HAlign::Right, prism::VAlign::Center});

    auto body = prism::vbox(6.0F);
    body->setPadding(prism::Margin(10.0F));
    body->add(header);
    body->add(grid, prism::Slot{1.0F});
    auto size = prism::make<prism::SizeBox>();
    size->setWidthOverride(340.0F);
    size->setHeightOverride(430.0F);
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

void EditorViewToolbarUVE::RefreshUVE() {
    if (m_snapButton) {
        m_snapButton->setChecked(m_editor.m_transformSnappingSettings.enabled);
    }
    if (m_gridButton) {
        m_gridButton->setChecked(m_editor.m_viewportOverlayState.gridVisible);
    }
    {
        const EditorUVE::ViewportOverlayStateUVE& state = m_editor.m_viewportOverlayState;
        if (m_projectionButton) {
            const std::string text = state.view != EditorUVE::ViewportViewUVE::User
                                         ? std::string(EditorUVE::GetViewportViewNameUVE(state.view))
                                         : (state.orthographic ? "Orthographic" : "Perspective");
            if (m_projectionButton->text() != text) {
                m_projectionButton->setText(text);
            }
        }
        if (m_shadingButton) {
            const std::string text =
                kShadingNamesUVE[static_cast<std::size_t>(std::clamp(state.shadingMode, 0, static_cast<int>(kShadingNamesUVE.size()) - 1))];
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
    using Mode = EditorUVE::ViewportGizmoModeUVE;
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
