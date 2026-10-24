#include "uve/input/android_motion_event_uve.h"
#include <android/input.h>

namespace UVE::Input {
bool RouteAndroidMotionEventUVE(AndroidTouchRouterUVE& router, const AInputEvent* event) {
    if (event == nullptr || AInputEvent_getType(event) != AINPUT_EVENT_TYPE_MOTION ||
        (AInputEvent_getSource(event) & AINPUT_SOURCE_TOUCHSCREEN) != AINPUT_SOURCE_TOUCHSCREEN) return false;
    const auto rawAction = AMotionEvent_getAction(event);
    const auto action = rawAction & AMOTION_EVENT_ACTION_MASK;
    if (action == AMOTION_EVENT_ACTION_CANCEL) { router.CancelUVE(); return true; }
    const auto count = AMotionEvent_getPointerCount(event);
    const auto index = static_cast<std::size_t>((rawAction & AMOTION_EVENT_ACTION_POINTER_INDEX_MASK)
                                               >> AMOTION_EVENT_ACTION_POINTER_INDEX_SHIFT);
    if (action == AMOTION_EVENT_ACTION_MOVE) {
        for (std::size_t i = 0U; i < count; ++i) {
            static_cast<void>(router.MoveUVE(AMotionEvent_getPointerId(event, i),
                {AMotionEvent_getX(event, i), AMotionEvent_getY(event, i)}, AMotionEvent_getPressure(event, i)));
        }
        return true;
    }
    if (index >= count) return false;
    const auto id = AMotionEvent_getPointerId(event, index);
    if (action == AMOTION_EVENT_ACTION_DOWN || action == AMOTION_EVENT_ACTION_POINTER_DOWN) {
        if (action == AMOTION_EVENT_ACTION_DOWN) router.CancelUVE();
        static_cast<void>(router.DownUVE(id, {AMotionEvent_getX(event, index), AMotionEvent_getY(event, index)},
                                       AMotionEvent_getPressure(event, index)));
        return true;
    }
    if (action == AMOTION_EVENT_ACTION_UP || action == AMOTION_EVENT_ACTION_POINTER_UP) {
        static_cast<void>(router.UpUVE(id));
        return true;
    }
    return false;
}
}
