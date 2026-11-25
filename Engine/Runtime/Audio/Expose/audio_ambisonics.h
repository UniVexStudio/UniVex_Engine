// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <array>
#include <cmath>
#include <cstddef>

#include "audio_spatialization.h"
#include "vector3.h"

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
inline constexpr std::size_t kFirstOrderAmbisonicChannelCount = 4U;
inline constexpr std::size_t kAmbisonicWChannelIndex = 0U;
inline constexpr std::size_t kAmbisonicYChannelIndex = 1U;
inline constexpr std::size_t kAmbisonicZChannelIndex = 2U;
inline constexpr std::size_t kAmbisonicXChannelIndex = 3U;

/// A basis shorter than this squared is not a direction any more; see ComputeAmbisonicRotation.
inline constexpr float kMinimumAmbisonicBasisLengthSquared = 1.0e-12F;

/// Engine direction (+X right, +Y up, -Z forward) to AmbiX (+X front, +Y left, +Z up). This is a
/// proper rotation - a signed axis permutation with determinant +1 - so it maps cross products
/// correctly, which is why the listener basis can be converted one vector at a time.
[[nodiscard]] inline Math::Vector3 ToAmbisonicDirection(const Math::Vector3& engineDirection) noexcept {
    return Math::Vector3{-engineDirection.z, -engineDirection.x, engineDirection.y};
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
struct AmbisonicRotation final {
    std::array<float, 9U> matrix{1.0F, 0.0F, 0.0F, 0.0F, 1.0F, 0.0F, 0.0F, 0.0F, 1.0F};

    [[nodiscard]] bool operator==(const AmbisonicRotation&) const noexcept = default;
};

/// Whether a listener basis can define a unique rotation at all: both axes finite and non-zero, and
/// forward not parallel to up. This is IsAudioListenerOrientationValid()'s rule plus the one it
/// deliberately leaves out - a parallel pair has no unique roll, and IsAudioListenerOrientationValid()
/// only promises a usable basis, not a roll-free one.
[[nodiscard]] inline bool IsAmbisonicListenerBasisValid(const Math::Vector3& listenerForward,
                                                           const Math::Vector3& listenerUp) noexcept {
    const bool finite = std::isfinite(listenerForward.x) && std::isfinite(listenerForward.y) &&
                        std::isfinite(listenerForward.z) && std::isfinite(listenerUp.x) &&
                        std::isfinite(listenerUp.y) && std::isfinite(listenerUp.z);
    if (!finite) return false;
    const float forwardLengthSquared = Math::LengthSquared(listenerForward);
    const float upLengthSquared = Math::LengthSquared(listenerUp);
    if (!(forwardLengthSquared > kMinimumAmbisonicBasisLengthSquared) ||
        !(upLengthSquared > kMinimumAmbisonicBasisLengthSquared)) {
        return false;
    }
    const float crossLengthSquared =
        Math::LengthSquared(Math::Cross(listenerForward * (1.0F / std::sqrt(forwardLengthSquared)), listenerUp));
    return crossLengthSquared > kMinimumAmbisonicBasisLengthSquared;
}

/// Rotation that renders a world-locked field on a listener whose head points along `forward` with
/// `up` as its up axis - the same two vectors IAudioSystem::SetListenerOrientation() takes,
/// so a camera or an HMD can drive it without an Euler-angle detour.
///
/// Degenerate input is not a rotation at all. Rather than synthesize a random orientation, this
/// returns the identity: the field plays unrotated, which is the one answer that is always safe.
/// Callers that want to refuse the update outright ask IsAmbisonicListenerBasisValid() first;
/// the adapter in ambisonic_stream_source_uve.h does exactly that.
[[nodiscard]] inline AmbisonicRotation ComputeAmbisonicRotation(const Math::Vector3& listenerForward,
                                                                     const Math::Vector3& listenerUp) noexcept {
    if (!IsAmbisonicListenerBasisValid(listenerForward, listenerUp)) return AmbisonicRotation{};

    const float forwardLengthSquared = Math::LengthSquared(listenerForward);
    const Math::Vector3 forward = listenerForward * (1.0F / std::sqrt(forwardLengthSquared));
    const Math::Vector3 right = Math::Cross(forward, listenerUp);
    const Math::Vector3 rightNormalised = right * (1.0F / std::sqrt(Math::LengthSquared(right)));
    // Re-derived rather than reused: an up vector that is not perpendicular to forward must not
    // shear the matrix, or the "rotation" would stop preserving energy.
    const Math::Vector3 up = Math::Cross(rightNormalised, forward);
    const Math::Vector3 left = rightNormalised * -1.0F;

    const Math::Vector3 ambisonicLeft = ToAmbisonicDirection(left);
    const Math::Vector3 ambisonicUp = ToAmbisonicDirection(up);
    const Math::Vector3 ambisonicForward = ToAmbisonicDirection(forward);

    // (x, y, z) of an AmbiX axis is (front, left, up); the coefficients it multiplies are ordered
    // (Y, Z, X), so each row is written (y, z, x) - see the struct comment.
    AmbisonicRotation rotation;
    rotation.matrix = {ambisonicLeft.y,    ambisonicLeft.z,    ambisonicLeft.x,
                       ambisonicUp.y,      ambisonicUp.z,      ambisonicUp.x,
                       ambisonicForward.y, ambisonicForward.z, ambisonicForward.x};
    return rotation;
}

/// Turns one world-space AmbiX frame into the listener's frame. In-place is safe: every input is
/// read before anything is written.
inline void RotateFirstOrderAmbisonicFrame(const AmbisonicRotation& rotation, const float* worldFrame,
                                              float* listenerFrame) noexcept {
    if (worldFrame == nullptr || listenerFrame == nullptr) return;
    // The directional coefficients are a vector in ACN order - (Y, Z, X) - and the matrix rows use
    // the same order, so this is a straight 3x3 multiply with no index shuffling.
    const Math::Vector3 worldDirection{worldFrame[kAmbisonicYChannelIndex],
                                          worldFrame[kAmbisonicZChannelIndex],
                                          worldFrame[kAmbisonicXChannelIndex]};
    const float* matrix = rotation.matrix.data();
    const Math::Vector3 listenerDirection{
        matrix[0] * worldDirection.x + matrix[1] * worldDirection.y + matrix[2] * worldDirection.z,
        matrix[3] * worldDirection.x + matrix[4] * worldDirection.y + matrix[5] * worldDirection.z,
        matrix[6] * worldDirection.x + matrix[7] * worldDirection.y + matrix[8] * worldDirection.z};

    listenerFrame[kAmbisonicWChannelIndex] = worldFrame[kAmbisonicWChannelIndex];
    listenerFrame[kAmbisonicYChannelIndex] = listenerDirection.x;
    listenerFrame[kAmbisonicZChannelIndex] = listenerDirection.y;
    listenerFrame[kAmbisonicXChannelIndex] = listenerDirection.z;
}

/// Decodes one listener-space AmbiX frame to the two-channel output the device mixes.
///
/// The decode is the classic coincident-cardioid pair: two virtual microphones pointing hard left
/// and hard right, which is the stereo decode every first-order system has used since the 1970s
/// (Malham & Myatt's stereo and binaural decodes both start from it). It is normalised so that a
/// pure omnidirectional field (W = 1, everything else zero) is EXACTLY unity on both channels -
/// the same "centre is unchanged" rule ComputeAudioPanGains() follows, so a bed crossfading
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
[[nodiscard]] inline AudioPanGains DecodeFirstOrderAmbisonicFrameToStereo(const float* listenerFrame,
                                                                                const float gain = 1.0F) noexcept {
    if (listenerFrame == nullptr) return AudioPanGains{0.0F, 0.0F};
    const float w = listenerFrame[kAmbisonicWChannelIndex];
    const float y = listenerFrame[kAmbisonicYChannelIndex];
    if (!std::isfinite(w) || !std::isfinite(y) || !std::isfinite(gain) || gain <= 0.0F) {
        return AudioPanGains{0.0F, 0.0F};
    }
    return AudioPanGains{gain * (w + y), gain * (w - y)};
}

} // namespace UVE::Audio
