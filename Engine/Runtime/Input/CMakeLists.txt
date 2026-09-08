# Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#
# Started as a deliberately partial port (desktop keyboard/mouse/gamepad only - IInputSystemUVE,
# InputSystemUVE, GamepadInputSystemUVE) since nothing in this repo targeted mobile yet. Grown
# here to add the mobile touch/gesture system (MobileInputSystemUVE, IMobileInputSystemUVE,
# MobileGestureSystemUVE, IMobileGestureSystemUVE, MobileGestureRecognizerUVE,
# TouchCoordinateTransformUVE) after finding EngineCoreUVE constructs the concrete
# MobileInputSystemUVE/MobileGestureSystemUVE unconditionally (not behind an Android-only
# guard) - it wants gesture-driven input bindings available uniformly across platforms, not
# just on Android.
#
# This now covers every file in the reference engine/input/ module - the full ~1,900-line
# system is ported.

add_library(uve_input STATIC
    src/input_system_uve.cpp
    src/gamepad_input_system_uve.cpp
    src/mobile_input_system_uve.cpp
    src/mobile_gesture_system_uve.cpp
    src/mobile_gesture_recognizer_uve.cpp
    src/touch_coordinate_transform_uve.cpp
)
target_include_directories(uve_input PUBLIC include)
target_link_libraries(uve_input PUBLIC uve_math uve_events)
target_compile_features(uve_input PUBLIC cxx_std_20)
uve_set_warnings(uve_input)
