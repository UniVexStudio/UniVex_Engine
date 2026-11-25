// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "mobile_input_system.h"
#include "input_frame_counter.h"

#include <algorithm>
#include <cmath>

namespace UVE::Input {

Math::Vector2 MobileInputSystem::SanitizePosition(const Math::Vector2 position) noexcept {
    return Math::Vector2{std::isfinite(position.x) ? position.x : 0.0F,
                            std::isfinite(position.y) ? position.y : 0.0F};
}

Math::Vector3 MobileInputSystem::SanitizeRotationRate(
    const Math::Vector3 rotationRate) noexcept {
    return Math::Vector3{std::isfinite(rotationRate.x) ? rotationRate.x : 0.0F,
                            std::isfinite(rotationRate.y) ? rotationRate.y : 0.0F,
                            std::isfinite(rotationRate.z) ? rotationRate.z : 0.0F};
}

float MobileInputSystem::SanitizePressure(const float pressure) noexcept {
    return std::isfinite(pressure) ? std::clamp(pressure, 0.0F, 1.0F) : 0.0F;
}

void MobileInputSystem::SetTouchState(const std::size_t touchSlot, const bool active,
                                            const std::uint64_t identifier,
                                            const Math::Vector2 position, const float pressure) {
    if (touchSlot >= kMaximumTouchCount || (active && identifier == 0U)) {
        return;
    }
    const std::lock_guard<std::mutex> lock(m_liveStateMutex);
    if (active) {
        for (std::size_t otherSlot = 0U; otherSlot < kMaximumTouchCount; ++otherSlot) {
            if (otherSlot != touchSlot && m_liveState.touches[otherSlot].active &&
                m_liveState.touches[otherSlot].identifier == identifier) {
                return;
            }
        }
    }
    TouchPointState& touch = m_liveState.touches[touchSlot];
    touch.active = active;
    touch.identifier = active ? identifier : 0U;
    touch.position = active ? SanitizePosition(position) : Math::Vector2{};
    touch.delta = {};
    touch.pressure = active ? SanitizePressure(pressure) : 0.0F;
}

void MobileInputSystem::SetGyroscopeRotationRate(const Math::Vector3 rotationRate) {
    const std::lock_guard<std::mutex> lock(m_liveStateMutex);
    m_liveState.gyroscopeRotationRate = SanitizeRotationRate(rotationRate);
}

void MobileInputSystem::Update() {
    const std::lock_guard<std::mutex> lock(m_liveStateMutex);
    m_previousState = m_currentState;
    AdvanceInputFrameNumber(m_liveState.frameNumber);
    m_currentState = m_liveState;

    for (std::size_t touchSlot = 0U; touchSlot < kMaximumTouchCount; ++touchSlot) {
        TouchPointState& currentTouch = m_currentState.touches[touchSlot];
        const TouchPointState& previousTouch = m_previousState.touches[touchSlot];
        if (currentTouch.active && previousTouch.active &&
            currentTouch.identifier == previousTouch.identifier) {
            const Math::Vector2 delta = currentTouch.position - previousTouch.position;
            currentTouch.delta = std::isfinite(delta.x) && std::isfinite(delta.y)
                ? delta
                : Math::Vector2{};
        } else {
            currentTouch.delta = {};
        }
    }
}

MobileInputSnapshot MobileInputSystem::GetSnapshot() const {
    const std::lock_guard<std::mutex> lock(m_liveStateMutex);
    return m_currentState;
}

MobileInputSnapshot MobileInputSystem::GetPreviousSnapshot() const {
    const std::lock_guard<std::mutex> lock(m_liveStateMutex);
    return m_previousState;
}

} // namespace UVE::Input
