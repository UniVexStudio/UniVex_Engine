#pragma once
#include "uve/input/android_touch_router_uve.h"
struct AInputEvent;
namespace UVE::Input {
// Called by the native event loop; returns whether a touch event was consumed.
// Coordinates are surface pixels. Pass the gesture service to clear cancelled gesture history.
// The host calls CancelAndroidInputUVE on focus/surface loss.
bool RouteAndroidMotionEventUVE(AndroidTouchRouterUVE& router, const AInputEvent* event,
                                IMobileGestureSystemUVE* gestures = nullptr);
}
