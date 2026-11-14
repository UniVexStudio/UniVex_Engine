// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
#pragma once

#include <cstdint>
#include <functional>
#include <span>
#include <string_view>

namespace UVE::Editor {

enum class AndroidTouchActionUVE : std::uint8_t {
    Down = 0,
    Move = 1,
    Up = 2,
    Cancel = 3,
    PointerDown = 4,
    PointerUp = 5,
    HoverMove = 6,
    Scroll = 7,
};

struct AndroidTouchContactUVE final {
    std::int32_t pointerId = -1;
    float x = 0.0F;
    float y = 0.0F;
};

struct AndroidEditorCallbacksUVE final {
    std::function<void(bool active)> setTextInputActive;
    std::function<void()> requestQuit;
};

void SetAndroidEditorCallbacksUVE(AndroidEditorCallbacksUVE callbacks);
void NotifyAndroidWindowAttachedUVE(void* nativeWindow, int width, int height);
void NotifyAndroidWindowDetachedUVE();
void NotifyAndroidWindowResizedUVE(int width, int height);

[[nodiscard]] bool DispatchAndroidTouchEventUVE(AndroidTouchActionUVE action,
                                                std::int32_t actionIndex,
                                                std::span<const AndroidTouchContactUVE> contacts,
                                                std::int32_t buttonState = 0,
                                                float scrollX = 0.0F,
                                                float scrollY = 0.0F);

[[nodiscard]] bool DispatchAndroidKeyEventUVE(std::int32_t keyCode,
                                              std::int32_t action,
                                              std::int32_t metaState,
                                              std::uint32_t unicodeChar,
                                              bool repeat);

void DispatchAndroidTextInputUVE(std::string_view utf8Text);
void CancelAndroidEditorInputUVE();

} // namespace UVE::Editor
