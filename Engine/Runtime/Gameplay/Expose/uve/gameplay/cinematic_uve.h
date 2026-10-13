// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include "uve/asset/asset_guid_uve.h"
#include "uve/component/entity_uve.h"
#include "uve/component/transform_component_uve.h"
#include "uve/math/quaternion_uve.h"
#include "uve/math/vector3_uve.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace UVE::Scene {

// ---- Cinematic timeline --------------------------------------------------------------
//
// A cutscene on a shared clock: event keys that fire gameplay events and camera cuts that switch
// the active camera, all evaluated from one currentTime. The component mixes authored data (keys,
// cuts, duration) with player state (isPlaying, finished, currentTime), the same shape as the
// AnimationSequencer player. Everything here is pure: stepping returns what fired and which camera
// is live, and the engine sync turns that into queued events plus SetActiveCameraUVE.
//
// Firing rules, stated once: keys fire going forward only, in passing order, exclusive of the
// time the playhead sits on (a key exactly under the playhead does not refire every frame).
// Keys at 0.0 fire on Play, not on the first Step. Loop wraps refire the wrapped span. Scrubbing
// and reverse motion never fire - they only move the clock and resolve the camera.
// The camera move track poses the cut-resolved camera: each step samples position (linear) and
// rotation (spherical) at the playhead and writes both into that camera's local transform, the
// same semantics as the animation track sampler. An empty track poses nothing, and keys never
// pick a camera - a timeline with keys but no cuts still needs one cut naming its camera.
// Animation and audio keys are edge-triggered like events: passing one fires a cue (a target
// player to start, a one-shot to play), and Play fires the ones sitting at 0.0. An animation cue
// names a player plus an optional clip override - an invalid guid plays the player's own clip.
// Audio cues are fire-and-forget one-shots - 2D, or spatial at a baked world position; the engine
// tracks their voices in liveOneShots and
// sweeps the stopped ones every frame. Those are bare handle values, not VoiceHandleUVE:
// gameplay cannot link uve_audio, which would cycle back through uve_scene.

inline constexpr std::size_t kMaximumCinematicEventsUVE = 64U;
inline constexpr std::size_t kMaximumCinematicCutsUVE = 32U;
inline constexpr std::size_t kMaximumCinematicCameraKeysUVE = 64U;
inline constexpr std::size_t kMaximumCinematicAnimationKeysUVE = 64U;
inline constexpr std::size_t kMaximumCinematicAudioKeysUVE = 64U;
inline constexpr std::size_t kMaximumCinematicAudioPathBytesUVE = 256U;
inline constexpr std::size_t kMaximumCinematicLiveOneShotsUVE = 16U;
inline constexpr std::size_t kMaximumCinematicEventIdBytesUVE = 64U;

enum class CinematicLoopModeUVE : std::uint8_t {
    Once = 0,
    Loop,
};

struct CinematicEventKeyUVE final {
    double timeSeconds = 0.0;
    std::string eventId;

    [[nodiscard]] bool operator==(const CinematicEventKeyUVE&) const = default;
};

struct CinematicCameraCutUVE final {
    double timeSeconds = 0.0;
    EntityUVE camera = kInvalidEntityUVE;

    [[nodiscard]] bool operator==(const CinematicCameraCutUVE&) const = default;
};

/// One camera pose on the move track: world-space position plus a unit-quaternion rotation.
/// Stored rotations are always normalized (Add normalizes, the validator demands it), so the
/// sampler never has to ask whether a slerp endpoint is usable.
struct CinematicCameraKeyUVE final {
    double timeSeconds = 0.0;
    Math::Vector3UVE position{};
    Math::QuaternionUVE rotation{};

    [[nodiscard]] bool operator==(const CinematicCameraKeyUVE&) const = default;
};

/// The move track sampled at one instant: what the live camera takes this frame.
struct CinematicCameraPoseUVE final {
    Math::Vector3UVE position{};
    Math::QuaternionUVE rotation{};

    [[nodiscard]] bool operator==(const CinematicCameraPoseUVE&) const = default;
};

/// Edge cue: start `target`'s player, switching it to `clip` first unless the guid is invalid.
struct CinematicAnimationCueUVE final {
    EntityUVE target = kInvalidEntityUVE;
    Asset::AssetGuidUVE clip{};

