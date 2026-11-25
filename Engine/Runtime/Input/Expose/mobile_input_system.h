// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cstddef>
#include <cstdint>
#include <mutex>

#include "i_mobile_input_system.h"

namespace UVE::Input {

class MobileInputSystem final : public IMobileInputSystem {
public:
    void SetTouchState(std::size_t touchSlot, bool active, std::uint64_t identifier,
                          Math::Vector2 position, float pressure) override;
    void SetGyroscopeRotationRate(Math::Vector3 rotationRate) override;
    void Update() override;

    [[nodiscard]] MobileInputSnapshot GetSnapshot() const override;
    [[nodiscard]] MobileInputSnapshot GetPreviousSnapshot() const override;

private:
    [[nodiscard]] static Math::Vector2 SanitizePosition(Math::Vector2 position) noexcept;
    [[nodiscard]] static Math::Vector3 SanitizeRotationRate(Math::Vector3 rotationRate) noexcept;
    [[nodiscard]] static float SanitizePressure(float pressure) noexcept;

    mutable std::mutex m_liveStateMutex;
    MobileInputSnapshot m_liveState{};
    MobileInputSnapshot m_currentState{};
    MobileInputSnapshot m_previousState{};
};

} // namespace UVE::Input
