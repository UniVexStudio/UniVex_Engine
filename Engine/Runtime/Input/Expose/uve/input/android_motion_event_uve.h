#pragma once
#include "uve/input/android_touch_router_uve.h"
struct AInputEvent;
namespace UVE::Input {
// Called by the native event loop; returns whether a touch event was consumed.
// Coordinates are surface pixels. The host must CancelUVE on focus/surface loss.
bool RouteAndroidMotionEventUVE(AndroidTouchRouterUVE& router, const AInputEvent* event);
}
