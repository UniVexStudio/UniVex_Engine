// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/nodes/3d/reflection_probe_3d_uve.h"

#include <cmath>

namespace UVE::Scene {

bool IsReflectionProbe3DNodeComponentValidUVE(const ReflectionProbe3DNodeComponentUVE& value) noexcept {
    return IsFinite3DNodeVectorUVE(value.size) && value.size.x > 0.0F && value.size.y > 0.0F &&
           value.size.z > 0.0F && value.updateMode <= ReflectionProbeUpdateModeUVE::OnDemand;
}

float ResolveReflectionProbe3DInfluenceWeightUVE(
    const Math::Vector3UVE& probeLocalPoint,
    const Math::Vector3UVE& probeHalfExtents) noexcept {
    if (!std::isfinite(probeLocalPoint.x) || !std::isfinite(probeLocalPoint.y) ||
        !std::isfinite(probeLocalPoint.z) || !std::isfinite(probeHalfExtents.x) ||
        !std::isfinite(probeHalfExtents.y) || !std::isfinite(probeHalfExtents.z) ||
        probeHalfExtents.x <= 0.0F || probeHalfExtents.y <= 0.0F || probeHalfExtents.z <= 0.0F) {
        return 0.0F; // degenerate boxes influence nothing - fail-closed, never NaN
    }
    const float normalizedDistance = std::fmax(
        std::fmax(std::fabs(probeLocalPoint.x) / probeHalfExtents.x,
                  std::fabs(probeLocalPoint.y) / probeHalfExtents.y),
        std::fabs(probeLocalPoint.z) / probeHalfExtents.z);
    if (normalizedDistance >= 1.0F) {
        return 0.0F; // at or beyond the face the probe ends - the boundary is not inside
    }
    return 1.0F - normalizedDistance;
}

ReflectionProbe3DCaptureActionUVE ResolveReflectionProbe3DCaptureActionUVE(
    const ReflectionProbe3DCaptureFrameUVE& frame) noexcept {
    if (!frame.enabled || frame.updateMode > ReflectionProbeUpdateModeUVE::OnDemand ||
        (frame.hasCameraViewer && !std::isfinite(frame.cameraInfluenceWeight))) {
        return ReflectionProbe3DCaptureActionUVE::None;
    }
    switch (frame.updateMode) {
    case ReflectionProbeUpdateModeUVE::EveryFrame:
        if (frame.hasCameraViewer && frame.cameraInfluenceWeight > 0.0F) {
            return ReflectionProbe3DCaptureActionUVE::Capture;
        }
        break;
    case ReflectionProbeUpdateModeUVE::OnDemand:
        if (frame.updateRequested) {
            return ReflectionProbe3DCaptureActionUVE::Capture;
        }
        break;
    case ReflectionProbeUpdateModeUVE::Once:
        if (!frame.capturedOnce) {
            return ReflectionProbe3DCaptureActionUVE::Capture;
        }
        break;
    }
    return ReflectionProbe3DCaptureActionUVE::None;
}

} // namespace UVE::Scene
