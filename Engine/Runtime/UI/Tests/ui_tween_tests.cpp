// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "ui_tween.h"

#include <cstdint>
#include <filesystem>

#include <gtest/gtest.h>

#include "entity.h"
#include "ui_anchor_component.h"
#include "ui_button_component.h"
#include "ui_image_component.h"
#include "ui_tween_component.h"
#include "entity_manager.h"
#include "event_system.h"
#include "input_system.h"
#include "rect.h"
#include "vector2.h"
#include "memory_manager.h"
#include "scene_serializer.h"
#include "ui_runtime.h"

namespace UVE::UI::Tests {
namespace {

class UITweenTest : public ::testing::Test {
protected:
    Memory::MemoryManager memoryManager;
    Events::EventSystem eventSystem;
    Scene::EntityManager entityManager{memoryManager.GetDefaultAllocator(), eventSystem};
    Input::InputSystem inputSystem{eventSystem};
    UIRuntime runtime;
    Scene::SceneSerializer serializer;

    void Frame(const float dt) {
        runtime.SetDeltaTime(dt);
        inputSystem.Update();
        runtime.Tick(entityManager, inputSystem);
    }

    Scene::Entity MakeButton(const Math::Rect rect) {
        const Scene::Entity entity = entityManager.CreateEntity();
        Scene::UIButtonComponent button;
        button.rect = rect;
        entityManager.AddComponent<Scene::UIButtonComponent>(entity, button);
        return entity;
    }

    Scene::Entity MakeImage(const Math::Rect rect, const float alpha) {
        const Scene::Entity entity = entityManager.CreateEntity();
        Scene::UIImageComponent image;
        image.rect = rect;
        image.alpha = alpha;
        entityManager.AddComponent<Scene::UIImageComponent>(entity, image);
        return entity;
    }

