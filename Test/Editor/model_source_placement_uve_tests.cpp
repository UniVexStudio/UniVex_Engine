// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include <gtest/gtest.h>

#include <cstdint>

#include "uve/editor/editor_uve.h"

namespace UVE::Editor {
namespace {

/// A source the project refresh read: a rigged character (geometry and motion).
[[nodiscard]] EditorModelSourceInfoUVE CharacterSourceUVE() {
    EditorModelSourceInfoUVE info;
    info.rigged = true;
    info.hasSkeleton = true;
    info.animationOnly = false;
    info.animationCount = 3U;
    return info;
}

/// A skinless download: bones and takes, nothing to draw.
[[nodiscard]] EditorModelSourceInfoUVE AnimationOnlySourceUVE() {
    EditorModelSourceInfoUVE info;
    info.rigged = false;
    info.hasSkeleton = true;
    info.animationOnly = true;
    info.animationCount = 1U;
    return info;
}

/// A static source: the refresh read it, and it reports neither geometry nor motion (an OBJ).
[[nodiscard]] EditorModelSourceInfoUVE StaticSourceUVE() {
    return EditorModelSourceInfoUVE{};
}

TEST(ModelSourcePlacementUVETest, AConvertedMeshIsPlacedWithItsSkeletonWhateverElseTheFileHolds) {
    // A 30 MB Mixamo "With Skin" download is this row: geometry, bones and takes in one file. The
    // mesh is what gets drawn, so it wins over every other reading of the file.
    const EditorModelSourceInfoUVE character = CharacterSourceUVE();
    EXPECT_EQ(DecideModelSourcePlacementUVE(true, &character, false), ModelSourcePlacementUVE::PlaceModel);
    // A failure recorded against an older attempt cannot outvote a mesh that is on disk now.
    EXPECT_EQ(DecideModelSourcePlacementUVE(true, &character, true), ModelSourcePlacementUVE::PlaceModel);
    EXPECT_EQ(DecideModelSourcePlacementUVE(true, nullptr, false), ModelSourcePlacementUVE::PlaceModel);
}

TEST(ModelSourcePlacementUVETest, ASourceWithNoGeometryIsPlacedAsBonesAndClips) {
    // The skinless download, which is what "animation only" means and what it is for.
    const EditorModelSourceInfoUVE skinless = AnimationOnlySourceUVE();
    EXPECT_EQ(DecideModelSourcePlacementUVE(false, &skinless, false), ModelSourcePlacementUVE::PlaceAnimationOnly);
}

TEST(ModelSourcePlacementUVETest, AGeometrySourceWhoseMeshIsNotWrittenYetWaitsInsteadOfBecomingBones) {
    // The reported bug, as one row: the bones are readable, the converted mesh is not there yet,
    // and the only honest answer is "not yet". Building the skeleton here is what left an Armature
    // in the scene that never gained a mesh.
    const EditorModelSourceInfoUVE character = CharacterSourceUVE();
    EXPECT_EQ(DecideModelSourcePlacementUVE(false, &character, false), ModelSourcePlacementUVE::WaitForImport);
    // Nor may an unread source be guessed at as animation-only.
    EXPECT_EQ(DecideModelSourcePlacementUVE(false, nullptr, false), ModelSourcePlacementUVE::WaitForImport);
    // A static source waiting on its first import is the same wait, not a failure.
    const EditorModelSourceInfoUVE staticSource = StaticSourceUVE();
    EXPECT_EQ(DecideModelSourcePlacementUVE(false, &staticSource, false), ModelSourcePlacementUVE::WaitForImport);
}

TEST(ModelSourcePlacementUVETest, AFailedImportIsReportedAsFailedAndNotAsStillImporting) {
    const EditorModelSourceInfoUVE character = CharacterSourceUVE();
    EXPECT_EQ(DecideModelSourcePlacementUVE(false, &character, true), ModelSourcePlacementUVE::ImportFailed);
}

TEST(ModelSourcePlacementUVETest, EveryPlacementIsReachedAndNoneFallsThrough) {
    // The table is small enough to enumerate, and a decision that silently returns a default would
    // put an object in the scene without anyone choosing its kind.
    const EditorModelSourceInfoUVE character = CharacterSourceUVE();
    const EditorModelSourceInfoUVE skinless = AnimationOnlySourceUVE();
    for (const EditorModelSourceInfoUVE* source : {&character, &skinless, static_cast<const EditorModelSourceInfoUVE*>(nullptr)}) {
        for (const bool converted : {true, false}) {
            for (const bool failed : {true, false}) {
                const ModelSourcePlacementUVE decision = DecideModelSourcePlacementUVE(converted, source, failed);
                EXPECT_TRUE(decision == ModelSourcePlacementUVE::PlaceModel ||
                            decision == ModelSourcePlacementUVE::PlaceAnimationOnly ||
                            decision == ModelSourcePlacementUVE::WaitForImport ||
                            decision == ModelSourcePlacementUVE::ImportFailed);
                // Only a finished mesh, or a source with no geometry at all, may create objects.
                const bool builds = decision == ModelSourcePlacementUVE::PlaceModel ||
                                    decision == ModelSourcePlacementUVE::PlaceAnimationOnly;
                EXPECT_EQ(builds, converted || (source != nullptr && source->animationOnly));
            }
        }
    }
}

} // namespace
} // namespace UVE::Editor
