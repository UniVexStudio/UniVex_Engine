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
#include "prism/Menu.h"
#include "prism/Widget.h"

#include <array>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace UVE::Editor {

// Slimmed AAA window chrome row heights (Desktop 2-row header, Android compact 2-row header with prominent logo).
inline constexpr float kTitleRowUVE = 20.0F;
inline constexpr float kTabRowUVE = 20.0F;
inline constexpr float kLogoBoxUVE = kTitleRowUVE + kTabRowUVE; // square: spans both rows on desktop
inline constexpr float kCompactTitleRowUVE = 22.0F;
inline constexpr float kCompactToolbarRowUVE = 22.0F;
inline constexpr float kCompactLogoBoxUVE = 22.0F;
inline constexpr float kCompactTwoRowLogoUVE = kCompactTitleRowUVE + kCompactToolbarRowUVE; // prominent 44x44 logo spanning title + play/toolbar row
// Sub row 2: the viewport settings strip.
inline constexpr float kSubRow2UVE = 22.0F;
inline constexpr float kCompactSubRow2UVE = 20.0F;

/// The viewport shading modes, in ViewportOverlayStateUVE::shadingMode order.
inline constexpr std::array<const char*, 5> kShadingNamesUVE{"Lit", "Unshaded", "Wireframe", "Lighting", "Normals"};

[[nodiscard]] prism::Ref<prism::Widget> PanelUVE(prism::Ref<prism::Widget> child);
[[nodiscard]] prism::Ref<prism::Button> ToolButtonUVE(prism::Icon icon, std::string tooltip, std::string text = {});
[[nodiscard]] prism::Ref<prism::Widget> HSeparatorUVE();

/// #1: the engine logo box. Spans the title bar and document-tab rows on desktop, or spans Row 1
/// + Row 2 (play/toolbar) on mobile when `spansTwoRows` is true so the logo stays prominent.
class LogoBoxUVE final : public prism::Widget {
public:
    explicit LogoBoxUVE(bool compact = false, bool spansTwoRows = false)
        : m_compact(compact), m_spansTwoRows(spansTwoRows) {}
    prism::Vec2 measure(prism::Vec2) override;
    void paint(prism::Canvas& c) override;

private:
    bool m_compact = false;
    bool m_spansTwoRows = false;
};

/// #2: the window's document - the level's Viewport object, or the asset an asset editor has open -
/// with its name and icon, and a dot while it has unsaved changes. Clicking it calls onClick.
class DocumentTabUVE final : public prism::Widget {
public:
    void setIcon(const std::uint32_t* pixels, int size);
    void setName(std::string name);
    void setFallbackIcon(prism::Icon icon);
    void setDirty(bool dirty);
    void setCompact(bool compact);
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
    bool m_compact = false;
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

/// A viewport's camera screen: a small frame in the view's lower right corner showing what a
/// Camera3D sees while the view itself stays the editor's. It takes only its own corner of the
/// view (input elsewhere reaches the viewport); the shell renders into view().
class CameraScreenUVE final : public prism::Widget {
public:
    CameraScreenUVE();
    void setTitle(std::string title);
    [[nodiscard]] prism::Widget* view() const noexcept { return m_view.get(); }
    [[nodiscard]] prism::Rect viewRect() const noexcept { return m_viewRect; }
    prism::Vec2 measure(prism::Vec2 available) override;
    void arrange(prism::Rect r) override;
    void paint(prism::Canvas& c) override;
    bool onMouseDown(const prism::MouseEvent& e) override;

private:
    prism::Ref<prism::Widget> m_view;
    prism::Rect m_viewRect{};
    std::string m_title;
};

/// One top-level menu entry used by BuildEditorHeaderUVE.
struct EditorHeaderMenuEntryUVE final {
    std::string title;
    prism::MenuFactory factory;
};

/// Configuration for BuildEditorHeaderUVE: shared by the Level Editor and all secondary asset editors
/// (Entity, Montage, Retarget, VFX, Animation Library).
/// - On Desktop: LogoBox spans MenuBar + DocumentTab; if `toolbarRow` is supplied it sits directly below.
/// - On Mobile (single-surface): if `toolbarRow` is supplied, LogoBox spans BOTH Row 1 ([Menu] [DocTab]
///   [DragStrip] [Float/Full] [X]) AND Row 2 (`toolbarRow`), keeping the brand logo prominent and clean.
struct EditorHeaderDescUVE final {
    std::vector<EditorHeaderMenuEntryUVE> menus;
    /// Optional trailing items added to the collapsed single Menu dropdown on single-surface hosts
    /// (for example: separator + "Return to Hub").
    std::function<std::vector<prism::MenuItem>()> compactMenuTail;
    prism::Ref<DocumentTabUVE> documentTab;
    /// Optional toolbar / Play row. Integrated alongside the 2-row LogoBox on mobile, or stacked below on desktop.
    prism::Ref<prism::Widget> toolbarRow;
    std::function<void()> onClose;
    bool mainWindow = false;
    /// Optional pointer receiving the outer header row container (for display-cutout horizontal padding).
    prism::Ref<prism::Box>* outTitleRow = nullptr;
};

[[nodiscard]] prism::Ref<prism::Widget> BuildEditorHeaderUVE(EditorHeaderDescUVE desc);

/// Shared PRISM title bar for secondary tool windows (Entity Class, Settings, Shortcuts, Input Map,
/// File Dialog, Network Capture, Audio Mixer) with icon, title, DragStrip, Floating/Full toggle, and Close.
[[nodiscard]] prism::Ref<prism::Widget> BuildToolWindowTitleBarUVE(std::string title, prism::Icon icon,
                                                                    std::function<void()> onClose,
                                                                    prism::Ref<prism::Box>* outRow = nullptr);

/// Appends window chrome controls for the current window:
/// - Desktop: Minimize (-), single Floating/Full toggle, and Close (x) with red danger hover.
/// - Mobile (single-surface): hidden on mainWindow; sub-windows get a single Floating/Full toggle + Close (x).
void AddWindowButtonsUVE(prism::Box& row, std::function<void()> onClose, bool mainWindow = false);

} // namespace UVE::Editor
