#pragma once
#include "android_touch_router.h"
struct AInputEvent;
namespace UVE::Input {
// Called by the native event loop; returns whether a touch event was consumed.
// Coordinates are surface pixels. Pass the gesture service to clear cancelled gesture history.
// The host calls CancelAndroidInput on focus/surface loss.
bool RouteAndroidMotionEvent(AndroidTouchRouter& router, const AInputEvent* event,
                                IMobileGestureSystem* gestures = nullptr);
}
