// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/audio/ambisonic_stream_source_uve.h"

#include <cmath>

#include "uve/logging/logging_macros_uve.h"

namespace UVE::Audio {

AmbisonicStreamSourceUVE::AmbisonicStreamSourceUVE(IAudioStreamSourceUVE& source) noexcept : m_source(source) {
    const AudioStreamFormatUVE format = m_source.GetFormatUVE();
    m_ready = IsAudioStreamSourceFormatValidUVE(format) &&
              format.channels == static_cast<std::uint32_t>(kFirstOrderAmbisonicChannelCountUVE);
    if (!m_ready) {
        // Logged here rather than on every read: a mis-wired bed should say so once, where it was
        // wired, and then stay quiet instead of spamming a line per audio block.
        UVE_ERROR("AmbisonicStreamSourceUVE: source is {} Hz / {} channels, expected a 4-channel AmbiX stream",
                  format.sampleRate, format.channels);
    }
}

bool AmbisonicStreamSourceUVE::SetListenerOrientationUVE(const Math::Vector3UVE& forward,
                                                         const Math::Vector3UVE& up) noexcept {
    // Refused rather than resolved to the identity: unlike the free function, this setter has a
    // "previous value" to keep, and a broken camera basis is a caller bug worth surfacing.
    if (!IsAmbisonicListenerBasisValidUVE(forward, up)) return false;
    m_rotation = ComputeAmbisonicRotationUVE(forward, up);
    return true;
}

bool AmbisonicStreamSourceUVE::SetGainUVE(const float gain) noexcept {
    if (!std::isfinite(gain) || gain < 0.0F || gain > 1.0F) return false;
    m_gain = gain;
    return true;
}

AudioStreamFormatUVE AmbisonicStreamSourceUVE::GetFormatUVE() const noexcept {
    if (!m_ready) return AudioStreamFormatUVE{0U, 0U};
    return AudioStreamFormatUVE{m_source.GetFormatUVE().sampleRate, 2U};
}

std::size_t AmbisonicStreamSourceUVE::GetFrameCountUVE() const noexcept {
    return m_ready ? m_source.GetFrameCountUVE() : 0U;
}

std::size_t AmbisonicStreamSourceUVE::ReadFramesUVE(const std::size_t requestedFrames,
                                                    std::vector<float>& outFrames) {
    outFrames.clear();
    if (!m_ready || requestedFrames == 0U) return 0U;
    // A window bigger than the device ring is a caller mistake: honouring it would size the scratch
    // buffer from an argument instead of from a bound, which is how "bounded streaming" stops being
    // bounded. Refusing is visible to the pump (it simply gets nothing this frame) and to a test.
    if (requestedFrames > kMaximumAudioStreamRingFramesUVE) {
        UVE_ERROR("AmbisonicStreamSourceUVE: refused a {}-frame window, the ring holds {} frames",
                  requestedFrames, kMaximumAudioStreamRingFramesUVE);
        return 0U;
    }

    const std::size_t frames = m_source.ReadFramesUVE(requestedFrames, m_scratch);
    const std::size_t channels = static_cast<std::size_t>(kFirstOrderAmbisonicChannelCountUVE);
    if (frames == 0U) return 0U;
    if (frames > requestedFrames || m_scratch.size() != frames * channels) {
        // A source that overruns its own contract would make us read past the window; dropping the
        // window turns a corrupt source into silence instead of into a heap overrun.
        UVE_ERROR("AmbisonicStreamSourceUVE: source returned {} frames ({} samples) for a {}-frame request", frames,
                  m_scratch.size(), requestedFrames);
        return 0U;
    }

    outFrames.reserve(frames * 2U);
    for (std::size_t frame = 0U; frame < frames; ++frame) {
        const float* ambisonicFrame = m_scratch.data() + frame * channels;
        float listenerFrame[kFirstOrderAmbisonicChannelCountUVE] = {};
        RotateFirstOrderAmbisonicFrameUVE(m_rotation, ambisonicFrame, listenerFrame);
        const AudioPanGainsUVE stereo = DecodeFirstOrderAmbisonicFrameToStereoUVE(listenerFrame, m_gain);
        // The decoder already turns non-finite coefficients into zero gains, but a rotation of a
        // huge finite value could still overflow; the ring must never carry infinity or NaN.
        outFrames.push_back(std::isfinite(stereo.left) ? stereo.left : 0.0F);
        outFrames.push_back(std::isfinite(stereo.right) ? stereo.right : 0.0F);
    }
    return frames;
}

bool AmbisonicStreamSourceUVE::SeekToFrameUVE(const std::size_t frame) {
    return m_ready && m_source.SeekToFrameUVE(frame);
}

} // namespace UVE::Audio
