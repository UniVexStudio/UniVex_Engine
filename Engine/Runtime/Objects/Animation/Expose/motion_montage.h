// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
#pragma once

#include "animation_graph.h"
#include "motion_montage_asset.h"
#include "montage_sequencer_component.h"
#include "skeleton_3d.h"

#include <string>
#include <string_view>
#include <vector>

namespace UVE::Scene {

// ---- Montage playback ----------------------------------------------------------------------------
// Pure functions over a montage asset and its playback, so the whole behaviour is testable without
// an engine. EngineCoreUVE::SyncAnimationUVE drives them for every MontageSequencer; the Motion Montage Editor
// drives them for its preview.

struct MontageEventUVE final {
    enum class KindUVE : std::uint8_t { Notify, NotifyBegin, NotifyEnd, Ended };
    KindUVE kind = KindUVE::Notify;
    std::string name;
    [[nodiscard]] bool operator==(const MontageEventUVE&) const = default;
};

/// Starts at `section` (empty: the first section, or the start). Blends in over the montage's
/// Blend In. False, leaving the playback alone, for a montage with nothing to play or a section it
/// does not have.
bool StartMontageUVE(const Asset::MotionMontageAssetUVE& montage, MontagePlaybackUVE& playback,
                     std::string_view section = {});

/// Moves the playhead to the start of `section` without blending (combos jump mid-swing).
bool JumpToMontageSectionUVE(const Asset::MotionMontageAssetUVE& montage, MontagePlaybackUVE& playback,
                             std::string_view section);

/// Starts the blend-out (or stops at once with no Blend Out). Ended is raised when it finishes.
void StopMontageUVE(const Asset::MotionMontageAssetUVE& montage, MontagePlaybackUVE& playback,
                    std::vector<MontageEventUVE>& events);

/// Advances by `deltaSeconds` (times the montage's Rate Scale): through each section into the one it
/// leads to (or itself, looping), blending out near the end of the last section (Auto Blend Out),
/// and collecting the notifies passed, the notify states entered and left, and Ended.
void StepMontageUVE(const Asset::MotionMontageAssetUVE& montage, MontagePlaybackUVE& playback, double deltaSeconds,
                    std::vector<MontageEventUVE>& events);

/// The montage's own pose at `time` for `skeleton` (one entry per bone, bones without a track at
/// rest); empty when the clip there is not loaded or has no bones.
[[nodiscard]] std::vector<SkeletonBonePoseUVE> PoseMontageUVE(const Asset::MotionMontageAssetUVE& montage, double time,
                                                              const AnimationClipResolverUVE& clipFor,
                                                              const Skeleton3DComponentUVE& skeleton);

/// Blends `target` (one pose per bone) over what `skeleton` holds now (its pose, else its rest).
bool BlendBonePosesUVE(Skeleton3DComponentUVE& skeleton, const std::vector<SkeletonBonePoseUVE>& target, float weight);

/// Blends the montage's pose at `time` over what `skeleton` holds now (its pose, else its rest), by
/// `weight`. False, leaving the skeleton alone, when the clip there is not loaded or has no bones.
bool BlendMontagePoseUVE(const Asset::MotionMontageAssetUVE& montage, double time, float weight,
                         const AnimationClipResolverUVE& clipFor, Skeleton3DComponentUVE& skeleton);

} // namespace UVE::Scene
