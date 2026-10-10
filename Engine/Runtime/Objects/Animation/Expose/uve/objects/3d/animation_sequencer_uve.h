// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "uve/animation/time_pose_contract_uve.h"
#include "uve/component/animation_sequencer_component_uve.h"
#include "uve/component/entity_uve.h"

namespace UVE::Asset {
struct AnimationClipAssetUVE;
struct AnimationAssetEventUVE;
struct AnimationAssetSampleUVE;
} // namespace UVE::Asset

namespace UVE::Scene {

class IEntityManagerUVE;
struct TransformComponentUVE;
struct Skeleton3DComponentUVE;

/// Authoring definition for the AnimationSequencer object: a pure Object - no transform, no visibility -
/// whose Inspector is its own section, its AnimationDriver base, then the Object section.
/// It plays a clip on another object (`target`, or its parent), so it can sit anywhere in the tree,
/// directly under the Object included.
struct AnimationSequencerObjectDefinitionUVE final {
    static constexpr std::string_view defaultName = "AnimationSequencer";

    AnimationSequencerComponentUVE player{};
    AnimationDriverComponentUVE mixer{};
};

[[nodiscard]] bool IsAnimationSequencerObjectDefinitionValidUVE(const AnimationSequencerObjectDefinitionUVE& value) noexcept;

/// Applies the AnimationDriver base (ApplyAnimationDriverBaseUVE) and adds the player when it is missing.
void ApplyAnimationSequencerObjectDefinitionUVE(IEntityManagerUVE& entityManager, EntityUVE entity,
                                           const AnimationSequencerObjectDefinitionUVE& value);

// ---- Playback ------------------------------------------------------------------------------------
// Pure functions over the component, the clip and the target's transform, so the whole behaviour
// is testable without an engine. EngineCoreUVE::SyncAnimationSequencersUVE drives them.

/// Starts playback from `startOffsetSeconds` (from the end when speed is negative), remembering
/// the target's current pose for the blend-in, relative playback and Return To Start.
void PlayAnimationSequencerUVE(AnimationSequencerComponentUVE& player, const TransformComponentUVE& targetNow,
                            double clipDurationSeconds) noexcept;

/// Stops playback where it is. The target keeps its current pose.
void StopAnimationSequencerUVE(AnimationSequencerComponentUVE& player) noexcept;

/// Advances a playing player by `deltaSeconds` and writes the clip's pose into `target`, through the
/// mixer's channel masks, the blend-in and relative mode. Returns true when `target` was written. A
/// clip that is empty or invalid stops the player and writes nothing.
[[nodiscard]] bool StepAnimationSequencerUVE(AnimationSequencerComponentUVE& player, const Asset::AnimationClipAssetUVE& clip,
                                          float deltaSeconds, TransformComponentUVE& target,
                                          const AnimationDriverComponentUVE& mixer = {}) noexcept;

/// Advances a playing player by `deltaSeconds` and writes a skeletal clip's pose into `skeleton`:
/// each bone takes the track of the same name, sampled like an object track; a bone with no track
/// keeps its rest pose. The loop mode, speed and On Finish work as for an object; Blend In eases from
/// the pose the skeleton had; Relative does not apply. Returns true when the pose was written. A
/// clip without bone tracks stops the player and writes nothing.
[[nodiscard]] bool StepSkeletalAnimationSequencerUVE(AnimationSequencerComponentUVE& player,
                                                  const Asset::AnimationClipAssetUVE& clip, float deltaSeconds,
                                                  Skeleton3DComponentUVE& skeleton,
                                                  const AnimationDriverComponentUVE& mixer = {});

/// The events of `events` the playhead passes going from `beforeSeconds` to `afterSeconds` in a clip
/// of `durationSeconds`: forwards or backwards, across a loop's wrap when `wrapped`. An event exactly
/// at the start is passed only when `includeStart` (the very first step). In passing order.
[[nodiscard]] std::vector<std::string> CollectPassedAnimationEventsUVE(
    const std::vector<Asset::AnimationAssetEventUVE>& events, double beforeSeconds, double afterSeconds,
    double durationSeconds, bool forward, bool wrapped, bool includeStart);

/// The bone whose ground travel is root motion: `boneName` when it names a bone with a track, else
/// (empty name) the first bone, parents first, whose track moves over 1 cm across the ground.
/// nullopt when there is none. With a root motion mode on, StepSkeletalAnimationSequencerUVE keeps
/// that bone over its first frame's ground position and reports its travel in rootMotionDelta.
[[nodiscard]] std::optional<std::size_t> ResolveRootMotionBoneUVE(const Skeleton3DComponentUVE& skeleton,
                                                                  const Asset::AnimationClipAssetUVE& clip,
                                                                  std::string_view boneName);

/// Poses `skeleton` at `timeSeconds` of a skeletal clip without a player: what scrubbing shows.
/// Each bone takes its track, else its rest pose, through the mixer's channel masks; with a root
/// motion mode on, the root motion bone stays over its first frame's ground position. Returns false,
/// leaving the skeleton alone, when the clip has no bone tracks or the skeleton no bones.
bool PoseSkeletonAtTimeUVE(const Asset::AnimationClipAssetUVE& clip, double timeSeconds,
                           Skeleton3DComponentUVE& skeleton, const AnimationDriverComponentUVE& mixer = {});

/// The clip's pose at `timeSeconds`: linear position and scale, spherical rotation between the two
/// samples around it, clamped to the first and last. The clip must have at least one sample.
[[nodiscard]] Core::TransformPoseUVE SampleAnimationClipAssetUVE(const Asset::AnimationClipAssetUVE& clip,
                                                                double timeSeconds) noexcept;

/// One track's pose at `timeSeconds`: linear position and scale, spherical rotation, clamped to the
/// first and last sample. `samples` must not be empty.
[[nodiscard]] Core::TransformPoseUVE SampleAnimationTrackUVE(const std::vector<Asset::AnimationAssetSampleUVE>& samples,
                                                             double timeSeconds) noexcept;

/// Inertialization's fade: 1 at `progress` 0, 0 at 1, with zero speed and acceleration at both ends.
[[nodiscard]] float AnimationInertialDecayUVE(float progress) noexcept;

/// Writes the chosen channels of `pose` into `target`. Rotation is normalized and mirrored into the
/// stored Euler angles, so the Inspector shows what is on screen.
void WriteAnimatedPoseUVE(const Core::TransformPoseUVE& pose, bool position, bool rotation, bool scale,
                          TransformComponentUVE& target) noexcept;

} // namespace UVE::Scene
