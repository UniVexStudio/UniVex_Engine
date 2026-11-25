// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include <gtest/gtest.h>

#include <limits>
#include <string>

#include "auto_translate_component.h"
#include "object_metadata_component.h"
#include "process_component.h"
#include "thread_group_component.h"

namespace UVE::Scene::Tests {
namespace {

// The four properties every object has in common. Each resolves against its ancestors the way
// visibility and physics interpolation already do - no component means the parent's answer passes
// straight through - and each differs from the others in exactly one way that is worth locking.

TEST(ProcessComponentTest, ResolveTickMode_InheritPassesThroughAndAnythingElseOverrides) {
    // An object that never opted in must not break the chain for the objects beneath it.
    EXPECT_EQ(ResolveTickMode(TickMode::Inherit, TickMode::Always), TickMode::Always);
    EXPECT_EQ(ResolveTickMode(TickMode::Inherit, TickMode::Never),
              TickMode::Never);

    // At the top of a hierarchy, Inherit means the default rather than nothing.
    EXPECT_EQ(ResolveTickMode(TickMode::Inherit, TickMode::Inherit),
              TickMode::Running);

    // A pause menu under a Running parent has to be able to say Always and be believed. This is
    // the case that makes the mode worth authoring per entity at all, so it is locked explicitly.
    EXPECT_EQ(ResolveTickMode(TickMode::Always, TickMode::Running), TickMode::Always);
    EXPECT_EQ(ResolveTickMode(TickMode::Running, TickMode::Never),
              TickMode::Running);
}

TEST(ProcessComponentTest, IsTicking_AnswersEachModeAgainstBothSimulationStates) {
    EXPECT_TRUE(IsTicking(TickMode::Running, /*simulationPaused=*/false));
    EXPECT_FALSE(IsTicking(TickMode::Running, /*simulationPaused=*/true));

    EXPECT_FALSE(IsTicking(TickMode::PausedOnly, /*simulationPaused=*/false));
    EXPECT_TRUE(IsTicking(TickMode::PausedOnly, /*simulationPaused=*/true));

    EXPECT_TRUE(IsTicking(TickMode::Always, /*simulationPaused=*/false));
    EXPECT_TRUE(IsTicking(TickMode::Always, /*simulationPaused=*/true));

    EXPECT_FALSE(IsTicking(TickMode::Never, /*simulationPaused=*/false));
    EXPECT_FALSE(IsTicking(TickMode::Never, /*simulationPaused=*/true));

    // An unresolved mode behaves like the default rather than like an error, so an entity created
    // without a parent still behaves like everything around it.
    EXPECT_TRUE(IsTicking(TickMode::Inherit, /*simulationPaused=*/false));
    EXPECT_FALSE(IsTicking(TickMode::Inherit, /*simulationPaused=*/true));
}

TEST(ThreadGroupComponentTest, ResolveThreadGroupMode_MainThreadIsAConstraintAChildCannotEscape) {
    EXPECT_EQ(ResolveThreadGroupMode(ThreadGroupMode::Inherit, ThreadGroupMode::SubThread),
              ThreadGroupMode::SubThread);
    EXPECT_EQ(ResolveThreadGroupMode(ThreadGroupMode::Inherit, ThreadGroupMode::Inherit),
              ThreadGroupMode::MainThread);

    // The asymmetry with TickMode, locked deliberately: a parent pinned to the main thread is
    // pinned because of shared state the child is part of, so a child claiming SubThread underneath
    // it would turn a declared safety property into a race that only appears under load.
    EXPECT_EQ(ResolveThreadGroupMode(ThreadGroupMode::SubThread, ThreadGroupMode::MainThread),
              ThreadGroupMode::MainThread);

    // Under a parent that permits workers, the child's own choice stands - including opting back
    // onto the main thread.
    EXPECT_EQ(ResolveThreadGroupMode(ThreadGroupMode::SubThread, ThreadGroupMode::SubThread),
              ThreadGroupMode::SubThread);
    EXPECT_EQ(ResolveThreadGroupMode(ThreadGroupMode::MainThread, ThreadGroupMode::SubThread),
              ThreadGroupMode::MainThread);
}

TEST(AutoTranslateComponentTest, ResolveLocalizeMode_ALabelCanOptOutInsideATranslatedMenu) {
    EXPECT_EQ(ResolveLocalizeMode(LocalizeMode::Inherit, LocalizeMode::Literal),
              LocalizeMode::Literal);
    EXPECT_EQ(ResolveLocalizeMode(LocalizeMode::Inherit, LocalizeMode::Inherit),
              LocalizeMode::Localized);

    // A player's own name inside a translated menu must be able to say Disabled and be believed.
    EXPECT_EQ(ResolveLocalizeMode(LocalizeMode::Literal, LocalizeMode::Localized),
              LocalizeMode::Literal);
}

[[nodiscard]] Core::Variant Text(std::string text) {
    return Core::Variant::MakeText(Core::VariantType::String, std::move(text));
}

TEST(ObjectMetadataComponentTest, IsObjectMetadataComponentValid_RejectsWhatWouldMakeLookupAmbiguous) {
    ObjectMetadataComponent metadata;
    EXPECT_TRUE(IsObjectMetadataComponentValid(metadata)); // Empty is a legitimate state.

    metadata.entries = {{"door", Text("north")}, {"charges", Core::Variant::MakeInt(3)}};
    EXPECT_TRUE(IsObjectMetadataComponentValid(metadata));

    // A duplicate key would make the answer depend on which entry happens to come first, which
    // only breaks once someone reorders the list.
    metadata.entries.push_back({"door", Text("south")});
    EXPECT_FALSE(IsObjectMetadataComponentValid(metadata));

    metadata.entries = {{"", Text("value")}};
    EXPECT_FALSE(IsObjectMetadataComponentValid(metadata));

    metadata.entries = {{std::string(kMaximumObjectMetadataKeyBytes + 1U, 'k'), Text("value")}};
    EXPECT_FALSE(IsObjectMetadataComponentValid(metadata));

    // A non-finite float would save as JSON null and break the next load, so it never gets in.
    metadata.entries = {{"speed", Core::Variant::MakeFloat(std::numeric_limits<double>::infinity())}};
    EXPECT_FALSE(IsObjectMetadataComponentValid(metadata));

    metadata.entries.clear();
    for (std::size_t index = 0; index <= kMaximumObjectMetadataEntries; ++index) {
        metadata.entries.push_back({"key" + std::to_string(index), Text("value")});
    }
    EXPECT_FALSE(IsObjectMetadataComponentValid(metadata));
}

TEST(ObjectMetadataComponentTest, IsObjectMetadataComponentValid_StillAcceptsKeysSavedBeforeTheIdentifierRule) {
    // Metadata saved before keys had to be identifiers may hold "door north". Rejecting it on load
    // would fail the component and roll back the whole scene, so the loader only checks structure.
    ObjectMetadataComponent metadata;
    metadata.entries = {{"door north", Text("open")}};
    EXPECT_TRUE(IsObjectMetadataComponentValid(metadata));
    // New authoring is held to the identifier rule, with a reason the editor can show.
    EXPECT_EQ(ValidateObjectMetadataKey("door north", ObjectMetadataComponent{}),
              ObjectMetadataKeyIssue::InvalidCharacter);
}

TEST(ObjectMetadataComponentTest, ValidateObjectMetadataKey_ExplainsEveryRejection) {
    ObjectMetadataComponent existing;
    existing.entries = {{"charges", Core::Variant::MakeInt(3)}};

    EXPECT_EQ(ValidateObjectMetadataKey("spawn_offset_2", existing), ObjectMetadataKeyIssue::None);
    EXPECT_EQ(ValidateObjectMetadataKey("", existing), ObjectMetadataKeyIssue::Empty);
    EXPECT_EQ(ValidateObjectMetadataKey("2nd", existing), ObjectMetadataKeyIssue::StartsWithDigit);
    EXPECT_EQ(ValidateObjectMetadataKey("a-b", existing), ObjectMetadataKeyIssue::InvalidCharacter);
    EXPECT_EQ(ValidateObjectMetadataKey(std::string(kMaximumObjectMetadataKeyBytes + 1U, 'k'), existing),
              ObjectMetadataKeyIssue::TooLong);
    EXPECT_EQ(ValidateObjectMetadataKey("charges", existing), ObjectMetadataKeyIssue::Duplicate);
    // Renaming an entry to its own name is not a collision with itself.
    EXPECT_EQ(ValidateObjectMetadataKey("charges", existing, "charges"), ObjectMetadataKeyIssue::None);
    // Every rejection comes with a reason to show; acceptance comes with none.
    for (const ObjectMetadataKeyIssue issue :
         {ObjectMetadataKeyIssue::Empty, ObjectMetadataKeyIssue::TooLong, ObjectMetadataKeyIssue::InvalidCharacter,
          ObjectMetadataKeyIssue::StartsWithDigit, ObjectMetadataKeyIssue::Duplicate}) {
        EXPECT_FALSE(DescribeObjectMetadataKeyIssue(issue).empty());
    }
    EXPECT_TRUE(DescribeObjectMetadataKeyIssue(ObjectMetadataKeyIssue::None).empty());
}

TEST(ObjectMetadataComponentTest, FindObjectMetadata_DistinguishesAbsentFromEmpty) {
    ObjectMetadataComponent metadata;
    metadata.entries = {{"door", Text("north")}, {"note", Text("")}};

    ASSERT_NE(FindObjectMetadata(metadata, "door"), nullptr);
    EXPECT_EQ(*FindObjectMetadata(metadata, "door"), Text("north"));

    // An entry whose value is empty is a value that was authored, not a missing entry.
    ASSERT_NE(FindObjectMetadata(metadata, "note"), nullptr);
    EXPECT_TRUE(FindObjectMetadata(metadata, "note")->TryGet<std::string>()->empty());

    EXPECT_EQ(FindObjectMetadata(metadata, "missing"), nullptr);
}

} // namespace
} // namespace UVE::Scene::Tests
