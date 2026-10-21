// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#pragma once

#include <cmath>
#include <string>

#include "uve/math/vector3_uve.h"

namespace UVE::Audio {

/// Describes a voice to create via IAudioDeviceUVE::CreateVoiceUVE(). `audioAssetPath` is an
/// identity only; caller-owned decoded PCM and bounded stream/effect preparation are supplied
/// separately through IAudioSystemUVE and are never stored in this device descriptor. A concrete
/// backend may resolve the identity to real audio data when it owns that capability.
struct AudioVoiceDescUVE {
    std::string audioAssetPath;
    bool looping = false;
};

/// Per-voice runtime parameters pushed via IAudioDeviceUVE::SetVoiceParamsUVE(). `gain` is a
/// final, already-attenuated linear multiplier in [0, 1] — AudioSystemUVE, not the device,
/// computes it; the device only ever receives the finished number. `pan` is likewise final:
/// -1 is hard left, 0 is centre, +1 is hard right, and only the system decides whether a source is
/// positional at all - a 2D source always arrives centred.
struct AudioVoiceParamsUVE {
    Math::Vector3UVE position{};
    float gain = 1.0F;
    float pitch = 1.0F;
    float pan = 0.0F;
};

/// Validates final device-facing voice parameters without performing device I/O or voice mutation.
[[nodiscard]] inline bool ValidateAudioVoiceParamsUVE(const AudioVoiceParamsUVE& params) noexcept {
    return std::isfinite(params.position.x) && std::isfinite(params.position.y) &&
           std::isfinite(params.position.z) && std::isfinite(params.gain) && params.gain >= 0.0F &&
           params.gain <= 1.0F && std::isfinite(params.pitch) && params.pitch >= 0.0F &&
           std::isfinite(params.pan) && params.pan >= -1.0F && params.pan <= 1.0F;
}

} // namespace UVE::Audio
