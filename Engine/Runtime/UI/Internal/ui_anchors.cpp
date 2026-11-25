// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "ui_anchors.h"

#include <algorithm>
#include <cstddef>
#include <vector>

#include "entity.h"
#include "hierarchy_component.h"
#include "ui_anchor_component.h"
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
#include "rect.h"
#include "vector2.h"
#include "canvas_ancestry.h"

namespace UVE::UI {

namespace {

struct AnchoredWidget final {
    Scene::Entity entity = Scene::kInvalidEntity;
    std::size_t depth = 0U;
};

[[nodiscard]] Math::Rect AnchorParentRect(Scene::IEntityManager& entityManager,
                                                const Scene::Entity widget,
                                                const Math::Vector2& viewportSize) {
    const Math::Rect viewport{{0.0F, 0.0F}, viewportSize};
    if (!entityManager.IsAlive(widget) ||
        !entityManager.HasComponent<Scene::HierarchyComponent>(widget)) {
        return viewport;
    }
    const Scene::Entity parent =
        entityManager.GetComponent<Scene::HierarchyComponent>(widget).parent;
    if (!entityManager.IsAlive(parent)) {
        return viewport;
    }
    if (entityManager.HasComponent<Scene::UILayoutContainerComponent>(parent)) {
        return entityManager.GetComponent<Scene::UILayoutContainerComponent>(parent).rect;
    }
    if (entityManager.HasComponent<Scene::UIButtonComponent>(parent)) {
        return entityManager.GetComponent<Scene::UIButtonComponent>(parent).rect;
    }
    if (entityManager.HasComponent<Scene::UIImageComponent>(parent)) {
        return entityManager.GetComponent<Scene::UIImageComponent>(parent).rect;
    }
    if (entityManager.HasComponent<Scene::UISliderComponent>(parent)) {
        return entityManager.GetComponent<Scene::UISliderComponent>(parent).rect;
    }
    if (entityManager.HasComponent<Scene::UIProgressBarComponent>(parent)) {
        return entityManager.GetComponent<Scene::UIProgressBarComponent>(parent).rect;
    }
    if (entityManager.HasComponent<Scene::UICheckboxComponent>(parent)) {
        return entityManager.GetComponent<Scene::UICheckboxComponent>(parent).rect;
    }
    if (entityManager.HasComponent<Scene::UIDropdownComponent>(parent)) {
        return entityManager.GetComponent<Scene::UIDropdownComponent>(parent).rect;
    }
    if (entityManager.HasComponent<Scene::UITextInputComponent>(parent)) {
        return entityManager.GetComponent<Scene::UITextInputComponent>(parent).rect;
    }
    if (entityManager.HasComponent<Scene::UIScrollContainerComponent>(parent)) {
        return entityManager.GetComponent<Scene::UIScrollContainerComponent>(parent).rect;
    }
    return viewport;
}

} // namespace

void ResolveUIAnchors(Scene::IEntityManager& entityManager,
                         const Math::Vector2& viewportSize) {
    std::vector<AnchoredWidget> widgets;
    entityManager.ForEach<Scene::UIAnchorComponent>(
        [&entityManager, &widgets](const Scene::Entity entity, const Scene::UIAnchorComponent&) {
            widgets.push_back(AnchoredWidget{entity, UIHierarchyDepth(entityManager, entity)});
        });
    std::sort(widgets.begin(), widgets.end(), [](const AnchoredWidget& lhs, const AnchoredWidget& rhs) {
        if (lhs.depth != rhs.depth) {
            return lhs.depth < rhs.depth;
        }
        if (lhs.entity.index != rhs.entity.index) {
            return lhs.entity.index < rhs.entity.index;
        }
        return lhs.entity.generation < rhs.entity.generation;
    });

    for (const AnchoredWidget& widget : widgets) {
        if (!entityManager.IsAlive(widget.entity)) {
            continue;
        }
        const Scene::UIAnchorComponent& anchor =
            entityManager.GetComponent<Scene::UIAnchorComponent>(widget.entity);
        if (!IsUIAnchorComponentValid(anchor)) {
            continue;
        }
        const bool isButton = entityManager.HasComponent<Scene::UIButtonComponent>(widget.entity);
        const bool isImage = entityManager.HasComponent<Scene::UIImageComponent>(widget.entity);
        const bool isContainer =
            entityManager.HasComponent<Scene::UILayoutContainerComponent>(widget.entity);
        const bool isSlider = entityManager.HasComponent<Scene::UISliderComponent>(widget.entity);
        const bool isProgress = entityManager.HasComponent<Scene::UIProgressBarComponent>(widget.entity);
        const bool isCheckbox = entityManager.HasComponent<Scene::UICheckboxComponent>(widget.entity);
        const bool isDropdown = entityManager.HasComponent<Scene::UIDropdownComponent>(widget.entity);
        const bool isTextInput = entityManager.HasComponent<Scene::UITextInputComponent>(widget.entity);
        const bool isScrollContainer =
            entityManager.HasComponent<Scene::UIScrollContainerComponent>(widget.entity);
        const bool isText = entityManager.HasComponent<Scene::UITextComponent>(widget.entity);
        if (!isButton && !isImage && !isContainer && !isSlider && !isProgress && !isCheckbox && !isDropdown &&
            !isTextInput && !isScrollContainer && !isText) {
            continue;
        }
        const Math::Rect parent = AnchorParentRect(entityManager, widget.entity, viewportSize);
        const Math::Rect resolvedRect = Scene::ResolveUIAnchoredRect(anchor, parent);
        const Math::Vector2 resolvedMin = resolvedRect.position;
        const Math::Vector2 resolvedSize = resolvedRect.size;
        if (isButton) {
            entityManager.GetComponent<Scene::UIButtonComponent>(widget.entity).rect = {resolvedMin,
                                                                                              resolvedSize};
        }
        if (isImage) {
            entityManager.GetComponent<Scene::UIImageComponent>(widget.entity).rect = {resolvedMin,
                                                                                             resolvedSize};
        }
        if (isContainer) {
            entityManager.GetComponent<Scene::UILayoutContainerComponent>(widget.entity).rect = {
                resolvedMin, resolvedSize};
        }
        if (isSlider) {
            entityManager.GetComponent<Scene::UISliderComponent>(widget.entity).rect = {resolvedMin,
                                                                                              resolvedSize};
        }
        if (isProgress) {
            entityManager.GetComponent<Scene::UIProgressBarComponent>(widget.entity).rect = {resolvedMin,
                                                                                                   resolvedSize};
        }
        if (isCheckbox) {
            entityManager.GetComponent<Scene::UICheckboxComponent>(widget.entity).rect = {resolvedMin,
                                                                                                resolvedSize};
        }
        if (isDropdown) {
            entityManager.GetComponent<Scene::UIDropdownComponent>(widget.entity).rect = {resolvedMin,
                                                                                                resolvedSize};
        }
        if (isTextInput) {
            entityManager.GetComponent<Scene::UITextInputComponent>(widget.entity).rect = {resolvedMin,
                                                                                                 resolvedSize};
        }
        if (isScrollContainer) {
            entityManager.GetComponent<Scene::UIScrollContainerComponent>(widget.entity).rect = {resolvedMin,
                                                                                                       resolvedSize};
        }
        if (isText) {
            entityManager.GetComponent<Scene::UITextComponent>(widget.entity).positionPixels = resolvedMin;
        }
    }
}

} // namespace UVE::UI
