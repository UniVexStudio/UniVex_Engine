// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/ui/ui_layout_uve.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "uve/component/entity_uve.h"
#include "uve/component/hierarchy_component_uve.h"
#include "uve/component/ui_button_component_uve.h"
#include "uve/component/ui_checkbox_component_uve.h"
#include "uve/component/ui_dropdown_component_uve.h"
#include "uve/component/ui_image_component_uve.h"
#include "uve/component/ui_scroll_container_component_uve.h"
#include "uve/component/ui_text_input_component_uve.h"
#include "uve/component/ui_layout_container_component_uve.h"
#include "uve/component/ui_progress_bar_component_uve.h"
#include "uve/component/ui_slider_component_uve.h"
#include "uve/component/ui_text_component_uve.h"
#include "uve/entity/i_entity_manager_uve.h"
#include "uve/input/i_input_system_uve.h"
#include "uve/input/key_code_uve.h"
#include "uve/math/vector2_uve.h"
#include "uve/ui/canvas_ancestry_uve.h"
#include "uve/ui/ui_font_atlas_uve.h"
#include "uve/ui/ui_scroll_uve.h"

namespace UVE::UI {

namespace {

struct OrderedContainerUVE final {
    Scene::EntityUVE entity = Scene::kInvalidEntityUVE;
    std::size_t depth = 0U;
};

struct OrderedChildUVE final {
    Scene::EntityUVE entity = Scene::kInvalidEntityUVE;
    std::int64_t siblingOrder = 0;
};

[[nodiscard]] bool IsEarlierChildUVE(const OrderedChildUVE& lhs, const OrderedChildUVE& rhs) {
    if (lhs.siblingOrder != rhs.siblingOrder) {
        return lhs.siblingOrder < rhs.siblingOrder;
    }
    if (lhs.entity.index != rhs.entity.index) {
        return lhs.entity.index < rhs.entity.index;
    }
    return lhs.entity.generation < rhs.entity.generation;
}

struct PositionableChildUVE final {
    Scene::EntityUVE entity = Scene::kInvalidEntityUVE;
    Math::Vector2UVE extent{};
    bool isButton = false;
    bool isImage = false;
    bool isContainer = false;
    bool isSlider = false;
    bool isProgress = false;
    bool isCheckbox = false;
    bool isDropdown = false;
    bool isTextInput = false;
    bool isScrollContainer = false;
    bool isText = false;
};

void MoveChildUVE(Scene::IEntityManagerUVE& entityManager, const PositionableChildUVE& child,
                  const Math::Vector2UVE position) {
    if (child.isButton) {
        entityManager.GetComponentUVE<Scene::UIButtonComponentUVE>(child.entity).rect.position = position;
    }
    if (child.isImage) {
        entityManager.GetComponentUVE<Scene::UIImageComponentUVE>(child.entity).rect.position = position;
    }
    if (child.isContainer) {
        entityManager.GetComponentUVE<Scene::UILayoutContainerComponentUVE>(child.entity).rect.position = position;
    }
    if (child.isSlider) {
        entityManager.GetComponentUVE<Scene::UISliderComponentUVE>(child.entity).rect.position = position;
    }
    if (child.isProgress) {
        entityManager.GetComponentUVE<Scene::UIProgressBarComponentUVE>(child.entity).rect.position = position;
    }
    if (child.isCheckbox) {
        entityManager.GetComponentUVE<Scene::UICheckboxComponentUVE>(child.entity).rect.position = position;
    }
    if (child.isDropdown) {
        entityManager.GetComponentUVE<Scene::UIDropdownComponentUVE>(child.entity).rect.position = position;
    }
    if (child.isTextInput) {
        entityManager.GetComponentUVE<Scene::UITextInputComponentUVE>(child.entity).rect.position = position;
    }
    if (child.isScrollContainer) {
        entityManager.GetComponentUVE<Scene::UIScrollContainerComponentUVE>(child.entity).rect.position = position;
    }
    if (child.isText) {
        entityManager.GetComponentUVE<Scene::UITextComponentUVE>(child.entity).positionPixels = position;
    }
}

std::vector<OrderedChildUVE> CollectContainerChildrenUVE(Scene::IEntityManagerUVE& entityManager,
                                                         const std::vector<OrderedChildUVE>& candidates,
                                                         const Scene::EntityUVE container) {
    std::vector<OrderedChildUVE> children;
    for (const OrderedChildUVE& candidate : candidates) {
        if (entityManager.GetComponentUVE<Scene::HierarchyComponentUVE>(candidate.entity).parent == container) {
            children.push_back(candidate);
        }
    }
    std::sort(children.begin(), children.end(), IsEarlierChildUVE);
    return children;
}

std::vector<PositionableChildUVE> CollectContainerItemsUVE(Scene::IEntityManagerUVE& entityManager,
                                                           const std::vector<OrderedChildUVE>& children,
                                                           const UIFontAtlasUVE& fontAtlas) {
    std::vector<PositionableChildUVE> items;
    for (const OrderedChildUVE& child : children) {
        PositionableChildUVE item;
        item.entity = child.entity;
        item.isButton = entityManager.HasComponentUVE<Scene::UIButtonComponentUVE>(child.entity);
        item.isImage = entityManager.HasComponentUVE<Scene::UIImageComponentUVE>(child.entity);
        item.isContainer = entityManager.HasComponentUVE<Scene::UILayoutContainerComponentUVE>(child.entity);
        item.isSlider = entityManager.HasComponentUVE<Scene::UISliderComponentUVE>(child.entity);
        item.isProgress = entityManager.HasComponentUVE<Scene::UIProgressBarComponentUVE>(child.entity);
        item.isCheckbox = entityManager.HasComponentUVE<Scene::UICheckboxComponentUVE>(child.entity);
        item.isDropdown = entityManager.HasComponentUVE<Scene::UIDropdownComponentUVE>(child.entity);
        item.isTextInput = entityManager.HasComponentUVE<Scene::UITextInputComponentUVE>(child.entity);
        item.isScrollContainer =
            entityManager.HasComponentUVE<Scene::UIScrollContainerComponentUVE>(child.entity);
        item.isText = entityManager.HasComponentUVE<Scene::UITextComponentUVE>(child.entity);
        if (!item.isButton && !item.isImage && !item.isContainer && !item.isSlider && !item.isProgress &&
            !item.isCheckbox && !item.isDropdown && !item.isTextInput && !item.isScrollContainer && !item.isText) {
            continue; // nothing positionable: ignored, not even spaced
        }
        if (item.isButton) {
            item.extent = entityManager.GetComponentUVE<Scene::UIButtonComponentUVE>(child.entity).rect.size;
        } else if (item.isImage) {
            item.extent = entityManager.GetComponentUVE<Scene::UIImageComponentUVE>(child.entity).rect.size;
        } else if (item.isContainer) {
            item.extent =
                entityManager.GetComponentUVE<Scene::UILayoutContainerComponentUVE>(child.entity).rect.size;
        } else if (item.isSlider) {
            item.extent = entityManager.GetComponentUVE<Scene::UISliderComponentUVE>(child.entity).rect.size;
        } else if (item.isProgress) {
            item.extent = entityManager.GetComponentUVE<Scene::UIProgressBarComponentUVE>(child.entity).rect.size;
        } else if (item.isCheckbox) {
            item.extent = entityManager.GetComponentUVE<Scene::UICheckboxComponentUVE>(child.entity).rect.size;
        } else if (item.isDropdown) {
            item.extent = entityManager.GetComponentUVE<Scene::UIDropdownComponentUVE>(child.entity).rect.size;
        } else if (item.isTextInput) {
            item.extent = entityManager.GetComponentUVE<Scene::UITextInputComponentUVE>(child.entity).rect.size;
        } else if (item.isScrollContainer) {
            item.extent =
                entityManager.GetComponentUVE<Scene::UIScrollContainerComponentUVE>(child.entity).rect.size;
        } else {
            const Scene::UITextComponentUVE& text =
                entityManager.GetComponentUVE<Scene::UITextComponentUVE>(child.entity);
            item.extent = Math::Vector2UVE{fontAtlas.MeasureTextWidthUVE(text.text, text.fontSize), text.fontSize};
        }
        items.push_back(item);
    }
    return items;
}

/// The size the container's content asks for: children plus `spacing` between them, plus
/// `padding` on both sides of each axis. Stacks sum the main axis and take the cross maximum;
/// grids fit their cells, lines, and both-axis spacing. No items means padding only.
[[nodiscard]] Math::Vector2UVE ContentSizeUVE(const Scene::UILayoutContainerComponentUVE& layout,
                                             const std::vector<PositionableChildUVE>& items) {
    const bool vertical = layout.direction == Scene::UILayoutDirectionUVE::Vertical;
    float main = 0.0F;
    float cross = 0.0F;
    if (layout.wrapAfter != 0U) {
        Math::Vector2UVE cell{};
        for (const PositionableChildUVE& item : items) {
            cell.x = std::max(cell.x, item.extent.x);
            cell.y = std::max(cell.y, item.extent.y);
        }
        const std::size_t slots = std::min(items.size(), static_cast<std::size_t>(layout.wrapAfter));
        if (slots > 0U) {
            const float cellMain = vertical ? cell.y : cell.x;
            const float cellCross = vertical ? cell.x : cell.y;
            main = static_cast<float>(slots) * cellMain + static_cast<float>(slots - 1U) * layout.spacing;
            const std::size_t lines = (items.size() + layout.wrapAfter - 1U) / layout.wrapAfter;
            cross = static_cast<float>(lines) * cellCross + static_cast<float>(lines - 1U) * layout.spacing;
        }
    } else {
        bool first = true;
        for (const PositionableChildUVE& item : items) {
            if (!first) {
                main += layout.spacing;
            }
            first = false;
            main += vertical ? item.extent.y : item.extent.x;
            cross = std::max(cross, vertical ? item.extent.x : item.extent.y);
        }
    }
    const Math::Vector2UVE content =
        vertical ? Math::Vector2UVE{cross, main} : Math::Vector2UVE{main, cross};
    return Math::Vector2UVE{content.x + 2.0F * layout.padding, content.y + 2.0F * layout.padding};
}

void AutoSizeContainerUVE(Scene::IEntityManagerUVE& entityManager, const Scene::EntityUVE container,
                          const Scene::UILayoutContainerComponentUVE& layout,
                          const std::vector<PositionableChildUVE>& items) {
    const Math::Vector2UVE content = ContentSizeUVE(layout, items);
    Scene::UILayoutContainerComponentUVE& stored =
        entityManager.GetComponentUVE<Scene::UILayoutContainerComponentUVE>(container);
    if (layout.autoSizeWidth) {
        stored.rect.size.x = content.x;
    }
    if (layout.autoSizeHeight) {
        stored.rect.size.y = content.y;
    }
}

void PositionContainerItemsUVE(Scene::IEntityManagerUVE& entityManager,
                               const Scene::UILayoutContainerComponentUVE& layout,
                               const std::vector<PositionableChildUVE>& items) {
    const bool vertical = layout.direction == Scene::UILayoutDirectionUVE::Vertical;
    const float innerMinX = layout.rect.position.x + layout.padding;
    const float innerMinY = layout.rect.position.y + layout.padding;
    const float innerWidth = std::max(0.0F, layout.rect.size.x - 2.0F * layout.padding);
    const float innerHeight = std::max(0.0F, layout.rect.size.y - 2.0F * layout.padding);
    const float alignFactor = layout.alignment == Scene::UILayoutAlignmentUVE::Start
                                  ? 0.0F
                                  : (layout.alignment == Scene::UILayoutAlignmentUVE::Center ? 0.5F : 1.0F);

    if (layout.wrapAfter != 0U) {
        Math::Vector2UVE cell{};
        for (const PositionableChildUVE& item : items) {
            cell.x = std::max(cell.x, item.extent.x);
            cell.y = std::max(cell.y, item.extent.y);
        }
        const float cellMain = vertical ? cell.y : cell.x;
        const float cellCross = vertical ? cell.x : cell.y;
        for (std::size_t index = 0U; index < items.size(); ++index) {
            const std::size_t line = index / layout.wrapAfter;
            const std::size_t slot = index % layout.wrapAfter;
            const float mainPos = (vertical ? innerMinY : innerMinX) +
                                  static_cast<float>(slot) * (cellMain + layout.spacing);
            const float crossBase = (vertical ? innerMinX : innerMinY) +
                                    static_cast<float>(line) * (cellCross + layout.spacing);
            const float childCross = vertical ? items[index].extent.x : items[index].extent.y;
            // Within the cell now, not the inner rect: a child larger than its cell clamps to the
            // cell's edge instead of spilling into the next one.
            const float crossOffset = std::max(0.0F, alignFactor * (cellCross - childCross));
            const Math::Vector2UVE position =
                vertical ? Math::Vector2UVE{crossBase + crossOffset, mainPos}
                         : Math::Vector2UVE{mainPos, crossBase + crossOffset};
            MoveChildUVE(entityManager, items[index], position);
        }
        return;
    }

    float cursor = 0.0F;
    bool first = true;
    for (const PositionableChildUVE& item : items) {
        if (!first) {
            cursor += layout.spacing;
        }
        first = false;
        const float mainExtent = vertical ? item.extent.y : item.extent.x;
        const float crossInner = vertical ? innerWidth : innerHeight;
        const float crossExtent = vertical ? item.extent.x : item.extent.y;
        // A child larger than the inner rect clamps to the inner edge instead of spilling past it.
        const float crossOffset = std::max(0.0F, alignFactor * (crossInner - crossExtent));
        const Math::Vector2UVE position = vertical ? Math::Vector2UVE{innerMinX + crossOffset, innerMinY + cursor}
                                                   : Math::Vector2UVE{innerMinX + cursor, innerMinY + crossOffset};
        MoveChildUVE(entityManager, item, position);
        cursor += mainExtent;
    }
}

[[nodiscard]] bool HasScrollAncestorUVE(Scene::IEntityManagerUVE& entityManager, Scene::EntityUVE entity) {
    Scene::EntityUVE current = entity;
    for (std::size_t depth = 0U; depth < kMaximumCanvasAncestorWalkUVE; ++depth) {
        if (!entityManager.IsAliveUVE(current) ||
            !entityManager.HasComponentUVE<Scene::HierarchyComponentUVE>(current)) {
            return false;
        }
        const Scene::EntityUVE parent =
            entityManager.GetComponentUVE<Scene::HierarchyComponentUVE>(current).parent;
        if (parent == Scene::kInvalidEntityUVE || !entityManager.IsAliveUVE(parent)) {
            return false;
        }
        if (entityManager.HasComponentUVE<Scene::UIScrollContainerComponentUVE>(parent)) {
            return true;
        }
        if (parent == current) {
            return false;
        }
        current = parent;
    }
    return false;
}

} // namespace

void LayoutUIScrollContainersUVE(Scene::IEntityManagerUVE& entityManager, const Input::IInputSystemUVE& input,
                                 const UIFontAtlasUVE& fontAtlas) {
    std::vector<OrderedContainerUVE> containers;
    entityManager.ForEachUVE<Scene::UIScrollContainerComponentUVE>(
        [&entityManager, &containers](const Scene::EntityUVE entity, const Scene::UIScrollContainerComponentUVE&) {
            containers.push_back(OrderedContainerUVE{entity, UIHierarchyDepthUVE(entityManager, entity)});
        });
    std::sort(containers.begin(), containers.end(), [](const OrderedContainerUVE& lhs, const OrderedContainerUVE& rhs) {
        if (lhs.depth != rhs.depth) {
            return lhs.depth < rhs.depth;
        }
        if (lhs.entity.index != rhs.entity.index) {
            return lhs.entity.index < rhs.entity.index;
        }
        return lhs.entity.generation < rhs.entity.generation;
    });

    std::vector<OrderedChildUVE> candidates;
    entityManager.ForEachUVE<Scene::HierarchyComponentUVE>(
        [&candidates](const Scene::EntityUVE entity, const Scene::HierarchyComponentUVE& hierarchy) {
            candidates.push_back(OrderedChildUVE{entity, hierarchy.siblingOrder});
        });

    const float wheel = input.GetMouseScrollDeltaUVE();
    const Math::Vector2UVE mouse = input.GetMousePositionUVE();
    const bool horizontal = input.IsKeyDownUVE(Input::KeyCodeUVE::LeftShift) ||
                            input.IsKeyDownUVE(Input::KeyCodeUVE::RightShift);
    for (const OrderedContainerUVE& container : containers) {
        if (!entityManager.IsAliveUVE(container.entity) ||
            !entityManager.HasComponentUVE<Scene::UIScrollContainerComponentUVE>(container.entity)) {
            continue;
        }
        const Scene::UIScrollContainerComponentUVE scroll =
            entityManager.GetComponentUVE<Scene::UIScrollContainerComponentUVE>(container.entity);
        if (!IsUIScrollContainerComponentValidUVE(scroll)) {
            continue;
        }
        const std::vector<OrderedChildUVE> children =
            CollectContainerChildrenUVE(entityManager, candidates, container.entity);
        const std::vector<PositionableChildUVE> items =
            CollectContainerItemsUVE(entityManager, children, fontAtlas);
        float widest = 0.0F;
        float stacked = 0.0F;
        bool first = true;
        for (const PositionableChildUVE& item : items) {
            if (!first) {
                stacked += scroll.gap;
            }
            first = false;
            widest = std::max(widest, item.extent.x);
            stacked += item.extent.y;
        }
        Scene::UIScrollContainerComponentUVE& stored =
            entityManager.GetComponentUVE<Scene::UIScrollContainerComponentUVE>(container.entity);
        stored.contentSize =
            Math::Vector2UVE{widest + 2.0F * scroll.padding, stacked + 2.0F * scroll.padding};
        if (HasScrollAncestorUVE(entityManager, container.entity)) {
            stored.scrollOffset = Math::Vector2UVE{0.0F, 0.0F};
        } else {
            if (wheel != 0.0F && Math::ContainsUVE(scroll.rect, mouse)) {
                if (horizontal) {
                    stored.scrollOffset.x -= wheel * scroll.wheelStep;
                } else {
                    stored.scrollOffset.y -= wheel * scroll.wheelStep;
                }
            }
            stored.scrollOffset.x = std::clamp(stored.scrollOffset.x, 0.0F,
                                               std::max(0.0F, stored.contentSize.x - scroll.rect.size.x));
            stored.scrollOffset.y = std::clamp(stored.scrollOffset.y, 0.0F,
                                               std::max(0.0F, stored.contentSize.y - scroll.rect.size.y));
        }
        float cursor = scroll.padding - stored.scrollOffset.y;
        for (const PositionableChildUVE& item : items) {
            MoveChildUVE(entityManager, item,
                         Math::Vector2UVE{scroll.rect.position.x + scroll.padding - stored.scrollOffset.x,
                                           scroll.rect.position.y + cursor});
            cursor += item.extent.y + scroll.gap;
        }
    }
}

void LayoutUIContainersUVE(Scene::IEntityManagerUVE& entityManager, const UIFontAtlasUVE& fontAtlas) {
    std::vector<OrderedContainerUVE> containers;
    entityManager.ForEachUVE<Scene::UILayoutContainerComponentUVE>(
        [&entityManager, &containers](const Scene::EntityUVE entity, const Scene::UILayoutContainerComponentUVE&) {
            containers.push_back(OrderedContainerUVE{entity, UIHierarchyDepthUVE(entityManager, entity)});
        });
    std::sort(containers.begin(), containers.end(), [](const OrderedContainerUVE& lhs, const OrderedContainerUVE& rhs) {
        if (lhs.depth != rhs.depth) {
            return lhs.depth < rhs.depth;
        }
        if (lhs.entity.index != rhs.entity.index) {
            return lhs.entity.index < rhs.entity.index;
        }
        return lhs.entity.generation < rhs.entity.generation;
    });

    std::vector<OrderedChildUVE> candidates;
    entityManager.ForEachUVE<Scene::HierarchyComponentUVE>(
        [&candidates](const Scene::EntityUVE entity, const Scene::HierarchyComponentUVE& hierarchy) {
            candidates.push_back(OrderedChildUVE{entity, hierarchy.siblingOrder});
        });
    // Two passes over one ordering, each re-gathering items: the resize pass runs deepest-first
    // so an auto-sized container settles before its parent measures it, then the position pass
    // runs shallowest-first so a nested container lands in its final parent before positioning
    // its own children. The resize pass exists to change the very sizes the position pass reads,
    // so sharing one gather between them would be a bug, not an optimization.
    for (auto container = containers.rbegin(); container != containers.rend(); ++container) {
        if (!entityManager.IsAliveUVE(container->entity) ||
            !entityManager.HasComponentUVE<Scene::UILayoutContainerComponentUVE>(container->entity)) {
            continue;
        }
        const Scene::UILayoutContainerComponentUVE layout =
            entityManager.GetComponentUVE<Scene::UILayoutContainerComponentUVE>(container->entity);
        if (!IsUILayoutContainerComponentValidUVE(layout) || (!layout.autoSizeWidth && !layout.autoSizeHeight)) {
            continue;
        }
        const std::vector<OrderedChildUVE> children =
            CollectContainerChildrenUVE(entityManager, candidates, container->entity);
        AutoSizeContainerUVE(entityManager, container->entity, layout,
                             CollectContainerItemsUVE(entityManager, children, fontAtlas));
    }
    // One hierarchy scan shared by every container; each container filters its direct children.
    for (const OrderedContainerUVE& container : containers) {
        if (!entityManager.IsAliveUVE(container.entity) ||
            !entityManager.HasComponentUVE<Scene::UILayoutContainerComponentUVE>(container.entity)) {
            continue;
        }
        const std::vector<OrderedChildUVE> children =
            CollectContainerChildrenUVE(entityManager, candidates, container.entity);
        const Scene::UILayoutContainerComponentUVE layout =
            entityManager.GetComponentUVE<Scene::UILayoutContainerComponentUVE>(container.entity);
        if (!IsUILayoutContainerComponentValidUVE(layout)) {
            continue;
        }
        PositionContainerItemsUVE(entityManager, layout,
                                  CollectContainerItemsUVE(entityManager, children, fontAtlas));
    }
}

} // namespace UVE::UI
