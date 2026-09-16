// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/nodes/3d/reflection_probe_3d_uve.h"

namespace UVE::Scene {

bool IsReflectionProbe3DNodeComponentValidUVE(const ReflectionProbe3DNodeComponentUVE& value) noexcept {
    return IsFinite3DNodeVectorUVE(value.size) && value.size.x > 0.0F && value.size.y > 0.0F && value.size.z > 0.0F &&
           value.updateMode <= ReflectionProbeUpdateModeUVE::OnDemand;
}

} // namespace UVE::Scene
