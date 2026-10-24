#include "uve/input/android_touch_router_uve.h"
#include "uve/input/mobile_input_system_uve.h"
#include <gtest/gtest.h>
using namespace UVE::Input;
TEST(AndroidTouchRouterUVETest, StableIdentitiesCancellationAndCapacity) {
    MobileInputSystemUVE input;
    AndroidTouchRouterUVE router(input);
    ASSERT_TRUE(router.DownUVE(7, {1,2}, 1));
    ASSERT_TRUE(router.DownUVE(2, {3,4}, 1));
    ASSERT_TRUE(!router.DownUVE(7, {}, 1));
    input.UpdateUVE();
    auto initial = input.GetSnapshotUVE();
    ASSERT_TRUE(router.UpUVE(7));
    ASSERT_TRUE(router.MoveUVE(2, {9,4}, 1));
    ASSERT_TRUE(router.DownUVE(7, {}, 1));
    input.UpdateUVE();
    auto next = input.GetSnapshotUVE();
    ASSERT_TRUE(next.touches[1].identifier == initial.touches[1].identifier);
    ASSERT_TRUE(next.touches[1].delta.x == 6);
    ASSERT_TRUE(next.touches[0].identifier != initial.touches[0].identifier);
    ASSERT_TRUE(next.touches[0].delta.x == 0);
    ASSERT_TRUE(!router.UpUVE(-1));
    router.CancelUVE();
    input.UpdateUVE();
    for (const auto& touch : input.GetSnapshotUVE().touches) ASSERT_TRUE(!touch.active);
    for (int i=0;i<10;++i) ASSERT_TRUE(router.DownUVE(i,{},1));
    ASSERT_TRUE(!router.DownUVE(20,{},1));
    router.CancelUVE();
    ASSERT_TRUE(router.DownUVE(20,{},1));
}
