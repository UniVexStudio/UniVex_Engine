// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/objects/3d/object_3d_common_uve.h"

namespace UVE::Scene {

bool IsBounded3DObjectStringUVE(const std::string& value, const bool allowEmpty) noexcept {
    return (allowEmpty || !value.empty()) && value.size() <= kMaximum3DObjectStringLengthUVE &&
           value.find('\0') == std::string::npos;
}

bool IsFinite3DObjectVectorUVE(const Math::Vector3UVE& value) noexcept {
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

bool IsFinite3DObjectQuaternionUVE(const Math::QuaternionUVE& value) noexcept {
    return Math::IsFiniteUVE(value) && std::isfinite(Math::LengthSquaredUVE(value));
}

} // namespace UVE::Scene
