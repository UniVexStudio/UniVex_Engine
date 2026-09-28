// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <optional>
#include <string>
#include <vector>

#include "uve/asset/animation_clip_asset_uve.h"

namespace UVE::Editor {

/// The Timeline's editing operations on a clip, as pure functions so they are testable without a
/// window. A key is one sample of one track: every channel of a sample moves together, because a
/// sample stores the whole pose.
///
/// Tracks are named: a bone track by its bone, the clip's own node track by the empty string.

/// One key: a track and the time of its sample.
struct ClipKeyUVE final {
    std::string track;
    double timeSeconds = 0.0;
    [[nodiscard]] bool operator==(const ClipKeyUVE&) const = default;
};

/// Keys copied from a clip, with times relative to the earliest one.
struct ClipKeyClipboardUVE final {
    struct EntryUVE final {
        std::string track;
        double offsetSeconds = 0.0;
        Asset::AnimationAssetPoseUVE pose;
    };
    std::vector<EntryUVE> entries;
    [[nodiscard]] bool IsEmptyUVE() const noexcept { return entries.empty(); }
};

/// Two times name the same key when they are closer than this.
inline constexpr double kClipKeyTimeToleranceUVE = 1.0e-4;

/// The samples of `track`, or null when the clip has no such track.
[[nodiscard]] std::vector<Asset::AnimationAssetSampleUVE>* FindClipTrackSamplesUVE(Asset::AnimationClipAssetUVE& clip,
                                                                                 const std::string& track);
[[nodiscard]] const std::vector<Asset::AnimationAssetSampleUVE>* FindClipTrackSamplesUVE(
    const Asset::AnimationClipAssetUVE& clip, const std::string& track);

/// Removes the keys. A track keeps at least its last remaining sample, so it never becomes empty.
/// Returns how many samples were removed.
std::size_t DeleteClipKeysUVE(Asset::AnimationClipAssetUVE& clip, const std::vector<ClipKeyUVE>& keys);

/// Moves the keys by `deltaSeconds`, each landing on the nearest frame of `frameRate` inside the
/// clip. A key landing on another key of its track replaces it. Returns the keys where they landed.
std::vector<ClipKeyUVE> MoveClipKeysUVE(Asset::AnimationClipAssetUVE& clip, const std::vector<ClipKeyUVE>& keys,
                                        double deltaSeconds, double frameRate);

/// Copies the keys' poses.
[[nodiscard]] ClipKeyClipboardUVE CopyClipKeysUVE(const Asset::AnimationClipAssetUVE& clip,
                                                  const std::vector<ClipKeyUVE>& keys);

/// Pastes a clipboard with its earliest key at `atSeconds` (snapped to `frameRate`), replacing keys
/// already there. Keys past the clip's end are dropped. Returns the pasted keys.
std::vector<ClipKeyUVE> PasteClipKeysUVE(Asset::AnimationClipAssetUVE& clip, const ClipKeyClipboardUVE& clipboard,
                                         double atSeconds, double frameRate);

/// Adds a key to `track` at `atSeconds` (snapped to `frameRate`) holding the pose the track has
/// there now, so the motion does not change until the key is edited. Returns false when the track
/// does not exist or already has a key there.
bool InsertClipKeyUVE(Asset::AnimationClipAssetUVE& clip, const std::string& track, double atSeconds,
                      double frameRate);

/// One number of a key, as a curve shows it: `channel` 0 position (metres), 1 rotation (XYZ Euler
/// degrees), 2 scale; `axis` 0 x, 1 y, 2 z.
[[nodiscard]] float GetClipPoseComponentUVE(const Asset::AnimationAssetPoseUVE& pose, int channel, int axis);

/// Sets that number on the key of `track` at `timeSeconds`. A rotation is rebuilt from its Euler
/// angles with the one angle changed. Returns false when there is no such key or the value is not
/// finite.
bool SetClipKeyComponentUVE(Asset::AnimationClipAssetUVE& clip, const std::string& track, double timeSeconds,
                            int channel, int axis, float value);

// ---- Events ------------------------------------------------------------------------------------
// A clip's events stay sorted by time; every function returns where the event ended up.

/// Adds an event at `atSeconds` (snapped to `frameRate`) named "event", "event_2", ... - a name no
/// other event of the clip has. Returns its index.
std::size_t AddClipEventUVE(Asset::AnimationClipAssetUVE& clip, double atSeconds, double frameRate);

/// Moves event `index` to `toSeconds` (snapped, inside the clip). Returns its new index, or nullopt
/// for a bad index.
std::optional<std::size_t> MoveClipEventUVE(Asset::AnimationClipAssetUVE& clip, std::size_t index, double toSeconds,
                                            double frameRate);

/// Renames event `index`. False for a bad index or a name that is empty or longer than 128 bytes.
bool RenameClipEventUVE(Asset::AnimationClipAssetUVE& clip, std::size_t index, const std::string& name);

/// Removes event `index`. False for a bad index.
bool RemoveClipEventUVE(Asset::AnimationClipAssetUVE& clip, std::size_t index);

} // namespace UVE::Editor
