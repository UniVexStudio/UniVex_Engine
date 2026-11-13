// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "ui_view_3d_component.h"

#include <cmath>

namespace UVE::Scene {

bool IsUIView3DComponentValidUVE(const UIView3DComponentUVE& component) noexcept {
    return Math::IsFiniteUVE(component.rect) && component.rect.size.x >= kMinimumUIView3DSizePixelsUVE &&
           component.rect.size.x <= kMaximumUIView3DSizePixelsUVE && component.rect.size.y >= kMinimumUIView3DSizePixelsUVE &&
           component.rect.size.y <= kMaximumUIView3DSizePixelsUVE && std::isfinite(component.cameraDistance) &&
           component.cameraDistance > 0.0F && std::isfinite(component.lookAtHeight) && std::isfinite(component.pitchDegrees) &&
           std::isfinite(component.fieldOfViewDegrees) && component.fieldOfViewDegrees >= 1.0F &&
           component.fieldOfViewDegrees <= 170.0F && std::isfinite(component.yawDegrees) &&
           std::isfinite(component.spinDegreesPerSecond) && std::isfinite(component.backgroundColor.x) &&
           std::isfinite(component.backgroundColor.y) && std::isfinite(component.backgroundColor.z) &&
           std::isfinite(component.backgroundAlpha) && component.backgroundAlpha >= 0.0F && component.backgroundAlpha <= 1.0F;
}

Math::Vector3UVE UIView3DStagePositionUVE(const std::uint32_t slot) noexcept {
    return Math::Vector3UVE{static_cast<float>(slot % 16U) * 40.0F, -20000.0F, static_cast<float>(slot / 16U) * 40.0F};
}

} // namespace UVE::Scene
