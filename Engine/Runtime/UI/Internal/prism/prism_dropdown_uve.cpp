// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/ui/prism/prism_dropdown_uve.h"
#include <algorithm>

namespace UVE::UI::Prism {

namespace {
constexpr float kHeaderHeightUVE = 24.0F;
constexpr float kItemHeightUVE = 22.0F;
constexpr float kCaretSizeUVE = 8.0F;
} // namespace

PrismDropdownUVE::PrismDropdownUVE(std::vector<std::string> items, int initialSelectedIndex)
    : PrismWidgetUVE("PrismDropdown"),
      m_items(std::move(items)) {
    m_isFocusable = true;
    m_style.height = PrismLengthUVE::Px(kHeaderHeightUVE);
    m_style.padding = PrismEdgesUVE{6.0F, 2.0F};

    if (!m_items.empty()) {
        m_selectedIndex = std::clamp(initialSelectedIndex, 0, static_cast<int>(m_items.size()) - 1);
    }
}

void PrismDropdownUVE::SetItems(std::vector<std::string> items) {
    m_items = std::move(items);
    if (m_items.empty()) {
        m_selectedIndex = -1;
    } else {
        m_selectedIndex = std::clamp(m_selectedIndex, 0, static_cast<int>(m_items.size()) - 1);
    }
}

void PrismDropdownUVE::SetSelectedIndex(int index) noexcept {
    if (m_items.empty()) {
        m_selectedIndex = -1;
        return;
    }

    const int clamped = std::clamp(index, 0, static_cast<int>(m_items.size()) - 1);
    if (m_selectedIndex != clamped) {
        m_selectedIndex = clamped;
        if (m_onSelectionChanged) {
            m_onSelectionChanged(m_selectedIndex, m_items[static_cast<std::size_t>(m_selectedIndex)]);
        }
    }
}

std::string PrismDropdownUVE::GetSelectedItem() const {
    if (m_selectedIndex >= 0 && static_cast<std::size_t>(m_selectedIndex) < m_items.size()) {
        return m_items[static_cast<std::size_t>(m_selectedIndex)];
    }
    return "";
}

Math::RectUVE PrismDropdownUVE::GetDropdownListRect() const noexcept {
    const float listY = m_layoutRect.position.y + m_layoutRect.size.y + 2.0F;
    const float listH = static_cast<float>(m_items.size()) * kItemHeightUVE + 4.0F;
    return Math::RectUVE{{m_layoutRect.position.x, listY}, {m_layoutRect.size.x, listH}};
}

int PrismDropdownUVE::GetItemIndexAtPosition(Math::Vector2UVE pos) const noexcept {
    const Math::RectUVE listRect = GetDropdownListRect();
    if (!Math::ContainsUVE(listRect, pos)) {
        return -1;
    }

    const float relativeY = pos.y - (listRect.position.y + 2.0F);
    if (relativeY < 0.0F) return -1;

    const int idx = static_cast<int>(relativeY / kItemHeightUVE);
    if (idx >= 0 && static_cast<std::size_t>(idx) < m_items.size()) {
        return idx;
    }
    return -1;
}

bool PrismDropdownUVE::HitTestUVE(Math::Vector2UVE screenPoint) const noexcept {
    if (PrismWidgetUVE::HitTestUVE(screenPoint)) {
        return true;
    }
    return m_isOpen && !m_items.empty() && Math::ContainsUVE(GetDropdownListRect(), screenPoint);
}

void PrismDropdownUVE::OnFocusLost() {
    m_isOpen = false;
    PrismWidgetUVE::OnFocusLost();
}

PrismEventResultUVE PrismDropdownUVE::OnPointerDown(const PrismPointerEventUVE& event) {
    PrismWidgetUVE::OnPointerDown(event);

    if (event.button == Input::MouseButtonUVE::Left) {
        if (m_isOpen) {
            const int clickedItem = GetItemIndexAtPosition(event.position);
            if (clickedItem >= 0) {
                SetSelectedIndex(clickedItem);
                m_isOpen = false;
                return PrismEventResultUVE::Handled;
            }

            // Clicked header to close
            if (Math::ContainsUVE(m_layoutRect, event.position)) {
                m_isOpen = false;
                return PrismEventResultUVE::Handled;
            }

            // Clicked outside
            m_isOpen = false;
            return PrismEventResultUVE::Unhandled;
        }

        // Collapsed -> Open
        if (Math::ContainsUVE(m_layoutRect, event.position)) {
            m_isOpen = true;
            return PrismEventResultUVE::Handled;
        }
    }

    return PrismEventResultUVE::Unhandled;
}

PrismEventResultUVE PrismDropdownUVE::OnPointerMove(const PrismPointerEventUVE& event) {
    if (m_isOpen) {
        m_hoveredItemIndex = GetItemIndexAtPosition(event.position);
        return PrismEventResultUVE::Handled;
    }
    return PrismWidgetUVE::OnPointerMove(event);
}

PrismEventResultUVE PrismDropdownUVE::OnKeyDown(const PrismKeyEventUVE& event) {
    if (event.keyCode == Input::KeyCodeUVE::Escape) {
        if (m_isOpen) {
            m_isOpen = false;
            return PrismEventResultUVE::Handled;
        }
    }

    if (event.keyCode == Input::KeyCodeUVE::Down) {
        if (!m_isOpen) {
            m_isOpen = true;
        } else {
            SetSelectedIndex(m_selectedIndex + 1);
        }
        return PrismEventResultUVE::Handled;
    }

    if (event.keyCode == Input::KeyCodeUVE::Up) {
        if (m_isOpen) {
            SetSelectedIndex(m_selectedIndex - 1);
            return PrismEventResultUVE::Handled;
        }
    }

    if (event.keyCode == Input::KeyCodeUVE::Enter) {
        if (m_isOpen) {
            m_isOpen = false;
            return PrismEventResultUVE::Handled;
        }
    }

    return PrismWidgetUVE::OnKeyDown(event);
}

Math::Vector2UVE PrismDropdownUVE::OnMeasureContentUVE(Math::Vector2UVE /*availableSize*/) {
    float maxItemWidth = 80.0F;
    for (const auto& item : m_items) {
        maxItemWidth = std::max(maxItemWidth, static_cast<float>(item.length()) * 7.5F);
    }
    return Math::Vector2UVE{maxItemWidth + 40.0F + m_style.padding.Horizontal(), kHeaderHeightUVE};
}

void PrismDropdownUVE::OnPaintContentUVE(PrismDrawListUVE& drawList) {
    PrismWidgetUVE::OnPaintContentUVE(drawList);

    const auto& theme = PrismThemeUVE::Dark();

    // 1. Header Card
    const PrismColorUVE headerBg = m_isOpen ? theme.bgWidgetActive : (m_isHovered ? theme.bgWidgetHover : theme.bgWidget);
    drawList.AddRectFilled(m_layoutRect, headerBg, theme.radiusSmall);

    const PrismColorUVE border = (m_isFocused || m_isOpen) ? theme.accentFocusRing : theme.borderSubtle;
    drawList.AddRectStroke(m_layoutRect, border, 1.0F, theme.radiusSmall);

    // Selected Text
    const std::string text = GetSelectedItem().empty() ? "Select..." : GetSelectedItem();
    const float textX = m_layoutRect.position.x + m_style.padding.left;
    const float textY = m_layoutRect.position.y + (m_layoutRect.size.y - 14.0F) * 0.5F;
    drawList.AddText(Math::Vector2UVE{textX, textY}, text, GetSelectedItem().empty() ? theme.textSecondary : theme.textPrimary, 13.0F);

    // Caret Indicator (Down chevron)
    const float caretX = m_layoutRect.position.x + m_layoutRect.size.x - 16.0F;
    const float caretY = m_layoutRect.position.y + m_layoutRect.size.y * 0.5F;
    drawList.AddLine(Math::Vector2UVE{caretX - 4.0F, caretY - 2.0F}, Math::Vector2UVE{caretX, caretY + 2.5F}, theme.textSecondary, 1.5F);
    drawList.AddLine(Math::Vector2UVE{caretX, caretY + 2.5F}, Math::Vector2UVE{caretX + 4.0F, caretY - 2.0F}, theme.textSecondary, 1.5F);

    // 2. Open Popup List
    if (m_isOpen && !m_items.empty()) {
        const Math::RectUVE listRect = GetDropdownListRect();

        // Popup background with shadow effect
        drawList.AddRectFilled(listRect, theme.bgSurfaceElevated, theme.radiusSmall);
        drawList.AddRectStroke(listRect, theme.borderProminent, 1.0F, theme.radiusSmall);

        for (std::size_t i = 0; i < m_items.size(); ++i) {
            const float itemY = listRect.position.y + 2.0F + static_cast<float>(i) * kItemHeightUVE;
            const Math::RectUVE itemRect{
                {listRect.position.x + 2.0F, itemY},
                {listRect.size.x - 4.0F, kItemHeightUVE}
            };

            const bool isHovered = (static_cast<int>(i) == m_hoveredItemIndex);
            const bool isSelected = (static_cast<int>(i) == m_selectedIndex);

            if (isSelected) {
                drawList.AddRectFilled(itemRect, PrismColorUVE{0.18F, 0.28F, 0.46F, 0.45F}, theme.radiusSmall);
                drawList.AddRectStroke(itemRect, PrismColorUVE{0.32F, 0.48F, 0.78F, 0.35F}, 1.0F, theme.radiusSmall);
            } else if (isHovered) {
                drawList.AddRectFilled(itemRect, theme.bgWidgetHover, theme.radiusSmall);
            }

            // Item text
            const auto& itemStr = m_items[i];
            const float itTextX = itemRect.position.x + 8.0F;
            const float itTextY = itemRect.position.y + (kItemHeightUVE - 14.0F) * 0.5F;
            drawList.AddText(Math::Vector2UVE{itTextX, itTextY}, itemStr, isSelected ? theme.textPrimary : theme.textSecondary, 13.0F);
        }
    }
}

} // namespace UVE::UI::Prism
