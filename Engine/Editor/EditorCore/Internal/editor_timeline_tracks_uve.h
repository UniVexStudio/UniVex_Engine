// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

// The Timeline's track list and its blend-shape preview, kept out of the panel's ImGui code.
//
// Two rules the dope sheet lives by are worth more than a screenshot: WHICH rows the tab shows for
// a clip, and WHICH weights a playing clip puts on WHICH object. Both are pure decisions over asset
// data and scene facts, so both live here - testable headlessly, without a window, a renderer or an
// ImGui context - and the panel is left with what a panel should own: turning the rows into pixels.

#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "uve/asset/animation_clip_asset_uve.h"
#include "uve/component/entity_uve.h"
#include "uve/component/morph_target_component_uve.h"

namespace UVE::Editor {

/// What one row of the dope sheet's track column stands for.
enum class TimelineRowKindUVE : std::uint8_t {
    /// The event row: always present, so the first event has somewhere to be added.
    Events,
    /// The clip's own object track (`clip.samples`), shown as "Transform".
    Transform,
    /// One bone track, shown at its hierarchy depth.
    Bone,
    /// One blend-shape track: a single row of keys, with no channels under it (a weight vector has
    /// no position, rotation or scale to open into).
    Morph,
    /// The Transform row opened into one of its three channels.
    TransformChannel,
    /// A bone row opened into one of its three channels.
    BoneChannel,
};

/// One row, ready to be drawn: the text in the track column and everything selection and opening
/// need to refer back to the clip.
struct TimelineRowUVE final {
    TimelineRowKindUVE kind = TimelineRowKindUVE::Bone;
    /// Text shown in the track column: a bone or node name, "Transform", or a channel's name.
    std::string label;
    /// Identity the row is selected and opened by: the bone name, the blend-shape node name, or
    /// "Transform" for the clip's object track. A channel row carries its parent's identity, so the
    /// three channels of one track answer to one name.
    std::string track;
    /// Index into `clip.bones` or `clip.morphWeights`; on a channel row, its parent track's index.
    /// Meaningless for Events and Transform, which have no list to index.
    std::size_t trackIndex = 0U;
    /// 0 for a track row, 1 for a channel row: the track column's indent.
    int depth = 0;
    /// 0 position, 1 rotation, 2 scale on a channel row; -1 otherwise.
    int channel = -1;
};

/// The skeleton the clip is shown on, as the row list needs it.
struct TimelineSkeletonUVE final {
    /// False when the object the clip is previewed on has no Skeleton3D at all: the clip's own bone
    /// order is then used, flat, and every bone track gets a row. A skeleton that IS present but has
    /// no bones shows no bone rows - the clip is being previewed against an empty rig, and inventing
    /// a flat list would hide that.
    bool present = false;

    struct BoneUVE final {
        std::string name;
        /// Distance from the root of the hierarchy, which is what indents the row.
        int depth = 0;
    };

    /// In hierarchy order - parents before children - so the rows read as the rig is built.
    std::vector<BoneUVE> bones;
};

/// The rows the dope sheet shows, top to bottom:
///
///   Events
///   Transform                                    (when the clip has an object track)
///   each bone that carries a track               (skeleton order, or the clip's order)
///   its three channel rows, when it is opened
///   one row per blend-shape track                (clip order)
///
/// Blend-shape rows come after the bones on purpose: a facial clip and a body clip are authored
/// separately, and a rig with 90 bones would push every shape off the bottom of the tab if the
/// shapes led. They keep the clip's own order so a re-import does not reshuffle the list.
///
/// `filter` matches a track's own label, case-insensitively; a channel row inherits its parent's
/// match - "Position" is not a useful search term. The Transform row is never filtered: it is the
/// clip's own motion, and hiding it would make the tab look empty.
///
/// Pure: same clip, skeleton, filter and open set give the same rows, in the same order.
[[nodiscard]] std::vector<TimelineRowUVE> BuildTimelineRowsUVE(const Asset::AnimationClipAssetUVE& clip,
                                                               const TimelineSkeletonUVE& skeleton,
                                                               std::string_view filter,
                                                               std::span<const std::string> expandedTracks);

/// One object the blend-shape preview drives, with the weights it should hold while the clip plays.
///
/// The weights are the component's own name-addressed entries - every shape the mesh carries, named
/// from the mesh - rather than a bare float array: the runtime component is addressed by name, and
/// resolving the names once per frame here keeps the write itself a plain assignment.
struct TimelineMorphWriteUVE final {
    Scene::EntityUVE entity = Scene::kInvalidEntityUVE;
    std::vector<Scene::MorphTargetWeightUVE> weights;
};

/// The scene as the blend-shape preview needs to see it: which object a track's node name refers to,
/// and the blend shapes that object's mesh carries, in the mesh's own order.
///
/// Deliberately two callables rather than a reference to a scene: the resolve below is then a pure
/// function over "names and weights", which is what makes it testable, and the panel supplies the
/// real scene (a subtree walk and a `.uvmodel` read) behind them.
struct TimelineMorphSceneUVE final {
    std::function<std::optional<Scene::EntityUVE>(std::string_view node)> entityForNode;
    std::function<std::span<const std::string>(Scene::EntityUVE entity)> targetNames;
};

/// What one frame of the blend-shape preview does.
struct TimelineMorphPreviewUVE final {
    /// One entry per object, in track order. Two tracks that name the same object appear twice, and
    /// the later one wins when they are applied in order - the same last-writer-wins rule the
    /// sample data itself follows.
    std::vector<TimelineMorphWriteUVE> writes;
    /// Tracks that could NOT be played: a track with no keys, a node name no object answers to, an
    /// object whose mesh has no blend shapes, or a key whose weight count differs from the mesh's
    /// shape count. Counted rather than logged once per frame; the tab shows the number, because a
    /// face that does not move should say why instead of looking like a broken preview.
    std::size_t unresolvedTracks = 0U;
};

/// Resolves a clip's blend-shape tracks onto the objects they name, at `timeSeconds`.
///
/// Skipped, and counted, rather than fatal: a clip and a scene that disagree is a normal authoring
/// state - the face mesh may not be in this scene yet, or a re-export may have added a shape - and
/// the rest of the tracks must still play. A track with no keys has no answer at all (the sampler
/// refuses rather than inventing zeroes), and a key whose weight count does not match the mesh's
/// shape count is driving some other mesh, so writing it would put the jaw's weight on the eyes.
[[nodiscard]] TimelineMorphPreviewUVE ResolveTimelineMorphPreviewUVE(const Asset::AnimationClipAssetUVE& clip,
                                                                     double timeSeconds,
                                                                     const TimelineMorphSceneUVE& scene);

/// True when sample `index` of a blend-shape track is a key worth showing: the first or last key of
/// the track, or one whose weight vector differs from a neighbour's. A baked facial clip stores a
/// key every frame; only the frames a shape actually moves on are keys an author can point at.
[[nodiscard]] bool ShowsMorphKeyUVE(const Asset::AnimationAssetMorphTrackUVE& track, std::size_t index);

} // namespace UVE::Editor
