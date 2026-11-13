// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
#pragma once

#include "asset_guid.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace UVE::Asset {

/// An Animation Montage (`.uvmontage`), the standard montage: a timed piece of animation a character
/// plays on top of what it is doing - an attack, a reload, an emote.
///
///   time ->   0 ........................................................ length
///   Sections  [Start      ][Attack1          ][Attack2        ][End    ]
///   Slot      [ clip A           ][ clip B (x2)        ][ clip C        ]
///   Notifies        ^footstep          ^hit                  ^hit
///   States             [---- hit_window ----]
///   Curves    ~~~~~~~~ (a value over time the game can read)
///
/// The clips of the slot play one after another; their lengths make the montage's length.
/// Sections cut that time into named parts; each says which section follows it (or that it loops,
/// or that the montage ends there), which is how combos are made. Notifies and notify states are
/// raised to the character's script as the playhead passes them.
inline constexpr std::size_t kMaximumMontageSegmentsUVE = 64U;
inline constexpr std::size_t kMaximumMontageSectionsUVE = 64U;
inline constexpr std::size_t kMaximumMontageNotifiesUVE = 512U;
inline constexpr std::size_t kMaximumMontageCurvesUVE = 32U;
inline constexpr std::size_t kMaximumMontageCurveKeysUVE = 1024U;
inline constexpr std::size_t kMaximumMontageNameBytesUVE = 128U;

/// One clip in the slot track: the part of it [clipStart, clipEnd] (seconds of the clip; clipEnd
/// <= 0 means the clip's end), played `loops` times at `rate`.
struct MontageSegmentUVE final {
    AssetGuidUVE clip;
    double clipStart = 0.0;
    double clipEnd = 0.0;
    double rate = 1.0;
    std::uint32_t loops = 1U;
    /// The clip's own length, kept so the montage's timing reads without loading every clip.
    double clipLength = 0.0;
    [[nodiscard]] bool operator==(const MontageSegmentUVE&) const = default;
};

/// A named part of the montage starting at `time`. `next` names the section that follows: empty
/// ends the montage at this section's end, its own name loops it.
struct MontageSectionUVE final {
    std::string name;
    double time = 0.0;
    std::string next;
    [[nodiscard]] bool operator==(const MontageSectionUVE&) const = default;
};

/// A moment (`notify`) on a notify track.
struct MontageNotifyUVE final {
    std::string name;
    double time = 0.0;
    std::uint32_t track = 0U;
    /// How likely it is to be raised each time it is passed (1: always).
    float triggerChance = 1.0F;
    /// Raised only while the montage weighs at least this much (0: also while blending in or out).
    float minWeight = 0.0F;
    /// Its colour on the timeline, 0xRRGGBB (0: the track's own).
    std::uint32_t colour = 0U;
    [[nodiscard]] bool operator==(const MontageNotifyUVE&) const = default;
};

/// A window [begin, end] on a notify track: its begin and end are both raised.
struct MontageNotifyStateUVE final {
    std::string name;
    double begin = 0.0;
    double end = 0.0;
    std::uint32_t track = 0U;
    [[nodiscard]] bool operator==(const MontageNotifyStateUVE&) const = default;
};

struct MontageCurveKeyUVE final {
    double time = 0.0;
    float value = 0.0F;
    [[nodiscard]] bool operator==(const MontageCurveKeyUVE&) const = default;
};

/// A value over time (linear between keys) the game reads with `montage.curve(name)`.
struct MontageCurveUVE final {
    std::string name;
    std::vector<MontageCurveKeyUVE> keys;
    [[nodiscard]] bool operator==(const MontageCurveUVE&) const = default;
};

enum class MontageRootMotionUVE : std::uint8_t {
    Off = 0,
    InPlace = 1,
    ApplyToTarget = 2,
};

/// The shape of a blend in or out over its time.
enum class MontageBlendCurveUVE : std::uint8_t {
    Linear = 0,
    EaseIn,
    EaseOut,
    EaseInOut,
};

/// The weight `curve` gives at `t` (0..1, clamped) of the way through a blend.
[[nodiscard]] float MontageBlendCurveWeightUVE(MontageBlendCurveUVE curve, double t) noexcept;

struct AnimationMontageAssetUVE final {
    /// The slot it plays on: a Slot node of that name in the character's Anim Graph, or the whole
    /// body when the graph has none.
    std::string slot = "DefaultSlot";
    /// The entity the Montage Editor previews it on (editor only; the game plays it on whoever asks).
    AssetGuidUVE previewEntity;
    std::vector<MontageSegmentUVE> segments;
    std::vector<MontageSectionUVE> sections;
    std::vector<MontageNotifyUVE> notifies;
    std::vector<MontageNotifyStateUVE> notifyStates;
    std::vector<MontageCurveUVE> curves;
    /// How many notify tracks the editor shows (at least every track a notify uses).
    std::uint32_t notifyTracks = 1U;
    /// The notify tracks' names, by track (a missing or empty one shows as "Notify <n>").
    std::vector<std::string> notifyTrackNames;
    double blendInSeconds = 0.25;
    double blendOutSeconds = 0.25;
    MontageBlendCurveUVE blendInCurve = MontageBlendCurveUVE::Linear;
    MontageBlendCurveUVE blendOutCurve = MontageBlendCurveUVE::Linear;
    /// When the blend-out starts, in seconds before the end; below 0 it starts blendOutSeconds before.
    double blendOutTriggerSeconds = -1.0;
    /// Off: the montage holds its last pose until stopped.
    bool autoBlendOut = true;
    double rateScale = 1.0;
    MontageRootMotionUVE rootMotion = MontageRootMotionUVE::Off;
    [[nodiscard]] bool operator==(const AnimationMontageAssetUVE&) const = default;
};

[[nodiscard]] bool IsAnimationMontageAssetValidUVE(const AnimationMontageAssetUVE& montage) noexcept;

/// Loads a `.uvmontage` envelope (AssetKindUVE::AnimationMontage, JSON payload). Output is
/// published only after the payload parses and validates.
[[nodiscard]] bool LoadAnimationMontageAssetUVE(const std::filesystem::path& path, AnimationMontageAssetUVE& outMontage);

/// Saves a validated montage; fields at their default are left out of the file.
[[nodiscard]] bool SaveAnimationMontageAssetUVE(const AnimationMontageAssetUVE& montage, const std::filesystem::path& path);

} // namespace UVE::Asset
