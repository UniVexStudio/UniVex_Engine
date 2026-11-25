#include "android_touch_router.h"
#include "mobile_input_system.h"
#include "mobile_gesture_system.h"
#include <gtest/gtest.h>
using namespace UVE::Input;
TEST(AndroidTouchRouterTest, StableIdentitiesCancellationAndCapacity) {
    MobileInputSystem input;
    AndroidTouchRouter router(input);
    ASSERT_TRUE(router.Down(7, {1,2}, 1));
    ASSERT_TRUE(router.Down(2, {3,4}, 1));
    ASSERT_TRUE(!router.Down(7, {}, 1));
    input.Update();
    auto initial = input.GetSnapshot();
    ASSERT_TRUE(router.Up(7));
    ASSERT_TRUE(router.Move(2, {9,4}, 1));
    ASSERT_TRUE(router.Down(7, {}, 1));
    input.Update();
    auto next = input.GetSnapshot();
    ASSERT_TRUE(next.touches[1].identifier == initial.touches[1].identifier);
    ASSERT_TRUE(next.touches[1].delta.x == 6);
    ASSERT_TRUE(next.touches[0].identifier != initial.touches[0].identifier);
    ASSERT_TRUE(next.touches[0].delta.x == 0);
    ASSERT_TRUE(!router.Up(-1));
    router.Cancel();
    input.Update();
    for (const auto& touch : input.GetSnapshot().touches) ASSERT_TRUE(!touch.active);
    for (int i=0;i<10;++i) ASSERT_TRUE(router.Down(i,{},1));
    ASSERT_TRUE(!router.Down(20,{},1));
    router.Cancel();
    ASSERT_TRUE(router.Down(20,{},1));
}

TEST(AndroidTouchRouterTest, CancellationDropsGestureHistoryAndAllowsTheNextTap) {
    MobileInputSystem input;
    AndroidTouchRouter router(input);
    MobileGestureSystem gestures(input);
    ASSERT_TRUE(router.Down(0, {10.0F, 20.0F}, 1.0F));
    input.Update(); gestures.Update(0.016F);
    CancelAndroidInput(router, gestures);
    input.Update(); gestures.Update(0.016F);
    EXPECT_EQ(gestures.GetLastReport().count, 0U);
    ASSERT_TRUE(router.Down(0, {10.0F, 20.0F}, 1.0F));
    input.Update(); gestures.Update(0.016F);
    ASSERT_TRUE(router.Up(0));
    input.Update(); gestures.Update(0.016F);
    ASSERT_EQ(gestures.GetLastReport().count, 1U);
    EXPECT_EQ(gestures.GetLastReport().events[0].type, MobileGestureType::Tap);
}
