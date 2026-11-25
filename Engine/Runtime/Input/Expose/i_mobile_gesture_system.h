// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
#pragma once

#include "mobile_gesture_recognizer.h"

namespace UVE::Input {

/// EngineServices boundary for bounded mobile gesture classification. The service borrows the
/// caller-owned mobile snapshot service, retains only the latest copied report and recognizer state,
/// and never owns platform callbacks, lifecycle, touch polling, or ECS state.
class IMobileGestureSystem {
public:
    virtual ~IMobileGestureSystem() = default;

    /// Consumes the mobile service's current copied snapshot once for the frame. Must run after
    /// IMobileInputSystem::Update() on the main thread.
    virtual void Update(float frameDeltaSeconds) = 0;
    [[nodiscard]] virtual MobileGestureReport GetLastReport() const noexcept = 0;
    virtual void Reset() noexcept = 0;
};

} // namespace UVE::Input
