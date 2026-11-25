// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
#pragma once

#include "editor_widget_icons.h"
#include "entity.h"
#include "prism/Widget.h"

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace UVE::Editor {

/// The Widget Editor's Designer: a Widget Blueprint's screen at its design resolution, scaled to
/// fit, every widget drawn the way the game draws it (text, buttons, images, bars, sliders, check
/// boxes, text boxes). Clicking picks a widget; dragging moves it (one undo step, made by the
/// owner through onMoved). What it shows is given to it (SetItems) from the open document.
class WidgetDesignerView final : public prism::Widget {
public:
    using Kind = WidgetKind;

    struct Item {
        Scene::Entity entity = Scene::kInvalidEntity;
        /// The widget it sits in (a Button's label sits in the Button), or none.
        Scene::Entity parent = Scene::kInvalidEntity;
        Kind kind = Kind::Image;
        std::string name;
        /// Design pixels: top-left origin, as the runtime lays them out.
        prism::Rect rect{};
        std::string text;
        prism::Color color{255, 255, 255};
        float fontSize = 16.0F;
        /// Text laid out as the runtime lays it out: each line's baseline start (design pixels).
        std::vector<std::pair<prism::Vec2, std::string>> lines;
        prism::Color shadow{0, 0, 0, 0};
        prism::Vec2 shadowOffset{};
        prism::Color outline{0, 0, 0, 0};
        float outlineWidth = 0.0F;
        /// A bar's or slider's fraction, a check box's state.
        float value = 0.0F;
        bool checked = false;
        bool visible = true;
    };

    WidgetDesignerView();
    /// The width of `text` at `fontSize` design pixels, in the Designer's own font.
    [[nodiscard]] static float MeasureText(std::string_view text, float fontSize);

    void SetItems(std::vector<Item> items);
    [[nodiscard]] const std::vector<Item>& Items() const noexcept { return m_items; }
    void SetSelected(Scene::Entity entity);
    /// The 3D View whose live picture the owner draws under the Designer: its rect is left open.
    void SetLivePreview(Scene::Entity entity);
    [[nodiscard]] Scene::Entity LivePreview() const noexcept { return m_livePreview; }
    /// Where a widget is drawn, in window pixels, cut to the visible canvas (empty when it is not shown).
    [[nodiscard]] prism::Rect ViewRectOf(Scene::Entity entity) const;
    void SetResolution(float width, float height);
    [[nodiscard]] float Width() const noexcept { return m_designW; }
    [[nodiscard]] float Height() const noexcept { return m_designH; }

    /// A widget clicked (kInvalidEntity: the empty canvas).
    std::function<void(Scene::Entity)> onPicked;
    /// A widget dragged by `delta` design pixels: live while held, `finished` when let go.
    std::function<void(Scene::Entity, float dx, float dy, bool finished)> onMoved;
    /// A widget resized from one of its corner handles to `rect` (design pixels): live, then `finished`.
    std::function<void(Scene::Entity, prism::Rect rect, bool finished)> onResized;

    prism::Vec2 measure(prism::Vec2 available) override;
    void paint(prism::Canvas& c) override;
    bool onMouseDown(const prism::MouseEvent& e) override;
    bool onMouseMove(const prism::MouseEvent& e) override;
    bool onMouseUp(const prism::MouseEvent& e) override;
    prism::Cursor cursor() const override;

private:
    /// Where the design canvas sits in the view, and its scale.
    [[nodiscard]] prism::Rect CanvasRect() const;
    [[nodiscard]] float Scale() const;
    [[nodiscard]] prism::Rect ToView(const prism::Rect& design) const;
    [[nodiscard]] Scene::Entity Hit(prism::Vec2 p) const;

    std::vector<Item> m_items;
    Scene::Entity m_selected = Scene::kInvalidEntity;
    Scene::Entity m_hover = Scene::kInvalidEntity;
    Scene::Entity m_livePreview = Scene::kInvalidEntity;
    /// The selected widget's corner handle under `p` (0 top-left, 1 top-right, 2 bottom-left,
    /// 3 bottom-right), or -1.
    [[nodiscard]] int HandleAt(prism::Vec2 p) const;
    int m_resizeCorner = -1;
    int m_hoverCorner = -1;
    prism::Rect m_resizeStart{};
    prism::Vec2 m_resizePress{};
    bool m_resized = false;
    float m_designW = 1920.0F;
    float m_designH = 1080.0F;
    bool m_dragging = false;
    bool m_moved = false;
    prism::Vec2 m_dragLast{};
    prism::Vec2 m_dragAccum{};
};

} // namespace UVE::Editor
