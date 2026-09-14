// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/core/script_gameplay_bindings_uve.h"

#include <cmath>
#include <cstdint>

namespace UVE::Core {

namespace {

[[nodiscard]] bool TryTokenToKeyCodeUVE(const float token, Input::KeyCodeUVE& outKey) noexcept {
    if (!std::isfinite(token) || token < 0.0F) {
        return false;
    }
    const auto index = static_cast<std::uint32_t>(token);
    if (index >= static_cast<std::uint32_t>(Input::KeyCodeUVE::Count)) {
        return false;
    }
    outKey = static_cast<Input::KeyCodeUVE>(index);
    return true;
}

[[nodiscard]] bool TryTokenToMouseButtonUVE(const float token, Input::MouseButtonUVE& outButton) noexcept {
    if (!std::isfinite(token) || token < 0.0F) {
        return false;
    }
    const auto index = static_cast<std::uint32_t>(token);
    if (index >= static_cast<std::uint32_t>(Input::MouseButtonUVE::Count)) {
        return false;
    }
    outButton = static_cast<Input::MouseButtonUVE>(index);
    return true;
}

bool ScriptInputKeyPressedUVE(void* const userData, const float keyToken, bool* const outResult) noexcept {
    auto* const context = static_cast<ScriptGameplayBindingContextUVE*>(userData);
    Input::KeyCodeUVE key{};
    if (context == nullptr || context->inputSystem == nullptr || outResult == nullptr ||
        !TryTokenToKeyCodeUVE(keyToken, key)) {
        return false;
    }
    *outResult = context->inputSystem->WasKeyPressedThisFrameUVE(key);
    return true;
}

bool ScriptInputKeyReleasedUVE(void* const userData, const float keyToken, bool* const outResult) noexcept {
    auto* const context = static_cast<ScriptGameplayBindingContextUVE*>(userData);
    Input::KeyCodeUVE key{};
    if (context == nullptr || context->inputSystem == nullptr || outResult == nullptr ||
        !TryTokenToKeyCodeUVE(keyToken, key)) {
        return false;
    }
    *outResult = context->inputSystem->WasKeyReleasedThisFrameUVE(key);
    return true;
}

bool ScriptInputKeyDownUVE(void* const userData, const float keyToken, bool* const outResult) noexcept {
    auto* const context = static_cast<ScriptGameplayBindingContextUVE*>(userData);
    Input::KeyCodeUVE key{};
    if (context == nullptr || context->inputSystem == nullptr || outResult == nullptr ||
        !TryTokenToKeyCodeUVE(keyToken, key)) {
        return false;
    }
    *outResult = context->inputSystem->IsKeyDownUVE(key);
    return true;
}

bool ScriptInputMousePositionUVE(void* const userData,
                                  Scripting::ScriptVector2ValueUVE* const outPosition) noexcept {
    auto* const context = static_cast<ScriptGameplayBindingContextUVE*>(userData);
    if (context == nullptr || context->inputSystem == nullptr || outPosition == nullptr) {
        return false;
    }
    outPosition->value = context->inputSystem->GetMousePositionUVE();
    return true;
}

bool ScriptInputMouseButtonUVE(void* const userData, const float buttonToken, bool* const outResult) noexcept {
    auto* const context = static_cast<ScriptGameplayBindingContextUVE*>(userData);
    Input::MouseButtonUVE button{};
    if (context == nullptr || context->inputSystem == nullptr || outResult == nullptr ||
        !TryTokenToMouseButtonUVE(buttonToken, button)) {
        return false;
    }
    *outResult = context->inputSystem->IsMouseButtonDownUVE(button);
    return true;
}

} // namespace

Scripting::ScriptEngineCallBindingsUVE MakeScriptGameplayBindingsUVE(
    ScriptGameplayBindingContextUVE& context) noexcept {
    Scripting::ScriptEngineCallBindingsUVE bindings;
    bindings.userData = &context;
    bindings.inputKeyPressed = &ScriptInputKeyPressedUVE;
    bindings.inputKeyReleased = &ScriptInputKeyReleasedUVE;
    bindings.inputKeyDown = &ScriptInputKeyDownUVE;
    bindings.inputMousePosition = &ScriptInputMousePositionUVE;
    bindings.inputMouseButton = &ScriptInputMouseButtonUVE;
    return bindings;
}

} // namespace UVE::Core
