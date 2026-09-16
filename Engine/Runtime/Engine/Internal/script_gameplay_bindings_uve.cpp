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

bool ScriptPhysicsCollisionTransitionUVE(void* const userData, const Scene::EntityUVE body,
                                          Scene::EntityUVE* const outOther, bool* const outResult,
                                          const Physics::CollisionTransitionKindUVE kind) noexcept {
    auto* const context = static_cast<ScriptGameplayBindingContextUVE*>(userData);
    if (context == nullptr || context->collisionTransitionsThisTick == nullptr || outOther == nullptr ||
        outResult == nullptr || body == Scene::kInvalidEntityUVE) {
        return false;
    }
    *outResult = false;
    *outOther = Scene::kInvalidEntityUVE;
    for (const Physics::CollisionTransitionUVE& transition : *context->collisionTransitionsThisTick) {
        if (transition.kind != kind) {
            continue;
        }
        if (transition.pair.first == body) {
            *outResult = true;
            *outOther = transition.pair.second;
            break;
        }
        if (transition.pair.second == body) {
            *outResult = true;
            *outOther = transition.pair.first;
            break;
        }
    }
    return true;
}

bool ScriptPhysicsCollisionEnterUVE(void* const userData, const Scene::EntityUVE body,
                                     Scene::EntityUVE* const outOther, bool* const outResult) noexcept {
    return ScriptPhysicsCollisionTransitionUVE(userData, body, outOther, outResult,
                                                Physics::CollisionTransitionKindUVE::Entered);
}

bool ScriptPhysicsCollisionExitUVE(void* const userData, const Scene::EntityUVE body,
                                    Scene::EntityUVE* const outOther, bool* const outResult) noexcept {
    return ScriptPhysicsCollisionTransitionUVE(userData, body, outOther, outResult,
                                                Physics::CollisionTransitionKindUVE::Exited);
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
    bindings.physicsCollisionEnter = &ScriptPhysicsCollisionEnterUVE;
    bindings.physicsCollisionExit = &ScriptPhysicsCollisionExitUVE;
    return bindings;
}

} // namespace UVE::Core
