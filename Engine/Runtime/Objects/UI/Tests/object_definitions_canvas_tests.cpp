// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include <type_traits>
#include <concepts>

#include <gtest/gtest.h>

#include "canvas_component.h"
#include "entity.h"
#include "ui_anchor_component.h"
#include "ui_button_component.h"
#include "ui_checkbox_component.h"
#include "ui_dropdown_component.h"
#include "ui_image_component.h"
#include "ui_layout_container_component.h"
#include "ui_progress_bar_component.h"
#include "ui_scroll_container_component.h"
#include "ui_scrollbar_component.h"
#include "ui_slider_component.h"
#include "ui_text_component.h"
#include "ui_text_input_component.h"
#include "ui_tooltip_component.h"
#include "ui_tween_component.h"
#include "visibility_component.h"
#include "entity_manager.h"
#include "event_system.h"
#include "memory_manager.h"
#include "all_objects_canvas.h"
#include "scene_object_registry.h"

namespace UVE::Scene::Tests {
namespace {
static_assert(std::equality_comparable<CanvasObjectDefinition>);
static_assert(std::equality_comparable<UIButtonObjectDefinition>);
static_assert(std::equality_comparable<UIImageObjectDefinition>);
static_assert(std::equality_comparable<UITextObjectDefinition>);


// All four promoted UI kinds' definitions are reachable from the aggregate header, mirroring
// the registry test's guarantee: no Canvas kind's home file can be silently dropped.
static_assert(std::is_class_v<CanvasObjectDefinition>);   // Canvas
static_assert(std::is_class_v<UITextObjectDefinition>);   // UIText
static_assert(std::is_class_v<UIImageObjectDefinition>);  // UIImage
static_assert(std::is_class_v<UIButtonObjectDefinition>); // UIButton

// The names match the Inspector's own component labels exactly, so both entry points (Add
// Component list and Add Object list) feel identical for the same kind.
static_assert(CanvasObjectDefinition::defaultName == "Canvas");
static_assert(UITextObjectDefinition::defaultName == "UI Text");
static_assert(UIImageObjectDefinition::defaultName == "UI Image");
static_assert(UIButtonObjectDefinition::defaultName == "UI Button");

class CanvasObjectDefinitionsTest : public ::testing::Test {
protected:
    Memory::MemoryManager memoryManager;
    Events::EventSystem eventSystem;
    EntityManager entityManager{memoryManager.GetDefaultAllocator(), eventSystem};