    [[nodiscard]] bool operator==(const CinematicAnimationCueUVE&) const = default;
};

/// Edge cue: play a one-shot of `audioAssetPath` at `volume` - 2D, or spatial at `position`
/// (baked when fired; one-shots never move) inside `minDistance`..`maxDistance`.
struct CinematicAudioCueUVE final {
    std::string audioAssetPath;
    float volume = 1.0F;
    bool spatial = false;
    Math::Vector3UVE position{};
    float minDistance = 1.0F;
    float maxDistance = 25.0F;

    [[nodiscard]] bool operator==(const CinematicAudioCueUVE&) const = default;
};

struct CinematicAnimationKeyUVE final {
    double timeSeconds = 0.0;
    EntityUVE target = kInvalidEntityUVE;
    Asset::AssetGuidUVE clip{};

    [[nodiscard]] bool operator==(const CinematicAnimationKeyUVE&) const = default;
};

struct CinematicAudioKeyUVE final {
    double timeSeconds = 0.0;
    std::string audioAssetPath;
    float volume = 1.0F;
    bool spatial = false;
    Math::Vector3UVE position{};
    float minDistance = 1.0F;
    float maxDistance = 25.0F;

    [[nodiscard]] bool operator==(const CinematicAudioKeyUVE&) const = default;
};

struct CinematicComponentUVE final {
    double durationSeconds = 0.0;
    std::vector<CinematicEventKeyUVE> events;
    std::vector<CinematicCameraCutUVE> cuts;
    std::vector<CinematicCameraKeyUVE> cameraKeys;
    std::vector<CinematicAnimationKeyUVE> animationKeys;
    std::vector<CinematicAudioKeyUVE> audioKeys;
    /// Runtime state, never saved: Audio::VoiceHandleUVE values of one-shots this cinematic fired
    /// that have not stopped yet, swept by the engine every frame.
    std::vector<std::uint32_t> liveOneShots;
    /// Cutscenes stay parked until something presses play; autoplay is opt-in per shot.
    bool autoplay = false;
    float speed = 1.0F;
    CinematicLoopModeUVE loopMode = CinematicLoopModeUVE::Once;
    bool isPlaying = false;
    bool finished = false;
    double currentTimeSeconds = 0.0;

    [[nodiscard]] bool operator==(const CinematicComponentUVE&) const = default;
};

/// Finite positive duration and speed, key times inside [0, duration], usable event ids, valid
/// cut cameras, both lists sorted by time and within their caps, plus a sorted camera-key track
/// whose positions are finite and whose rotations are finite unit quaternions. Animation keys
/// name valid targets; audio keys name usable paths at finite non-negative volume with valid
/// distance bands and finite positions.
[[nodiscard]] bool IsCinematicComponentValidUVE(const CinematicComponentUVE& value) noexcept;

/// Inserts an event key keeping time order (equal times keep insertion order). False for a bad
/// id, a time outside [0, duration], or a full track. Times are judged against the duration the
/// component has now; changing the duration afterwards can strand keys past the end.
[[nodiscard]] bool AddCinematicEventUVE(CinematicComponentUVE& cinematic, double timeSeconds,
                                        std::string eventId);

/// Inserts a camera cut keeping time order. False for an invalid camera, a time outside
/// [0, duration], or a full track.
[[nodiscard]] bool AddCinematicCutUVE(CinematicComponentUVE& cinematic, double timeSeconds,
                                      EntityUVE camera);

/// Removes the key/cut at `index`. False when out of range.
[[nodiscard]] bool RemoveCinematicEventUVE(CinematicComponentUVE& cinematic, std::size_t index);
[[nodiscard]] bool RemoveCinematicCutUVE(CinematicComponentUVE& cinematic, std::size_t index);

/// Inserts a camera key keeping time order. The rotation is normalized on store. False for a
/// non-finite position, a zero-length rotation, a time outside [0, duration], or a full track.
[[nodiscard]] bool AddCinematicCameraKeyUVE(CinematicComponentUVE& cinematic, double timeSeconds,
                                            Math::Vector3UVE position, Math::QuaternionUVE rotation);

/// Removes the camera key at `index`. False when out of range.
[[nodiscard]] bool RemoveCinematicCameraKeyUVE(CinematicComponentUVE& cinematic, std::size_t index);

