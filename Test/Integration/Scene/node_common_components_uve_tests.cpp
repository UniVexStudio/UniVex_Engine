// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include <gtest/gtest.h>

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

TEST(NodeMetadataComponentUVETest, IsNodeMetadataComponentValidUVE_RejectsWhatWouldMakeLookupAmbiguous) {
    NodeMetadataComponentUVE metadata;
    EXPECT_TRUE(IsNodeMetadataComponentValidUVE(metadata)); // Empty is a legitimate state.

    metadata.entries = {{"door", "north"}, {"charges", "3"}};
    EXPECT_TRUE(IsNodeMetadataComponentValidUVE(metadata));

    // A duplicate key would make the answer depend on which entry happens to come first, which
    // only breaks once someone reorders the list.
    metadata.entries.push_back({"door", "south"});
    EXPECT_FALSE(IsNodeMetadataComponentValidUVE(metadata));

    metadata.entries = {{"", "value"}};
    EXPECT_FALSE(IsNodeMetadataComponentValidUVE(metadata));

    metadata.entries = {{std::string(kMaximumNodeMetadataKeyBytesUVE + 1U, 'k'), "value"}};
    EXPECT_FALSE(IsNodeMetadataComponentValidUVE(metadata));

    metadata.entries = {{"key", std::string(kMaximumNodeMetadataValueBytesUVE + 1U, 'v')}};
    EXPECT_FALSE(IsNodeMetadataComponentValidUVE(metadata));

    metadata.entries.clear();
    for (std::size_t index = 0; index <= kMaximumNodeMetadataEntriesUVE; ++index) {
        metadata.entries.push_back({"key" + std::to_string(index), "value"});
    }
    EXPECT_FALSE(IsNodeMetadataComponentValidUVE(metadata));
}

TEST(NodeMetadataComponentUVETest, FindNodeMetadataUVE_DistinguishesAbsentFromEmpty) {
    NodeMetadataComponentUVE metadata;
    metadata.entries = {{"door", "north"}, {"note", ""}};

    ASSERT_NE(FindNodeMetadataUVE(metadata, "door"), nullptr);
    EXPECT_EQ(*FindNodeMetadataUVE(metadata, "door"), "north");

    // An entry whose value is empty is a value that was authored, not a missing entry.
    ASSERT_NE(FindNodeMetadataUVE(metadata, "note"), nullptr);
    EXPECT_TRUE(FindNodeMetadataUVE(metadata, "note")->empty());

    EXPECT_EQ(FindNodeMetadataUVE(metadata, "missing"), nullptr);
}

} // namespace
} // namespace UVE::Scene::Tests
