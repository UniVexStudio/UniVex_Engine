//                                      UVE
//                                UniVex Engine
//
// UniVex Engine (UVE) — Proprietary Game Engine
// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
// Unauthorized copying, modification, distribution, or use of this code
// in whole or in part is strictly prohibited without express written
// permission from UniVex Studios.
// Violators will be prosecuted to the fullest extent of the law.


#pragma once

#include <cstdint>
#include <string_view>

namespace UVE::Core {

/// The engine's formal lifecycle state. EngineCore always transitions
/// through these states strictly in order, one step at a time — see
/// IsValidTransition().
enum class EngineState : std::uint8_t {
    Uninitialized = 0,
    Initializing = 1,
    Running = 2,
    ShuttingDown = 3,
    Shutdown = 4,
};

/// Returns the display name of an EngineState value.
[[nodiscard]] constexpr std::string_view ToString(EngineState state) noexcept {
    switch (state) {
        case EngineState::Uninitialized:
            return "Uninitialized";
        case EngineState::Initializing:
            return "Initializing";
        case EngineState::Running:
            return "Running";
        case EngineState::ShuttingDown:
            return "ShuttingDown";
        case EngineState::Shutdown:
            return "Shutdown";
    }
    return "Unknown";
}

/// Returns true if moving from `from` directly to `to` is a valid single
/// step in the engine lifecycle: Uninitialized -> Initializing -> Running
/// -> ShuttingDown -> Shutdown, strictly in that order. No state is ever
/// revisited and no step may be skipped.
[[nodiscard]] constexpr bool IsValidTransition(EngineState from, EngineState to) noexcept {
    switch (from) {
        case EngineState::Uninitialized:
            return to == EngineState::Initializing;
        case EngineState::Initializing:
            return to == EngineState::Running;
        case EngineState::Running:
            return to == EngineState::ShuttingDown;
        case EngineState::ShuttingDown:
            return to == EngineState::Shutdown;
        case EngineState::Shutdown:
            return false;
    }
    return false;
}

} // namespace UVE::Core
