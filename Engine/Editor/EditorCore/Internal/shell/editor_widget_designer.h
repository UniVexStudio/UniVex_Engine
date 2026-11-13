// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
#pragma once

#include "editor_widget_icons.h"
#include "entity.h"
#include "prism/Widget.h"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace UVE::Editor {

/// The Widget Editor's Designer: a Widget Blueprint's screen at its design resolution, scaled to
/// fit, every widget drawn the way the game draws it (text, buttons, images, bars, sliders, check
/// boxes, text boxes). Clicking picks a widget; dragging moves it (one undo step, made by the
/// owner through onMoved). What it shows is given to it (SetItemsUVE) from the open document.
class WidgetDesignerViewUVE final : public prism::Widget {
public:
    using KindUVE = WidgetKindUVE;

    struct ItemUVE {
        Scene::EntityUVE entity = Scene::kInvalidEntityUVE;
        /// The widget it sits in (a Button's label sits in the Button), or none.
        Scene::EntityUVE parent = Scene::kInvalidEntityUVE;
        KindUVE kind = KindUVE::Image;
        std::string name;
        /// Design pixels: top-left origin, as the runtime lays them out.
        prism::Rect rect{};
        std::string text;
        prism::Color color{255, 255, 255};
        float fontSize = 16.0F;
        /// A bar's or slider's fraction, a check box's state.
        float value = 0.0F;
        bool checked = false;
        bool visible = true;
    };

    WidgetDesignerViewUVE();

    void SetItemsUVE(std::vector<ItemUVE> items);
    void SetSelectedUVE(Scene::EntityUVE entity);
    void SetResolutionUVE(float width, float height);
    [[nodiscard]] float WidthUVE() const noexcept { return m_designW; }
    [[nodiscard]] float HeightUVE() const noexcept { return m_designH; }

    /// A widget clicked (kInvalidEntityUVE: the empty canvas).
    std::function<void(Scene::EntityUVE)> onPicked;
    /// A widget dragged by `delta` design pixels: live while held, `finished` when let go.
    std::function<void(Scene::EntityUVE, float dx, float dy, bool finished)> onMoved;

    prism::Vec2 measure(prism::Vec2 available) override;
    void paint(prism::Canvas& c) override;
    bool onMouseDown(const prism::MouseEvent& e) override;
    bool onMouseMove(const prism::MouseEvent& e) override;
    bool onMouseUp(const prism::MouseEvent& e) override;

private:
    /// Where the design canvas sits in the view, and its scale.
    [[nodiscard]] prism::Rect CanvasRectUVE() const;
    [[nodiscard]] float ScaleUVE() const;
    [[nodiscard]] prism::Rect ToViewUVE(const prism::Rect& design) const;
    [[nodiscard]] Scene::EntityUVE HitUVE(prism::Vec2 p) const;

    std::vector<ItemUVE> m_items;
    Scene::EntityUVE m_selected = Scene::kInvalidEntityUVE;
    Scene::EntityUVE m_hover = Scene::kInvalidEntityUVE;
    float m_designW = 1920.0F;
    float m_designH = 1080.0F;
    bool m_dragging = false;
    bool m_moved = false;
    prism::Vec2 m_dragLast{};
    prism::Vec2 m_dragAccum{};
};

} // namespace UVE::Editor
