// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/ui/ui_tween_uve.h"

#include <cstdint>
#include <filesystem>

#include <gtest/gtest.h>

#include "uve/component/entity_uve.h"
#include "uve/component/ui_anchor_component_uve.h"
#include "uve/component/ui_button_component_uve.h"
#include "uve/component/ui_image_component_uve.h"
#include "uve/component/ui_tween_component_uve.h"
#include "uve/entity/entity_manager_uve.h"
#include "uve/events/event_system_uve.h"
#include "uve/input/input_system_uve.h"
#include "uve/math/rect_uve.h"
#include "uve/math/vector2_uve.h"
#include "uve/memory/memory_manager_uve.h"
#include "uve/scene/scene_serializer_uve.h"
#include "uve/ui/ui_runtime_uve.h"

namespace UVE::UI::Tests {
namespace {

class UITweenUVETest : public ::testing::Test {
protected:
    Memory::MemoryManagerUVE memoryManager;
    Events::EventSystemUVE eventSystem;
    Scene::EntityManagerUVE entityManager{memoryManager.GetDefaultAllocatorUVE(), eventSystem};
    Input::InputSystemUVE inputSystem{eventSystem};
    UIRuntimeUVE runtime;
    Scene::SceneSerializerUVE serializer;

    void FrameUVE(const float dt) {
        runtime.SetDeltaTimeUVE(dt);
        inputSystem.UpdateUVE();
        runtime.TickUVE(entityManager, inputSystem);
    }

    Scene::EntityUVE MakeButtonUVE(const Math::RectUVE rect) {
        const Scene::EntityUVE entity = entityManager.CreateEntityUVE();
        Scene::UIButtonComponentUVE button;
        button.rect = rect;
        entityManager.AddComponentUVE<Scene::UIButtonComponentUVE>(entity, button);
        return entity;
    }

    Scene::EntityUVE MakeImageUVE(const Math::RectUVE rect, const float alpha) {
        const Scene::EntityUVE entity = entityManager.CreateEntityUVE();
        Scene::UIImageComponentUVE image;
        image.rect = rect;
        image.alpha = alpha;
        entityManager.AddComponentUVE<Scene::UIImageComponentUVE>(entity, image);
        return entity;
    }