    void AddTween(const Scene::Entity entity, const Scene::UITweenComponent& tween) {
        entityManager.AddComponent<Scene::UITweenComponent>(entity, tween);
    }
};

TEST_F(UITweenTest, Easings_PinEndpointsMidpointsAndTheBackOvershoot) {
    using Scene::UITweenEase;
    const UITweenEase all[] = {UITweenEase::Linear,     UITweenEase::SineInOut,  UITweenEase::QuadIn,
                                  UITweenEase::QuadOut,    UITweenEase::QuadInOut,  UITweenEase::CubicIn,
                                  UITweenEase::CubicOut,   UITweenEase::CubicInOut, UITweenEase::OutBack};
    for (const UITweenEase ease : all) {
        EXPECT_FLOAT_EQ(EaseUITween(ease, 0.0F), 0.0F);
        EXPECT_FLOAT_EQ(EaseUITween(ease, 1.0F), 1.0F);
    }
    EXPECT_FLOAT_EQ(EaseUITween(UITweenEase::SineInOut, 0.5F), 0.5F);
    EXPECT_FLOAT_EQ(EaseUITween(UITweenEase::QuadIn, 0.5F), 0.25F);
    EXPECT_FLOAT_EQ(EaseUITween(UITweenEase::QuadOut, 0.5F), 0.75F);
    EXPECT_FLOAT_EQ(EaseUITween(UITweenEase::QuadInOut, 0.25F), 0.125F);
    EXPECT_FLOAT_EQ(EaseUITween(UITweenEase::CubicIn, 0.5F), 0.125F);
    EXPECT_FLOAT_EQ(EaseUITween(UITweenEase::CubicOut, 0.5F), 0.875F);
    EXPECT_FLOAT_EQ(EaseUITween(UITweenEase::CubicInOut, 0.25F), 0.0625F);
    EXPECT_GT(EaseUITween(UITweenEase::OutBack, 0.5F), 1.0F);
    EXPECT_FLOAT_EQ(EaseUITween(static_cast<UITweenEase>(99), 0.3F), 0.3F);
}

TEST_F(UITweenTest, RectTween_AdvancesLinearlyWithDeltaTime) {
    const Scene::Entity widget = MakeButton(Math::Rect{{0.0F, 0.0F}, {100.0F, 20.0F}});
    Scene::UITweenComponent tween;
    tween.fromRect = Math::Rect{{0.0F, 0.0F}, {100.0F, 20.0F}};
    tween.toRect = Math::Rect{{200.0F, 0.0F}, {100.0F, 20.0F}};
    AddTween(widget, tween);

    Frame(0.25F);
    EXPECT_FLOAT_EQ(entityManager.GetComponent<Scene::UIButtonComponent>(widget).rect.position.x, 50.0F);
    Frame(0.25F);
    EXPECT_FLOAT_EQ(entityManager.GetComponent<Scene::UIButtonComponent>(widget).rect.position.x, 100.0F);
}

TEST_F(UITweenTest, Delay_HoldsFromUntilDelayExpires) {
    const Scene::Entity widget = MakeButton(Math::Rect{{0.0F, 0.0F}, {100.0F, 20.0F}});
    Scene::UITweenComponent tween;
    tween.fromRect = Math::Rect{{0.0F, 0.0F}, {100.0F, 20.0F}};
    tween.toRect = Math::Rect{{200.0F, 0.0F}, {100.0F, 20.0F}};
    tween.delay = 0.5F;
    AddTween(widget, tween);

    Frame(0.25F);
    EXPECT_FLOAT_EQ(entityManager.GetComponent<Scene::UIButtonComponent>(widget).rect.position.x, 0.0F);
    Frame(0.5F);
    EXPECT_FLOAT_EQ(entityManager.GetComponent<Scene::UIButtonComponent>(widget).rect.position.x, 50.0F);
}

TEST_F(UITweenTest, Once_CompletesAndHoldsTo) {
    const Scene::Entity widget = MakeButton(Math::Rect{{0.0F, 0.0F}, {100.0F, 20.0F}});
    Scene::UITweenComponent tween;
    tween.fromRect = Math::Rect{{0.0F, 0.0F}, {100.0F, 20.0F}};
    tween.toRect = Math::Rect{{200.0F, 0.0F}, {100.0F, 20.0F}};
    AddTween(widget, tween);

    Frame(0.6F);
    EXPECT_FLOAT_EQ(entityManager.GetComponent<Scene::UIButtonComponent>(widget).rect.position.x, 120.0F);
    Frame(0.6F);
    const Scene::UITweenComponent& done = entityManager.GetComponent<Scene::UITweenComponent>(widget);
    EXPECT_FLOAT_EQ(entityManager.GetComponent<Scene::UIButtonComponent>(widget).rect.position.x, 200.0F);
    EXPECT_FALSE(done.playing);
    EXPECT_TRUE(done.completedThisFrame);
    Frame(0.5F);
    EXPECT_FLOAT_EQ(entityManager.GetComponent<Scene::UIButtonComponent>(widget).rect.position.x, 200.0F);
    EXPECT_FALSE(entityManager.GetComponent<Scene::UITweenComponent>(widget).completedThisFrame);
}

TEST_F(UITweenTest, Loop_WrapsAndSignalsEachWrap) {
    const Scene::Entity widget = MakeButton(Math::Rect{{0.0F, 0.0F}, {100.0F, 20.0F}});
    Scene::UITweenComponent tween;
    tween.fromRect = Math::Rect{{0.0F, 0.0F}, {100.0F, 20.0F}};
    tween.toRect = Math::Rect{{200.0F, 0.0F}, {100.0F, 20.0F}};
    tween.loop = Scene::UITweenLoop::Loop;
    AddTween(widget, tween);

    Frame(0.75F);
    EXPECT_FLOAT_EQ(entityManager.GetComponent<Scene::UIButtonComponent>(widget).rect.position.x, 150.0F);
    Frame(0.5F);
    EXPECT_FLOAT_EQ(entityManager.GetComponent<Scene::UIButtonComponent>(widget).rect.position.x, 50.0F);
    EXPECT_TRUE(entityManager.GetComponent<Scene::UITweenComponent>(widget).completedThisFrame);
    Frame(0.5F);
    EXPECT_FLOAT_EQ(entityManager.GetComponent<Scene::UIButtonComponent>(widget).rect.position.x, 150.0F);
    EXPECT_FALSE(entityManager.GetComponent<Scene::UITweenComponent>(widget).completedThisFrame);
}

TEST_F(UITweenTest, PingPong_ReversesEachCycle) {
    const Scene::Entity widget = MakeButton(Math::Rect{{0.0F, 0.0F}, {100.0F, 20.0F}});
    Scene::UITweenComponent tween;
    tween.fromRect = Math::Rect{{0.0F, 0.0F}, {100.0F, 20.0F}};
    tween.toRect = Math::Rect{{200.0F, 0.0F}, {100.0F, 20.0F}};
    tween.loop = Scene::UITweenLoop::PingPong;
    AddTween(widget, tween);

    Frame(1.0F);
    EXPECT_FLOAT_EQ(entityManager.GetComponent<Scene::UIButtonComponent>(widget).rect.position.x, 200.0F);
    EXPECT_TRUE(entityManager.GetComponent<Scene::UITweenComponent>(widget).completedThisFrame);
    Frame(0.5F);
    EXPECT_FLOAT_EQ(entityManager.GetComponent<Scene::UIButtonComponent>(widget).rect.position.x, 100.0F);
    EXPECT_FALSE(entityManager.GetComponent<Scene::UITweenComponent>(widget).completedThisFrame);
}

TEST_F(UITweenTest, AlphaTween_MultipliesAuthoredAlpha) {
    const Scene::Entity widget = MakeImage(Math::Rect{{0.0F, 0.0F}, {100.0F, 20.0F}}, 0.5F);
    Scene::UITweenComponent tween;
    tween.target = Scene::UITweenTarget::Alpha;
    tween.fromAlpha = 0.0F;
    tween.toAlpha = 1.0F;
    AddTween(widget, tween);

    Frame(0.5F);

    EXPECT_FLOAT_EQ(entityManager.GetComponent<Scene::UITweenComponent>(widget).currentAlpha, 0.5F);
    const std::vector<UIQuad>& quads = runtime.GetDrawBatch().quads;
    ASSERT_EQ(quads.size(), 1U);
    EXPECT_FLOAT_EQ(quads[0].alpha, 0.25F);
}

TEST_F(UITweenTest, TweenOverridesAnchoredRect) {
    const Scene::Entity widget = MakeButton(Math::Rect{{0.0F, 0.0F}, {10.0F, 10.0F}});
    Scene::UIAnchorComponent anchor; // full-stretch: anchors alone would fill the viewport
    entityManager.AddComponent<Scene::UIAnchorComponent>(widget, anchor);
    Scene::UITweenComponent tween;
    tween.fromRect = Math::Rect{{10.0F, 10.0F}, {50.0F, 50.0F}};
    tween.toRect = Math::Rect{{10.0F, 10.0F}, {50.0F, 50.0F}};
    AddTween(widget, tween);
    runtime.SetViewportSize({400.0F, 300.0F});

    Frame(0.5F);

    EXPECT_EQ(entityManager.GetComponent<Scene::UIButtonComponent>(widget).rect,
              (Math::Rect{{10.0F, 10.0F}, {50.0F, 50.0F}}));
}

TEST_F(UITweenTest, InvalidTween_ParksNeutrallyAndDimsNothing) {
    const Scene::Entity widget = MakeImage(Math::Rect{{0.0F, 0.0F}, {100.0F, 20.0F}}, 0.5F);
    Scene::UITweenComponent tween;
    tween.target = Scene::UITweenTarget::Alpha;
    tween.duration = 0.0F;
    tween.currentAlpha = 0.3F;
    AddTween(widget, tween);

    Frame(0.5F);

    const Scene::UITweenComponent& parked = entityManager.GetComponent<Scene::UITweenComponent>(widget);
    EXPECT_FALSE(parked.playing);
    EXPECT_FLOAT_EQ(parked.currentAlpha, 1.0F);
    EXPECT_FLOAT_EQ(runtime.GetDrawBatch().quads[0].alpha, 0.5F);
}

TEST_F(UITweenTest, SaveThenLoad_Tween_RestartsFromAuthoredValues) {
    const Scene::Entity entity = entityManager.CreateEntity();
    Scene::UITweenComponent tween;
    tween.target = Scene::UITweenTarget::Rect;
    tween.fromRect = Math::Rect{{5.0F, 5.0F}, {60.0F, 20.0F}};
    tween.toRect = Math::Rect{{100.0F, 100.0F}, {80.0F, 40.0F}};
    tween.fromAlpha = 0.2F;
    tween.toAlpha = 0.9F;
    tween.duration = 2.0F;
    tween.delay = 0.5F;
    tween.ease = Scene::UITweenEase::QuadOut;
    tween.loop = Scene::UITweenLoop::PingPong;
    tween.elapsed = 0.7F;
    tween.playing = true;
    tween.completedThisFrame = true;
    tween.currentAlpha = 0.3F;
    entityManager.AddComponent<Scene::UITweenComponent>(entity, tween);

    const std::filesystem::path path = "uve_scene_serializer_tests_ui_tween.uvscene";
    std::filesystem::remove(path);
    ASSERT_TRUE(serializer.Save(entityManager, {entity}, path, Scene::SceneAssetType::Scene));

    Scene::EntityManager loadedManager(memoryManager.GetDefaultAllocator(), eventSystem);
    const std::vector<Scene::Entity> loaded = serializer.Load(loadedManager, path);
    ASSERT_EQ(loaded.size(), 1U);
    const Scene::UITweenComponent& restored =
        loadedManager.GetComponent<Scene::UITweenComponent>(loaded[0]);
    EXPECT_EQ(restored.target, Scene::UITweenTarget::Rect);
    EXPECT_FLOAT_EQ(restored.fromRect.position.x, 5.0F);
    EXPECT_FLOAT_EQ(restored.fromRect.size.y, 20.0F);
    EXPECT_FLOAT_EQ(restored.toRect.position.x, 100.0F);
    EXPECT_FLOAT_EQ(restored.toRect.size.y, 40.0F);
    EXPECT_FLOAT_EQ(restored.fromAlpha, 0.2F);
    EXPECT_FLOAT_EQ(restored.toAlpha, 0.9F);
    EXPECT_FLOAT_EQ(restored.duration, 2.0F);
    EXPECT_FLOAT_EQ(restored.delay, 0.5F);
    EXPECT_EQ(restored.ease, Scene::UITweenEase::QuadOut);
    EXPECT_EQ(restored.loop, Scene::UITweenLoop::PingPong);
    EXPECT_FLOAT_EQ(restored.elapsed, 0.0F);
    EXPECT_TRUE(restored.playing);
    EXPECT_FALSE(restored.completedThisFrame);
    EXPECT_FLOAT_EQ(restored.currentAlpha, 1.0F);

    std::filesystem::remove(path);
}

} // namespace
} // namespace UVE::UI::Tests
