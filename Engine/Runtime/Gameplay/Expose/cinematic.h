// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include "asset_guid.h"
#include "entity.h"
#include "transform_component.h"
#include "quaternion.h"
#include "vector3.h"

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
// is live, and the engine sync turns that into queued events plus SetActiveCamera.
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
// sweeps the stopped ones every frame. Those are bare handle values, not VoiceHandle:
// gameplay cannot link uve_audio, which would cycle back through uve_scene.

inline constexpr std::size_t kMaximumCinematicEvents = 64U;
inline constexpr std::size_t kMaximumCinematicCuts = 32U;
inline constexpr std::size_t kMaximumCinematicCameraKeys = 64U;
inline constexpr std::size_t kMaximumCinematicAnimationKeys = 64U;
inline constexpr std::size_t kMaximumCinematicAudioKeys = 64U;
inline constexpr std::size_t kMaximumCinematicAudioPathBytes = 256U;
inline constexpr std::size_t kMaximumCinematicLiveOneShots = 16U;
inline constexpr std::size_t kMaximumCinematicEventIdBytes = 64U;

enum class CinematicLoopMode : std::uint8_t {
    Once = 0,
    Loop,
};

struct CinematicEventKey final {
    double timeSeconds = 0.0;
    std::string eventId;

    [[nodiscard]] bool operator==(const CinematicEventKey&) const = default;
};

struct CinematicCameraCut final {
    double timeSeconds = 0.0;
    Entity camera = kInvalidEntity;

    [[nodiscard]] bool operator==(const CinematicCameraCut&) const = default;
};

/// One camera pose on the move track: world-space position plus a unit-quaternion rotation.
/// Stored rotations are always normalized (Add normalizes, the validator demands it), so the
/// sampler never has to ask whether a slerp endpoint is usable.
struct CinematicCameraKey final {
    double timeSeconds = 0.0;
    Math::Vector3 position{};
    Math::Quaternion rotation{};

    [[nodiscard]] bool operator==(const CinematicCameraKey&) const = default;
};

/// The move track sampled at one instant: what the live camera takes this frame.
struct CinematicCameraPose final {
    Math::Vector3 position{};
    Math::Quaternion rotation{};

    [[nodiscard]] bool operator==(const CinematicCameraPose&) const = default;
};

/// Edge cue: start `target`'s player, switching it to `clip` first unless the guid is invalid.
struct CinematicAnimationCue final {
    Entity target = kInvalidEntity;
    Asset::AssetGuid clip{};

    [[nodiscard]] bool operator==(const CinematicAnimationCue&) const = default;
};

/// Edge cue: play a one-shot of `audioAssetPath` at `volume` - 2D, or spatial at `position`
/// (baked when fired; one-shots never move) inside `minDistance`..`maxDistance`.
struct CinematicAudioCue final {
    std::string audioAssetPath;
    float volume = 1.0F;
    bool spatial = false;
    Math::Vector3 position{};
    float minDistance = 1.0F;
    float maxDistance = 25.0F;

    [[nodiscard]] bool operator==(const CinematicAudioCue&) const = default;
};

struct CinematicAnimationKey final {
    double timeSeconds = 0.0;
    Entity target = kInvalidEntity;
    Asset::AssetGuid clip{};

    [[nodiscard]] bool operator==(const CinematicAnimationKey&) const = default;
};

struct CinematicAudioKey final {
    double timeSeconds = 0.0;
    std::string audioAssetPath;
    float volume = 1.0F;
    bool spatial = false;
    Math::Vector3 position{};
    float minDistance = 1.0F;
    float maxDistance = 25.0F;

    [[nodiscard]] bool operator==(const CinematicAudioKey&) const = default;
};

struct CinematicComponent final {
    double durationSeconds = 0.0;
    std::vector<CinematicEventKey> events;
    std::vector<CinematicCameraCut> cuts;
    std::vector<CinematicCameraKey> cameraKeys;
    std::vector<CinematicAnimationKey> animationKeys;
    std::vector<CinematicAudioKey> audioKeys;
    /// Runtime state, never saved: Audio::VoiceHandle values of one-shots this cinematic fired
    /// that have not stopped yet, swept by the engine every frame.
    std::vector<std::uint32_t> liveOneShots;
    /// Cutscenes stay parked until something presses play; autoplay is opt-in per shot.
    bool autoplay = false;
    float speed = 1.0F;
    CinematicLoopMode loopMode = CinematicLoopMode::Once;
    bool isPlaying = false;
    bool finished = false;
    double currentTimeSeconds = 0.0;

    [[nodiscard]] bool operator==(const CinematicComponent&) const = default;
};

/// Finite positive duration and speed, key times inside [0, duration], usable event ids, valid
/// cut cameras, both lists sorted by time and within their caps, plus a sorted camera-key track
/// whose positions are finite and whose rotations are finite unit quaternions. Animation keys
/// name valid targets; audio keys name usable paths at finite non-negative volume with valid
/// distance bands and finite positions.
[[nodiscard]] bool IsCinematicComponentValid(const CinematicComponent& value) noexcept;

