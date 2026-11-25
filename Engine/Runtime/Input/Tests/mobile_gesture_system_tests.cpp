// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
#include "mobile_gesture_system.h"
#include "mobile_input_system.h"

#include <gtest/gtest.h>

namespace UVE::Input::Tests {

TEST(MobileGestureSystemTest, ConsumesMobileSnapshotsAndPublishesTapOnRelease) {
    MobileInputSystem mobileInput;
    MobileGestureSystem gestureSystem(mobileInput);

    mobileInput.SetTouchState(0U, true, 7U, Math::Vector2{10.0F, 20.0F}, 0.5F);
    mobileInput.Update();
    gestureSystem.Update(0.016F);
    EXPECT_EQ(gestureSystem.GetLastReport().count, 0U);

    mobileInput.SetTouchState(0U, false, 0U, Math::Vector2{}, 0.0F);
    mobileInput.Update();
    gestureSystem.Update(0.016F);

    const MobileGestureReport report = gestureSystem.GetLastReport();
    ASSERT_EQ(report.count, 1U);
    EXPECT_EQ(report.events[0].type, MobileGestureType::Tap);
    EXPECT_EQ(report.events[0].touchIdentifier, 7U);
    EXPECT_EQ(report.events[0].startPosition, (Math::Vector2{10.0F, 20.0F}));
}

TEST(MobileGestureSystemTest, ResetClearsLatestReportAndRecognizerState) {
    MobileInputSystem mobileInput;
    MobileGestureSystem gestureSystem(mobileInput);

    mobileInput.SetTouchState(0U, true, 9U, Math::Vector2{1.0F, 2.0F}, 0.5F);
    mobileInput.Update();
    gestureSystem.Update(0.016F);
    gestureSystem.Reset();

    EXPECT_EQ(gestureSystem.GetLastReport(), MobileGestureReport{});
}

} // namespace UVE::Input::Tests
