// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
#pragma once

#include "i_mobile_gesture_system.h"

namespace UVE::Input {

class MobileGestureSystem final : public IMobileGestureSystem {
public:
    explicit MobileGestureSystem(IMobileInputSystem& mobileInput,
                                    MobileGestureRecognizerConfig config = {}) noexcept;

    void Update(float frameDeltaSeconds) override;
    [[nodiscard]] MobileGestureReport GetLastReport() const noexcept override;
    void Reset() noexcept override;

private:
    IMobileInputSystem& m_mobileInput;
    MobileGestureRecognizer m_recognizer;
    MobileGestureReport m_lastReport{};
};

} // namespace UVE::Input
