// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
#pragma once

#include "motion_montage_asset.h"

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>

namespace UVE::Asset {

/// How long a montage with no clip yet lets notifies, states, sections and keys be laid out.
inline constexpr double kMontageEmptyTimelineSeconds = 1.0;

// What the Motion Montage Editor does to a montage, as pure operations on the asset (tested without an
// editor). Times are montage seconds unless a name says clip.

/// How long one segment plays: its clip range, looped, at its rate.
[[nodiscard]] double MontageSegmentDuration(const MontageSegment& segment) noexcept;
/// Where segment `index` starts on the montage's time.
[[nodiscard]] double MontageSegmentStart(const MotionMontageAsset& montage, std::size_t index) noexcept;
/// The montage's length: its segments end to end.
[[nodiscard]] double MontageLength(const MotionMontageAsset& montage) noexcept;

/// Which segment plays at montage time `time`, and where in its clip (clamped to the montage).
struct MontageClipTime final {
    std::size_t segment = 0U;
    double clipTime = 0.0;
};
[[nodiscard]] std::optional<MontageClipTime> MapMontageTime(const MotionMontageAsset& montage, double time) noexcept;

/// Puts `clip` (of length `clipLength`) in the slot track at `index` (past the end: last). Returns
/// its index. A montage with no section yet gets "Default" at 0.
std::size_t AddMontageSegment(MotionMontageAsset& montage, AssetGuid clip, double clipLength,
                                 std::size_t index = static_cast<std::size_t>(-1));
bool RemoveMontageSegment(MotionMontageAsset& montage, std::size_t index);
bool MoveMontageSegment(MotionMontageAsset& montage, std::size_t from, std::size_t to);

/// A section name not used yet: `base`, else "base 2", "base 3"...
[[nodiscard]] std::string MakeUniqueMontageSectionName(const MotionMontageAsset& montage, std::string_view base);
[[nodiscard]] MontageSection* FindMontageSection(MotionMontageAsset& montage, std::string_view name) noexcept;
[[nodiscard]] const MontageSection* FindMontageSection(const MotionMontageAsset& montage,
                                                             std::string_view name) noexcept;
/// Adds a section at `time` (kept in time order). Its next is empty; the section before it now
/// leads into it when it led nowhere new (it is the natural continuation). Returns its name.
std::string AddMontageSection(MotionMontageAsset& montage, std::string_view name, double time);
/// Renames a section and every next that names it. False for a missing or a taken name.
bool RenameMontageSection(MotionMontageAsset& montage, std::string_view from, std::string_view to);
/// Removes a section; nexts that named it now name what it led to.
bool RemoveMontageSection(MotionMontageAsset& montage, std::string_view name);
bool MoveMontageSection(MotionMontageAsset& montage, std::string_view name, double time);
/// `next`: another section, the section itself (loop), or empty (the montage ends after it).
bool SetMontageSectionNext(MotionMontageAsset& montage, std::string_view name, std::string_view next);
/// The section playing at `time`: the last one starting at or before it (nullptr when none).
[[nodiscard]] const MontageSection* MontageSectionAt(const MotionMontageAsset& montage, double time) noexcept;
/// Where section `name` ends: the next section's start in time order, else the montage's end.
[[nodiscard]] double MontageSectionEnd(const MotionMontageAsset& montage, std::string_view name) noexcept;

std::size_t AddMontageNotify(MotionMontageAsset& montage, std::string_view name, double time, std::uint32_t track);
std::size_t AddMontageNotifyState(MotionMontageAsset& montage, std::string_view name, double begin, double end,
                                     std::uint32_t track);
/// Removes notify track `track` with what is on it; the tracks below move up. False for the last track.
bool RemoveMontageNotifyTrack(MotionMontageAsset& montage, std::uint32_t track);
/// A notify track's name ("Notify <n>" when it has none), and setting it.
[[nodiscard]] std::string MontageNotifyTrackName(const MotionMontageAsset& montage, std::uint32_t track);
void SetMontageNotifyTrackName(MotionMontageAsset& montage, std::uint32_t track, std::string_view name);
/// Sets a curve key at `time` (replacing one within a millisecond), keys kept in time order.
void SetMontageCurveKey(MotionMontageAsset& montage, std::string_view curve, double time, float value);
/// A curve's value at `time`: linear between keys, clamped to the first and last; 0 with no keys.
[[nodiscard]] float EvaluateMontageCurve(const MotionMontageAsset& montage, std::string_view curve,
                                            double time) noexcept;

} // namespace UVE::Asset
