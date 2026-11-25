// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
#include "mobile_gesture_system.h"

namespace UVE::Input {

MobileGestureSystem::MobileGestureSystem(IMobileInputSystem& mobileInput,
                                               const MobileGestureRecognizerConfig config) noexcept
    : m_mobileInput(mobileInput), m_recognizer(config) {}

void MobileGestureSystem::Update(const float frameDeltaSeconds) {
    m_lastReport = m_recognizer.ConsumeSnapshot(m_mobileInput.GetSnapshot(), frameDeltaSeconds);
}

MobileGestureReport MobileGestureSystem::GetLastReport() const noexcept {
    return m_lastReport;
}

void MobileGestureSystem::Reset() noexcept {
    m_recognizer.Reset();
    m_lastReport = MobileGestureReport{};
}

} // namespace UVE::Input
