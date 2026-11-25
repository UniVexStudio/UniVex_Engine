// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "ui_layout.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "entity.h"
#include "hierarchy_component.h"
#include "ui_button_component.h"
#include "ui_checkbox_component.h"
#include "ui_dropdown_component.h"
#include "ui_image_component.h"
#include "ui_scroll_container_component.h"
#include "ui_text_input_component.h"
#include "ui_layout_container_component.h"
#include "ui_progress_bar_component.h"
#include "ui_slider_component.h"
#include "ui_text_component.h"
#include "i_entity_manager.h"
#include "i_input_system.h"
#include "key_code.h"
#include "vector2.h"
#include "canvas_ancestry.h"
#include "ui_font_atlas.h"
#include "ui_scroll.h"

namespace UVE::UI {

namespace {

struct OrderedContainer final {
    Scene::Entity entity = Scene::kInvalidEntity;
    std::size_t depth = 0U;
};

struct OrderedChild final {
    Scene::Entity entity = Scene::kInvalidEntity;
    std::int64_t siblingOrder = 0;
};

[[nodiscard]] bool IsEarlierChild(const OrderedChild& lhs, const OrderedChild& rhs) {
    if (lhs.siblingOrder != rhs.siblingOrder) {
        return lhs.siblingOrder < rhs.siblingOrder;
    }
    if (lhs.entity.index != rhs.entity.index) {
        return lhs.entity.index < rhs.entity.index;
    }
    return lhs.entity.generation < rhs.entity.generation;
}

struct PositionableChild final {
    Scene::Entity entity = Scene::kInvalidEntity;
    Math::Vector2 extent{};
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

void MoveChild(Scene::IEntityManager& entityManager, const PositionableChild& child,
                  const Math::Vector2 position) {
    if (child.isButton) {
        entityManager.GetComponent<Scene::UIButtonComponent>(child.entity).rect.position = position;
    }
    if (child.isImage) {
        entityManager.GetComponent<Scene::UIImageComponent>(child.entity).rect.position = position;
    }
    if (child.isContainer) {
        entityManager.GetComponent<Scene::UILayoutContainerComponent>(child.entity).rect.position = position;
    }
    if (child.isSlider) {
        entityManager.GetComponent<Scene::UISliderComponent>(child.entity).rect.position = position;
    }
    if (child.isProgress) {
        entityManager.GetComponent<Scene::UIProgressBarComponent>(child.entity).rect.position = position;
    }
    if (child.isCheckbox) {
        entityManager.GetComponent<Scene::UICheckboxComponent>(child.entity).rect.position = position;
    }
    if (child.isDropdown) {
        entityManager.GetComponent<Scene::UIDropdownComponent>(child.entity).rect.position = position;
    }
    if (child.isTextInput) {
        entityManager.GetComponent<Scene::UITextInputComponent>(child.entity).rect.position = position;
    }
    if (child.isScrollContainer) {
        entityManager.GetComponent<Scene::UIScrollContainerComponent>(child.entity).rect.position = position;
    }
    if (child.isText) {
        entityManager.GetComponent<Scene::UITextComponent>(child.entity).positionPixels = position;
    }
}

std::vector<OrderedChild> CollectContainerChildren(Scene::IEntityManager& entityManager,
                                                         const std::vector<OrderedChild>& candidates,
                                                         const Scene::Entity container) {
    std::vector<OrderedChild> children;
    for (const OrderedChild& candidate : candidates) {
        if (entityManager.GetComponent<Scene::HierarchyComponent>(candidate.entity).parent == container) {
            children.push_back(candidate);
        }
    }
    std::sort(children.begin(), children.end(), IsEarlierChild);
    return children;
}

std::vector<PositionableChild> CollectContainerItems(Scene::IEntityManager& entityManager,
                                                           const std::vector<OrderedChild>& children,
                                                           const UIFontAtlas& fontAtlas) {
    std::vector<PositionableChild> items;
    for (const OrderedChild& child : children) {
        PositionableChild item;
        item.entity = child.entity;
        item.isButton = entityManager.HasComponent<Scene::UIButtonComponent>(child.entity);
        item.isImage = entityManager.HasComponent<Scene::UIImageComponent>(child.entity);
        item.isContainer = entityManager.HasComponent<Scene::UILayoutContainerComponent>(child.entity);
        item.isSlider = entityManager.HasComponent<Scene::UISliderComponent>(child.entity);
        item.isProgress = entityManager.HasComponent<Scene::UIProgressBarComponent>(child.entity);
        item.isCheckbox = entityManager.HasComponent<Scene::UICheckboxComponent>(child.entity);
        item.isDropdown = entityManager.HasComponent<Scene::UIDropdownComponent>(child.entity);
        item.isTextInput = entityManager.HasComponent<Scene::UITextInputComponent>(child.entity);
        item.isScrollContainer =
            entityManager.HasComponent<Scene::UIScrollContainerComponent>(child.entity);
        item.isText = entityManager.HasComponent<Scene::UITextComponent>(child.entity);
        if (!item.isButton && !item.isImage && !item.isContainer && !item.isSlider && !item.isProgress &&
            !item.isCheckbox && !item.isDropdown && !item.isTextInput && !item.isScrollContainer && !item.isText) {
            continue; // nothing positionable: ignored, not even spaced
        }
        if (item.isButton) {
            item.extent = entityManager.GetComponent<Scene::UIButtonComponent>(child.entity).rect.size;
        } else if (item.isImage) {
            item.extent = entityManager.GetComponent<Scene::UIImageComponent>(child.entity).rect.size;
        } else if (item.isContainer) {
            item.extent =
                entityManager.GetComponent<Scene::UILayoutContainerComponent>(child.entity).rect.size;
        } else if (item.isSlider) {
            item.extent = entityManager.GetComponent<Scene::UISliderComponent>(child.entity).rect.size;
        } else if (item.isProgress) {
            item.extent = entityManager.GetComponent<Scene::UIProgressBarComponent>(child.entity).rect.size;
        } else if (item.isCheckbox) {
            item.extent = entityManager.GetComponent<Scene::UICheckboxComponent>(child.entity).rect.size;
        } else if (item.isDropdown) {
            item.extent = entityManager.GetComponent<Scene::UIDropdownComponent>(child.entity).rect.size;
        } else if (item.isTextInput) {
            item.extent = entityManager.GetComponent<Scene::UITextInputComponent>(child.entity).rect.size;
        } else if (item.isScrollContainer) {
            item.extent =
                entityManager.GetComponent<Scene::UIScrollContainerComponent>(child.entity).rect.size;
        } else {
            const Scene::UITextComponent& text =
                entityManager.GetComponent<Scene::UITextComponent>(child.entity);
            item.extent = Math::Vector2{fontAtlas.MeasureTextWidth(text.text, text.fontSize), text.fontSize};
        }
        items.push_back(item);
    }
    return items;
}

/// The size the container's content asks for: children plus `spacing` between them, plus
/// `padding` on both sides of each axis. Stacks sum the main axis and take the cross maximum;
/// grids fit their cells, lines, and both-axis spacing. No items means padding only.
[[nodiscard]] Math::Vector2 ContentSize(const Scene::UILayoutContainerComponent& layout,
                                             const std::vector<PositionableChild>& items) {
    const bool vertical = layout.direction == Scene::UILayoutDirection::Vertical;
    float main = 0.0F;
    float cross = 0.0F;
    if (layout.wrapAfter != 0U) {
        Math::Vector2 cell{};
        for (const PositionableChild& item : items) {
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
        for (const PositionableChild& item : items) {
            if (!first) {
                main += layout.spacing;
            }
            first = false;
            main += vertical ? item.extent.y : item.extent.x;
            cross = std::max(cross, vertical ? item.extent.x : item.extent.y);
        }
    }
    const Math::Vector2 content =
        vertical ? Math::Vector2{cross, main} : Math::Vector2{main, cross};
    return Math::Vector2{content.x + 2.0F * layout.padding, content.y + 2.0F * layout.padding};
}

void AutoSizeContainer(Scene::IEntityManager& entityManager, const Scene::Entity container,
                          const Scene::UILayoutContainerComponent& layout,
                          const std::vector<PositionableChild>& items) {
    const Math::Vector2 content = ContentSize(layout, items);
    Scene::UILayoutContainerComponent& stored =
        entityManager.GetComponent<Scene::UILayoutContainerComponent>(container);
    if (layout.autoSizeWidth) {
        stored.rect.size.x = content.x;
    }
    if (layout.autoSizeHeight) {
        stored.rect.size.y = content.y;
    }
}

void PositionContainerItems(Scene::IEntityManager& entityManager,
                               const Scene::UILayoutContainerComponent& layout,
                               const std::vector<PositionableChild>& items) {
    const bool vertical = layout.direction == Scene::UILayoutDirection::Vertical;
    const Math::Rect innerRect = Scene::ResolveUILayoutContentRect(layout);
    const float innerMinX = innerRect.position.x;
    const float innerMinY = innerRect.position.y;
    const float innerWidth = innerRect.size.x;
    const float innerHeight = innerRect.size.y;
    const float alignFactor = layout.alignment == Scene::UILayoutAlignment::Start
                                  ? 0.0F
                                  : (layout.alignment == Scene::UILayoutAlignment::Center ? 0.5F : 1.0F);

    if (layout.wrapAfter != 0U) {
        Math::Vector2 cell{};
        for (const PositionableChild& item : items) {
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
            const Math::Vector2 position =
                vertical ? Math::Vector2{crossBase + crossOffset, mainPos}
                         : Math::Vector2{mainPos, crossBase + crossOffset};
            MoveChild(entityManager, items[index], position);
        }
        return;
    }

    float cursor = 0.0F;
    bool first = true;
    for (const PositionableChild& item : items) {
        if (!first) {
            cursor += layout.spacing;
        }
        first = false;
        const float mainExtent = vertical ? item.extent.y : item.extent.x;
        const float crossInner = vertical ? innerWidth : innerHeight;
        const float crossExtent = vertical ? item.extent.x : item.extent.y;
        // A child larger than the inner rect clamps to the inner edge instead of spilling past it.
        const float crossOffset = std::max(0.0F, alignFactor * (crossInner - crossExtent));
        const Math::Vector2 position = vertical ? Math::Vector2{innerMinX + crossOffset, innerMinY + cursor}
                                                   : Math::Vector2{innerMinX + cursor, innerMinY + crossOffset};
        MoveChild(entityManager, item, position);
        cursor += mainExtent;
    }
}

[[nodiscard]] bool HasScrollAncestor(Scene::IEntityManager& entityManager, Scene::Entity entity) {
    Scene::Entity current = entity;
    for (std::size_t depth = 0U; depth < kMaximumCanvasAncestorWalk; ++depth) {
        if (!entityManager.IsAlive(current) ||
            !entityManager.HasComponent<Scene::HierarchyComponent>(current)) {
            return false;
        }
        const Scene::Entity parent =
            entityManager.GetComponent<Scene::HierarchyComponent>(current).parent;
        if (parent == Scene::kInvalidEntity || !entityManager.IsAlive(parent)) {
            return false;
        }
        if (entityManager.HasComponent<Scene::UIScrollContainerComponent>(parent)) {
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

void LayoutUIScrollContainers(Scene::IEntityManager& entityManager, const Input::IInputSystem& input,
                                 const UIFontAtlas& fontAtlas,
                                 const std::optional<Math::Vector2> pointerPositionInAuthoredSpace) {
    std::vector<OrderedContainer> containers;
    entityManager.ForEach<Scene::UIScrollContainerComponent>(
        [&entityManager, &containers](const Scene::Entity entity, const Scene::UIScrollContainerComponent&) {
            containers.push_back(OrderedContainer{entity, UIHierarchyDepth(entityManager, entity)});
        });
    std::sort(containers.begin(), containers.end(), [](const OrderedContainer& lhs, const OrderedContainer& rhs) {
        if (lhs.depth != rhs.depth) {
            return lhs.depth < rhs.depth;
        }
        if (lhs.entity.index != rhs.entity.index) {
            return lhs.entity.index < rhs.entity.index;
        }
        return lhs.entity.generation < rhs.entity.generation;
    });

    std::vector<OrderedChild> candidates;
    entityManager.ForEach<Scene::HierarchyComponent>(
        [&candidates](const Scene::Entity entity, const Scene::HierarchyComponent& hierarchy) {
            candidates.push_back(OrderedChild{entity, hierarchy.siblingOrder});
        });

    const float wheel = input.GetMouseScrollDelta();
    const Math::Vector2 mouse = pointerPositionInAuthoredSpace.value_or(input.GetMousePosition());
    const bool horizontal = input.IsKeyDown(Input::KeyCode::LeftShift) ||
                            input.IsKeyDown(Input::KeyCode::RightShift);
    for (const OrderedContainer& container : containers) {
        if (!entityManager.IsAlive(container.entity) ||
            !entityManager.HasComponent<Scene::UIScrollContainerComponent>(container.entity)) {
            continue;
        }
        const Scene::UIScrollContainerComponent scroll =
            entityManager.GetComponent<Scene::UIScrollContainerComponent>(container.entity);
        if (!IsUIScrollContainerComponentValid(scroll)) {
            continue;
        }
        const std::vector<OrderedChild> children =
            CollectContainerChildren(entityManager, candidates, container.entity);
        const std::vector<PositionableChild> items =
            CollectContainerItems(entityManager, children, fontAtlas);
        float widest = 0.0F;
        float stacked = 0.0F;
        bool first = true;
        for (const PositionableChild& item : items) {
            if (!first) {
                stacked += scroll.gap;
            }
            first = false;
            widest = std::max(widest, item.extent.x);
            stacked += item.extent.y;
        }
        Scene::UIScrollContainerComponent& stored =
            entityManager.GetComponent<Scene::UIScrollContainerComponent>(container.entity);
        stored.contentSize =
            Math::Vector2{widest + 2.0F * scroll.padding, stacked + 2.0F * scroll.padding};
        if (HasScrollAncestor(entityManager, container.entity)) {
            stored.scrollOffset = Math::Vector2{0.0F, 0.0F};
        } else {
            if (wheel != 0.0F && Math::Contains(scroll.rect, mouse)) {
                (void)Scene::ApplyUIScrollWheelDelta(stored, wheel, horizontal);
            } else {
                stored.scrollOffset = Scene::ClampUIScrollOffset(stored, stored.scrollOffset);
            }
        }
        float cursor = scroll.padding - stored.scrollOffset.y;
        for (const PositionableChild& item : items) {
            MoveChild(entityManager, item,
                         Math::Vector2{scroll.rect.position.x + scroll.padding - stored.scrollOffset.x,
                                           scroll.rect.position.y + cursor});
            cursor += item.extent.y + scroll.gap;
        }
    }
}

void LayoutUIContainers(Scene::IEntityManager& entityManager, const UIFontAtlas& fontAtlas) {
    std::vector<OrderedContainer> containers;
    entityManager.ForEach<Scene::UILayoutContainerComponent>(
        [&entityManager, &containers](const Scene::Entity entity, const Scene::UILayoutContainerComponent&) {
            containers.push_back(OrderedContainer{entity, UIHierarchyDepth(entityManager, entity)});
        });
    std::sort(containers.begin(), containers.end(), [](const OrderedContainer& lhs, const OrderedContainer& rhs) {
        if (lhs.depth != rhs.depth) {
            return lhs.depth < rhs.depth;
        }
        if (lhs.entity.index != rhs.entity.index) {
            return lhs.entity.index < rhs.entity.index;
        }
        return lhs.entity.generation < rhs.entity.generation;
    });

    std::vector<OrderedChild> candidates;
    entityManager.ForEach<Scene::HierarchyComponent>(
        [&candidates](const Scene::Entity entity, const Scene::HierarchyComponent& hierarchy) {
            candidates.push_back(OrderedChild{entity, hierarchy.siblingOrder});
        });
    // Two passes over one ordering, each re-gathering items: the resize pass runs deepest-first
    // so an auto-sized container settles before its parent measures it, then the position pass
    // runs shallowest-first so a nested container lands in its final parent before positioning
    // its own children. The resize pass exists to change the very sizes the position pass reads,
    // so sharing one gather between them would be a bug, not an optimization.
    for (auto container = containers.rbegin(); container != containers.rend(); ++container) {
        if (!entityManager.IsAlive(container->entity) ||
            !entityManager.HasComponent<Scene::UILayoutContainerComponent>(container->entity)) {
            continue;
        }
        const Scene::UILayoutContainerComponent layout =
            entityManager.GetComponent<Scene::UILayoutContainerComponent>(container->entity);
        if (!IsUILayoutContainerComponentValid(layout) || (!layout.autoSizeWidth && !layout.autoSizeHeight)) {
            continue;
        }
        const std::vector<OrderedChild> children =
            CollectContainerChildren(entityManager, candidates, container->entity);
        AutoSizeContainer(entityManager, container->entity, layout,
                             CollectContainerItems(entityManager, children, fontAtlas));
    }
    // One hierarchy scan shared by every container; each container filters its direct children.
    for (const OrderedContainer& container : containers) {
        if (!entityManager.IsAlive(container.entity) ||
            !entityManager.HasComponent<Scene::UILayoutContainerComponent>(container.entity)) {
            continue;
        }
        const std::vector<OrderedChild> children =
            CollectContainerChildren(entityManager, candidates, container.entity);
        const Scene::UILayoutContainerComponent layout =
            entityManager.GetComponent<Scene::UILayoutContainerComponent>(container.entity);
        if (!IsUILayoutContainerComponentValid(layout)) {
            continue;
        }
        PositionContainerItems(entityManager, layout,
                                  CollectContainerItems(entityManager, children, fontAtlas));
    }
}

} // namespace UVE::UI
