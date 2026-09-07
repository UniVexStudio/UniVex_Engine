# Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#
# Deliberately partial port of the reference engine's Input module: IInputSystemUVE plus its
# concrete, engine-standard implementation InputSystemUVE (desktop keyboard/mouse + a named
# action-mapping layer), and GamepadInputSystemUVE (bounded gamepad device snapshots, since
# InputSystemUVE's action layer can bind to one and its own test suite exercises that path) - what
# Engine/Runtime/Window's WindowManagerUVE needs to feed real device state into during
# PollEventsUVE(), and what a real consumer (e.g. an interactive editor) needs to actually query
# input with, not just hold an interface pointer to. The mobile touch/gesture system
# (MobileInputSystemUVE, MobileGestureRecognizerUVE, ~730 lines) is NOT ported - nothing in this
# repo targets mobile yet - so this stays honestly scoped to desktop keyboard/mouse/gamepad, not
# the full ~1,900-line reference module.

add_library(uve_input STATIC
    src/input_system_uve.cpp
    src/gamepad_input_system_uve.cpp
)
target_include_directories(uve_input PUBLIC include)
target_link_libraries(uve_input PUBLIC uve_math uve_events)
target_compile_features(uve_input PUBLIC cxx_std_20)
uve_set_warnings(uve_input)
