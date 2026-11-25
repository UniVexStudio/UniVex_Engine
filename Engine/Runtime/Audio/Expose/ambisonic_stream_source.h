// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cstddef>
#include <vector>

#include "audio_ambisonics.h"
#include "audio_stream.h"
#include "vector3.h"

namespace UVE::Audio {

/// Plays a first-order ambisonic bed: wraps any four-channel AmbiX source and produces the stereo
/// stream a device voice accepts, rotated into the listener's current frame.
///
/// Why an adapter rather than a new voice type: the device's ring, pitch, gain, mixer routing and
/// pump already exist and are already tested, and a bed is just a stream whose samples happen to be
/// a decoded sound field. IAudioSystem::CreateStreamingSource() therefore needs no changes -
/// this reports two channels, the device allocates a stereo ring, and the existing once-per-frame
/// pump keeps it fed. Memory stays at the ring plus one decode window, so an eight-minute 360
/// ambience costs exactly what a two-minute one does.
///
/// Rotation happens once per ReadFrames() window, not per sample. A window is bounded by the
/// device ring, and the pump tops it up every engine frame, so head motion lands within a frame or
/// two of being heard; the alternative - a 3x3 multiply per frame per channel against an
/// orientation that cannot change mid-call - buys nothing. A caller that wants tighter tracking
/// keeps the ring shallow by pumping right before the device consumes it.
///
/// Bounded and validated: construction refuses any source that is not four-channel AmbiX
/// (IsReady() reports it, GetFormat() reports an invalid format so
/// CreateStreamingSource() fails loudly instead of allocating the wrong ring), read windows are
/// capped at kMaximumAudioStreamRingFrames rather than growing an unbounded buffer, and a
/// non-finite sample becomes silence instead of poisoning the mix.
///
/// Thread-safety: not thread-safe, and deliberately so - the pump calls it from the main thread,
/// which is also the only thread allowed to move the listener. The wrapped source is borrowed and
/// must outlive this adapter.
class AmbisonicStreamSource final : public IAudioStreamSource {
public:
    explicit AmbisonicStreamSource(IAudioStreamSource& source) noexcept;
    ~AmbisonicStreamSource() override = default;

    AmbisonicStreamSource(const AmbisonicStreamSource&) = delete;
    AmbisonicStreamSource& operator=(const AmbisonicStreamSource&) = delete;
    AmbisonicStreamSource(AmbisonicStreamSource&&) = delete;
    AmbisonicStreamSource& operator=(AmbisonicStreamSource&&) = delete;

    /// True when the wrapped source is four channels at an accepted rate. A false here means every
    /// read returns nothing and the system refuses to create a voice; it is never a silent mono
    /// downmix of a bed that would have been mis-localised.
    [[nodiscard]] bool IsReady() const noexcept { return m_ready; }

    /// Sets the listener basis the field is rendered against, in engine space (+X right, +Y up,
    /// -Z forward) - the same pair IAudioSystem::SetListenerOrientation() takes, so a camera
    /// or HMD can drive both. Returns false and leaves the previous orientation in place for
    /// non-finite input, a zero-length axis, or forward parallel to up.
    [[nodiscard]] bool SetListenerOrientation(const Math::Vector3& forward,
                                                 const Math::Vector3& up) noexcept;

    /// Final linear bed gain, in [0, 1] like every device-facing gain. False (and no change) outside
    /// that range or for a non-finite value.
    [[nodiscard]] bool SetGain(float gain) noexcept;
    [[nodiscard]] float GetGain() const noexcept { return m_gain; }

    /// The rotation currently applied - useful for diagnostics and for tests that want to assert the
    /// basis rather than infer it from samples. Identity until SetListenerOrientation() succeeds.
    [[nodiscard]] const AmbisonicRotation& GetRotation() const noexcept { return m_rotation; }

    // --- IAudioStreamSource. Two channels out, at the wrapped source's rate. ---
    [[nodiscard]] AudioStreamFormat GetFormat() const noexcept override;
    /// The wrapped source's length, forwarded unchanged: this adapter neither loops nor buffers
    /// beyond its own window, so whatever cannot-seek/end-of-stream rules apply there still apply.
    [[nodiscard]] std::size_t GetFrameCount() const noexcept override;
    /// Reads up to `requestedFrames` ambisonic frames, rotates and decodes them, and appends
    /// `frames * 2` interleaved stereo samples to `outFrames` (which is cleared first, per the
    /// IAudioStreamSource contract). Returns the frame count, or 0 at the end of the source.
    [[nodiscard]] std::size_t ReadFrames(std::size_t requestedFrames, std::vector<float>& outFrames) override;
    /// Forwarded verbatim; the adapter holds no cursor of its own.
    [[nodiscard]] bool SeekToFrame(std::size_t frame) override;

private:
    IAudioStreamSource& m_source;
    AmbisonicRotation m_rotation{};
    float m_gain = 1.0F;
    bool m_ready = false;
    /// The wrapped source's own output for one window, reused so the pump does not allocate per
    /// frame. Four floats per frame, and never more than one ring's worth.
    std::vector<float> m_scratch;
};

} // namespace UVE::Audio