/// Inserts an animation key keeping time order. False for an invalid target, a time outside
/// [0, duration], or a full track. An invalid `clip` plays the target player's own clip.
[[nodiscard]] bool AddCinematicAnimationKeyUVE(CinematicComponentUVE& cinematic, double timeSeconds,
                                               EntityUVE target, Asset::AssetGuidUVE clip);
/// Removes the animation key at `index`. False when out of range.
[[nodiscard]] bool RemoveCinematicAnimationKeyUVE(CinematicComponentUVE& cinematic, std::size_t index);
/// Inserts a 2D audio key keeping time order: non-spatial, default distance band. False for an
/// empty or overlong path, a non-finite or negative volume, a time outside [0, duration], or a
/// full track.
[[nodiscard]] bool AddCinematicAudioKeyUVE(CinematicComponentUVE& cinematic, double timeSeconds,
                                           std::string audioAssetPath, float volume);
/// Inserts a fully-specified audio key (spatial cues included) keeping time order. False when any
/// field is unusable: bad path, volume, distance band (finite, min above zero, max above min), or
/// position, a time outside [0, duration], or a full track.
[[nodiscard]] bool AddCinematicAudioKeyUVE(CinematicComponentUVE& cinematic, CinematicAudioKeyUVE key);
/// Removes the audio key at `index`. False when out of range.
[[nodiscard]] bool RemoveCinematicAudioKeyUVE(CinematicComponentUVE& cinematic, std::size_t index);

/// The move track at `timeSeconds`: linear position, spherical rotation between the two keys
/// around it, clamped to the first and last. Nullopt when the track is empty or the time is not
/// finite - what scrubbing previews and what each step carries.
[[nodiscard]] std::optional<CinematicCameraPoseUVE>
SampleCinematicCameraUVE(const CinematicComponentUVE& cinematic, double timeSeconds) noexcept;

/// Writes `pose` into a camera's local transform: position outright, rotation normalized and
/// mirrored into the stored Euler angles so the Inspector shows what is on screen - the same two
/// channels WriteAnimatedPoseUVE would write, without the scale channel a camera has no use for.
/// A zero rotation keeps the previous orientation rather than collapsing it.
void WriteCinematicCameraPoseUVE(const CinematicCameraPoseUVE& pose,
                                 TransformComponentUVE& target) noexcept;

/// What Play fires: the event, animation, and audio keys sitting exactly at 0.0.
struct CinematicPlayResultUVE final {
    std::vector<std::string> firedEventIds;
    std::vector<CinematicAnimationCueUVE> animationCues;
    std::vector<CinematicAudioCueUVE> audioCues;

    [[nodiscard]] bool operator==(const CinematicPlayResultUVE&) const = default;
};

/// Restarts the clock and fires the keys sitting exactly at 0.0. A non-positive duration starts
/// nothing and fires nothing.
[[nodiscard]] CinematicPlayResultUVE PlayCinematicUVE(CinematicComponentUVE& cinematic) noexcept;

/// Parks the playhead where it is. Play restarts from zero.
void StopCinematicUVE(CinematicComponentUVE& cinematic) noexcept;

struct CinematicStepResultUVE final {
    std::vector<std::string> firedEventIds;
    EntityUVE activeCamera = kInvalidEntityUVE;
    std::optional<CinematicCameraPoseUVE> cameraPose;
    std::vector<CinematicAnimationCueUVE> firedAnimationCues;
    std::vector<CinematicAudioCueUVE> firedAudioCues;
    bool justFinished = false;

    [[nodiscard]] bool operator==(const CinematicStepResultUVE&) const = default;
};

/// Advances a playing cinematic by `deltaSeconds` (scaled by speed): collects the fired event,
/// animation, and audio keys,
/// resolves the live camera cut, samples the move track at the new time, and parks at the end
/// for Once (looping wraps instead). Returns
/// nothing and moves nothing when parked, finished, or stepped by a non-positive/non-finite dt.
[[nodiscard]] CinematicStepResultUVE StepCinematicUVE(CinematicComponentUVE& cinematic, float deltaSeconds);

/// The cut live at `timeSeconds` without moving anything: what scrubbing previews.
[[nodiscard]] EntityUVE ActiveCinematicCameraUVE(const CinematicComponentUVE& cinematic,
                                                 double timeSeconds) noexcept;

} // namespace UVE::Scene
