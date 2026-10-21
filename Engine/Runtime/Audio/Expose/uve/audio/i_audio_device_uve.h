// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#pragma once

#include <cstddef>
#include <span>
#include <string_view>

#include "uve/audio/audio_stream_uve.h"
#include "uve/audio/audio_voice_desc_uve.h"
#include "uve/audio/voice_handle_uve.h"
#include "uve/audio/voice_playback_state_uve.h"

namespace UVE::Audio {

/// IAudioDeviceUVE is the engine's backend-agnostic audio hardware interface (the spec's
/// AudioSystemUVE's "Built-in: OpenAL-soft backend"), mirroring Render::IRenderDeviceUVE's shape:
/// a small, explicit resource-handle RHI a real backend implements directly. Two implementations
/// exist: MiniaudioAudioDeviceUVE (the production backend, driving real hardware through the
/// vendored miniaudio library - chosen instead of the spec's OpenAL-soft name-check because it is
/// a single header with zero link-time system dependencies, so CI needs no audio packages) and
/// NullAudioDeviceUVE (the silent fallback for machines without a usable output device, and the
/// call-recording test double). Every backend implements exactly this interface, so nothing above
/// the RHI (AudioSystemUVE, AudioSourceSystemUVE) changes between them.
/// Thread-safety: implementation-defined; each implementation documents its own contract. Callers
/// should assume an audio device is only safe to use from the main engine/audio thread unless a
/// concrete implementation states otherwise.
class IAudioDeviceUVE {
public:
    virtual ~IAudioDeviceUVE() = default;

    /// Creates a voice per `desc`. Never returns kInvalidVoiceHandleUVE on success.
    [[nodiscard]] virtual VoiceHandleUVE CreateVoiceUVE(const AudioVoiceDescUVE& desc) = 0;

    /// Destroys `voice`. A handle already destroyed (or never valid) is a safe no-op (logged).
    virtual void DestroyVoiceUVE(VoiceHandleUVE voice) = 0;

    /// Starts (or restarts, if already playing) `voice`. Returns false (logging the reason) if
    /// `voice` is unknown.
    [[nodiscard]] virtual bool PlayUVE(VoiceHandleUVE voice) = 0;

    /// Stops `voice`. Returns false (logging the reason) if `voice` is unknown.
    [[nodiscard]] virtual bool StopUVE(VoiceHandleUVE voice) = 0;

    /// Overwrites `voice`'s runtime position/gain/pitch. Returns false (logging the reason) if
    /// `voice` is unknown.
    [[nodiscard]] virtual bool SetVoiceParamsUVE(VoiceHandleUVE voice, const AudioVoiceParamsUVE& params) = 0;

    /// `voice`'s current playback state. Returns VoicePlaybackStateUVE::Stopped (logging the
    /// reason) if `voice` is unknown — a query that may legitimately miss (the voice could have
    /// just been destroyed by another system this same frame), not a programmer-precondition
    /// violation, matching IConfigManagerUVE's "keyed lookup that may legitimately miss" precedent.
    [[nodiscard]] virtual VoicePlaybackStateUVE GetVoiceStateUVE(VoiceHandleUVE voice) const = 0;

    // --- Streaming voices. ---
    // A streaming voice has no clip and never decodes one: the caller pushes interleaved frames as
    // they become available and the backend keeps a bounded ring. A long music bed therefore costs
    // one ring, not its whole decoded length. Everything else about the voice - handle lifetime,
    // Play/Stop/state, gain/pitch/pan through SetVoiceParamsUVE - is identical to a clip voice.

    /// Creates a voice fed by PushStreamFramesUVE() instead of a decoded clip. The ring holds
    /// kAudioStreamRingFramesUVE frames; the backend upmixes mono exactly like it does for clips.
    [[nodiscard]] virtual VoiceHandleUVE CreateStreamVoiceUVE(const AudioStreamFormatUVE& format) = 0;

    /// Appends up to `interleavedFrames.size() / format.channels` frames and returns how many were
    /// accepted - fewer than offered means the ring is full, which is the caller's cue to pump
    /// again later rather than to grow anything. Returns 0 (logging the reason) for an unknown
    /// handle, a clip voice, or a frame count that is not a whole number of frames.
    [[nodiscard]] virtual std::size_t PushStreamFramesUVE(VoiceHandleUVE voice,
                                                          std::span<const float> interleavedFrames) = 0;

    /// Declares that no more frames will be pushed: the voice plays out what is buffered, then
    /// moves itself to VoicePlaybackStateUVE::Stopped so `IsEntityPlayingUVE` answers honestly.
    /// Returns false (logging the reason) for an unknown handle or a clip voice.
    [[nodiscard]] virtual bool EndStreamUVE(VoiceHandleUVE voice) = 0;

    /// Frames waiting in the ring - the pump's headroom, and the number an underrun test watches.
    [[nodiscard]] virtual std::size_t GetStreamBufferedFramesUVE(VoiceHandleUVE voice) const = 0;

    /// Frames the ring can still take. Returns 0 for an unknown handle or a clip voice.
    [[nodiscard]] virtual std::size_t GetStreamFreeFramesUVE(VoiceHandleUVE voice) const = 0;

    /// A short human-readable backend name (e.g. `"Null"`), for logging/diagnostics.
    [[nodiscard]] virtual std::string_view GetBackendNameUVE() const noexcept = 0;
};

} // namespace UVE::Audio
