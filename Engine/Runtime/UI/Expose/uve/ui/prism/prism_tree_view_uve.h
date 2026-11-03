// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>
#include "uve/ui/prism/prism_theme_uve.h"
#include "uve/ui/prism/prism_widget_uve.h"

namespace UVE::UI::Prism {

class PrismTreeNodeUVE : public std::enable_shared_from_this<PrismTreeNodeUVE> {
public:
    explicit PrismTreeNodeUVE(std::string label, std::uint64_t entityId = 0);

    void AddChild(std::shared_ptr<PrismTreeNodeUVE> child);
    void RemoveChild(const std::shared_ptr<PrismTreeNodeUVE>& child);
    void ClearChildren() noexcept;

    [[nodiscard]] const std::vector<std::shared_ptr<PrismTreeNodeUVE>>& GetChildren() const noexcept {
        return m_children;
    }

    [[nodiscard]] const std::string& GetLabel() const noexcept { return m_label; }
    void SetLabel(std::string label) { m_label = std::move(label); }

    [[nodiscard]] std::uint64_t GetEntityId() const noexcept { return m_entityId; }
    void SetEntityId(std::uint64_t id) noexcept { m_entityId = id; }

    [[nodiscard]] bool IsExpanded() const noexcept { return m_isExpanded; }
    void SetExpanded(bool expanded) noexcept { m_isExpanded = expanded; }

    [[nodiscard]] bool IsSelected() const noexcept { return m_isSelected; }
    void SetSelected(bool selected) noexcept { m_isSelected = selected; }

    [[nodiscard]] bool IsLeaf() const noexcept { return m_children.empty(); }

private:
    std::string m_label;
    std::uint64_t m_entityId = 0;
    bool m_isExpanded = false;
    bool m_isSelected = false;
    std::vector<std::shared_ptr<PrismTreeNodeUVE>> m_children;
};

/// High-performance expandable tree view for Scene Hierarchy and Outliners.
class PrismTreeViewUVE : public PrismWidgetUVE {
public:
    explicit PrismTreeViewUVE(std::string debugName = "TreeView");

    void AddRootNode(std::shared_ptr<PrismTreeNodeUVE> node);
    void ClearNodes() noexcept;
    [[nodiscard]] const std::vector<std::shared_ptr<PrismTreeNodeUVE>>& GetRootNodes() const noexcept {
        return m_roots;
    }

    void SelectNode(const std::shared_ptr<PrismTreeNodeUVE>& node);
    [[nodiscard]] std::shared_ptr<PrismTreeNodeUVE> GetSelectedNode() const noexcept {
        return m_selectedNode;
    }

    void SetOnSelectionChanged(std::function<void(const std::shared_ptr<PrismTreeNodeUVE>&)> callback) {
        m_onSelectionChanged = std::move(callback);
    }

    // --- State overrides ---
    void ArrangeUVE(const Math::RectUVE& finalRect) override;
    PrismEventResultUVE OnPointerDown(const PrismPointerEventUVE& event) override;
    PrismEventResultUVE OnPointerMove(const PrismPointerEventUVE& event) override;
    PrismEventResultUVE OnKeyDown(const PrismKeyEventUVE& event) override;

protected:
    Math::Vector2UVE OnMeasureContentUVE(Math::Vector2UVE availableSize) override;
    void OnPaintContentUVE(PrismDrawListUVE& drawList) override;

private:
    struct FlatRowUVE {
        std::shared_ptr<PrismTreeNodeUVE> node;
        int depth = 0;
        Math::RectUVE rowRect;
        Math::RectUVE arrowRect;
    };

    void BuildFlatRows(
        const std::vector<std::shared_ptr<PrismTreeNodeUVE>>& nodes,
        int depth,
        std::vector<FlatRowUVE>& outRows
    ) const;

    std::vector<std::shared_ptr<PrismTreeNodeUVE>> m_roots;
    std::shared_ptr<PrismTreeNodeUVE>             m_selectedNode;
    int                                           m_hoveredRowIndex = -1;

    mutable std::vector<FlatRowUVE>               m_flatRows;

    std::function<void(const std::shared_ptr<PrismTreeNodeUVE>&)> m_onSelectionChanged;
};

} // namespace UVE::UI::Prism
