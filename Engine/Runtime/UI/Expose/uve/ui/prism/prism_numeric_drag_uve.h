// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <functional>
#include <limits>
#include <string>
#include "uve/ui/prism/prism_theme_uve.h"
#include "uve/ui/prism/prism_widget_uve.h"

namespace UVE::UI::Prism {

/// High-performance draggable numeric input field inspired by Unreal & Blender.
/// Supports horizontal scrub dragging, fine-scrub with Shift, and direct keyboard typing.
class PrismNumericDragUVE : public PrismWidgetUVE {
public:
    explicit PrismNumericDragUVE(
        std::string badge = "X",
        float initialValue = 0.0F,
        float speed = 0.05F,
        PrismColorUVE badgeColor = PrismColorUVE{0.85F, 0.25F, 0.25F, 1.0F}
    );

    void SetValue(float value) noexcept;
    [[nodiscard]] float GetValue() const noexcept { return m_value; }

    void SetRange(float minVal, float maxVal) noexcept;
    [[nodiscard]] float GetMin() const noexcept { return m_min; }
    [[nodiscard]] float GetMax() const noexcept { return m_max; }

    void SetSpeed(float speed) noexcept { m_speed = speed; }
    [[nodiscard]] float GetSpeed() const noexcept { return m_speed; }

    void SetBadge(std::string badge) { m_badge = std::move(badge); }
    [[nodiscard]] const std::string& GetBadge() const noexcept { return m_badge; }

    void SetBadgeColor(PrismColorUVE color) noexcept { m_badgeColor = color; }
    [[nodiscard]] PrismColorUVE GetBadgeColor() const noexcept { return m_badgeColor; }

    void SetPrecision(int decimalPlaces) noexcept { m_precision = decimalPlaces; }

    void SetOnValueChanged(std::function<void(float)> callback) {
        m_onValueChanged = std::move(callback);
    }

    // --- State overrides ---
    PrismEventResultUVE OnPointerDown(const PrismPointerEventUVE& event) override;
    PrismEventResultUVE OnPointerMove(const PrismPointerEventUVE& event) override;
    PrismEventResultUVE OnPointerUp(const PrismPointerEventUVE& event) override;
    PrismEventResultUVE OnKeyDown(const PrismKeyEventUVE& event) override;
    PrismEventResultUVE OnCharInput(const PrismCharInputEventUVE& event) override;

    void OnFocusGained() override;
    void OnFocusLost() override;

protected:
    Math::Vector2UVE OnMeasureContentUVE(Math::Vector2UVE availableSize) override;
    void OnPaintContentUVE(PrismDrawListUVE& drawList) override;

private:
    void CommitTextEdit();
    void CancelTextEdit();
    [[nodiscard]] std::string FormatValue() const;

    float m_value = 0.0F;
    float m_min = -std::numeric_limits<float>::infinity();
    float m_max = std::numeric_limits<float>::infinity();
    float m_speed = 0.05F;
    int   m_precision = 2;

    std::string   m_badge;
    PrismColorUVE m_badgeColor;

    bool  m_isDragging = false;
    float m_dragStartX = 0.0F;
    float m_dragStartValue = 0.0F;

    bool        m_isEditingText = false;
    std::string m_textBuffer;

    std::function<void(float)> m_onValueChanged;
};

} // namespace UVE::UI::Prism
