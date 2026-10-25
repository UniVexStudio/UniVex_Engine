// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include <type_traits>
#include <concepts>

#include <gtest/gtest.h>

#include "uve/component/canvas_component_uve.h"
#include "uve/component/entity_uve.h"
#include "uve/component/ui_anchor_component_uve.h"
#include "uve/component/ui_button_component_uve.h"
#include "uve/component/ui_checkbox_component_uve.h"
#include "uve/component/ui_dropdown_component_uve.h"
#include "uve/component/ui_image_component_uve.h"
#include "uve/component/ui_layout_container_component_uve.h"
#include "uve/component/ui_progress_bar_component_uve.h"
#include "uve/component/ui_scroll_container_component_uve.h"
#include "uve/component/ui_scrollbar_component_uve.h"
#include "uve/component/ui_slider_component_uve.h"
#include "uve/component/ui_text_component_uve.h"
#include "uve/component/ui_text_input_component_uve.h"
#include "uve/component/ui_tooltip_component_uve.h"
#include "uve/component/ui_tween_component_uve.h"
#include "uve/component/visibility_component_uve.h"
#include "uve/entity/entity_manager_uve.h"
#include "uve/events/event_system_uve.h"
#include "uve/memory/memory_manager_uve.h"
#include "uve/objects/canvas/all_objects_canvas_uve.h"
#include "uve/scene/objects/scene_object_registry_uve.h"

namespace UVE::Scene::Tests {
namespace {
static_assert(std::equality_comparable<CanvasObjectDefinitionUVE>);
static_assert(std::equality_comparable<UIButtonObjectDefinitionUVE>);
static_assert(std::equality_comparable<UIImageObjectDefinitionUVE>);
static_assert(std::equality_comparable<UITextObjectDefinitionUVE>);


// All four promoted UI kinds' definitions are reachable from the aggregate header, mirroring
// the registry test's guarantee: no Canvas kind's home file can be silently dropped.
static_assert(std::is_class_v<CanvasObjectDefinitionUVE>);   // Canvas
static_assert(std::is_class_v<UITextObjectDefinitionUVE>);   // UIText
static_assert(std::is_class_v<UIImageObjectDefinitionUVE>);  // UIImage
static_assert(std::is_class_v<UIButtonObjectDefinitionUVE>); // UIButton

// The names match the Inspector's own component labels exactly, so both entry points (Add
// Component list and Add Object list) feel identical for the same kind.
static_assert(CanvasObjectDefinitionUVE::defaultName == "Canvas");
static_assert(UITextObjectDefinitionUVE::defaultName == "UI Text");
static_assert(UIImageObjectDefinitionUVE::defaultName == "UI Image");
static_assert(UIButtonObjectDefinitionUVE::defaultName == "UI Button");

class CanvasObjectDefinitionsUVETest : public ::testing::Test {
protected:
    Memory::MemoryManagerUVE memoryManager;
    Events::EventSystemUVE eventSystem;
    EntityManagerUVE entityManager{memoryManager.GetDefaultAllocatorUVE(), eventSystem};

