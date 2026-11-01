// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <memory>
#include <string>
#include <vector>
#include "uve/math/rect_int_uve.h"
#include "uve/math/rect_uve.h"
#include "uve/math/vector2_uve.h"
#include "uve/ui/prism/prism_draw_list_uve.h"
#include "uve/ui/prism/prism_style_uve.h"

namespace UVE::UI::Prism {

/// Base interface for every retained Prism widget.
/// Implements the two-pass layout contract:
/// 1. MeasureUVE() - Bottom-up: Computes m_desiredSize based on content + constraints.
/// 2. ArrangeUVE() - Top-down: Places widget inside final allocated Rect.
/// 3. PaintUVE()   - Emits draw commands into PrismDrawListUVE.
class PrismWidgetUVE : public std::enable_shared_from_this<PrismWidgetUVE> {
public:
    explicit PrismWidgetUVE(std::string debugName = "Widget");
    virtual ~PrismWidgetUVE() = default;

    PrismWidgetUVE(const PrismWidgetUVE&) = delete;
    PrismWidgetUVE& operator=(const PrismWidgetUVE&) = delete;

    // --- Hierarchy ---
    void AddChild(std::shared_ptr<PrismWidgetUVE> child);
    void RemoveChild(const std::shared_ptr<PrismWidgetUVE>& child);
    void ClearChildren();
    [[nodiscard]] const std::vector<std::shared_ptr<PrismWidgetUVE>>& GetChildren() const noexcept { return m_children; }
    [[nodiscard]] std::shared_ptr<PrismWidgetUVE> GetParent() const noexcept { return m_parent.lock(); }

    // --- Styling & Properties ---
    PrismStyleUVE& GetStyle() noexcept { return m_style; }
    [[nodiscard]] const PrismStyleUVE& GetStyle() const noexcept { return m_style; }
    void SetStyle(const PrismStyleUVE& style) noexcept { m_style = style; }

    [[nodiscard]] const std::string& GetDebugName() const noexcept { return m_debugName; }
    [[nodiscard]] Math::Vector2UVE GetDesiredSize() const noexcept { return m_desiredSize; }
    [[nodiscard]] Math::RectUVE GetLayoutRect() const noexcept { return m_layoutRect; }

    // --- Core Layout & Paint Contract ---
    /// Bottom-up measure pass: determines preferred content bounds given available parent limits.
    virtual Math::Vector2UVE MeasureUVE(Math::Vector2UVE availableSize);

    /// Top-down arrange pass: positions widget and distributes final layout space to children.
    virtual void ArrangeUVE(const Math::RectUVE& finalRect);

    /// Generates draw list geometry and applies hierarchical clip rects if clipChildren is enabled.
    virtual void PaintUVE(PrismDrawListUVE& drawList);

    // --- Input & Hit-Testing ---
    [[nodiscard]] virtual bool HitTestUVE(Math::Vector2UVE screenPoint) const noexcept;

protected:
    virtual Math::Vector2UVE OnMeasureContentUVE(Math::Vector2UVE availableSize);
    virtual void OnPaintContentUVE(PrismDrawListUVE& drawList);

    std::string                                     m_debugName;
    PrismStyleUVE                                   m_style;
    std::weak_ptr<PrismWidgetUVE>                   m_parent;
    std::vector<std::shared_ptr<PrismWidgetUVE>>    m_children;

    Math::Vector2UVE                                m_desiredSize{0.0F, 0.0F};
    Math::RectUVE                                   m_layoutRect{};
};

} // namespace UVE::UI::Prism
