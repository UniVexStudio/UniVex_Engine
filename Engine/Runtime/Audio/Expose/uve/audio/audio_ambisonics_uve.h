// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <array>
#include <cmath>
#include <cstddef>

#include "uve/audio/audio_spatialization_uve.h"
#include "uve/math/vector3_uve.h"

namespace UVE::Audio {

/// First-order ambisonics in the AmbiX convention: four interleaved channels in ACN order
/// (W, Y, Z, X) with SN3D normalisation. AmbiX is the interchange layout the 360/VR ecosystem
/// standardised on (the AmbiX specification from the 2011 Ambisonic Symposium, adopted by YouTube
/// 360 and Facebook 360), so a bed authored anywhere in that ecosystem drops in without a
/// conversion step. The two conventions that trip people up are both fixed here:
///
///  * ACN, not the older FuMa order. Channel 0 is the omnidirectional W, then Y, Z, X.
///  * SN3D, not FuMa's maxN. A plane wave from direction d produces exactly (1, d.y, d.z, d.x),
///    which is what makes the rotation below a plain 3x3 matrix over (Y, Z, X).
///
/// A FuMa file converted at import is a format conversion, not a runtime branch: nothing here
/// accepts a second layout, so a mis-ordered file is refused rather than quietly mis-localised.
inline constexpr std::size_t kFirstOrderAmbisonicChannelCountUVE = 4U;
inline constexpr std::size_t kAmbisonicWChannelIndexUVE = 0U;
inline constexpr std::size_t kAmbisonicYChannelIndexUVE = 1U;
inline constexpr std::size_t kAmbisonicZChannelIndexUVE = 2U;
inline constexpr std::size_t kAmbisonicXChannelIndexUVE = 3U;

/// A basis shorter than this squared is not a direction any more; see ComputeAmbisonicRotationUVE.
inline constexpr float kMinimumAmbisonicBasisLengthSquaredUVE = 1.0e-12F;

/// Engine direction (+X right, +Y up, -Z forward) to AmbiX (+X front, +Y left, +Z up). This is a
/// proper rotation - a signed axis permutation with determinant +1 - so it maps cross products
/// correctly, which is why the listener basis can be converted one vector at a time.
[[nodiscard]] inline Math::Vector3UVE ToAmbisonicDirectionUVE(const Math::Vector3UVE& engineDirection) noexcept {
    return Math::Vector3UVE{-engineDirection.z, -engineDirection.x, engineDirection.y};
}

/// The world-to-listener rotation of a first-order sound field.
///
/// A first-order field is fully described by W plus a direction vector (Y, Z, X). Turning the
/// listener is therefore a rotation of that vector, and because the engine's AmbiX axes are
/// right-handed this is an orthonormal 3x3 matrix with determinant +1 - energy and the
/// omnidirectional channel are exactly preserved, not approximately.
///
/// Layout: `matrix` is row-major and BOTH axes are in ACN order, rows (Y', Z', X') against columns
/// (Y, Z, X). That choice is what keeps the identity rotation equal to the identity matrix - the
/// one property a reader will assume before reading any documentation. The rows themselves are the
/// listener's LEFT, UP and FORWARD axes expressed in world AmbiX coordinates, permuted from
/// (front, left, up) into (left, up, front) to match the column order.
struct AmbisonicRotationUVE final {
    std::array<float, 9U> matrix{1.0F, 0.0F, 0.0F, 0.0F, 1.0F, 0.0F, 0.0F, 0.0F, 1.0F};