    void AddTweenUVE(const Scene::EntityUVE entity, const Scene::UITweenComponentUVE& tween) {
        entityManager.AddComponentUVE<Scene::UITweenComponentUVE>(entity, tween);
    }
};

TEST_F(UITweenUVETest, Easings_PinEndpointsMidpointsAndTheBackOvershoot) {
    using Scene::UITweenEaseUVE;
    const UITweenEaseUVE all[] = {UITweenEaseUVE::Linear,     UITweenEaseUVE::SineInOut,  UITweenEaseUVE::QuadIn,
                                  UITweenEaseUVE::QuadOut,    UITweenEaseUVE::QuadInOut,  UITweenEaseUVE::CubicIn,
                                  UITweenEaseUVE::CubicOut,   UITweenEaseUVE::CubicInOut, UITweenEaseUVE::OutBack};
    for (const UITweenEaseUVE ease : all) {
        EXPECT_FLOAT_EQ(EaseUITweenUVE(ease, 0.0F), 0.0F);
        EXPECT_FLOAT_EQ(EaseUITweenUVE(ease, 1.0F), 1.0F);
    }
    EXPECT_FLOAT_EQ(EaseUITweenUVE(UITweenEaseUVE::SineInOut, 0.5F), 0.5F);
    EXPECT_FLOAT_EQ(EaseUITweenUVE(UITweenEaseUVE::QuadIn, 0.5F), 0.25F);
    EXPECT_FLOAT_EQ(EaseUITweenUVE(UITweenEaseUVE::QuadOut, 0.5F), 0.75F);
    EXPECT_FLOAT_EQ(EaseUITweenUVE(UITweenEaseUVE::QuadInOut, 0.25F), 0.125F);
    EXPECT_FLOAT_EQ(EaseUITweenUVE(UITweenEaseUVE::CubicIn, 0.5F), 0.125F);
    EXPECT_FLOAT_EQ(EaseUITweenUVE(UITweenEaseUVE::CubicOut, 0.5F), 0.875F);
    EXPECT_FLOAT_EQ(EaseUITweenUVE(UITweenEaseUVE::CubicInOut, 0.25F), 0.0625F);
    EXPECT_GT(EaseUITweenUVE(UITweenEaseUVE::OutBack, 0.5F), 1.0F);
    EXPECT_FLOAT_EQ(EaseUITweenUVE(static_cast<UITweenEaseUVE>(99), 0.3F), 0.3F);
}

TEST_F(UITweenUVETest, RectTween_AdvancesLinearlyWithDeltaTime) {
    const Scene::EntityUVE widget = MakeButtonUVE(Math::RectUVE{{0.0F, 0.0F}, {100.0F, 20.0F}});
    Scene::UITweenComponentUVE tween;
    tween.fromRect = Math::RectUVE{{0.0F, 0.0F}, {100.0F, 20.0F}};
    tween.toRect = Math::RectUVE{{200.0F, 0.0F}, {100.0F, 20.0F}};
    AddTweenUVE(widget, tween);

    FrameUVE(0.25F);
    EXPECT_FLOAT_EQ(entityManager.GetComponentUVE<Scene::UIButtonComponentUVE>(widget).rect.position.x, 50.0F);
    FrameUVE(0.25F);
    EXPECT_FLOAT_EQ(entityManager.GetComponentUVE<Scene::UIButtonComponentUVE>(widget).rect.position.x, 100.0F);
}

TEST_F(UITweenUVETest, Delay_HoldsFromUntilDelayExpires) {
    const Scene::EntityUVE widget = MakeButtonUVE(Math::RectUVE{{0.0F, 0.0F}, {100.0F, 20.0F}});
    Scene::UITweenComponentUVE tween;
    tween.fromRect = Math::RectUVE{{0.0F, 0.0F}, {100.0F, 20.0F}};
    tween.toRect = Math::RectUVE{{200.0F, 0.0F}, {100.0F, 20.0F}};
    tween.delay = 0.5F;
    AddTweenUVE(widget, tween);

    FrameUVE(0.25F);
    EXPECT_FLOAT_EQ(entityManager.GetComponentUVE<Scene::UIButtonComponentUVE>(widget).rect.position.x, 0.0F);
    FrameUVE(0.5F);
    EXPECT_FLOAT_EQ(entityManager.GetComponentUVE<Scene::UIButtonComponentUVE>(widget).rect.position.x, 50.0F);
}

TEST_F(UITweenUVETest, Once_CompletesAndHoldsTo) {
    const Scene::EntityUVE widget = MakeButtonUVE(Math::RectUVE{{0.0F, 0.0F}, {100.0F, 20.0F}});
    Scene::UITweenComponentUVE tween;
    tween.fromRect = Math::RectUVE{{0.0F, 0.0F}, {100.0F, 20.0F}};
    tween.toRect = Math::RectUVE{{200.0F, 0.0F}, {100.0F, 20.0F}};
    AddTweenUVE(widget, tween);

    FrameUVE(0.6F);
    EXPECT_FLOAT_EQ(entityManager.GetComponentUVE<Scene::UIButtonComponentUVE>(widget).rect.position.x, 120.0F);
    FrameUVE(0.6F);
    const Scene::UITweenComponentUVE& done = entityManager.GetComponentUVE<Scene::UITweenComponentUVE>(widget);
    EXPECT_FLOAT_EQ(entityManager.GetComponentUVE<Scene::UIButtonComponentUVE>(widget).rect.position.x, 200.0F);
    EXPECT_FALSE(done.playing);
    EXPECT_TRUE(done.completedThisFrame);
    FrameUVE(0.5F);
    EXPECT_FLOAT_EQ(entityManager.GetComponentUVE<Scene::UIButtonComponentUVE>(widget).rect.position.x, 200.0F);
    EXPECT_FALSE(entityManager.GetComponentUVE<Scene::UITweenComponentUVE>(widget).completedThisFrame);
}

TEST_F(UITweenUVETest, Loop_WrapsAndSignalsEachWrap) {
    const Scene::EntityUVE widget = MakeButtonUVE(Math::RectUVE{{0.0F, 0.0F}, {100.0F, 20.0F}});
    Scene::UITweenComponentUVE tween;
    tween.fromRect = Math::RectUVE{{0.0F, 0.0F}, {100.0F, 20.0F}};
    tween.toRect = Math::RectUVE{{200.0F, 0.0F}, {100.0F, 20.0F}};
    tween.loop = Scene::UITweenLoopUVE::Loop;
    AddTweenUVE(widget, tween);

    FrameUVE(0.75F);
    EXPECT_FLOAT_EQ(entityManager.GetComponentUVE<Scene::UIButtonComponentUVE>(widget).rect.position.x, 150.0F);
    FrameUVE(0.5F);
    EXPECT_FLOAT_EQ(entityManager.GetComponentUVE<Scene::UIButtonComponentUVE>(widget).rect.position.x, 50.0F);
    EXPECT_TRUE(entityManager.GetComponentUVE<Scene::UITweenComponentUVE>(widget).completedThisFrame);
    FrameUVE(0.5F);
    EXPECT_FLOAT_EQ(entityManager.GetComponentUVE<Scene::UIButtonComponentUVE>(widget).rect.position.x, 150.0F);
    EXPECT_FALSE(entityManager.GetComponentUVE<Scene::UITweenComponentUVE>(widget).completedThisFrame);
}

TEST_F(UITweenUVETest, PingPong_ReversesEachCycle) {
    const Scene::EntityUVE widget = MakeButtonUVE(Math::RectUVE{{0.0F, 0.0F}, {100.0F, 20.0F}});
    Scene::UITweenComponentUVE tween;
    tween.fromRect = Math::RectUVE{{0.0F, 0.0F}, {100.0F, 20.0F}};
    tween.toRect = Math::RectUVE{{200.0F, 0.0F}, {100.0F, 20.0F}};
    tween.loop = Scene::UITweenLoopUVE::PingPong;
    AddTweenUVE(widget, tween);

    FrameUVE(1.0F);
    EXPECT_FLOAT_EQ(entityManager.GetComponentUVE<Scene::UIButtonComponentUVE>(widget).rect.position.x, 200.0F);
    EXPECT_TRUE(entityManager.GetComponentUVE<Scene::UITweenComponentUVE>(widget).completedThisFrame);
    FrameUVE(0.5F);
    EXPECT_FLOAT_EQ(entityManager.GetComponentUVE<Scene::UIButtonComponentUVE>(widget).rect.position.x, 100.0F);
    EXPECT_FALSE(entityManager.GetComponentUVE<Scene::UITweenComponentUVE>(widget).completedThisFrame);
}

TEST_F(UITweenUVETest, AlphaTween_MultipliesAuthoredAlpha) {
    const Scene::EntityUVE widget = MakeImageUVE(Math::RectUVE{{0.0F, 0.0F}, {100.0F, 20.0F}}, 0.5F);
    Scene::UITweenComponentUVE tween;
    tween.target = Scene::UITweenTargetUVE::Alpha;
    tween.fromAlpha = 0.0F;
    tween.toAlpha = 1.0F;
    AddTweenUVE(widget, tween);

    FrameUVE(0.5F);

    EXPECT_FLOAT_EQ(entityManager.GetComponentUVE<Scene::UITweenComponentUVE>(widget).currentAlpha, 0.5F);
    const std::vector<UIQuadUVE>& quads = runtime.GetDrawBatchUVE().quads;
    ASSERT_EQ(quads.size(), 1U);
    EXPECT_FLOAT_EQ(quads[0].alpha, 0.25F);
}

TEST_F(UITweenUVETest, TweenOverridesAnchoredRect) {
    const Scene::EntityUVE widget = MakeButtonUVE(Math::RectUVE{{0.0F, 0.0F}, {10.0F, 10.0F}});
    Scene::UIAnchorComponentUVE anchor; // full-stretch: anchors alone would fill the viewport
    entityManager.AddComponentUVE<Scene::UIAnchorComponentUVE>(widget, anchor);
    Scene::UITweenComponentUVE tween;
    tween.fromRect = Math::RectUVE{{10.0F, 10.0F}, {50.0F, 50.0F}};
    tween.toRect = Math::RectUVE{{10.0F, 10.0F}, {50.0F, 50.0F}};
    AddTweenUVE(widget, tween);
    runtime.SetViewportSizeUVE({400.0F, 300.0F});

    FrameUVE(0.5F);

    EXPECT_EQ(entityManager.GetComponentUVE<Scene::UIButtonComponentUVE>(widget).rect,
              (Math::RectUVE{{10.0F, 10.0F}, {50.0F, 50.0F}}));
}

TEST_F(UITweenUVETest, InvalidTween_ParksNeutrallyAndDimsNothing) {
    const Scene::EntityUVE widget = MakeImageUVE(Math::RectUVE{{0.0F, 0.0F}, {100.0F, 20.0F}}, 0.5F);
    Scene::UITweenComponentUVE tween;
    tween.target = Scene::UITweenTargetUVE::Alpha;
    tween.duration = 0.0F;
    tween.currentAlpha = 0.3F;
    AddTweenUVE(widget, tween);

    FrameUVE(0.5F);

    const Scene::UITweenComponentUVE& parked = entityManager.GetComponentUVE<Scene::UITweenComponentUVE>(widget);
    EXPECT_FALSE(parked.playing);
    EXPECT_FLOAT_EQ(parked.currentAlpha, 1.0F);
    EXPECT_FLOAT_EQ(runtime.GetDrawBatchUVE().quads[0].alpha, 0.5F);
}

TEST_F(UITweenUVETest, SaveThenLoad_Tween_RestartsFromAuthoredValues) {
    const Scene::EntityUVE entity = entityManager.CreateEntityUVE();
    Scene::UITweenComponentUVE tween;
    tween.target = Scene::UITweenTargetUVE::Rect;
    tween.fromRect = Math::RectUVE{{5.0F, 5.0F}, {60.0F, 20.0F}};
    tween.toRect = Math::RectUVE{{100.0F, 100.0F}, {80.0F, 40.0F}};
    tween.fromAlpha = 0.2F;
    tween.toAlpha = 0.9F;
    tween.duration = 2.0F;
    tween.delay = 0.5F;
    tween.ease = Scene::UITweenEaseUVE::QuadOut;
    tween.loop = Scene::UITweenLoopUVE::PingPong;
    tween.elapsed = 0.7F;
    tween.playing = true;
    tween.completedThisFrame = true;
    tween.currentAlpha = 0.3F;
    entityManager.AddComponentUVE<Scene::UITweenComponentUVE>(entity, tween);

    const std::filesystem::path path = "uve_scene_serializer_tests_ui_tween.uvscene";
    std::filesystem::remove(path);
    ASSERT_TRUE(serializer.SaveUVE(entityManager, {entity}, path, Scene::SceneAssetTypeUVE::Scene));

    Scene::EntityManagerUVE loadedManager(memoryManager.GetDefaultAllocatorUVE(), eventSystem);
    const std::vector<Scene::EntityUVE> loaded = serializer.LoadUVE(loadedManager, path);
    ASSERT_EQ(loaded.size(), 1U);
    const Scene::UITweenComponentUVE& restored =
        loadedManager.GetComponentUVE<Scene::UITweenComponentUVE>(loaded[0]);
    EXPECT_EQ(restored.target, Scene::UITweenTargetUVE::Rect);
    EXPECT_FLOAT_EQ(restored.fromRect.position.x, 5.0F);
    EXPECT_FLOAT_EQ(restored.fromRect.size.y, 20.0F);
    EXPECT_FLOAT_EQ(restored.toRect.position.x, 100.0F);
    EXPECT_FLOAT_EQ(restored.toRect.size.y, 40.0F);
    EXPECT_FLOAT_EQ(restored.fromAlpha, 0.2F);
    EXPECT_FLOAT_EQ(restored.toAlpha, 0.9F);
    EXPECT_FLOAT_EQ(restored.duration, 2.0F);
    EXPECT_FLOAT_EQ(restored.delay, 0.5F);
    EXPECT_EQ(restored.ease, Scene::UITweenEaseUVE::QuadOut);
    EXPECT_EQ(restored.loop, Scene::UITweenLoopUVE::PingPong);
    EXPECT_FLOAT_EQ(restored.elapsed, 0.0F);
    EXPECT_TRUE(restored.playing);
    EXPECT_FALSE(restored.completedThisFrame);
    EXPECT_FLOAT_EQ(restored.currentAlpha, 1.0F);

    std::filesystem::remove(path);
}

} // namespace
} // namespace UVE::UI::Tests
