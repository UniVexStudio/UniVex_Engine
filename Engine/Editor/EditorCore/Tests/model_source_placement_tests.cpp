// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include <gtest/gtest.h>

#include <cstdint>

#include "editor.h"

namespace UVE::Editor {
namespace {

/// A source the project refresh read: a rigged character (geometry and motion).
[[nodiscard]] EditorModelSourceInfo CharacterSource() {
    EditorModelSourceInfo info;
    info.rigged = true;
    info.hasSkeleton = true;
    info.animationOnly = false;
    info.animationCount = 3U;
    return info;
}

/// A skinless download: bones and takes, nothing to draw.
[[nodiscard]] EditorModelSourceInfo AnimationOnlySource() {
    EditorModelSourceInfo info;
    info.rigged = false;
    info.hasSkeleton = true;
    info.animationOnly = true;
    info.animationCount = 1U;
    return info;
}

/// A static source: the refresh read it, and it reports neither geometry nor motion (an OBJ).
[[nodiscard]] EditorModelSourceInfo StaticSource() {
    return EditorModelSourceInfo{};
}

TEST(ModelSourcePlacementTest, AConvertedMeshIsPlacedWithItsSkeletonWhateverElseTheFileHolds) {
    // A 30 MB Mixamo "With Skin" download is this row: geometry, bones and takes in one file. The
    // mesh is what gets drawn, so it wins over every other reading of the file.
    const EditorModelSourceInfo character = CharacterSource();
    EXPECT_EQ(DecideModelSourcePlacement(true, &character, false), ModelSourcePlacement::PlaceModel);
    // A failure recorded against an older attempt cannot outvote a mesh that is on disk now.
    EXPECT_EQ(DecideModelSourcePlacement(true, &character, true), ModelSourcePlacement::PlaceModel);
    EXPECT_EQ(DecideModelSourcePlacement(true, nullptr, false), ModelSourcePlacement::PlaceModel);
}

TEST(ModelSourcePlacementTest, ASourceWithNoGeometryIsPlacedAsBonesAndClips) {
    // The skinless download, which is what "animation only" means and what it is for.
    const EditorModelSourceInfo skinless = AnimationOnlySource();
    EXPECT_EQ(DecideModelSourcePlacement(false, &skinless, false), ModelSourcePlacement::PlaceAnimationOnly);
}

TEST(ModelSourcePlacementTest, AGeometrySourceWhoseMeshIsNotWrittenYetWaitsInsteadOfBecomingBones) {
    // The reported bug, as one row: the bones are readable, the converted mesh is not there yet,
    // and the only honest answer is "not yet". Building the skeleton here is what left an Armature
    // in the scene that never gained a mesh.
    const EditorModelSourceInfo character = CharacterSource();
    EXPECT_EQ(DecideModelSourcePlacement(false, &character, false), ModelSourcePlacement::WaitForImport);
    // Nor may an unread source be guessed at as animation-only.
    EXPECT_EQ(DecideModelSourcePlacement(false, nullptr, false), ModelSourcePlacement::WaitForImport);
    // A static source waiting on its first import is the same wait, not a failure.
    const EditorModelSourceInfo staticSource = StaticSource();
    EXPECT_EQ(DecideModelSourcePlacement(false, &staticSource, false), ModelSourcePlacement::WaitForImport);
}

TEST(ModelSourcePlacementTest, AFailedImportIsReportedAsFailedAndNotAsStillImporting) {
    const EditorModelSourceInfo character = CharacterSource();
    EXPECT_EQ(DecideModelSourcePlacement(false, &character, true), ModelSourcePlacement::ImportFailed);
}

TEST(ModelSourcePlacementTest, EveryPlacementIsReachedAndNoneFallsThrough) {
    // The table is small enough to enumerate, and a decision that silently returns a default would
    // put an object in the scene without anyone choosing its kind.
    const EditorModelSourceInfo character = CharacterSource();
    const EditorModelSourceInfo skinless = AnimationOnlySource();
    for (const EditorModelSourceInfo* source : {&character, &skinless, static_cast<const EditorModelSourceInfo*>(nullptr)}) {
        for (const bool converted : {true, false}) {
            for (const bool failed : {true, false}) {
                const ModelSourcePlacement decision = DecideModelSourcePlacement(converted, source, failed);
                EXPECT_TRUE(decision == ModelSourcePlacement::PlaceModel ||
                            decision == ModelSourcePlacement::PlaceAnimationOnly ||
                            decision == ModelSourcePlacement::WaitForImport ||
                            decision == ModelSourcePlacement::ImportFailed);
                // Only a finished mesh, or a source with no geometry at all, may create objects.
                const bool builds = decision == ModelSourcePlacement::PlaceModel ||
                                    decision == ModelSourcePlacement::PlaceAnimationOnly;
                EXPECT_EQ(builds, converted || (source != nullptr && source->animationOnly));
            }
        }
    }
}

} // namespace
} // namespace UVE::Editor