/// Inserts an event key keeping time order (equal times keep insertion order). False for a bad
/// id, a time outside [0, duration], or a full track. Times are judged against the duration the
/// component has now; changing the duration afterwards can strand keys past the end.
[[nodiscard]] bool AddCinematicEvent(CinematicComponent& cinematic, double timeSeconds,
                                        std::string eventId);

/// Inserts a camera cut keeping time order. False for an invalid camera, a time outside
/// [0, duration], or a full track.
[[nodiscard]] bool AddCinematicCut(CinematicComponent& cinematic, double timeSeconds,
                                      Entity camera);

/// Removes the key/cut at `index`. False when out of range.
[[nodiscard]] bool RemoveCinematicEvent(CinematicComponent& cinematic, std::size_t index);
[[nodiscard]] bool RemoveCinematicCut(CinematicComponent& cinematic, std::size_t index);

/// Inserts a camera key keeping time order. The rotation is normalized on store. False for a
/// non-finite position, a zero-length rotation, a time outside [0, duration], or a full track.
[[nodiscard]] bool AddCinematicCameraKey(CinematicComponent& cinematic, double timeSeconds,
                                            Math::Vector3 position, Math::Quaternion rotation);

/// Removes the camera key at `index`. False when out of range.
[[nodiscard]] bool RemoveCinematicCameraKey(CinematicComponent& cinematic, std::size_t index);

/// Inserts an animation key keeping time order. False for an invalid target, a time outside
/// [0, duration], or a full track. An invalid `clip` plays the target player's own clip.
[[nodiscard]] bool AddCinematicAnimationKey(CinematicComponent& cinematic, double timeSeconds,
                                               Entity target, Asset::AssetGuid clip);
/// Removes the animation key at `index`. False when out of range.
[[nodiscard]] bool RemoveCinematicAnimationKey(CinematicComponent& cinematic, std::size_t index);
/// Inserts a 2D audio key keeping time order: non-spatial, default distance band. False for an
/// empty or overlong path, a non-finite or negative volume, a time outside [0, duration], or a
/// full track.
[[nodiscard]] bool AddCinematicAudioKey(CinematicComponent& cinematic, double timeSeconds,
                                           std::string audioAssetPath, float volume);
/// Inserts a fully-specified audio key (spatial cues included) keeping time order. False when any
/// field is unusable: bad path, volume, distance band (finite, min above zero, max above min), or
/// position, a time outside [0, duration], or a full track.
[[nodiscard]] bool AddCinematicAudioKey(CinematicComponent& cinematic, CinematicAudioKey key);
/// Removes the audio key at `index`. False when out of range.
[[nodiscard]] bool RemoveCinematicAudioKey(CinematicComponent& cinematic, std::size_t index);

/// The move track at `timeSeconds`: linear position, spherical rotation between the two keys
/// around it, clamped to the first and last. Nullopt when the track is empty or the time is not
/// finite - what scrubbing previews and what each step carries.
[[nodiscard]] std::optional<CinematicCameraPose>
SampleCinematicCamera(const CinematicComponent& cinematic, double timeSeconds) noexcept;

/// Writes `pose` into a camera's local transform: position outright, rotation normalized and
/// mirrored into the stored Euler angles so the Inspector shows what is on screen - the same two
/// channels WriteAnimatedPose would write, without the scale channel a camera has no use for.
/// A zero rotation keeps the previous orientation rather than collapsing it.
void WriteCinematicCameraPose(const CinematicCameraPose& pose,
                                 TransformComponent& target) noexcept;

/// What Play fires: the event, animation, and audio keys sitting exactly at 0.0.
struct CinematicPlayResult final {
    std::vector<std::string> firedEventIds;
    std::vector<CinematicAnimationCue> animationCues;
    std::vector<CinematicAudioCue> audioCues;

    [[nodiscard]] bool operator==(const CinematicPlayResult&) const = default;
};

/// Restarts the clock and fires the keys sitting exactly at 0.0. A non-positive duration starts
/// nothing and fires nothing.
[[nodiscard]] CinematicPlayResult PlayCinematic(CinematicComponent& cinematic) noexcept;

/// Parks the playhead where it is. Play restarts from zero.
void StopCinematic(CinematicComponent& cinematic) noexcept;

struct CinematicStepResult final {
    std::vector<std::string> firedEventIds;
    Entity activeCamera = kInvalidEntity;
    std::optional<CinematicCameraPose> cameraPose;
    std::vector<CinematicAnimationCue> firedAnimationCues;
    std::vector<CinematicAudioCue> firedAudioCues;
    bool justFinished = false;

    [[nodiscard]] bool operator==(const CinematicStepResult&) const = default;
};

/// Advances a playing cinematic by `deltaSeconds` (scaled by speed): collects the fired event,
/// animation, and audio keys,
/// resolves the live camera cut, samples the move track at the new time, and parks at the end
/// for Once (looping wraps instead). Returns
/// nothing and moves nothing when parked, finished, or stepped by a non-positive/non-finite dt.
[[nodiscard]] CinematicStepResult StepCinematic(CinematicComponent& cinematic, float deltaSeconds);

/// The cut live at `timeSeconds` without moving anything: what scrubbing previews.
[[nodiscard]] Entity ActiveCinematicCamera(const CinematicComponent& cinematic,
                                                 double timeSeconds) noexcept;

} // namespace UVE::Scene
