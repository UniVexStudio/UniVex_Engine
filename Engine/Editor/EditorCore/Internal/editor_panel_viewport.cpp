// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

// The level view's state: what the view shows, how it is framed, and the per-frame overlay hook.

#include "shell/editor_shell.h"
#include "editor.h"
#include "scalar.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#if !defined(__ANDROID__)
#include <GLFW/glfw3.h>
#endif

#include "i_asset_import_queue.h"
#include "light_emitter_component.h"
#include "script_component.h"
#include "transform_component.h"
#include "editor_settings.h"
#include "color.h"
#include "quaternion.h"
#include "preview_lighting.h"
#include "world_environment_3d.h"
#include "scene_object_registry.h"

namespace UVE::Editor {

void EditorUVE::ReleaseGameMouseOnFocusLossUVE() {
    // A game holding the mouse lets go when its window loses focus (Escape is the shell's key
    // hook), so the editor's own controls can be reached again.
    if (m_gameMouseCaptured && m_services != nullptr && m_services->GetWindowManagerUVE().IsValidUVE() &&
        !m_services->GetWindowManagerUVE().IsFocusedUVE()) {
        SetGameMouseCapturedUVE(false);
    }
}

void EditorUVE::RenderOverlayUVE() {
    if (m_state != EditorStateUVE::Running || !m_uiInitialized) {
        return;
    }
    ReleaseGameMouseOnFocusLossUVE();
    if (m_levelShell != nullptr) {
        m_levelShell->FrameUVE();
    }
}

void EditorUVE::SetViewportPanelRendererUVE(ViewportPanelRendererUVE renderer) {
    m_viewportPanelRenderer = std::move(renderer);
}

void EditorUVE::SetEntityContextToolbarAnchorUVE(const Scene::EntityUVE entity, const float pixelX,
                                                 const float pixelY) {
    m_viewportOverlayState.entityContextToolbarOpen = true;
    m_viewportOverlayState.entityContextToolbarEntity = entity;
    m_viewportOverlayState.entityContextToolbarPixelX = pixelX;
    m_viewportOverlayState.entityContextToolbarPixelY = pixelY;
}

void EditorUVE::ClearEntityContextToolbarUVE() noexcept {
    m_viewportOverlayState.entityContextToolbarOpen = false;
    m_viewportOverlayState.entityContextToolbarEntity = Scene::kInvalidEntityUVE;
}

namespace {

[[nodiscard]] bool IsViewportAxisColorValidUVE(const EditorUVE::ViewportAxisColorUVE& color) {
    const auto channelValid = [](const float channel) {
        // NaN fails both comparisons, so it is refused here rather than surviving as a colour.
        return channel >= 0.0F && channel <= 1.0F;
    };
    return channelValid(color.r) && channelValid(color.g) && channelValid(color.b);
}

} // namespace

void EditorUVE::SetViewportOrthographicUVE(const bool orthographic) noexcept {
    m_viewportOverlayState.orthographic = orthographic;
    m_viewportOrthographicIsAutomatic = false;
}

bool EditorUVE::CanFocusEntityInViewportUVE(const Scene::EntityUVE entity) const {
    return IsDocumentEntityUVE(entity) && (ComposeMarker3DFocusBookmarkUVE(entity).has_value() ||
                                           ResolveEntityFocusTargetUVE(entity).has_value());
}

bool EditorUVE::RequestViewportFocusUVE(const Scene::EntityUVE entity) {
    if (!CanFocusEntityInViewportUVE(entity)) {
        return false;
    }
    m_viewportOverlayState.focusEntity = entity;
    ++m_viewportOverlayState.focusRequestSerial;
    return true;
}

void EditorUVE::RequestViewportViewUVE(const ViewportViewUVE view) noexcept {
    m_viewportOverlayState.view = view;
    ++m_viewportOverlayState.viewRequestSerial;
    // An axis view reads best flat; switch to orthographic unless the author already chose it.
    if (view != ViewportViewUVE::User && !m_viewportOverlayState.orthographic) {
        m_viewportOverlayState.orthographic = true;
        m_viewportOrthographicIsAutomatic = true;
    }
}

void EditorUVE::NotifyViewportOrbitedUVE() noexcept {
    if (m_viewportOverlayState.view == ViewportViewUVE::User) {
        return;
    }
    m_viewportOverlayState.view = ViewportViewUVE::User;
    if (m_viewportOrthographicIsAutomatic) {
        m_viewportOverlayState.orthographic = false;
        m_viewportOrthographicIsAutomatic = false;
    }
}

const char* EditorUVE::GetViewportViewNameUVE(const ViewportViewUVE view) noexcept {
    switch (view) {
        case ViewportViewUVE::Top: return "Top";
        case ViewportViewUVE::Bottom: return "Bottom";
        case ViewportViewUVE::Front: return "Front";
        case ViewportViewUVE::Back: return "Back";
        case ViewportViewUVE::Right: return "Right";
        case ViewportViewUVE::Left: return "Left";
        case ViewportViewUVE::User: break;
    }
    return "User";
}

std::optional<EditorUVE::ViewportViewUVE> EditorUVE::GetViewportViewForKeypadDigitUVE(const int digit,
                                                                                    const bool opposite) noexcept {
    switch (digit) {
        case 7: return opposite ? ViewportViewUVE::Bottom : ViewportViewUVE::Top;
        case 1: return opposite ? ViewportViewUVE::Back : ViewportViewUVE::Front;
        case 3: return opposite ? ViewportViewUVE::Left : ViewportViewUVE::Right;
        default: return std::nullopt;
    }
}

const char* EditorUVE::GetViewportViewShortcutUVE(const ViewportViewUVE view) noexcept {
    switch (view) {
        case ViewportViewUVE::Top: return "Numpad 7  /  Alt+7";
        case ViewportViewUVE::Bottom: return "Ctrl+Numpad 7  /  Ctrl+Alt+7";
        case ViewportViewUVE::Front: return "Numpad 1  /  Alt+1";
        case ViewportViewUVE::Back: return "Ctrl+Numpad 1  /  Ctrl+Alt+1";
        case ViewportViewUVE::Right: return "Numpad 3  /  Alt+3";
        case ViewportViewUVE::Left: return "Ctrl+Numpad 3  /  Ctrl+Alt+3";
        case ViewportViewUVE::User: break;
    }
    return "";
}

bool EditorUVE::SetViewportGridUVE(const bool visible, const float opacity) {
    if (!std::isfinite(opacity) || opacity < kMinimumViewportGridOpacityUVE || opacity > 1.0F) {
        return false;
    }
    const bool previousVisible = m_viewportOverlayState.gridVisible;
    const float previousOpacity = m_viewportOverlayState.gridOpacity;
    m_viewportOverlayState.gridVisible = visible;
    m_viewportOverlayState.gridOpacity = opacity;
    namespace Id = EditorSettingIdUVE;
    NotifyEditorSettingChangedUVE(Id::kGridVisibleUVE, previousVisible, visible);
    NotifyEditorSettingChangedUVE(Id::kGridOpacityUVE, static_cast<double>(previousOpacity),
                                  static_cast<double>(opacity));
    return true;
}

bool EditorUVE::SetViewportGridCellSizeUVE(const float cellSize) {
    if (!std::isfinite(cellSize) || cellSize < kMinimumViewportGridCellSizeUVE ||
        cellSize > kMaximumViewportGridCellSizeUVE) {
        return false;
    }
    const float previousCellSize = m_viewportOverlayState.gridCellSize;
    m_viewportOverlayState.gridCellSize = cellSize;
    NotifyEditorSettingChangedUVE(EditorSettingIdUVE::kGridCellSizeUVE,
                                  static_cast<double>(previousCellSize), static_cast<double>(cellSize));
    return true;
}

bool EditorUVE::SetViewportSelectionOutlineUVE(const bool visible, const ViewportAxisColorUVE color,
                                               const float thickness) {
    if (!IsViewportAxisColorValidUVE(color) || !std::isfinite(thickness) ||
        thickness < kMinimumSelectionOutlineThicknessUVE || thickness > kMaximumSelectionOutlineThicknessUVE) {
        return false;
    }
    const bool previousVisible = m_viewportOverlayState.selectionOutlineVisible;
    const ViewportAxisColorUVE previousColor = m_viewportOverlayState.selectionOutlineColor;
    const float previousThickness = m_viewportOverlayState.selectionOutlineThickness;
    m_viewportOverlayState.selectionOutlineVisible = visible;
    m_viewportOverlayState.selectionOutlineColor = color;
    m_viewportOverlayState.selectionOutlineThickness = thickness;
    namespace Id = EditorSettingIdUVE;
    NotifyEditorSettingChangedUVE(Id::kSelectionOutlineVisibleUVE, previousVisible, visible);
    NotifyEditorSettingChangedUVE(Id::kSelectionOutlineColorUVE,
                                  Config::SettingColorUVE{previousColor.r, previousColor.g, previousColor.b},
                                  Config::SettingColorUVE{color.r, color.g, color.b});
    NotifyEditorSettingChangedUVE(Id::kSelectionOutlineThicknessUVE, static_cast<double>(previousThickness),
                                  static_cast<double>(thickness));
    return true;
}

bool EditorUVE::SetViewportAxisColorsUVE(const ViewportAxisColorUVE x, const ViewportAxisColorUVE y,
                                         const ViewportAxisColorUVE z) {
    // All three or none: a half-applied palette would leave one axis in a colour the author never
    // chose, which is worse than refusing the whole change.
    if (!IsViewportAxisColorValidUVE(x) || !IsViewportAxisColorValidUVE(y) ||
        !IsViewportAxisColorValidUVE(z)) {
        return false;
    }
    m_viewportOverlayState.axisColorX = x;
    m_viewportOverlayState.axisColorY = y;
    m_viewportOverlayState.axisColorZ = z;
    m_viewportOverlayState.axisColorsValid = true;
    return true;
}

bool EditorUVE::AreViewportAxisColorsSetUVE() const noexcept {
    return m_viewportOverlayState.axisColorsValid;
}

void EditorUVE::ResetViewportAxisColorsUVE() noexcept {
    m_viewportOverlayState.axisColorsValid = false;
    m_viewportOverlayState.axisColorX = ViewportAxisColorUVE{};
    m_viewportOverlayState.axisColorY = ViewportAxisColorUVE{};
    m_viewportOverlayState.axisColorZ = ViewportAxisColorUVE{};
}

EditorUVE::ViewportAxisColorUVE EditorUVE::GetViewportAxisColorUVE(const int axisIndex) const {
    switch (axisIndex) {
        case 0: return m_viewportOverlayState.axisColorX;
        case 1: return m_viewportOverlayState.axisColorY;
        case 2: return m_viewportOverlayState.axisColorZ;
        default: return ViewportAxisColorUVE{};
    }
}

} // namespace UVE::Editor

