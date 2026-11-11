// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
#pragma once

// The window chrome every editor window shares (the editor design sheet's #1, #2 and #15): the
// logo box spanning the title bar and the document-tab row, the drag strips that move the window,
// the document tab, the window buttons, and the small building blocks the shells' rows use.
// Every piece acts on the window it is in (prism::Application::currentWindow() while it arranges
// or handles an event), so the same widgets serve the level editor and the asset editors.

#include "prism/Containers.h"
#include "prism/Controls.h"
#include "prism/Icons.h"
#include "prism/Widget.h"

#include <array>
#include <cstdint>
#include <functional>
#include <string>

namespace UVE::Editor {

// Title bar (#15) and document tab (#2) row heights, and the logo box (#1) spanning both.
inline constexpr float kTitleRowUVE = 24.0F;
inline constexpr float kTabRowUVE = 24.0F;
inline constexpr float kLogoBoxUVE = kTitleRowUVE + kTabRowUVE; // square: as wide as the two rows are tall
// Sub row 2: the viewport settings strip.
inline constexpr float kSubRow2UVE = 27.0F;

/// The viewport shading modes, in ViewportOverlayStateUVE::shadingMode order.
inline constexpr std::array<const char*, 5> kShadingNamesUVE{"Lit", "Unshaded", "Wireframe", "Lighting", "Normals"};

[[nodiscard]] prism::Ref<prism::Widget> PanelUVE(prism::Ref<prism::Widget> child);
[[nodiscard]] prism::Ref<prism::Button> ToolButtonUVE(prism::Icon icon, std::string tooltip, std::string text = {});
[[nodiscard]] prism::Ref<prism::Widget> HSeparatorUVE();

/// #1: the engine logo box. It spans the title bar row and the document-tab row, the same mark the
/// editor has always shown there (a rounded blue square with the U), kept small like a desktop
/// editor's app mark so the title bar stays thin.
class LogoBoxUVE final : public prism::Widget {
public:
    prism::Vec2 measure(prism::Vec2) override;
    void paint(prism::Canvas& c) override;
};

/// #2: the window's document - the level's Viewport object, or the asset an asset editor has open -
/// with its name and icon, and a dot while it has unsaved changes. Clicking it calls onClick.
class DocumentTabUVE final : public prism::Widget {
public:
    void setIcon(const std::uint32_t* pixels, int size);
    void setName(std::string name);
    void setFallbackIcon(prism::Icon icon);
    void setDirty(bool dirty);
    std::function<void()> onClick;
    bool onMouseDown(const prism::MouseEvent& e) override;
    prism::Vec2 measure(prism::Vec2) override;
    void paint(prism::Canvas& c) override;

private:
    std::string m_name = "Viewport";
    const std::uint32_t* m_icon = nullptr;
    int m_iconSize = 0;
    prism::Icon m_fallback = prism::Icon::Globe;
    bool m_dirty = false;
};

/// #15: a drag strip. Reports itself to the host as caption area of the window it is in (the
/// host moves the window from it); a double click toggles maximize.
class DragStripUVE final : public prism::Widget {
public:
    /// The title bar's own strip is drawn hatched; an `extra` strip is further empty title-bar
    /// space (the rest of the document-tab row) that moves the window too.
    explicit DragStripUVE(bool extra = false) : m_extra(extra) {}
    prism::Vec2 measure(prism::Vec2) override;
    void arrange(prism::Rect r) override;
    void paint(prism::Canvas& c) override;
    bool onMouseDown(const prism::MouseEvent& e) override;

private:
    bool m_extra = false;
};

/// Appends minimize / maximize-restore / close for the window the buttons are in. `onClose`
/// runs for the close button (the level editor quits; an asset editor asks to close its document).
void AddWindowButtonsUVE(prism::Box& row, std::function<void()> onClose);

} // namespace UVE::Editor