    [[nodiscard]] EntityUVE CreateEntityUVE() {
        return entityManager.CreateEntityUVE();
    }
};

TEST_F(CanvasObjectDefinitionsUVETest, AllDefinitionDefaultsAreValid) {
    EXPECT_TRUE(IsCanvasObjectDefinitionValidUVE(CanvasObjectDefinitionUVE{}));
    EXPECT_TRUE(IsUITextObjectDefinitionValidUVE(UITextObjectDefinitionUVE{}));
    EXPECT_TRUE(IsUIImageObjectDefinitionValidUVE(UIImageObjectDefinitionUVE{}));
    EXPECT_TRUE(IsUIButtonObjectDefinitionValidUVE(UIButtonObjectDefinitionUVE{}));
}

TEST_F(CanvasObjectDefinitionsUVETest, ApplyAttachesEachKindsExactComponentRecipe) {
    {
        const EntityUVE entity = CreateEntityUVE();
        ApplyCanvasObjectDefinitionUVE(entityManager, entity, CanvasObjectDefinitionUVE{});
        EXPECT_TRUE(entityManager.HasComponentUVE<CanvasComponentUVE>(entity));
    }
    {
        const EntityUVE entity = CreateEntityUVE();
        ApplyUITextObjectDefinitionUVE(entityManager, entity, UITextObjectDefinitionUVE{});
        EXPECT_TRUE(entityManager.HasComponentUVE<UITextComponentUVE>(entity));
    }
    {
        const EntityUVE entity = CreateEntityUVE();
        ApplyUIImageObjectDefinitionUVE(entityManager, entity, UIImageObjectDefinitionUVE{});
        EXPECT_TRUE(entityManager.HasComponentUVE<UIImageComponentUVE>(entity));
    }
    {
        const EntityUVE entity = CreateEntityUVE();
        ApplyUIButtonObjectDefinitionUVE(entityManager, entity, UIButtonObjectDefinitionUVE{});
        EXPECT_TRUE(entityManager.HasComponentUVE<UIButtonComponentUVE>(entity));
    }
}

TEST_F(CanvasObjectDefinitionsUVETest, PromotedKindsAreRegisteredAndLibraryCreatable) {
    // The whole point of the promotion: these kinds must now appear in the Add-Object list's
    // backing registry, creatable like every other library kind.
    for (const std::string_view typeId : {"canvas", "ui_text", "ui_image", "ui_button"}) {
        const Objects::SceneObjectDescriptorUVE* descriptor = Objects::FindSceneObjectDescriptorUVE(typeId);
        ASSERT_NE(descriptor, nullptr);
        EXPECT_EQ(descriptor->category, "UI");
        EXPECT_TRUE(descriptor->libraryCreatable);
    }
}

TEST_F(CanvasObjectDefinitionsUVETest, CanvasNodePolicyVisibilityAndSortOrder) {
    CanvasComponentUVE back{};
    back.sortOrder = -5;
    CanvasComponentUVE front{};
    front.sortOrder = 10;

    EXPECT_TRUE(CanvasUVE::IsCanvasVisibleUVE(back));
    EXPECT_EQ(CanvasUVE::EffectiveSortOrderUVE(back), -5);
    EXPECT_TRUE(CanvasUVE::RendersBeforeUVE(back, front));
    EXPECT_FALSE(CanvasUVE::RendersBeforeUVE(front, back));

    const EntityUVE entity = CreateEntityUVE();
    ApplyCanvasObjectDefinitionUVE(entityManager, entity, CanvasObjectDefinitionUVE{back});
    EXPECT_TRUE(CanvasUVE::IsEntityCanvasVisibleUVE(entityManager, entity));

    VisibilityComponentUVE hidden{};
    hidden.visible = false;
    hidden.visibleInHierarchy = false;
    entityManager.AddComponentUVE<VisibilityComponentUVE>(entity, hidden);
    EXPECT_FALSE(CanvasUVE::IsEntityCanvasVisibleUVE(entityManager, entity));
}

TEST_F(CanvasObjectDefinitionsUVETest, UIButtonNodePolicyHitTestingStateAndPalette) {
    UIButtonComponentUVE button{};
    button.rect = Math::RectUVE{Math::Vector2UVE{10.0F, 20.0F}, Math::Vector2UVE{100.0F, 40.0F}};

    EXPECT_TRUE(UIButtonUVE::ContainsPointUVE(button, Math::Vector2UVE{50.0F, 35.0F}));
    EXPECT_FALSE(UIButtonUVE::ContainsPointUVE(button, Math::Vector2UVE{5.0F, 35.0F}));

    UIButtonUVE::UpdateInteractionStateUVE(button, /*pointerHovered=*/true, /*mousePressedThisFrame=*/true,
                                           /*keyboardFocused=*/false, /*activatePressedThisFrame=*/false);
    EXPECT_TRUE(button.isHovered);
    EXPECT_TRUE(button.wasClickedThisFrame);
    EXPECT_EQ(UIButtonUVE::ResolveVisualColorUVE(button, false, /*pressActive=*/true), button.pressedColor);
    EXPECT_EQ(UIButtonUVE::ResolveVisualColorUVE(button, false, /*pressActive=*/false), button.hoverColor);

    UIButtonUVE::ClearRuntimeStateUVE(button);
    EXPECT_FALSE(button.isHovered);
    EXPECT_FALSE(button.wasClickedThisFrame);
    EXPECT_EQ(UIButtonUVE::ResolveVisualColorUVE(button, false, false), button.normalColor);
}

TEST_F(CanvasObjectDefinitionsUVETest, UIImageNodePolicyTextureAndNineSliceSplits) {
    UIImageComponentUVE image{};
    image.rect = Math::RectUVE{Math::Vector2UVE{0.0F, 0.0F}, Math::Vector2UVE{100.0F, 60.0F}};
    EXPECT_FALSE(UIImageUVE::HasTextureUVE(image));
    EXPECT_FALSE(UIImageUVE::UsesNineSliceUVE(image));

    image.textureAssetGuid = Asset::AssetGuidUVE{42ULL};
    image.nineSliceEnabled = true;
    image.sliceMarginMin = Math::Vector2UVE{60.0F, 10.0F};
    image.sliceMarginMax = Math::Vector2UVE{60.0F, 20.0F};
    image.sliceUVMin = Math::Vector2UVE{0.25F, 0.1F};
    image.sliceUVMax = Math::Vector2UVE{0.25F, 0.2F};

    EXPECT_TRUE(UIImageUVE::HasTextureUVE(image));
    EXPECT_TRUE(UIImageUVE::UsesNineSliceUVE(image));
    const UIImageNineSliceSplitsUVE splits = UIImageUVE::ResolveNineSliceSplitsUVE(image);
    // Horizontal margins 60 + 60 > 100 are scaled proportionally to 50 + 50 == 100.
    EXPECT_NEAR(splits.marginLeft, 50.0F, 1e-4F);
    EXPECT_NEAR(splits.marginRight, 50.0F, 1e-4F);
    EXPECT_NEAR(splits.marginTop, 10.0F, 1e-4F);
    EXPECT_NEAR(splits.marginBottom, 20.0F, 1e-4F);
    EXPECT_NEAR(splits.uvLeft, 0.25F, 1e-4F);
    EXPECT_NEAR(splits.uvRight, 0.25F, 1e-4F);
}

TEST_F(CanvasObjectDefinitionsUVETest, UITextNodePolicyBaselineAndBoundingRect) {
    UITextComponentUVE text{};
    text.positionPixels = Math::Vector2UVE{12.0F, 34.0F};
    text.fontSize = 20.0F;
    EXPECT_FALSE(UITextUVE::HasTextUVE(text));

    text.text = "Score: 100";
    EXPECT_TRUE(UITextUVE::HasTextUVE(text));
    const Math::Vector2UVE cursor = UITextUVE::BaselineCursorPixelsUVE(text);
    EXPECT_FLOAT_EQ(cursor.x, 12.0F);
    EXPECT_FLOAT_EQ(cursor.y, 54.0F);

    const Math::RectUVE bounds = UITextUVE::ResolveBoundingRectUVE(text, 88.0F);
    EXPECT_FLOAT_EQ(bounds.position.x, 12.0F);
    EXPECT_FLOAT_EQ(bounds.position.y, 34.0F);
    EXPECT_FLOAT_EQ(bounds.size.x, 88.0F);
    EXPECT_FLOAT_EQ(bounds.size.y, 20.0F);
}

TEST_F(CanvasObjectDefinitionsUVETest, UIComponentDomainHelpersCoverAnchorLayoutScrollTweenAndWidgets) {
    // UIAnchorComponentUVE
    UIAnchorComponentUVE anchor{};
    EXPECT_TRUE(IsUIAnchorStretchingUVE(anchor));
    const Math::RectUVE anchored =
        ResolveUIAnchoredRectUVE(anchor, Math::RectUVE{{10.0F, 20.0F}, {200.0F, 100.0F}});
    EXPECT_FLOAT_EQ(anchored.position.x, 10.0F);
    EXPECT_FLOAT_EQ(anchored.position.y, 20.0F);
    EXPECT_FLOAT_EQ(anchored.size.x, 200.0F);
    EXPECT_FLOAT_EQ(anchored.size.y, 100.0F);

    // UILayoutContainerComponentUVE
    UILayoutContainerComponentUVE layout{};
    layout.rect = Math::RectUVE{{0.0F, 0.0F}, {300.0F, 200.0F}};
    layout.padding = 10.0F;
    layout.wrapAfter = 3U;
    const Math::RectUVE inner = ResolveUILayoutContentRectUVE(layout);
    EXPECT_FLOAT_EQ(inner.position.x, 10.0F);
    EXPECT_FLOAT_EQ(inner.size.x, 280.0F);
    EXPECT_EQ(ComputeUILayoutLineCountUVE(layout, 7U), 3U);

    // UIScrollContainerComponentUVE & UIScrollbarComponentUVE
    UIScrollContainerComponentUVE scroll{};
    scroll.rect = Math::RectUVE{{0.0F, 0.0F}, {100.0F, 100.0F}};
    scroll.contentSize = Math::Vector2UVE{100.0F, 250.0F};
    scroll.wheelStep = 25.0F;
    EXPECT_TRUE(HasUIScrollVerticalOverflowUVE(scroll));
    (void)ApplyUIScrollWheelDeltaUVE(scroll, -2.0F, false);
    EXPECT_FLOAT_EQ(scroll.scrollOffset.y, 50.0F);

    UIScrollbarComponentUVE scrollbar{};
    scrollbar.rect = Math::RectUVE{{90.0F, 0.0F}, {10.0F, 100.0F}};
    EXPECT_FLOAT_EQ(ResolveUIScrollbarThumbHeightUVE(scrollbar, 100.0F, 250.0F), 40.0F);
    const Math::RectUVE thumbRect =
        ResolveUIScrollbarThumbRectUVE(scrollbar, 50.0F, 100.0F, 250.0F);
    EXPECT_NEAR(thumbRect.position.y, 20.0F, 1e-4F);

    // UISliderComponentUVE & UIProgressBarComponentUVE
    UISliderComponentUVE slider{};
    slider.minValue = 0.0F;
    slider.maxValue = 10.0F;
    slider.step = 2.5F;
    slider.value = SnapAndClampUISliderValueUVE(slider, 6.1F);
    EXPECT_FLOAT_EQ(slider.value, 5.0F);
    EXPECT_FLOAT_EQ(ResolveUISliderNormalizedRatioUVE(slider), 0.5F);

    UIProgressBarComponentUVE progress{};
    progress.rect = Math::RectUVE{{0.0F, 0.0F}, {200.0F, 20.0F}};
    progress.minValue = 0.0F;
    progress.maxValue = 100.0F;
    progress.value = 25.0F;
    EXPECT_FLOAT_EQ(ResolveUIProgressRatioUVE(progress), 0.25F);
    EXPECT_FLOAT_EQ(ResolveUIProgressFillRectUVE(progress).size.x, 50.0F);

    // UICheckboxComponentUVE & UITooltipComponentUVE
    UICheckboxComponentUVE checkbox{};
    EXPECT_TRUE(ToggleUICheckboxUVE(checkbox));
    EXPECT_TRUE(checkbox.wasToggledThisFrame);
    EXPECT_FLOAT_EQ(ResolveUICheckboxMarkRectUVE(checkbox, 0.25F).size.x, 10.0F);

    UITooltipComponentUVE tooltip{};
    tooltip.text = "Save";
    tooltip.delay = 0.5F;
    EXPECT_FALSE(StepUITooltipHoverStateUVE(tooltip, true, 0.25F));
    EXPECT_TRUE(StepUITooltipHoverStateUVE(tooltip, true, 0.30F));
    EXPECT_TRUE(ShouldDisplayUITooltipUVE(tooltip));

    // UITweenComponentUVE
    UITweenComponentUVE tween{};
    tween.delay = 0.2F;
    tween.duration = 1.0F;
    tween.elapsed = 0.7F;
    EXPECT_FALSE(IsUITweenInDelayPhaseUVE(tween));
    EXPECT_NEAR(ResolveUITweenNormalizedCycleProgressUVE(tween), 0.5F, 1e-4F);

    // UITextInputComponentUVE & UIDropdownComponentUVE
    UITextInputComponentUVE input{};
    input.maxLength = 5;
    EXPECT_TRUE(InsertUITextInputCharactersUVE(input, "HelloWorld"));
    EXPECT_EQ(input.text, "Hello");
    EXPECT_TRUE(ApplyUITextInputBackspaceUVE(input));
    EXPECT_EQ(input.text, "Hell");

    UIDropdownComponentUVE dropdown{};
    dropdown.options = "Low\nMedium\nHigh";
    EXPECT_EQ(SplitUIDropdownOptionsUVE(dropdown).size(), 3U);
    EXPECT_TRUE(SelectUIDropdownOptionUVE(dropdown, 2));
    EXPECT_EQ(ResolveUIDropdownDisplayLabelUVE(dropdown), "High");

    // Batch 5 Extended UI Containers, Graph Edit, and Controls
    const EntityUVE containerEnt = entityManager.CreateEntityUVE();
    UIExtendedContainerObjectDefinitionUVE contDef{};
    contDef.container.kind = UIExtendedContainerKindUVE::Margin;
    ApplyUIExtendedContainerObjectDefinitionUVE(entityManager, containerEnt, contDef);
    const Math::RectUVE innerRect = UIExtendedContainerUVE::ResolveContentRectUVE(
        entityManager.GetComponentUVE<UIExtendedContainerComponentUVE>(containerEnt),
        Math::Vector2UVE{100.0F, 50.0F});
    EXPECT_FLOAT_EQ(innerRect.position.x, 8.0F);

    const EntityUVE graphEnt = entityManager.CreateEntityUVE();
    UIGraphEditObjectDefinitionUVE graphDef{};
    graphDef.graphEdit.nodes = {UIGraphNodeSlotUVE{1U, "Source", {}, 1U, 1U},
                                UIGraphNodeSlotUVE{2U, "Sink", {}, 1U, 1U}};
    ApplyUIGraphEditObjectDefinitionUVE(entityManager, graphEnt, graphDef);
    auto& graphComp = entityManager.GetComponentUVE<UIGraphEditComponentUVE>(graphEnt);
    EXPECT_TRUE(UIGraphEditUVE::TryConnectPortsUVE(graphComp, 1U, 0U, 2U, 0U));
    EXPECT_FALSE(UIGraphEditUVE::TryConnectPortsUVE(graphComp, 1U, 0U, 2U, 0U));

    const EntityUVE spinEnt = entityManager.CreateEntityUVE();
    UIRangeControlObjectDefinitionUVE spinDef{};
    spinDef.range.stepValue = 5.0F;
    spinDef.range.value = 12.3F;
    ApplyUIRangeControlObjectDefinitionUVE(entityManager, spinEnt, spinDef);
    EXPECT_GE(UpdateUIExtendedObjectsUVE(entityManager, 0.016F), 1U);
    EXPECT_FLOAT_EQ(entityManager.GetComponentUVE<UIRangeControlComponentUVE>(spinEnt).value,
                    10.0F);
}

} // namespace
} // namespace UVE::Scene::Tests
