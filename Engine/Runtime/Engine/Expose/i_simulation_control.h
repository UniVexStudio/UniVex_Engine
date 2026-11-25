//                                UniVex Engine
//
// UniVex Engine (UVE) — Proprietary Game Engine
// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
// Unauthorized copying, modification, distribution, or use of this code
// in whole or in part is strictly prohibited without express written
// permission from UniVex Studios.
// Violators will be prosecuted to the fullest extent of the law.


#pragma once

#include <string_view>

namespace UVE::Core {

/// Controls whether EngineCore consumes normal fixed simulation time, holds it, or consumes one
/// explicitly requested fixed step. Core owns the execution decision; editor clients only request it.
enum class SimulationExecutionMode {
    Running,
    Paused,
};

/// A narrow Core-owned control seam for transient editor simulation sessions. It deliberately does
/// not expose physics, timers, checkpoints, or renderer implementation details to editor code.
class ISimulationControl {
public:
    virtual ~ISimulationControl() = default;

    /// Sets fixed simulation execution. Paused mode retains frame maintenance and rendering while
    /// consuming no normal fixed physics steps.
    [[nodiscard]] virtual bool SetSimulationExecutionMode(SimulationExecutionMode mode) noexcept = 0;
    [[nodiscard]] virtual SimulationExecutionMode GetSimulationExecutionMode() const noexcept = 0;

    /// Queues exactly one fixed simulation step for the next engine update. Valid only while paused
    /// and while no prior request is pending.
    [[nodiscard]] virtual bool RequestSingleSimulationStep() noexcept = 0;

    /// Marks an editor-owned transient simulation session. Core suppresses checkpoint/save-game
    /// advancement while this flag is active, independently from fixed execution mode.
    [[nodiscard]] virtual bool SetTransientSimulationSessionActive(bool active) noexcept = 0;
    [[nodiscard]] virtual bool IsTransientSimulationSessionActive() const noexcept = 0;

    /// Updates the optional Editor Play scene-name title suffix. Empty clears the suffix and
    /// exits editor-play title mode. Hosts that do not own a native window may ignore this hint.
    [[nodiscard]] virtual bool SetEditorPlaySceneName(std::string_view sceneName) noexcept {
        static_cast<void>(sceneName);
        return false;
    }

    /// Sets or queries the simulation speed multiplier (1.0 = normal speed, >1.0 = fast-forward).
    [[nodiscard]] virtual bool SetSimulationSpeedScale(float speedScale) noexcept {
        static_cast<void>(speedScale);
        return false;
    }
    [[nodiscard]] virtual float GetSimulationSpeedScale() const noexcept {
        return 1.0F;
    }
};

} // namespace UVE::Core
