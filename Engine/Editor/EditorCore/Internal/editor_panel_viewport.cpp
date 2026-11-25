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

#include <GLFW/glfw3.h>

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

void Editor::ReleaseGameMouseOnFocusLoss() {
    // A game holding the mouse lets go when its window loses focus (Escape is the shell's key
    // hook), so the editor's own controls can be reached again.
    if (m_gameMouseCaptured && m_services != nullptr && m_services->GetWindowManager().IsValid() &&
        !m_services->GetWindowManager().IsFocused()) {
        SetGameMouseCaptured(false);
    }
}

void Editor::RenderOverlay() {
    if (m_state != EditorState::Running || !m_uiInitialized) {
        return;
    }
    ReleaseGameMouseOnFocusLoss();
    if (m_levelShell != nullptr) {
        m_levelShell->Frame();
    }
}

void Editor::SetEditorViewports(EditorViewports viewports) {
    m_viewports = std::move(viewports);
}

void Editor::ClearEditorViewports() {
    m_viewports = EditorViewports{};
}

void Editor::SetLevelViewportRenderer(ViewportRenderCallback renderer) {
    m_viewports.level = std::move(renderer);
}

void Editor::SetCameraPreviewViewportRenderer(ViewportRenderCallback renderer) {
    m_viewports.cameraPreview = std::move(renderer);
}

void Editor::SetEntityViewportRenderer(ViewportRenderCallback renderer) {
    m_viewports.entity = std::move(renderer);
}

void Editor::SetWidgetPreviewViewportRenderer(ViewportRenderCallback renderer) {
    m_viewports.widgetPreview = std::move(renderer);
}

void Editor::SetRetargetViewportRenderer(ViewportRenderCallback renderer) {
    m_viewports.retarget = std::move(renderer);
}

void Editor::SetMontageViewportRenderer(ViewportRenderCallback renderer) {
    m_viewports.montage = std::move(renderer);
}

void Editor::SetVfxViewportRenderer(ViewportRenderCallback renderer) {
    m_viewports.vfx = std::move(renderer);
}

void Editor::SetEntityContextToolbarAnchor(const Scene::Entity entity, const float pixelX,
                                                 const float pixelY) {
    m_viewportOverlayState.entityContextToolbarOpen = true;
    m_viewportOverlayState.entityContextToolbarEntity = entity;
    m_viewportOverlayState.entityContextToolbarPixelX = pixelX;
    m_viewportOverlayState.entityContextToolbarPixelY = pixelY;
}

void Editor::ClearEntityContextToolbar() noexcept {
    m_viewportOverlayState.entityContextToolbarOpen = false;
    m_viewportOverlayState.entityContextToolbarEntity = Scene::kInvalidEntity;
}

namespace {

[[nodiscard]] bool IsViewportAxisColorValid(const Editor::ViewportAxisColor& color) {
    const auto channelValid = [](const float channel) {
        // NaN fails both comparisons, so it is refused here rather than surviving as a colour.
        return channel >= 0.0F && channel <= 1.0F;
    };
    return channelValid(color.r) && channelValid(color.g) && channelValid(color.b);
}

} // namespace

void Editor::SetViewportOrthographic(const bool orthographic) noexcept {
    m_viewportOverlayState.orthographic = orthographic;
    m_viewportOrthographicIsAutomatic = false;
}

bool Editor::CanFocusEntityInViewport(const Scene::Entity entity) const {
    return IsDocumentEntity(entity) && (ComposeMarker3DFocusBookmark(entity).has_value() ||
                                           ResolveEntityFocusTarget(entity).has_value());
}

bool Editor::RequestViewportFocus(const Scene::Entity entity) {
    if (!CanFocusEntityInViewport(entity)) {
        return false;
    }
    m_viewportOverlayState.focusEntity = entity;
    ++m_viewportOverlayState.focusRequestSerial;
    return true;
}

void Editor::RequestViewportView(const ViewportView view) noexcept {
    m_viewportOverlayState.view = view;
    ++m_viewportOverlayState.viewRequestSerial;
    // An axis view reads best flat; switch to orthographic unless the author already chose it.
    if (view != ViewportView::User && !m_viewportOverlayState.orthographic) {
        m_viewportOverlayState.orthographic = true;
        m_viewportOrthographicIsAutomatic = true;
    }
}

void Editor::NotifyViewportOrbited() noexcept {
    if (m_viewportOverlayState.view == ViewportView::User) {
        return;
    }
    m_viewportOverlayState.view = ViewportView::User;
    if (m_viewportOrthographicIsAutomatic) {
        m_viewportOverlayState.orthographic = false;
        m_viewportOrthographicIsAutomatic = false;
    }
}

const char* Editor::GetViewportViewName(const ViewportView view) noexcept {
    switch (view) {
        case ViewportView::Top: return "Top";
        case ViewportView::Bottom: return "Bottom";
        case ViewportView::Front: return "Front";
        case ViewportView::Back: return "Back";
        case ViewportView::Right: return "Right";
        case ViewportView::Left: return "Left";
        case ViewportView::User: break;
    }
    return "User";
}

