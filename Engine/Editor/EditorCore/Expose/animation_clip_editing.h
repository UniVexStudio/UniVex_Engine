// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <optional>
#include <string>
#include <vector>

#include "animation_clip_asset.h"

namespace UVE::Editor {

/// The Timeline's editing operations on a clip, as pure functions so they are testable without a
/// window. A key is one sample of one track: every channel of a sample moves together, because a
/// sample stores the whole pose.
///
/// Tracks are named: a bone track by its bone, the clip's own object track by the empty string,
/// and another object's track (`clip.objects`) by this prefix and the object's name.
inline constexpr char kClipObjectTrackPrefix = '@';

/// One key: a track and the time of its sample.
struct ClipKey final {
    std::string track;
    double timeSeconds = 0.0;
    [[nodiscard]] bool operator==(const ClipKey&) const = default;
};

/// Keys copied from a clip, with times relative to the earliest one.
struct ClipKeyClipboard final {
    struct Entry final {
        std::string track;
        double offsetSeconds = 0.0;
        Asset::AnimationAssetPose pose;
    };
    std::vector<Entry> entries;
    [[nodiscard]] bool IsEmpty() const noexcept { return entries.empty(); }
};

/// Two times name the same key when they are closer than this.
inline constexpr double kClipKeyTimeTolerance = 1.0e-4;

/// The samples of `track`, or null when the clip has no such track.
[[nodiscard]] std::vector<Asset::AnimationAssetSample>* FindClipTrackSamples(Asset::AnimationClipAsset& clip,
                                                                                 const std::string& track);
[[nodiscard]] const std::vector<Asset::AnimationAssetSample>* FindClipTrackSamples(
    const Asset::AnimationClipAsset& clip, const std::string& track);

/// Removes the keys. A track keeps at least its last remaining sample, so it never becomes empty.
/// Returns how many samples were removed.
std::size_t DeleteClipKeys(Asset::AnimationClipAsset& clip, const std::vector<ClipKey>& keys);

/// Moves the keys by `deltaSeconds`, each landing on the nearest frame of `frameRate` inside the
/// clip. A key landing on another key of its track replaces it. Returns the keys where they landed.
std::vector<ClipKey> MoveClipKeys(Asset::AnimationClipAsset& clip, const std::vector<ClipKey>& keys,
                                        double deltaSeconds, double frameRate);

/// Copies the keys' poses.
[[nodiscard]] ClipKeyClipboard CopyClipKeys(const Asset::AnimationClipAsset& clip,
                                                  const std::vector<ClipKey>& keys);

/// Pastes a clipboard with its earliest key at `atSeconds` (snapped to `frameRate`), replacing keys
/// already there. Keys past the clip's end are dropped. Returns the pasted keys.
std::vector<ClipKey> PasteClipKeys(Asset::AnimationClipAsset& clip, const ClipKeyClipboard& clipboard,
                                         double atSeconds, double frameRate);

/// Adds a key to `track` at `atSeconds` (snapped to `frameRate`) holding the pose the track has
/// there now, so the motion does not change until the key is edited. Returns false when the track
/// does not exist or already has a key there.
bool InsertClipKey(Asset::AnimationClipAsset& clip, const std::string& track, double atSeconds,
                      double frameRate);

/// One number of a key, as a curve shows it: `channel` 0 position (metres), 1 rotation (XYZ Euler
/// degrees), 2 scale; `axis` 0 x, 1 y, 2 z.
[[nodiscard]] float GetClipPoseComponent(const Asset::AnimationAssetPose& pose, int channel, int axis);

/// Sets that number on the key of `track` at `timeSeconds`. A rotation is rebuilt from its Euler
/// angles with the one angle changed. Returns false when there is no such key or the value is not
/// finite.
bool SetClipKeyComponent(Asset::AnimationClipAsset& clip, const std::string& track, double timeSeconds,
                            int channel, int axis, float value);

// ---- Events ------------------------------------------------------------------------------------
// A clip's events stay sorted by time; every function returns where the event ended up.

/// Adds an event at `atSeconds` (snapped to `frameRate`) named "event", "event_2", ... - a name no
/// other event of the clip has. Returns its index.
std::size_t AddClipEvent(Asset::AnimationClipAsset& clip, double atSeconds, double frameRate);

/// Moves event `index` to `toSeconds` (snapped, inside the clip). Returns its new index, or nullopt
/// for a bad index.
std::optional<std::size_t> MoveClipEvent(Asset::AnimationClipAsset& clip, std::size_t index, double toSeconds,
                                            double frameRate);

/// Renames event `index`. False for a bad index or a name that is empty or longer than 128 bytes.
bool RenameClipEvent(Asset::AnimationClipAsset& clip, std::size_t index, const std::string& name);

/// Removes event `index`. False for a bad index.
bool RemoveClipEvent(Asset::AnimationClipAsset& clip, std::size_t index);

} // namespace UVE::Editor
