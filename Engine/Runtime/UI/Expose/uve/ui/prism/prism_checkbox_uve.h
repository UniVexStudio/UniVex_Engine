// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <functional>
#include <string>
#include "uve/ui/prism/prism_theme_uve.h"
#include "uve/ui/prism/prism_widget_uve.h"

namespace UVE::UI::Prism {

/// High-performance modern toggle checkbox widget with label and focus ring.
class PrismCheckboxUVE : public PrismWidgetUVE {
public:
    explicit PrismCheckboxUVE(std::string label = "Checkbox", bool initialChecked = false);

    void SetChecked(bool checked) noexcept;
    [[nodiscard]] bool IsChecked() const noexcept { return m_checked; }

    void SetLabel(std::string label);
    [[nodiscard]] const std::string& GetLabel() const noexcept { return m_label; }

    void SetOnToggled(std::function<void(bool)> callback) { m_onToggled = std::move(callback); }

    // --- State overrides ---
    PrismEventResultUVE OnPointerDown(const PrismPointerEventUVE& event) override;
    PrismEventResultUVE OnKeyDown(const PrismKeyEventUVE& event) override;

protected:
    Math::Vector2UVE OnMeasureContentUVE(Math::Vector2UVE availableSize) override;
    void OnPaintContentUVE(PrismDrawListUVE& drawList) override;

    bool                      m_checked = false;
    std::string               m_label;
    std::function<void(bool)> m_onToggled;
};

} // namespace UVE::UI::Prism
