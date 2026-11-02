// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/ui/prism/prism_tab_group_uve.h"
#include <algorithm>
#include <cmath>

namespace UVE::UI::Prism {

namespace {
constexpr float kTabBarHeightUVE = 28.0F;
constexpr float kCloseButtonSizeUVE = 14.0F;
constexpr float kMinTabWidthUVE = 80.0F;
constexpr float kMaxTabWidthUVE = 200.0F;
} // namespace

PrismTabGroupUVE::PrismTabGroupUVE(std::string debugName)
    : PrismWidgetUVE(std::move(debugName)) {
    m_isFocusable = true;
}

void PrismTabGroupUVE::AddTab(std::string title, std::shared_ptr<PrismWidgetUVE> content, bool closable) {
    if (content) {
        AddChild(content);
    }
    m_tabs.push_back(PrismTabItemUVE{std::move(title), std::move(content), closable});
    if (m_tabs.size() == 1) {
        m_activeTabIndex = 0;
    }
}

void PrismTabGroupUVE::RemoveTab(std::size_t index) {
    if (index >= m_tabs.size()) return;

    if (m_tabs[index].content) {
        RemoveChild(m_tabs[index].content);
    }
    m_tabs.erase(m_tabs.begin() + static_cast<std::ptrdiff_t>(index));

    if (m_activeTabIndex >= m_tabs.size() && !m_tabs.empty()) {
        m_activeTabIndex = m_tabs.size() - 1;
    }
    ArrangeUVE(m_layoutRect);
}

void PrismTabGroupUVE::ClearTabs() noexcept {
    ClearChildren();
    m_tabs.clear();
    m_activeTabIndex = 0;
}

void PrismTabGroupUVE::SetActiveTabIndex(std::size_t index) noexcept {
    if (m_tabs.empty()) {
        m_activeTabIndex = 0;
        return;
    }

    const std::size_t clamped = std::min(index, m_tabs.size() - 1);
    if (m_activeTabIndex != clamped) {
        m_activeTabIndex = clamped;
        if (m_onTabSelected) {
            m_onTabSelected(m_activeTabIndex);
        }
        ArrangeUVE(m_layoutRect);
    }
}

std::shared_ptr<PrismWidgetUVE> PrismTabGroupUVE::GetActiveContent() const noexcept {
    if (m_activeTabIndex < m_tabs.size()) {
        return m_tabs[m_activeTabIndex].content;
    }
    return nullptr;
}

Math::RectUVE PrismTabGroupUVE::GetTabBarRect() const noexcept {
    return Math::RectUVE{m_layoutRect.position, {m_layoutRect.size.x, kTabBarHeightUVE}};
}

Math::RectUVE PrismTabGroupUVE::GetContentRect() const noexcept {
    const float contentY = m_layoutRect.position.y + kTabBarHeightUVE;
    const float contentH = std::max(0.0F, m_layoutRect.size.y - kTabBarHeightUVE);
    return Math::RectUVE{{m_layoutRect.position.x, contentY}, {m_layoutRect.size.x, contentH}};
}

void PrismTabGroupUVE::ComputeTabGeometries(std::vector<TabGeometryUVE>& outGeometries) const {
    outGeometries.clear();
    const Math::RectUVE barRect = GetTabBarRect();
    float currentX = barRect.position.x + 4.0F;

    for (const auto& tab : m_tabs) {
        const float textW = static_cast<float>(tab.title.length()) * 7.5F;
        const float rawW = textW + (tab.closable ? 32.0F : 16.0F);
        const float tabW = std::clamp(rawW, kMinTabWidthUVE, kMaxTabWidthUVE);

        TabGeometryUVE geom;
        geom.tabRect = Math::RectUVE{{currentX, barRect.position.y + 2.0F}, {tabW, kTabBarHeightUVE - 2.0F}};
        geom.closeRect = Math::RectUVE{
            {currentX + tabW - kCloseButtonSizeUVE - 6.0F, barRect.position.y + (kTabBarHeightUVE - kCloseButtonSizeUVE) * 0.5F},
            {kCloseButtonSizeUVE, kCloseButtonSizeUVE}
        };

        outGeometries.push_back(geom);
        currentX += tabW + 2.0F;
    }
}

void PrismTabGroupUVE::ArrangeUVE(const Math::RectUVE& finalRect) {
    m_layoutRect = finalRect;
    const Math::RectUVE contentRect = GetContentRect();

    // Arrange only the active tab content
    auto active = GetActiveContent();
    if (active) {
        active->ArrangeUVE(contentRect);
    }
}

PrismEventResultUVE PrismTabGroupUVE::OnPointerDown(const PrismPointerEventUVE& event) {
    PrismWidgetUVE::OnPointerDown(event);

    const Math::RectUVE barRect = GetTabBarRect();
    if (Math::ContainsUVE(barRect, event.position) && event.button == Input::MouseButtonUVE::Left) {
        std::vector<TabGeometryUVE> geometries;
        ComputeTabGeometries(geometries);

        for (std::size_t i = 0; i < geometries.size(); ++i) {
            if (Math::ContainsUVE(geometries[i].tabRect, event.position)) {
                // Clicked close button
                if (m_tabs[i].closable && Math::ContainsUVE(geometries[i].closeRect, event.position)) {
                    if (m_onTabCloseRequested) {
                        m_onTabCloseRequested(i);
                    } else {
                        RemoveTab(i);
                    }
                    return PrismEventResultUVE::Handled;
                }

                // Clicked tab body
                SetActiveTabIndex(i);
                m_isPotentialDrag = true;
                m_dragTabIndex = i;
                m_dragStartPos = event.position;
                return PrismEventResultUVE::Handled;
            }
        }
        return PrismEventResultUVE::Handled;
    }

    return PrismEventResultUVE::Unhandled;
}

PrismEventResultUVE PrismTabGroupUVE::OnPointerMove(const PrismPointerEventUVE& event) {
    if (m_isPotentialDrag && !m_isDragging) {
        const float dx = event.position.x - m_dragStartPos.x;
        const float dy = event.position.y - m_dragStartPos.y;
        if (std::sqrt(dx * dx + dy * dy) > 6.0F) {
            m_isDragging = true;
            if (m_onTabDragStarted) {
                m_onTabDragStarted(m_dragTabIndex, m_dragStartPos);
            }
        }
    }

    m_hoveredTabIndex = -1;
    m_hoveredCloseButton = false;

    const Math::RectUVE barRect = GetTabBarRect();
    if (Math::ContainsUVE(barRect, event.position)) {
        std::vector<TabGeometryUVE> geometries;
        ComputeTabGeometries(geometries);

        for (std::size_t i = 0; i < geometries.size(); ++i) {
            if (Math::ContainsUVE(geometries[i].tabRect, event.position)) {
                m_hoveredTabIndex = static_cast<int>(i);
                if (m_tabs[i].closable && Math::ContainsUVE(geometries[i].closeRect, event.position)) {
                    m_hoveredCloseButton = true;
                }
                break;
            }
        }
    }

    return PrismWidgetUVE::OnPointerMove(event);
}

PrismEventResultUVE PrismTabGroupUVE::OnPointerUp(const PrismPointerEventUVE& event) {
    m_isPotentialDrag = false;
    m_isDragging = false;
    return PrismWidgetUVE::OnPointerUp(event);
}

Math::Vector2UVE PrismTabGroupUVE::OnMeasureContentUVE(Math::Vector2UVE availableSize) {
    auto active = GetActiveContent();
    if (active) {
        const Math::Vector2UVE childAvail{availableSize.x, std::max(0.0F, availableSize.y - kTabBarHeightUVE)};
        const Math::Vector2UVE measured = active->MeasureUVE(childAvail);
        return Math::Vector2UVE{measured.x, measured.y + kTabBarHeightUVE};
    }
    return Math::Vector2UVE{100.0F, kTabBarHeightUVE + 50.0F};
}

void PrismTabGroupUVE::OnPaintContentUVE(PrismDrawListUVE& drawList) {
    PrismWidgetUVE::OnPaintContentUVE(drawList);

    const auto& theme = PrismThemeUVE::Dark();
    const Math::RectUVE barRect = GetTabBarRect();

    // 1. Tab Bar Background
    drawList.AddRectFilled(barRect, theme.bgSurface);

    // Bottom separator line along entire tab bar
    const Math::RectUVE lineRect{
        {barRect.position.x, barRect.position.y + barRect.size.y - 1.0F},
        {barRect.size.x, 1.0F}
    };
    drawList.AddRectFilled(lineRect, theme.borderSubtle);

    // 2. Render each tab
    std::vector<TabGeometryUVE> geometries;
    ComputeTabGeometries(geometries);

    for (std::size_t i = 0; i < geometries.size(); ++i) {
        const auto& geom = geometries[i];
        const bool isActive = (i == m_activeTabIndex);
        const bool isHovered = (static_cast<int>(i) == m_hoveredTabIndex);

        // Tab Pill Background
        const PrismColorUVE tabBg = isActive ? theme.bgPanel : (isHovered ? theme.bgWidgetHover : theme.bgSurface);
        drawList.AddRectFilled(geom.tabRect, tabBg, PrismCornerRadiiUVE{4.0F, 4.0F, 0.0F, 0.0F});

        if (isActive) {
            // Active tab accent top line or bottom border
            const Math::RectUVE activeIndicator{
                {geom.tabRect.position.x, geom.tabRect.position.y},
                {geom.tabRect.size.x, 2.0F}
            };
            drawList.AddRectFilled(activeIndicator, theme.accentPrimary);
        }

        // Tab Title Text
        const auto& title = m_tabs[i].title;
        const float textX = geom.tabRect.position.x + 8.0F;
        const float textY = geom.tabRect.position.y + (geom.tabRect.size.y - 12.0F) * 0.5F;
        const float charW = 7.5F;

        for (std::size_t c = 0; c < title.length(); ++c) {
            const float x = textX + static_cast<float>(c) * charW;
            if (x + charW < geom.closeRect.position.x) {
                drawList.AddRectFilled(
                    Math::RectUVE{{x, textY}, {charW, 12.0F}},
                    isActive ? theme.textPrimary : theme.textSecondary
                );
            }
        }

        // Tab Close Button ('x')
        if (m_tabs[i].closable) {
            const bool isCloseHovered = isHovered && m_hoveredCloseButton;
            const PrismColorUVE closeBg = isCloseHovered ? theme.accentDanger : PrismColorUVE{0.0F, 0.0F, 0.0F, 0.0F};
            if (isCloseHovered) {
                drawList.AddRectFilled(geom.closeRect, closeBg, PrismCornerRadiiUVE{3.0F});
            }

            // Close 'x' icon
            const PrismColorUVE xColor = isCloseHovered ? PrismColorUVE{1.0F, 1.0F, 1.0F, 1.0F} : theme.textMuted;
            drawList.AddRectFilled(
                Math::RectUVE{
                    {geom.closeRect.position.x + 3.0F, geom.closeRect.position.y + (geom.closeRect.size.y - 2.0F) * 0.5F},
                    {geom.closeRect.size.x - 6.0F, 2.0F}
                },
                xColor
            );
        }
    }

    // 3. Paint Active Tab Content
    auto active = GetActiveContent();
    if (active) {
        active->PaintUVE(drawList);
    }
}

} // namespace UVE::UI::Prism
