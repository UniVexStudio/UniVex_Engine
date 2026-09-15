// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/nodes/3d/marker_3d_uve.h"

namespace UVE::Scene {

bool IsMarker3DNodeComponentValidUVE(const Marker3DNodeComponentUVE& value) noexcept {
    return IsBounded3DNodeStringUVE(value.markerName, false) && IsFinite3DNodeVectorUVE(value.localPosition) &&
           IsFinite3DNodeQuaternionUVE(value.localRotation);
}

} // namespace UVE::Scene
