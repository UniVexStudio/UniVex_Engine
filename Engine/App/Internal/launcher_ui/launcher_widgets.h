// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <string>

#include "prism/Controls.h"
#include "prism/Icons.h"
#include "prism/Widget.h"

namespace UVE::LauncherUI {

/// One full-width selectable row (a sidebar page, a list choice): icon and label left-aligned, a
/// filled background on hover, and an accent bar along the left edge while it is the active one.
class NavItemUVE final : public prism::Widget {
public:
    NavItemUVE(std::string label, prism::Icon icon);

    void SetActiveUVE(bool active);
    [[nodiscard]] bool IsActiveUVE() const noexcept { return m_active; }
    void SetCompactUVE(bool compact);

    prism::Callback<> onActivate;

    prism::Vec2 measure(prism::Vec2 available) override;
    void paint(prism::Canvas& canvas) override;
    bool onMouseDown(const prism::MouseEvent& event) override;
    bool onMouseUp(const prism::MouseEvent& event) override;
    bool onKeyDown(const prism::KeyEvent& event) override;
    void onMouseEnter() override { invalidate(); }
    void onMouseLeave() override {
        m_pressed = false;
        invalidate();
    }
    [[nodiscard]] bool focusable() const override { return true; }
    [[nodiscard]] prism::Cursor cursor() const override { return prism::Cursor::Hand; }
    [[nodiscard]] prism::AccessRole accessRole() const override { return prism::AccessRole::ToggleButton; }
    [[nodiscard]] std::string accessName() const override { return m_label; }
    [[nodiscard]] bool accessChecked() const override { return m_active; }
    bool accessActivate() override;

private:
    std::string m_label;
    prism::Icon m_icon;
    bool m_active = false;
    bool m_pressed = false;
    bool m_compact = false;
};

/// The UniVex mark at a fixed size, from the brand image the host loads into PRISM. Without that
/// image (assets missing) it shows a plain accent disc of the same size.
class BrandMarkUVE final : public prism::Widget {
public:
    explicit BrandMarkUVE(float size) : m_size(size) {}
    prism::Vec2 measure(prism::Vec2 available) override;
    void paint(prism::Canvas& canvas) override;
    [[nodiscard]] bool hitSelf() const override { return false; }

private:
    float m_size;
};

} // namespace UVE::LauncherUI