    [[nodiscard]] bool operator==(const AmbisonicRotationUVE&) const noexcept = default;
};

/// Whether a listener basis can define a unique rotation at all: both axes finite and non-zero, and
/// forward not parallel to up. This is IsAudioListenerOrientationValidUVE()'s rule plus the one it
/// deliberately leaves out - a parallel pair has no unique roll, and IsAudioListenerOrientationValidUVE()
/// only promises a usable basis, not a roll-free one.
[[nodiscard]] inline bool IsAmbisonicListenerBasisValidUVE(const Math::Vector3UVE& listenerForward,
                                                           const Math::Vector3UVE& listenerUp) noexcept {
    const bool finite = std::isfinite(listenerForward.x) && std::isfinite(listenerForward.y) &&
                        std::isfinite(listenerForward.z) && std::isfinite(listenerUp.x) &&
                        std::isfinite(listenerUp.y) && std::isfinite(listenerUp.z);
    if (!finite) return false;
    const float forwardLengthSquared = Math::LengthSquaredUVE(listenerForward);
    const float upLengthSquared = Math::LengthSquaredUVE(listenerUp);
    if (!(forwardLengthSquared > kMinimumAmbisonicBasisLengthSquaredUVE) ||
        !(upLengthSquared > kMinimumAmbisonicBasisLengthSquaredUVE)) {
        return false;
    }
    const float crossLengthSquared =
        Math::LengthSquaredUVE(Math::CrossUVE(listenerForward * (1.0F / std::sqrt(forwardLengthSquared)), listenerUp));
    return crossLengthSquared > kMinimumAmbisonicBasisLengthSquaredUVE;
}

/// Rotation that renders a world-locked field on a listener whose head points along `forward` with
/// `up` as its up axis - the same two vectors IAudioSystemUVE::SetListenerOrientationUVE() takes,
/// so a camera or an HMD can drive it without an Euler-angle detour.
///
/// Degenerate input is not a rotation at all. Rather than synthesize a random orientation, this
/// returns the identity: the field plays unrotated, which is the one answer that is always safe.
/// Callers that want to refuse the update outright ask IsAmbisonicListenerBasisValidUVE() first;
/// the adapter in ambisonic_stream_source_uve.h does exactly that.
[[nodiscard]] inline AmbisonicRotationUVE ComputeAmbisonicRotationUVE(const Math::Vector3UVE& listenerForward,
                                                                     const Math::Vector3UVE& listenerUp) noexcept {
    if (!IsAmbisonicListenerBasisValidUVE(listenerForward, listenerUp)) return AmbisonicRotationUVE{};

    const float forwardLengthSquared = Math::LengthSquaredUVE(listenerForward);
    const Math::Vector3UVE forward = listenerForward * (1.0F / std::sqrt(forwardLengthSquared));
    const Math::Vector3UVE right = Math::CrossUVE(forward, listenerUp);
    const Math::Vector3UVE rightNormalised = right * (1.0F / std::sqrt(Math::LengthSquaredUVE(right)));
    // Re-derived rather than reused: an up vector that is not perpendicular to forward must not
    // shear the matrix, or the "rotation" would stop preserving energy.
    const Math::Vector3UVE up = Math::CrossUVE(rightNormalised, forward);
    const Math::Vector3UVE left = rightNormalised * -1.0F;

    const Math::Vector3UVE ambisonicLeft = ToAmbisonicDirectionUVE(left);
    const Math::Vector3UVE ambisonicUp = ToAmbisonicDirectionUVE(up);
    const Math::Vector3UVE ambisonicForward = ToAmbisonicDirectionUVE(forward);

    // (x, y, z) of an AmbiX axis is (front, left, up); the coefficients it multiplies are ordered
    // (Y, Z, X), so each row is written (y, z, x) - see the struct comment.
    AmbisonicRotationUVE rotation;
    rotation.matrix = {ambisonicLeft.y,    ambisonicLeft.z,    ambisonicLeft.x,
                       ambisonicUp.y,      ambisonicUp.z,      ambisonicUp.x,
                       ambisonicForward.y, ambisonicForward.z, ambisonicForward.x};
    return rotation;
}

/// Turns one world-space AmbiX frame into the listener's frame. In-place is safe: every input is
/// read before anything is written.
inline void RotateFirstOrderAmbisonicFrameUVE(const AmbisonicRotationUVE& rotation, const float* worldFrame,
                                              float* listenerFrame) noexcept {
    if (worldFrame == nullptr || listenerFrame == nullptr) return;
    // The directional coefficients are a vector in ACN order - (Y, Z, X) - and the matrix rows use
    // the same order, so this is a straight 3x3 multiply with no index shuffling.
    const Math::Vector3UVE worldDirection{worldFrame[kAmbisonicYChannelIndexUVE],
                                          worldFrame[kAmbisonicZChannelIndexUVE],
                                          worldFrame[kAmbisonicXChannelIndexUVE]};
    const float* matrix = rotation.matrix.data();
    const Math::Vector3UVE listenerDirection{
        matrix[0] * worldDirection.x + matrix[1] * worldDirection.y + matrix[2] * worldDirection.z,
        matrix[3] * worldDirection.x + matrix[4] * worldDirection.y + matrix[5] * worldDirection.z,
        matrix[6] * worldDirection.x + matrix[7] * worldDirection.y + matrix[8] * worldDirection.z};

    listenerFrame[kAmbisonicWChannelIndexUVE] = worldFrame[kAmbisonicWChannelIndexUVE];
    listenerFrame[kAmbisonicYChannelIndexUVE] = listenerDirection.x;
    listenerFrame[kAmbisonicZChannelIndexUVE] = listenerDirection.y;
    listenerFrame[kAmbisonicXChannelIndexUVE] = listenerDirection.z;
}

/// Decodes one listener-space AmbiX frame to the two-channel output the device mixes.
///
/// The decode is the classic coincident-cardioid pair: two virtual microphones pointing hard left
/// and hard right, which is the stereo decode every first-order system has used since the 1970s
/// (Malham & Myatt's stereo and binaural decodes both start from it). It is normalised so that a
/// pure omnidirectional field (W = 1, everything else zero) is EXACTLY unity on both channels -
/// the same "centre is unchanged" rule ComputeAudioPanGainsUVE() follows, so a bed crossfading
/// against a mono voice does not jump in level.
///
/// What that pair can and cannot say, stated plainly rather than discovered later:
///  * Left/right is exact, and a wave from hard left reaches 2.0 on the left channel and 0 on the
///    right - the +6 dB a cardioid pair produces at the sides, which the caller's bed gain is
///    there to absorb.
///  * Front and back are IDENTICAL: two microphones in a line cannot resolve the cone of confusion.
///    X still rotates into Y for content with height, so head roll and pitch do change what is
///    heard - but a front/back test would be a test of the physics, not of the code.
///  * Z and X are carried through the rotation and dropped here. An elevation-aware or binaural
///    decoder replaces this function, not the format.
///
/// `gain` is the caller's final bed gain, already folded in; non-finite input yields silence rather
/// than a poison frame.
[[nodiscard]] inline AudioPanGainsUVE DecodeFirstOrderAmbisonicFrameToStereoUVE(const float* listenerFrame,
                                                                                const float gain = 1.0F) noexcept {
    if (listenerFrame == nullptr) return AudioPanGainsUVE{0.0F, 0.0F};
    const float w = listenerFrame[kAmbisonicWChannelIndexUVE];
    const float y = listenerFrame[kAmbisonicYChannelIndexUVE];
    if (!std::isfinite(w) || !std::isfinite(y) || !std::isfinite(gain) || gain <= 0.0F) {
        return AudioPanGainsUVE{0.0F, 0.0F};
    }
    return AudioPanGainsUVE{gain * (w + y), gain * (w - y)};
}

} // namespace UVE::Audio
