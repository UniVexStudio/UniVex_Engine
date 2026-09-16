// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cmath>
#include <cstddef>
#include <string>

#include "uve/math/quaternion_uve.h"
#include "uve/math/vector3_uve.h"

namespace UVE::Scene {

inline constexpr std::size_t kMaximum3DNodeStringLengthUVE = 256U;

[[nodiscard]] bool IsBounded3DNodeStringUVE(const std::string& value, bool allowEmpty = true) noexcept;
[[nodiscard]] bool IsFinite3DNodeVectorUVE(const Math::Vector3UVE& value) noexcept;
[[nodiscard]] bool IsFinite3DNodeQuaternionUVE(const Math::QuaternionUVE& value) noexcept;

} // namespace UVE::Scene
