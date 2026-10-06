// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/ui/ui_runtime_uve.h"

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

#include "uve/component/ui_button_component_uve.h"
#include "uve/component/ui_image_component_uve.h"
#include "uve/component/ui_text_component_uve.h"
#include "uve/input/mouse_button_uve.h"
#include "uve/ui/canvas_ancestry_uve.h"

namespace UVE::UI {

namespace {

[[nodiscard]] bool IsPointInsideRectUVE(const Math::Vector2UVE& point, const Math::Vector2UVE& rectPosition,
                                        const Math::Vector2UVE& rectSize) noexcept {
    return point.x >= rectPosition.x && point.x <= rectPosition.x + rectSize.x && point.y >= rectPosition.y &&
           point.y <= rectPosition.y + rectSize.y;
}

struct RankedWidgetUVE final {
    std::int32_t sortOrder = 0;
    bool hasCanvas = false;
    std::uint32_t canvasIndex = 0;
    std::uint32_t canvasGeneration = 0;
    std::uint8_t layer = 0;
    std::uint32_t entityIndex = 0;
    std::uint32_t entityGeneration = 0;
    std::vector<UIQuadUVE> quads;
};

void RankWidgetUVE(const Scene::EntityUVE entity, const CanvasAncestryUVE& ancestry, const std::uint8_t layer,
                   std::vector<UIQuadUVE> quads, std::vector<RankedWidgetUVE>& ranked) {
    RankedWidgetUVE rankedWidget;
    rankedWidget.sortOrder = ancestry.sortOrder;
    rankedWidget.hasCanvas = ancestry.hasCanvas;
    rankedWidget.canvasIndex = ancestry.canvas.index;
    rankedWidget.canvasGeneration = ancestry.canvas.generation;
    rankedWidget.layer = layer;
    rankedWidget.entityIndex = entity.index;
    rankedWidget.entityGeneration = entity.generation;
    rankedWidget.quads = std::move(quads);
    ranked.push_back(std::move(rankedWidget));
}

} // namespace

void UIRuntimeUVE::TickUVE(Scene::IEntityManagerUVE& entityManager, const Input::IInputSystemUVE& inputSystem,
                           const UITextLocalizationUVE& localization) {
    m_drawBatch.quads.clear();
    std::vector<RankedWidgetUVE> ranked;

    entityManager.ForEachUVE<Scene::UIImageComponentUVE>(
        [&entityManager, &ranked](const Scene::EntityUVE entity, const Scene::UIImageComponentUVE& image) {
            if (!ShouldDrawUiWidgetUVE(entityManager, entity)) {
                return;
            }
            UIQuadUVE quad{};
            quad.positionPixels = image.positionPixels;
            quad.sizePixels = image.sizePixels;
            quad.color = image.tintColor;
            quad.alpha = image.alpha;
            quad.kind = image.textureAssetGuid.value == 0U ? UIDrawItemKindUVE::SolidColor : UIDrawItemKindUVE::Image;
            quad.imageAssetGuid = image.textureAssetGuid;
            RankWidgetUVE(entity, ResolveCanvasAncestryUVE(entityManager, entity), 0, {quad}, ranked);
        });

    const Math::Vector2UVE mousePosition = inputSystem.GetMousePositionUVE();
    const bool mouseDown = inputSystem.IsMouseButtonDownUVE(Input::MouseButtonUVE::Left);
    const bool mousePressedThisFrame = inputSystem.WasMouseButtonPressedThisFrameUVE(Input::MouseButtonUVE::Left);
    entityManager.ForEachUVE<Scene::UIButtonComponentUVE>(
        [&entityManager, &ranked, &mousePosition, mouseDown, mousePressedThisFrame](
            const Scene::EntityUVE entity, Scene::UIButtonComponentUVE& button) {
            if (!ShouldDrawUiWidgetUVE(entityManager, entity)) {
                button.isHovered = false;
                button.wasClickedThisFrame = false;
                return;
            }
            button.isHovered = IsPointInsideRectUVE(mousePosition, button.positionPixels, button.sizePixels);
            button.wasClickedThisFrame = button.isHovered && mousePressedThisFrame;

            UIQuadUVE quad{};
            quad.positionPixels = button.positionPixels;
            quad.sizePixels = button.sizePixels;
            quad.color = button.isHovered ? (mouseDown ? button.pressedColor : button.hoverColor) : button.normalColor;
            quad.alpha = 1.0F;
            quad.kind = UIDrawItemKindUVE::SolidColor;
            RankWidgetUVE(entity, ResolveCanvasAncestryUVE(entityManager, entity), 1, {quad}, ranked);
        });

    std::vector<UIGlyphQuadUVE> glyphQuads;
    std::string translated;
    entityManager.ForEachUVE<Scene::UITextComponentUVE>(
        [this, &entityManager, &ranked, &glyphQuads, &localization, &translated](const Scene::EntityUVE entity,
                                                                                const Scene::UITextComponentUVE& text) {
            if (!ShouldDrawUiWidgetUVE(entityManager, entity)) {
                return;
            }
            glyphQuads.clear();
            const std::string* shown = &text.text;
            if (localization.service != nullptr &&
                (!localization.isAutoTranslated || localization.isAutoTranslated(entity))) {
                translated = localization.service->TranslateUVE(text.text);
                shown = &translated;
            }
            float cursorX = text.positionPixels.x;
            float cursorY = text.positionPixels.y + text.fontSize;
            m_fontAtlas.AppendTextQuadsUVE(*shown, cursorX, cursorY, text.fontSize, glyphQuads);
            std::vector<UIQuadUVE> quads;
            quads.reserve(glyphQuads.size());
            for (const UIGlyphQuadUVE& glyphQuad : glyphQuads) {
                UIQuadUVE quad{};
                quad.positionPixels = Math::Vector2UVE{glyphQuad.x0, glyphQuad.y0};
                quad.sizePixels = Math::Vector2UVE{glyphQuad.x1 - glyphQuad.x0, glyphQuad.y1 - glyphQuad.y0};
                quad.u0 = glyphQuad.u0;
                quad.v0 = glyphQuad.v0;
                quad.u1 = glyphQuad.u1;
                quad.v1 = glyphQuad.v1;
                quad.color = text.color;
                quad.alpha = text.alpha;
                quad.kind = UIDrawItemKindUVE::Glyph;
                quads.push_back(quad);
            }
            RankWidgetUVE(entity, ResolveCanvasAncestryUVE(entityManager, entity), 2, std::move(quads), ranked);
        });

    std::sort(ranked.begin(), ranked.end(), [](const RankedWidgetUVE& lhs, const RankedWidgetUVE& rhs) {
        if (lhs.sortOrder != rhs.sortOrder) {
            return lhs.sortOrder < rhs.sortOrder;
        }
        if (lhs.hasCanvas != rhs.hasCanvas) {
            return !lhs.hasCanvas && rhs.hasCanvas;
        }
        if (lhs.canvasIndex != rhs.canvasIndex) {
            return lhs.canvasIndex < rhs.canvasIndex;
        }
        if (lhs.canvasGeneration != rhs.canvasGeneration) {
            return lhs.canvasGeneration < rhs.canvasGeneration;
        }
        if (lhs.layer != rhs.layer) {
            return lhs.layer < rhs.layer;
        }
        if (lhs.entityIndex != rhs.entityIndex) {
            return lhs.entityIndex < rhs.entityIndex;
        }
        return lhs.entityGeneration < rhs.entityGeneration;
    });
    for (const RankedWidgetUVE& widget : ranked) {
        m_drawBatch.quads.insert(m_drawBatch.quads.end(), widget.quads.begin(), widget.quads.end());
    }
}

} // namespace UVE::UI
