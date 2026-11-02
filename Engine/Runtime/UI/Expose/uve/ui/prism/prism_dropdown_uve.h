// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <functional>
#include <string>
#include <vector>
#include "uve/ui/prism/prism_theme_uve.h"
#include "uve/ui/prism/prism_widget_uve.h"

namespace UVE::UI::Prism {

/// Modern popup dropdown / combobox widget for enum and list selection.
class PrismDropdownUVE : public PrismWidgetUVE {
public:
    explicit PrismDropdownUVE(
        std::vector<std::string> items = {},
        int initialSelectedIndex = 0
    );

    void SetItems(std::vector<std::string> items);
    [[nodiscard]] const std::vector<std::string>& GetItems() const noexcept { return m_items; }

    void SetSelectedIndex(int index) noexcept;
    [[nodiscard]] int GetSelectedIndex() const noexcept { return m_selectedIndex; }
    [[nodiscard]] std::string GetSelectedItem() const;

    void SetOpen(bool open) noexcept { m_isOpen = open; }
    [[nodiscard]] bool IsOpen() const noexcept { return m_isOpen; }

    void SetOnSelectionChanged(std::function<void(int, const std::string&)> callback) {
        m_onSelectionChanged = std::move(callback);
    }

    // --- State overrides ---
    PrismEventResultUVE OnPointerDown(const PrismPointerEventUVE& event) override;
    PrismEventResultUVE OnPointerMove(const PrismPointerEventUVE& event) override;
    PrismEventResultUVE OnKeyDown(const PrismKeyEventUVE& event) override;

    void OnFocusLost() override;

protected:
    Math::Vector2UVE OnMeasureContentUVE(Math::Vector2UVE availableSize) override;
    void OnPaintContentUVE(PrismDrawListUVE& drawList) override;

private:
    [[nodiscard]] Math::RectUVE GetDropdownListRect() const noexcept;
    [[nodiscard]] int GetItemIndexAtPosition(Math::Vector2UVE pos) const noexcept;

    std::vector<std::string> m_items;
    int                      m_selectedIndex = -1;
    int                      m_hoveredItemIndex = -1;
    bool                     m_isOpen = false;

    std::function<void(int, const std::string&)> m_onSelectionChanged;
};

} // namespace UVE::UI::Prism
