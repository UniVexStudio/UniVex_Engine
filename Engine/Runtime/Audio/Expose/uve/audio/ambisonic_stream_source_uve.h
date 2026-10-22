// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cstddef>
#include <vector>

#include "uve/audio/audio_ambisonics_uve.h"
#include "uve/audio/audio_stream_uve.h"
#include "uve/math/vector3_uve.h"

namespace UVE::Audio {

/// Plays a first-order ambisonic bed: wraps any four-channel AmbiX source and produces the stereo
/// stream a device voice accepts, rotated into the listener's current frame.
///
/// Why an adapter rather than a new voice type: the device's ring, pitch, gain, mixer routing and
/// pump already exist and are already tested, and a bed is just a stream whose samples happen to be
/// a decoded sound field. IAudioSystemUVE::CreateStreamingSourceUVE() therefore needs no changes -
/// this reports two channels, the device allocates a stereo ring, and the existing once-per-frame
/// pump keeps it fed. Memory stays at the ring plus one decode window, so an eight-minute 360
/// ambience costs exactly what a two-minute one does.
///
/// Rotation happens once per ReadFramesUVE() window, not per sample. A window is bounded by the
/// device ring, and the pump tops it up every engine frame, so head motion lands within a frame or
/// two of being heard; the alternative - a 3x3 multiply per frame per channel against an
/// orientation that cannot change mid-call - buys nothing. A caller that wants tighter tracking
/// keeps the ring shallow by pumping right before the device consumes it.
///
/// Bounded and validated: construction refuses any source that is not four-channel AmbiX
/// (IsReadyUVE() reports it, GetFormatUVE() reports an invalid format so
/// CreateStreamingSourceUVE() fails loudly instead of allocating the wrong ring), read windows are
/// capped at kMaximumAudioStreamRingFramesUVE rather than growing an unbounded buffer, and a
/// non-finite sample becomes silence instead of poisoning the mix.
///
/// Thread-safety: not thread-safe, and deliberately so - the pump calls it from the main thread,
/// which is also the only thread allowed to move the listener. The wrapped source is borrowed and
/// must outlive this adapter.
class AmbisonicStreamSourceUVE final : public IAudioStreamSourceUVE {
public:
    explicit AmbisonicStreamSourceUVE(IAudioStreamSourceUVE& source) noexcept;
    ~AmbisonicStreamSourceUVE() override = default;

    AmbisonicStreamSourceUVE(const AmbisonicStreamSourceUVE&) = delete;
    AmbisonicStreamSourceUVE& operator=(const AmbisonicStreamSourceUVE&) = delete;
    AmbisonicStreamSourceUVE(AmbisonicStreamSourceUVE&&) = delete;
    AmbisonicStreamSourceUVE& operator=(AmbisonicStreamSourceUVE&&) = delete;

    /// True when the wrapped source is four channels at an accepted rate. A false here means every
    /// read returns nothing and the system refuses to create a voice; it is never a silent mono
    /// downmix of a bed that would have been mis-localised.
    [[nodiscard]] bool IsReadyUVE() const noexcept { return m_ready; }

    /// Sets the listener basis the field is rendered against, in engine space (+X right, +Y up,
    /// -Z forward) - the same pair IAudioSystemUVE::SetListenerOrientationUVE() takes, so a camera
    /// or HMD can drive both. Returns false and leaves the previous orientation in place for
    /// non-finite input, a zero-length axis, or forward parallel to up.
    [[nodiscard]] bool SetListenerOrientationUVE(const Math::Vector3UVE& forward,
                                                 const Math::Vector3UVE& up) noexcept;

    /// Final linear bed gain, in [0, 1] like every device-facing gain. False (and no change) outside
    /// that range or for a non-finite value.
    [[nodiscard]] bool SetGainUVE(float gain) noexcept;
    [[nodiscard]] float GetGainUVE() const noexcept { return m_gain; }

    /// The rotation currently applied - useful for diagnostics and for tests that want to assert the
    /// basis rather than infer it from samples. Identity until SetListenerOrientationUVE() succeeds.
    [[nodiscard]] const AmbisonicRotationUVE& GetRotationUVE() const noexcept { return m_rotation; }

    // --- IAudioStreamSourceUVE. Two channels out, at the wrapped source's rate. ---
    [[nodiscard]] AudioStreamFormatUVE GetFormatUVE() const noexcept override;
    /// The wrapped source's length, forwarded unchanged: this adapter neither loops nor buffers
    /// beyond its own window, so whatever cannot-seek/end-of-stream rules apply there still apply.
    [[nodiscard]] std::size_t GetFrameCountUVE() const noexcept override;
    /// Reads up to `requestedFrames` ambisonic frames, rotates and decodes them, and appends
    /// `frames * 2` interleaved stereo samples to `outFrames` (which is cleared first, per the
    /// IAudioStreamSourceUVE contract). Returns the frame count, or 0 at the end of the source.
    [[nodiscard]] std::size_t ReadFramesUVE(std::size_t requestedFrames, std::vector<float>& outFrames) override;
    /// Forwarded verbatim; the adapter holds no cursor of its own.
    [[nodiscard]] bool SeekToFrameUVE(std::size_t frame) override;

private:
    IAudioStreamSourceUVE& m_source;
    AmbisonicRotationUVE m_rotation{};
    float m_gain = 1.0F;
    bool m_ready = false;
    /// The wrapped source's own output for one window, reused so the pump does not allocate per
    /// frame. Four floats per frame, and never more than one ring's worth.
    std::vector<float> m_scratch;
};

} // namespace UVE::Audio
