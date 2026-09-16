// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/ui/ui_runtime_uve.h"

#include "uve/input/mouse_button_uve.h"
#include "uve/scene/components/ui_button_component_uve.h"
#include "uve/scene/components/ui_image_component_uve.h"
#include "uve/scene/components/ui_text_component_uve.h"

namespace UVE::UI {

namespace {

[[nodiscard]] bool IsPointInsideRectUVE(const Math::Vector2UVE& point, const Math::Vector2UVE& rectPosition,
                                        const Math::Vector2UVE& rectSize) noexcept {
    return point.x >= rectPosition.x && point.x <= rectPosition.x + rectSize.x && point.y >= rectPosition.y &&
           point.y <= rectPosition.y + rectSize.y;
}

} // namespace

void UIRuntimeUVE::TickUVE(Scene::IEntityManagerUVE& entityManager, const Input::IInputSystemUVE& inputSystem) {
    m_drawBatch.quads.clear();

    entityManager.ForEachUVE<Scene::UIImageComponentUVE>(
        [this](const Scene::EntityUVE, const Scene::UIImageComponentUVE& image) {
            UIQuadUVE quad{};
            quad.positionPixels = image.positionPixels;
            quad.sizePixels = image.sizePixels;
            quad.color = image.tintColor;
            quad.alpha = image.alpha;
            quad.kind = image.textureAssetGuid.value == 0U ? UIDrawItemKindUVE::SolidColor : UIDrawItemKindUVE::Image;
            quad.imageAssetGuid = image.textureAssetGuid;
            m_drawBatch.quads.push_back(quad);
        });

    const Math::Vector2UVE mousePosition = inputSystem.GetMousePositionUVE();
    const bool mouseDown = inputSystem.IsMouseButtonDownUVE(Input::MouseButtonUVE::Left);
    const bool mousePressedThisFrame = inputSystem.WasMouseButtonPressedThisFrameUVE(Input::MouseButtonUVE::Left);
    entityManager.ForEachUVE<Scene::UIButtonComponentUVE>(
        [this, &mousePosition, mouseDown, mousePressedThisFrame](const Scene::EntityUVE,
                                                                  Scene::UIButtonComponentUVE& button) {
            button.isHovered = IsPointInsideRectUVE(mousePosition, button.positionPixels, button.sizePixels);
            button.wasClickedThisFrame = button.isHovered && mousePressedThisFrame;

            UIQuadUVE quad{};
            quad.positionPixels = button.positionPixels;
            quad.sizePixels = button.sizePixels;
            quad.color = button.isHovered ? (mouseDown ? button.pressedColor : button.hoverColor) : button.normalColor;
            quad.alpha = 1.0F;
            quad.kind = UIDrawItemKindUVE::SolidColor;
            m_drawBatch.quads.push_back(quad);
        });

    std::vector<UIGlyphQuadUVE> glyphQuads;
    entityManager.ForEachUVE<Scene::UITextComponentUVE>(
        [this, &glyphQuads](const Scene::EntityUVE, const Scene::UITextComponentUVE& text) {
            glyphQuads.clear();
            // positionPixels is the text box's top-left; approximate the baseline as one fontSize
            // down (no separate ascent/descent metric is tracked) so the first line sits inside it.
            float cursorX = text.positionPixels.x;
            float cursorY = text.positionPixels.y + text.fontSize;
            m_fontAtlas.AppendTextQuadsUVE(text.text, cursorX, cursorY, text.fontSize, glyphQuads);
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
                m_drawBatch.quads.push_back(quad);
            }
        });
}

} // namespace UVE::UI