std::optional<Editor::ViewportView> Editor::GetViewportViewForKeypadDigit(const int digit,
                                                                                    const bool opposite) noexcept {
    switch (digit) {
        case 7: return opposite ? ViewportView::Bottom : ViewportView::Top;
        case 1: return opposite ? ViewportView::Back : ViewportView::Front;
        case 3: return opposite ? ViewportView::Left : ViewportView::Right;
        default: return std::nullopt;
    }
}

const char* Editor::GetViewportViewShortcut(const ViewportView view) noexcept {
    switch (view) {
        case ViewportView::Top: return "Numpad 7  /  Alt+7";
        case ViewportView::Bottom: return "Ctrl+Numpad 7  /  Ctrl+Alt+7";
        case ViewportView::Front: return "Numpad 1  /  Alt+1";
        case ViewportView::Back: return "Ctrl+Numpad 1  /  Ctrl+Alt+1";
        case ViewportView::Right: return "Numpad 3  /  Alt+3";
        case ViewportView::Left: return "Ctrl+Numpad 3  /  Ctrl+Alt+3";
        case ViewportView::User: break;
    }
    return "";
}

bool Editor::SetViewportGrid(const bool visible, const float opacity) {
    if (!std::isfinite(opacity) || opacity < kMinimumViewportGridOpacity || opacity > 1.0F) {
        return false;
    }
    const bool previousVisible = m_viewportOverlayState.gridVisible;
    const float previousOpacity = m_viewportOverlayState.gridOpacity;
    m_viewportOverlayState.gridVisible = visible;
    m_viewportOverlayState.gridOpacity = opacity;
    namespace Id = EditorSettingId;
    NotifyEditorSettingChanged(Id::kGridVisible, previousVisible, visible);
    NotifyEditorSettingChanged(Id::kGridOpacity, static_cast<double>(previousOpacity),
                                  static_cast<double>(opacity));
    return true;
}

bool Editor::SetViewportGridCellSize(const float cellSize) {
    if (!std::isfinite(cellSize) || cellSize < kMinimumViewportGridCellSize ||
        cellSize > kMaximumViewportGridCellSize) {
        return false;
    }
    const float previousCellSize = m_viewportOverlayState.gridCellSize;
    m_viewportOverlayState.gridCellSize = cellSize;
    NotifyEditorSettingChanged(EditorSettingId::kGridCellSize,
                                  static_cast<double>(previousCellSize), static_cast<double>(cellSize));
    return true;
}

bool Editor::SetViewportSelectionOutline(const bool visible, const ViewportAxisColor color,
                                               const float thickness) {
    if (!IsViewportAxisColorValid(color) || !std::isfinite(thickness) ||
        thickness < kMinimumSelectionOutlineThickness || thickness > kMaximumSelectionOutlineThickness) {
        return false;
    }
    const bool previousVisible = m_viewportOverlayState.selectionOutlineVisible;
    const ViewportAxisColor previousColor = m_viewportOverlayState.selectionOutlineColor;
    const float previousThickness = m_viewportOverlayState.selectionOutlineThickness;
    m_viewportOverlayState.selectionOutlineVisible = visible;
    m_viewportOverlayState.selectionOutlineColor = color;
    m_viewportOverlayState.selectionOutlineThickness = thickness;
    namespace Id = EditorSettingId;
    NotifyEditorSettingChanged(Id::kSelectionOutlineVisible, previousVisible, visible);
    NotifyEditorSettingChanged(Id::kSelectionOutlineColor,
                                  Config::SettingColor{previousColor.r, previousColor.g, previousColor.b},
                                  Config::SettingColor{color.r, color.g, color.b});
    NotifyEditorSettingChanged(Id::kSelectionOutlineThickness, static_cast<double>(previousThickness),
                                  static_cast<double>(thickness));
    return true;
}

bool Editor::SetViewportAxisColors(const ViewportAxisColor x, const ViewportAxisColor y,
                                         const ViewportAxisColor z) {
    // All three or none: a half-applied palette would leave one axis in a colour the author never
    // chose, which is worse than refusing the whole change.
    if (!IsViewportAxisColorValid(x) || !IsViewportAxisColorValid(y) ||
        !IsViewportAxisColorValid(z)) {
        return false;
    }
    m_viewportOverlayState.axisColorX = x;
    m_viewportOverlayState.axisColorY = y;
    m_viewportOverlayState.axisColorZ = z;
    m_viewportOverlayState.axisColorsValid = true;
    return true;
}

bool Editor::AreViewportAxisColorsSet() const noexcept {
    return m_viewportOverlayState.axisColorsValid;
}

void Editor::ResetViewportAxisColors() noexcept {
    m_viewportOverlayState.axisColorsValid = false;
    m_viewportOverlayState.axisColorX = ViewportAxisColor{};
    m_viewportOverlayState.axisColorY = ViewportAxisColor{};
    m_viewportOverlayState.axisColorZ = ViewportAxisColor{};
}

Editor::ViewportAxisColor Editor::GetViewportAxisColor(const int axisIndex) const {
    switch (axisIndex) {
        case 0: return m_viewportOverlayState.axisColorX;
        case 1: return m_viewportOverlayState.axisColorY;
        case 2: return m_viewportOverlayState.axisColorZ;
        default: return ViewportAxisColor{};
    }
}

} // namespace UVE::Editor

