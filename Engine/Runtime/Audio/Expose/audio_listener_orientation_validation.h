// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
#pragma once

#include "vector3.h"

namespace UVE::Audio {

/// Validates the copied listener basis inputs used by AudioSystem. Both vectors must be finite
/// and have a finite, strictly positive squared length. This does not normalize the basis, enforce
/// orthogonality, or select an audio backend.
[[nodiscard]] bool IsAudioListenerOrientationValid(Math::Vector3 forward,
                                                       Math::Vector3 up) noexcept;

} // namespace UVE::Audio
