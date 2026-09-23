// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include <gtest/gtest.h>

#include <limits>
#include <string>

#include "uve/component/auto_translate_component_uve.h"
#include "uve/component/node_metadata_component_uve.h"
#include "uve/component/process_component_uve.h"
#include "uve/component/thread_group_component_uve.h"

namespace UVE::Scene::Tests {
namespace {

// The four properties every node has in common. Each resolves against its ancestors the way
// visibility and physics interpolation already do - no component means the parent's answer passes
// straight through - and each differs from the others in exactly one way that is worth locking.

TEST(ProcessComponentUVETest, ResolveProcessModeUVE_InheritPassesThroughAndAnythingElseOverrides) {
    // A node that never opted in must not break the chain for the nodes beneath it.
    EXPECT_EQ(ResolveProcessModeUVE(ProcessModeUVE::Inherit, ProcessModeUVE::Always), ProcessModeUVE::Always);
    EXPECT_EQ(ResolveProcessModeUVE(ProcessModeUVE::Inherit, ProcessModeUVE::Disabled),
              ProcessModeUVE::Disabled);

    // At the top of a hierarchy, Inherit means the default rather than nothing.
    EXPECT_EQ(ResolveProcessModeUVE(ProcessModeUVE::Inherit, ProcessModeUVE::Inherit),
              ProcessModeUVE::Pausable);

    // A pause menu under a Pausable parent has to be able to say Always and be believed. This is
    // the case that makes the mode worth authoring per entity at all, so it is locked explicitly.
    EXPECT_EQ(ResolveProcessModeUVE(ProcessModeUVE::Always, ProcessModeUVE::Pausable), ProcessModeUVE::Always);
    EXPECT_EQ(ResolveProcessModeUVE(ProcessModeUVE::Pausable, ProcessModeUVE::Disabled),
              ProcessModeUVE::Pausable);
}

TEST(ProcessComponentUVETest, IsProcessingUVE_AnswersEachModeAgainstBothSimulationStates) {
    EXPECT_TRUE(IsProcessingUVE(ProcessModeUVE::Pausable, /*simulationPaused=*/false));
    EXPECT_FALSE(IsProcessingUVE(ProcessModeUVE::Pausable, /*simulationPaused=*/true));

    EXPECT_FALSE(IsProcessingUVE(ProcessModeUVE::WhenPaused, /*simulationPaused=*/false));
    EXPECT_TRUE(IsProcessingUVE(ProcessModeUVE::WhenPaused, /*simulationPaused=*/true));

    EXPECT_TRUE(IsProcessingUVE(ProcessModeUVE::Always, /*simulationPaused=*/false));
    EXPECT_TRUE(IsProcessingUVE(ProcessModeUVE::Always, /*simulationPaused=*/true));

    EXPECT_FALSE(IsProcessingUVE(ProcessModeUVE::Disabled, /*simulationPaused=*/false));
    EXPECT_FALSE(IsProcessingUVE(ProcessModeUVE::Disabled, /*simulationPaused=*/true));

    // An unresolved mode behaves like the default rather than like an error, so an entity created
    // without a parent still behaves like everything around it.
    EXPECT_TRUE(IsProcessingUVE(ProcessModeUVE::Inherit, /*simulationPaused=*/false));
    EXPECT_FALSE(IsProcessingUVE(ProcessModeUVE::Inherit, /*simulationPaused=*/true));
}

TEST(ThreadGroupComponentUVETest, ResolveThreadGroupModeUVE_MainThreadIsAConstraintAChildCannotEscape) {
    EXPECT_EQ(ResolveThreadGroupModeUVE(ThreadGroupModeUVE::Inherit, ThreadGroupModeUVE::SubThread),
              ThreadGroupModeUVE::SubThread);
    EXPECT_EQ(ResolveThreadGroupModeUVE(ThreadGroupModeUVE::Inherit, ThreadGroupModeUVE::Inherit),
              ThreadGroupModeUVE::MainThread);

    // The asymmetry with ProcessModeUVE, locked deliberately: a parent pinned to the main thread is
    // pinned because of shared state the child is part of, so a child claiming SubThread underneath
    // it would turn a declared safety property into a race that only appears under load.
    EXPECT_EQ(ResolveThreadGroupModeUVE(ThreadGroupModeUVE::SubThread, ThreadGroupModeUVE::MainThread),
              ThreadGroupModeUVE::MainThread);

    // Under a parent that permits workers, the child's own choice stands - including opting back
    // onto the main thread.
    EXPECT_EQ(ResolveThreadGroupModeUVE(ThreadGroupModeUVE::SubThread, ThreadGroupModeUVE::SubThread),
              ThreadGroupModeUVE::SubThread);
    EXPECT_EQ(ResolveThreadGroupModeUVE(ThreadGroupModeUVE::MainThread, ThreadGroupModeUVE::SubThread),
              ThreadGroupModeUVE::MainThread);
}

TEST(AutoTranslateComponentUVETest, ResolveAutoTranslateModeUVE_ALabelCanOptOutInsideATranslatedMenu) {
    EXPECT_EQ(ResolveAutoTranslateModeUVE(AutoTranslateModeUVE::Inherit, AutoTranslateModeUVE::Disabled),
              AutoTranslateModeUVE::Disabled);
    EXPECT_EQ(ResolveAutoTranslateModeUVE(AutoTranslateModeUVE::Inherit, AutoTranslateModeUVE::Inherit),
              AutoTranslateModeUVE::Always);

    // A player's own name inside a translated menu must be able to say Disabled and be believed.
    EXPECT_EQ(ResolveAutoTranslateModeUVE(AutoTranslateModeUVE::Disabled, AutoTranslateModeUVE::Always),
              AutoTranslateModeUVE::Disabled);
}

[[nodiscard]] Core::VariantUVE TextUVE(std::string text) {
    return Core::VariantUVE::MakeTextUVE(Core::VariantTypeUVE::String, std::move(text));
}

TEST(NodeMetadataComponentUVETest, IsNodeMetadataComponentValidUVE_RejectsWhatWouldMakeLookupAmbiguous) {
    NodeMetadataComponentUVE metadata;
    EXPECT_TRUE(IsNodeMetadataComponentValidUVE(metadata)); // Empty is a legitimate state.

    metadata.entries = {{"door", TextUVE("north")}, {"charges", Core::VariantUVE::MakeIntUVE(3)}};
    EXPECT_TRUE(IsNodeMetadataComponentValidUVE(metadata));

    // A duplicate key would make the answer depend on which entry happens to come first, which
    // only breaks once someone reorders the list.
    metadata.entries.push_back({"door", TextUVE("south")});
    EXPECT_FALSE(IsNodeMetadataComponentValidUVE(metadata));

    metadata.entries = {{"", TextUVE("value")}};
    EXPECT_FALSE(IsNodeMetadataComponentValidUVE(metadata));

    metadata.entries = {{std::string(kMaximumNodeMetadataKeyBytesUVE + 1U, 'k'), TextUVE("value")}};
    EXPECT_FALSE(IsNodeMetadataComponentValidUVE(metadata));

    // A non-finite float would save as JSON null and break the next load, so it never gets in.
    metadata.entries = {{"speed", Core::VariantUVE::MakeFloatUVE(std::numeric_limits<double>::infinity())}};
    EXPECT_FALSE(IsNodeMetadataComponentValidUVE(metadata));

    metadata.entries.clear();
    for (std::size_t index = 0; index <= kMaximumNodeMetadataEntriesUVE; ++index) {
        metadata.entries.push_back({"key" + std::to_string(index), TextUVE("value")});
    }
    EXPECT_FALSE(IsNodeMetadataComponentValidUVE(metadata));
}

TEST(NodeMetadataComponentUVETest, IsNodeMetadataComponentValidUVE_StillAcceptsKeysSavedBeforeTheIdentifierRule) {
    // Metadata saved before keys had to be identifiers may hold "door north". Rejecting it on load
    // would fail the component and roll back the whole scene, so the loader only checks structure.
    NodeMetadataComponentUVE metadata;
    metadata.entries = {{"door north", TextUVE("open")}};
    EXPECT_TRUE(IsNodeMetadataComponentValidUVE(metadata));
    // New authoring is held to the identifier rule, with a reason the editor can show.
    EXPECT_EQ(ValidateNodeMetadataKeyUVE("door north", NodeMetadataComponentUVE{}),
              NodeMetadataKeyIssueUVE::InvalidCharacter);
}

TEST(NodeMetadataComponentUVETest, ValidateNodeMetadataKeyUVE_ExplainsEveryRejection) {
    NodeMetadataComponentUVE existing;
    existing.entries = {{"charges", Core::VariantUVE::MakeIntUVE(3)}};

    EXPECT_EQ(ValidateNodeMetadataKeyUVE("spawn_offset_2", existing), NodeMetadataKeyIssueUVE::None);
    EXPECT_EQ(ValidateNodeMetadataKeyUVE("", existing), NodeMetadataKeyIssueUVE::Empty);
    EXPECT_EQ(ValidateNodeMetadataKeyUVE("2nd", existing), NodeMetadataKeyIssueUVE::StartsWithDigit);
    EXPECT_EQ(ValidateNodeMetadataKeyUVE("a-b", existing), NodeMetadataKeyIssueUVE::InvalidCharacter);
    EXPECT_EQ(ValidateNodeMetadataKeyUVE(std::string(kMaximumNodeMetadataKeyBytesUVE + 1U, 'k'), existing),
              NodeMetadataKeyIssueUVE::TooLong);
    EXPECT_EQ(ValidateNodeMetadataKeyUVE("charges", existing), NodeMetadataKeyIssueUVE::Duplicate);
    // Renaming an entry to its own name is not a collision with itself.
    EXPECT_EQ(ValidateNodeMetadataKeyUVE("charges", existing, "charges"), NodeMetadataKeyIssueUVE::None);
    // Every rejection comes with a reason to show; acceptance comes with none.
    for (const NodeMetadataKeyIssueUVE issue :
         {NodeMetadataKeyIssueUVE::Empty, NodeMetadataKeyIssueUVE::TooLong, NodeMetadataKeyIssueUVE::InvalidCharacter,
          NodeMetadataKeyIssueUVE::StartsWithDigit, NodeMetadataKeyIssueUVE::Duplicate}) {
        EXPECT_FALSE(DescribeNodeMetadataKeyIssueUVE(issue).empty());
    }
    EXPECT_TRUE(DescribeNodeMetadataKeyIssueUVE(NodeMetadataKeyIssueUVE::None).empty());
}

TEST(NodeMetadataComponentUVETest, FindNodeMetadataUVE_DistinguishesAbsentFromEmpty) {
    NodeMetadataComponentUVE metadata;
    metadata.entries = {{"door", TextUVE("north")}, {"note", TextUVE("")}};

    ASSERT_NE(FindNodeMetadataUVE(metadata, "door"), nullptr);
    EXPECT_EQ(*FindNodeMetadataUVE(metadata, "door"), TextUVE("north"));

    // An entry whose value is empty is a value that was authored, not a missing entry.
    ASSERT_NE(FindNodeMetadataUVE(metadata, "note"), nullptr);
    EXPECT_TRUE(FindNodeMetadataUVE(metadata, "note")->TryGetUVE<std::string>()->empty());

    EXPECT_EQ(FindNodeMetadataUVE(metadata, "missing"), nullptr);
}

} // namespace
} // namespace UVE::Scene::Tests
