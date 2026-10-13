// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/ui/ui_anchors_uve.h"

#include <algorithm>
#include <cstddef>
#include <vector>

#include "uve/component/entity_uve.h"
#include "uve/component/hierarchy_component_uve.h"
#include "uve/component/ui_anchor_component_uve.h"
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
#include "uve/math/rect_uve.h"
#include "uve/math/vector2_uve.h"
#include "uve/ui/canvas_ancestry_uve.h"

namespace UVE::UI {

namespace {

struct AnchoredWidgetUVE final {
    Scene::EntityUVE entity = Scene::kInvalidEntityUVE;
    std::size_t depth = 0U;
};

[[nodiscard]] Math::RectUVE AnchorParentRectUVE(Scene::IEntityManagerUVE& entityManager,
                                                const Scene::EntityUVE widget,
                                                const Math::Vector2UVE& viewportSize) {
    const Math::RectUVE viewport{{0.0F, 0.0F}, viewportSize};
    if (!entityManager.IsAliveUVE(widget) ||
        !entityManager.HasComponentUVE<Scene::HierarchyComponentUVE>(widget)) {
        return viewport;
    }
    const Scene::EntityUVE parent =
        entityManager.GetComponentUVE<Scene::HierarchyComponentUVE>(widget).parent;
    if (!entityManager.IsAliveUVE(parent)) {
        return viewport;
    }
    if (entityManager.HasComponentUVE<Scene::UILayoutContainerComponentUVE>(parent)) {
        return entityManager.GetComponentUVE<Scene::UILayoutContainerComponentUVE>(parent).rect;
    }
    if (entityManager.HasComponentUVE<Scene::UIButtonComponentUVE>(parent)) {
        return entityManager.GetComponentUVE<Scene::UIButtonComponentUVE>(parent).rect;
    }
    if (entityManager.HasComponentUVE<Scene::UIImageComponentUVE>(parent)) {
        return entityManager.GetComponentUVE<Scene::UIImageComponentUVE>(parent).rect;
    }
    if (entityManager.HasComponentUVE<Scene::UISliderComponentUVE>(parent)) {
        return entityManager.GetComponentUVE<Scene::UISliderComponentUVE>(parent).rect;
    }
    if (entityManager.HasComponentUVE<Scene::UIProgressBarComponentUVE>(parent)) {
        return entityManager.GetComponentUVE<Scene::UIProgressBarComponentUVE>(parent).rect;
    }
    if (entityManager.HasComponentUVE<Scene::UICheckboxComponentUVE>(parent)) {
        return entityManager.GetComponentUVE<Scene::UICheckboxComponentUVE>(parent).rect;
    }
    if (entityManager.HasComponentUVE<Scene::UIDropdownComponentUVE>(parent)) {
        return entityManager.GetComponentUVE<Scene::UIDropdownComponentUVE>(parent).rect;
    }
    if (entityManager.HasComponentUVE<Scene::UITextInputComponentUVE>(parent)) {
        return entityManager.GetComponentUVE<Scene::UITextInputComponentUVE>(parent).rect;
    }
    if (entityManager.HasComponentUVE<Scene::UIScrollContainerComponentUVE>(parent)) {
        return entityManager.GetComponentUVE<Scene::UIScrollContainerComponentUVE>(parent).rect;
    }
    return viewport;
}

} // namespace

void ResolveUIAnchorsUVE(Scene::IEntityManagerUVE& entityManager,
                         const Math::Vector2UVE& viewportSize) {
    std::vector<AnchoredWidgetUVE> widgets;
    entityManager.ForEachUVE<Scene::UIAnchorComponentUVE>(
        [&entityManager, &widgets](const Scene::EntityUVE entity, const Scene::UIAnchorComponentUVE&) {
            widgets.push_back(AnchoredWidgetUVE{entity, UIHierarchyDepthUVE(entityManager, entity)});
        });
    std::sort(widgets.begin(), widgets.end(), [](const AnchoredWidgetUVE& lhs, const AnchoredWidgetUVE& rhs) {
        if (lhs.depth != rhs.depth) {
            return lhs.depth < rhs.depth;
        }
        if (lhs.entity.index != rhs.entity.index) {
            return lhs.entity.index < rhs.entity.index;
        }
        return lhs.entity.generation < rhs.entity.generation;
    });

    for (const AnchoredWidgetUVE& widget : widgets) {
        if (!entityManager.IsAliveUVE(widget.entity)) {
            continue;
        }
        const Scene::UIAnchorComponentUVE& anchor =
            entityManager.GetComponentUVE<Scene::UIAnchorComponentUVE>(widget.entity);
        if (!IsUIAnchorComponentValidUVE(anchor)) {
            continue;
        }
        const bool isButton = entityManager.HasComponentUVE<Scene::UIButtonComponentUVE>(widget.entity);
        const bool isImage = entityManager.HasComponentUVE<Scene::UIImageComponentUVE>(widget.entity);
        const bool isContainer =
            entityManager.HasComponentUVE<Scene::UILayoutContainerComponentUVE>(widget.entity);
        const bool isSlider = entityManager.HasComponentUVE<Scene::UISliderComponentUVE>(widget.entity);
        const bool isProgress = entityManager.HasComponentUVE<Scene::UIProgressBarComponentUVE>(widget.entity);
        const bool isCheckbox = entityManager.HasComponentUVE<Scene::UICheckboxComponentUVE>(widget.entity);
        const bool isDropdown = entityManager.HasComponentUVE<Scene::UIDropdownComponentUVE>(widget.entity);
        const bool isTextInput = entityManager.HasComponentUVE<Scene::UITextInputComponentUVE>(widget.entity);
        const bool isScrollContainer =
            entityManager.HasComponentUVE<Scene::UIScrollContainerComponentUVE>(widget.entity);
        const bool isText = entityManager.HasComponentUVE<Scene::UITextComponentUVE>(widget.entity);
        if (!isButton && !isImage && !isContainer && !isSlider && !isProgress && !isCheckbox && !isDropdown &&
            !isTextInput && !isScrollContainer && !isText) {
            continue;
        }
        const Math::RectUVE parent = AnchorParentRectUVE(entityManager, widget.entity, viewportSize);
        // No component-wise Vector2 multiply: each axis resolves explicitly.
        const Math::Vector2UVE resolvedMin{
            parent.position.x + anchor.anchorMin.x * parent.size.x + anchor.offsetMin.x,
            parent.position.y + anchor.anchorMin.y * parent.size.y + anchor.offsetMin.y};
        const Math::Vector2UVE resolvedMax{
            parent.position.x + anchor.anchorMax.x * parent.size.x + anchor.offsetMax.x,
            parent.position.y + anchor.anchorMax.y * parent.size.y + anchor.offsetMax.y};
        const Math::Vector2UVE resolvedSize{std::max(0.0F, resolvedMax.x - resolvedMin.x),
                                            std::max(0.0F, resolvedMax.y - resolvedMin.y)};
        if (isButton) {
            entityManager.GetComponentUVE<Scene::UIButtonComponentUVE>(widget.entity).rect = {resolvedMin,
                                                                                              resolvedSize};
        }
        if (isImage) {
            entityManager.GetComponentUVE<Scene::UIImageComponentUVE>(widget.entity).rect = {resolvedMin,
                                                                                             resolvedSize};
        }
        if (isContainer) {
            entityManager.GetComponentUVE<Scene::UILayoutContainerComponentUVE>(widget.entity).rect = {
                resolvedMin, resolvedSize};
        }
        if (isSlider) {
            entityManager.GetComponentUVE<Scene::UISliderComponentUVE>(widget.entity).rect = {resolvedMin,
                                                                                              resolvedSize};
        }
        if (isProgress) {
            entityManager.GetComponentUVE<Scene::UIProgressBarComponentUVE>(widget.entity).rect = {resolvedMin,
                                                                                                   resolvedSize};
        }
        if (isCheckbox) {
            entityManager.GetComponentUVE<Scene::UICheckboxComponentUVE>(widget.entity).rect = {resolvedMin,
                                                                                                resolvedSize};
        }
        if (isDropdown) {
            entityManager.GetComponentUVE<Scene::UIDropdownComponentUVE>(widget.entity).rect = {resolvedMin,
                                                                                                resolvedSize};
        }
        if (isTextInput) {
            entityManager.GetComponentUVE<Scene::UITextInputComponentUVE>(widget.entity).rect = {resolvedMin,
                                                                                                 resolvedSize};
        }
        if (isScrollContainer) {
            entityManager.GetComponentUVE<Scene::UIScrollContainerComponentUVE>(widget.entity).rect = {resolvedMin,
                                                                                                       resolvedSize};
        }
        if (isText) {
            entityManager.GetComponentUVE<Scene::UITextComponentUVE>(widget.entity).positionPixels = resolvedMin;
        }
    }
}

} // namespace UVE::UI