    [[nodiscard]] Entity CreateEntity() {
        return entityManager.CreateEntity();
    }
};

TEST_F(CanvasObjectDefinitionsTest, AllDefinitionDefaultsAreValid) {
    EXPECT_TRUE(IsCanvasObjectDefinitionValid(CanvasObjectDefinition{}));
    EXPECT_TRUE(IsUITextObjectDefinitionValid(UITextObjectDefinition{}));
    EXPECT_TRUE(IsUIImageObjectDefinitionValid(UIImageObjectDefinition{}));
    EXPECT_TRUE(IsUIButtonObjectDefinitionValid(UIButtonObjectDefinition{}));
}

TEST_F(CanvasObjectDefinitionsTest, ApplyAttachesEachKindsExactComponentRecipe) {
    {
        const Entity entity = CreateEntity();
        ApplyCanvasObjectDefinition(entityManager, entity, CanvasObjectDefinition{});
        EXPECT_TRUE(entityManager.HasComponent<CanvasComponent>(entity));
    }
    {
        const Entity entity = CreateEntity();
        ApplyUITextObjectDefinition(entityManager, entity, UITextObjectDefinition{});
        EXPECT_TRUE(entityManager.HasComponent<UITextComponent>(entity));
    }
    {
        const Entity entity = CreateEntity();
        ApplyUIImageObjectDefinition(entityManager, entity, UIImageObjectDefinition{});
        EXPECT_TRUE(entityManager.HasComponent<UIImageComponent>(entity));
    }
    {
        const Entity entity = CreateEntity();
        ApplyUIButtonObjectDefinition(entityManager, entity, UIButtonObjectDefinition{});
        EXPECT_TRUE(entityManager.HasComponent<UIButtonComponent>(entity));
    }
}

TEST_F(CanvasObjectDefinitionsTest, PromotedKindsAreRegisteredAndLibraryCreatable) {
    // The whole point of the promotion: these kinds must now appear in the Add-Object list's
    // backing registry, creatable like every other library kind.
    for (const std::string_view typeId : {"canvas", "ui_text", "ui_image", "ui_button"}) {
        const Objects::SceneObjectDescriptor* descriptor = Objects::FindSceneObjectDescriptor(typeId);
        ASSERT_NE(descriptor, nullptr);
        EXPECT_EQ(descriptor->category, "UI");
        EXPECT_TRUE(descriptor->libraryCreatable);
    }
}

TEST_F(CanvasObjectDefinitionsTest, CanvasNodePolicyVisibilityAndSortOrder) {
    CanvasComponent back{};
    back.sortOrder = -5;
    CanvasComponent front{};
    front.sortOrder = 10;

    EXPECT_TRUE(Canvas::IsCanvasVisible(back));
    EXPECT_EQ(Canvas::EffectiveSortOrder(back), -5);
    EXPECT_TRUE(Canvas::RendersBefore(back, front));
    EXPECT_FALSE(Canvas::RendersBefore(front, back));

    const Entity entity = CreateEntity();
    ApplyCanvasObjectDefinition(entityManager, entity, CanvasObjectDefinition{back});
    EXPECT_TRUE(Canvas::IsEntityCanvasVisible(entityManager, entity));

    VisibilityComponent hidden{};
    hidden.visible = false;
    hidden.visibleInHierarchy = false;
    entityManager.AddComponent<VisibilityComponent>(entity, hidden);
    EXPECT_FALSE(Canvas::IsEntityCanvasVisible(entityManager, entity));
}

TEST_F(CanvasObjectDefinitionsTest, UIButtonNodePolicyHitTestingStateAndPalette) {
    UIButtonComponent button{};
    button.rect = Math::Rect{Math::Vector2{10.0F, 20.0F}, Math::Vector2{100.0F, 40.0F}};

    EXPECT_TRUE(UIButton::ContainsPoint(button, Math::Vector2{50.0F, 35.0F}));
    EXPECT_FALSE(UIButton::ContainsPoint(button, Math::Vector2{5.0F, 35.0F}));

    UIButton::UpdateInteractionState(button, /*pointerHovered=*/true, /*mousePressedThisFrame=*/true,
                                           /*keyboardFocused=*/false, /*activatePressedThisFrame=*/false);
    EXPECT_TRUE(button.isHovered);
    EXPECT_TRUE(button.wasClickedThisFrame);
    EXPECT_EQ(UIButton::ResolveVisualColor(button, false, /*pressActive=*/true), button.pressedColor);
    EXPECT_EQ(UIButton::ResolveVisualColor(button, false, /*pressActive=*/false), button.hoverColor);

    UIButton::ClearRuntimeState(button);
    EXPECT_FALSE(button.isHovered);
    EXPECT_FALSE(button.wasClickedThisFrame);
    EXPECT_EQ(UIButton::ResolveVisualColor(button, false, false), button.normalColor);
}

TEST_F(CanvasObjectDefinitionsTest, UIImageNodePolicyTextureAndNineSliceSplits) {
    UIImageComponent image{};
    image.rect = Math::Rect{Math::Vector2{0.0F, 0.0F}, Math::Vector2{100.0F, 60.0F}};
    EXPECT_FALSE(UIImage::HasTexture(image));
    EXPECT_FALSE(UIImage::UsesNineSlice(image));

    image.textureAssetGuid = Asset::AssetGuid{42ULL};
    image.nineSliceEnabled = true;
    image.sliceMarginMin = Math::Vector2{60.0F, 10.0F};
    image.sliceMarginMax = Math::Vector2{60.0F, 20.0F};
    image.sliceUVMin = Math::Vector2{0.25F, 0.1F};
    image.sliceUVMax = Math::Vector2{0.25F, 0.2F};

    EXPECT_TRUE(UIImage::HasTexture(image));
    EXPECT_TRUE(UIImage::UsesNineSlice(image));
    const UIImageNineSliceSplits splits = UIImage::ResolveNineSliceSplits(image);
    // Horizontal margins 60 + 60 > 100 are scaled proportionally to 50 + 50 == 100.
    EXPECT_NEAR(splits.marginLeft, 50.0F, 1e-4F);
    EXPECT_NEAR(splits.marginRight, 50.0F, 1e-4F);
    EXPECT_NEAR(splits.marginTop, 10.0F, 1e-4F);
    EXPECT_NEAR(splits.marginBottom, 20.0F, 1e-4F);
    EXPECT_NEAR(splits.uvLeft, 0.25F, 1e-4F);
    EXPECT_NEAR(splits.uvRight, 0.25F, 1e-4F);
}

TEST_F(CanvasObjectDefinitionsTest, UITextNodePolicyBaselineAndBoundingRect) {
    UITextComponent text{};
    text.positionPixels = Math::Vector2{12.0F, 34.0F};
    text.fontSize = 20.0F;
    EXPECT_FALSE(UIText::HasText(text));

    text.text = "Score: 100";
    EXPECT_TRUE(UIText::HasText(text));
    const Math::Vector2 cursor = UIText::BaselineCursorPixels(text);
    EXPECT_FLOAT_EQ(cursor.x, 12.0F);
    EXPECT_FLOAT_EQ(cursor.y, 54.0F);

    const Math::Rect bounds = UIText::ResolveBoundingRect(text, 88.0F);
    EXPECT_FLOAT_EQ(bounds.position.x, 12.0F);
    EXPECT_FLOAT_EQ(bounds.position.y, 34.0F);
    EXPECT_FLOAT_EQ(bounds.size.x, 88.0F);
    EXPECT_FLOAT_EQ(bounds.size.y, 20.0F);
}

TEST_F(CanvasObjectDefinitionsTest, UIComponentDomainHelpersCoverAnchorLayoutScrollTweenAndWidgets) {
    // UIAnchorComponent
    UIAnchorComponent anchor{};
    EXPECT_TRUE(IsUIAnchorStretching(anchor));
    const Math::Rect anchored =
        ResolveUIAnchoredRect(anchor, Math::Rect{{10.0F, 20.0F}, {200.0F, 100.0F}});
    EXPECT_FLOAT_EQ(anchored.position.x, 10.0F);
    EXPECT_FLOAT_EQ(anchored.position.y, 20.0F);
    EXPECT_FLOAT_EQ(anchored.size.x, 200.0F);
    EXPECT_FLOAT_EQ(anchored.size.y, 100.0F);

    // UILayoutContainerComponent
    UILayoutContainerComponent layout{};
    layout.rect = Math::Rect{{0.0F, 0.0F}, {300.0F, 200.0F}};
    layout.padding = 10.0F;
    layout.wrapAfter = 3U;
    const Math::Rect inner = ResolveUILayoutContentRect(layout);
    EXPECT_FLOAT_EQ(inner.position.x, 10.0F);
    EXPECT_FLOAT_EQ(inner.size.x, 280.0F);
    EXPECT_EQ(ComputeUILayoutLineCount(layout, 7U), 3U);

    // UIScrollContainerComponent & UIScrollbarComponent
    UIScrollContainerComponent scroll{};
    scroll.rect = Math::Rect{{0.0F, 0.0F}, {100.0F, 100.0F}};
    scroll.contentSize = Math::Vector2{100.0F, 250.0F};
    scroll.wheelStep = 25.0F;
    EXPECT_TRUE(HasUIScrollVerticalOverflow(scroll));
    (void)ApplyUIScrollWheelDelta(scroll, -2.0F, false);
    EXPECT_FLOAT_EQ(scroll.scrollOffset.y, 50.0F);

    UIScrollbarComponent scrollbar{};
    scrollbar.rect = Math::Rect{{90.0F, 0.0F}, {10.0F, 100.0F}};
    EXPECT_FLOAT_EQ(ResolveUIScrollbarThumbHeight(scrollbar, 100.0F, 250.0F), 40.0F);
    const Math::Rect thumbRect =
        ResolveUIScrollbarThumbRect(scrollbar, 50.0F, 100.0F, 250.0F);
    EXPECT_NEAR(thumbRect.position.y, 20.0F, 1e-4F);

    // UISliderComponent & UIProgressBarComponent
    UISliderComponent slider{};
    slider.minValue = 0.0F;
    slider.maxValue = 10.0F;
    slider.step = 2.5F;
    slider.value = SnapAndClampUISliderValue(slider, 6.1F);
    EXPECT_FLOAT_EQ(slider.value, 5.0F);
    EXPECT_FLOAT_EQ(ResolveUISliderNormalizedRatio(slider), 0.5F);

    UIProgressBarComponent progress{};
    progress.rect = Math::Rect{{0.0F, 0.0F}, {200.0F, 20.0F}};
    progress.minValue = 0.0F;
    progress.maxValue = 100.0F;
    progress.value = 25.0F;
    EXPECT_FLOAT_EQ(ResolveUIProgressRatio(progress), 0.25F);
    EXPECT_FLOAT_EQ(ResolveUIProgressFillRect(progress).size.x, 50.0F);

    // UICheckboxComponent & UITooltipComponent
    UICheckboxComponent checkbox{};
    EXPECT_TRUE(ToggleUICheckbox(checkbox));
    EXPECT_TRUE(checkbox.wasToggledThisFrame);
    EXPECT_FLOAT_EQ(ResolveUICheckboxMarkRect(checkbox, 0.25F).size.x, 10.0F);

    UITooltipComponent tooltip{};
    tooltip.text = "Save";
    tooltip.delay = 0.5F;
    EXPECT_FALSE(StepUITooltipHoverState(tooltip, true, 0.25F));
    EXPECT_TRUE(StepUITooltipHoverState(tooltip, true, 0.30F));
    EXPECT_TRUE(ShouldDisplayUITooltip(tooltip));

    // UITweenComponent
    UITweenComponent tween{};
    tween.delay = 0.2F;
    tween.duration = 1.0F;
    tween.elapsed = 0.7F;
    EXPECT_FALSE(IsUITweenInDelayPhase(tween));
    EXPECT_NEAR(ResolveUITweenNormalizedCycleProgress(tween), 0.5F, 1e-4F);

    // UITextInputComponent & UIDropdownComponent
    UITextInputComponent input{};
    input.maxLength = 5;
    EXPECT_TRUE(InsertUITextInputCharacters(input, "HelloWorld"));
    EXPECT_EQ(input.text, "Hello");
    EXPECT_TRUE(ApplyUITextInputBackspace(input));
    EXPECT_EQ(input.text, "Hell");

    UIDropdownComponent dropdown{};
    dropdown.options = "Low\nMedium\nHigh";
    EXPECT_EQ(SplitUIDropdownOptions(dropdown).size(), 3U);
    EXPECT_TRUE(SelectUIDropdownOption(dropdown, 2));
    EXPECT_EQ(ResolveUIDropdownDisplayLabel(dropdown), "High");

    // Batch 5 Extended UI Containers, Graph Edit, and Controls
    const Entity containerEnt = entityManager.CreateEntity();
    UIExtendedContainerObjectDefinition contDef{};
    contDef.container.kind = UIExtendedContainerKind::Margin;
    ApplyUIExtendedContainerObjectDefinition(entityManager, containerEnt, contDef);
    const Math::Rect innerRect = UIExtendedContainer::ResolveContentRect(
        entityManager.GetComponent<UIExtendedContainerComponent>(containerEnt),
        Math::Vector2{100.0F, 50.0F});
    EXPECT_FLOAT_EQ(innerRect.position.x, 8.0F);

    const Entity graphEnt = entityManager.CreateEntity();
    UIGraphEditObjectDefinition graphDef{};
    graphDef.graphEdit.nodes = {UIGraphNodeSlot{1U, "Source", {}, 1U, 1U},
                                UIGraphNodeSlot{2U, "Sink", {}, 1U, 1U}};
    ApplyUIGraphEditObjectDefinition(entityManager, graphEnt, graphDef);
    auto& graphComp = entityManager.GetComponent<UIGraphEditComponent>(graphEnt);
    EXPECT_TRUE(UIGraphEdit::TryConnectPorts(graphComp, 1U, 0U, 2U, 0U));
    EXPECT_FALSE(UIGraphEdit::TryConnectPorts(graphComp, 1U, 0U, 2U, 0U));

    const Entity spinEnt = entityManager.CreateEntity();
    UIRangeControlObjectDefinition spinDef{};
    spinDef.range.stepValue = 5.0F;
    spinDef.range.value = 12.3F;
    ApplyUIRangeControlObjectDefinition(entityManager, spinEnt, spinDef);
    EXPECT_GE(UpdateUIExtendedObjects(entityManager, 0.016F), 1U);
    EXPECT_FLOAT_EQ(entityManager.GetComponent<UIRangeControlComponent>(spinEnt).value,
                    10.0F);
}

} // namespace
} // namespace UVE::Scene::Tests
