// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/nodes/3d/node_3d_common_uve.h"

namespace UVE::Scene {

bool IsBounded3DNodeStringUVE(const std::string& value, const bool allowEmpty) noexcept {
    return (allowEmpty || !value.empty()) && value.size() <= kMaximum3DNodeStringLengthUVE &&
           value.find('\0') == std::string::npos;
}

bool IsFinite3DNodeVectorUVE(const Math::Vector3UVE& value) noexcept {
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

bool IsFinite3DNodeQuaternionUVE(const Math::QuaternionUVE& value) noexcept {
    return Math::IsFiniteUVE(value) && std::isfinite(Math::LengthSquaredUVE(value));
}

} // namespace UVE::Scene
