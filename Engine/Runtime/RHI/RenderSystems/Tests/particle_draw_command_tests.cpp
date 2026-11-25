// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "particle_draw_command.h"

#include <gtest/gtest.h>

#include <limits>

namespace UVE::Render::Tests {

TEST(ParticleDrawRecorderTest, Record_CopiesSortedQueueItemsWithoutMutatingQueue) {
    RenderQueue queue;
    queue.particleItems = {
        {{7U, 1U}, Math::Vector3{1.0F, 2.0F, 3.0F}, 2.0F, 3.0F, 4U},
        {{8U, 1U}, Math::Vector3{4.0F, 5.0F, 6.0F}, 1.0F, 1.0F, 5U},
    };
    const RenderQueue before = queue;

    const ParticleDrawRecording recording = ParticleDrawRecorder::Record(queue);
    ASSERT_EQ(recording.sourceItemCount, 2U);
    ASSERT_EQ(recording.commands.size(), 2U);
    EXPECT_FALSE(recording.truncated);
    EXPECT_EQ(recording.commands[0].entity, queue.particleItems[0].entity);
    EXPECT_EQ(recording.commands[0].position, queue.particleItems[0].position);
    EXPECT_EQ(recording.commands[0].sequence, 4U);
    EXPECT_EQ(queue.particleItems, before.particleItems);
    EXPECT_EQ(queue.particleItemsTruncated, before.particleItemsTruncated);
}

TEST(ParticleDrawRecorderTest, IsValidParticleDrawCommand_AcceptsFiniteLiveCommand) {
    const ParticleDrawCommand command{{1U, 1U}, Math::Vector3{1.0F, 2.0F, 3.0F}, 0.5F, -2.0F, 7U};
    EXPECT_TRUE(IsValidParticleDrawCommand(command));
}

TEST(ParticleDrawRecorderTest, IsValidParticleDrawCommand_RejectsUnsafeFields) {
    EXPECT_FALSE(IsValidParticleDrawCommand(
        ParticleDrawCommand{{1U, 1U}, Math::Vector3{}, -0.1F, 0.0F, 1U}));
    EXPECT_FALSE(IsValidParticleDrawCommand(
        ParticleDrawCommand{{1U, 1U}, Math::Vector3{std::numeric_limits<float>::quiet_NaN(), 0.0F, 0.0F},
                               1.0F, 0.0F, 1U}));
    EXPECT_FALSE(IsValidParticleDrawCommand(
        ParticleDrawCommand{{1U, 1U}, Math::Vector3{}, 1.0F, 0.0F, 0U}));
}

TEST(ParticleDrawRecorderTest, PlanParticleGpuUpload_ComputesFixedStrideBytes) {
    ParticleDrawRecording recording;
    recording.sourceItemCount = 2U;
    recording.commands = {
        ParticleDrawCommand{{1U, 1U}, {}, 1.0F, 0.0F, 1U},
        ParticleDrawCommand{{2U, 1U}, {}, 0.5F, 1.0F, 2U},
    };

    ParticleGpuUploadPlan plan;
    ASSERT_TRUE(PlanParticleGpuUpload(recording, 128U, plan));
    EXPECT_EQ(plan.commandCount, 2U);
    EXPECT_EQ(plan.byteCount, 2U * ParticleGpuUploadPlan::kParticleStrideBytes);
    EXPECT_FALSE(plan.truncated);
}

TEST(ParticleDrawRecorderTest, PlanParticleGpuUpload_PreservesTruncationAndRejectsUnsafeBatches) {
    ParticleDrawRecording recording;
    recording.sourceItemCount = 3U;
    recording.truncated = true;
    recording.commands = {ParticleDrawCommand{{1U, 1U}, {}, 1.0F, 0.0F, 1U}};
    ParticleGpuUploadPlan plan;
    ASSERT_TRUE(PlanParticleGpuUpload(recording, ParticleGpuUploadPlan::kParticleStrideBytes, plan));
    EXPECT_EQ(plan.commandCount, 1U);
    EXPECT_TRUE(plan.truncated);

    plan.commandCount = 9U;
    plan.byteCount = 10U;
    plan.truncated = false;
    EXPECT_FALSE(PlanParticleGpuUpload(recording, 1U, plan));
    EXPECT_EQ(plan.commandCount, 9U);
    EXPECT_EQ(plan.byteCount, 10U);
    EXPECT_FALSE(plan.truncated);

    recording.truncated = false;
    EXPECT_FALSE(PlanParticleGpuUpload(recording, 128U, plan));
}

TEST(ParticleDrawRecorderTest, Record_DropsInvalidPublicQueueItemsBeforeGpuHandoff) {
    RenderQueue queue;
    queue.particleItems = {
        {{1U, 1U}, Math::Vector3{1.0F, 2.0F, 3.0F}, 1.0F, 3.0F, 1U},
        {{2U, 1U}, Math::Vector3{std::numeric_limits<float>::quiet_NaN(), 0.0F, 0.0F}, 1.0F, 2.0F, 2U},
        {{3U, 1U}, Math::Vector3{4.0F, 5.0F, 6.0F}, 1.0F, 1.0F, 0U},
    };

    const ParticleDrawRecording recording = ParticleDrawRecorder::Record(queue);

    EXPECT_EQ(recording.sourceItemCount, 3U);
    ASSERT_EQ(recording.commands.size(), 1U);
    EXPECT_TRUE(recording.truncated);
    EXPECT_TRUE(IsValidParticleDrawCommand(recording.commands.front()));
    EXPECT_EQ(recording.commands.front().sequence, 1U);
}

TEST(ParticleDrawRecorderTest, Record_HonorsHardCapAndQueueTruncationFact) {
    RenderQueue queue;
    queue.particleItems = {
        {{1U, 1U}, {}, 1.0F, 3.0F, 1U},
        {{2U, 1U}, {}, 1.0F, 2.0F, 2U},
        {{3U, 1U}, {}, 1.0F, 1.0F, 3U},
    };
    queue.particleItemsTruncated = true;

    const ParticleDrawRecording recording = ParticleDrawRecorder::Record(queue, 2U);
    EXPECT_EQ(recording.sourceItemCount, 3U);
    EXPECT_EQ(recording.commands.size(), 2U);
    EXPECT_TRUE(recording.truncated);
    EXPECT_EQ(recording.commands[1].sequence, 2U);
}

TEST(ParticleDrawRecorderTest, Record_ZeroCapProducesNoCommandsAndPreservesSourceCount) {
    RenderQueue queue;
    queue.particleItems.push_back({{4U, 1U}, {}, 1.0F, 0.0F, 1U});

    const ParticleDrawRecording recording = ParticleDrawRecorder::Record(queue, 0U);
    EXPECT_EQ(recording.sourceItemCount, 1U);
    EXPECT_TRUE(recording.commands.empty());
    EXPECT_TRUE(recording.truncated);
}

TEST(ParticleDrawRecorderTest, RecordInto_MatchesWrapperAndReusesCommandCapacity) {
    RenderQueue queue;
    queue.particleItems = {
        {{1U, 1U}, Math::Vector3{1.0F, 2.0F, 3.0F}, 1.0F, 3.0F, 1U},
        {{2U, 1U}, Math::Vector3{4.0F, 5.0F, 6.0F}, 0.5F, 2.0F, 2U},
        {{3U, 1U}, Math::Vector3{7.0F, 8.0F, 9.0F}, 0.25F, 1.0F, 3U},
    };

    ParticleDrawRecording recording;
    recording.commands.reserve(8U);
    const ParticleDrawRecording expected = ParticleDrawRecorder::Record(queue, 2U);

    ParticleDrawRecorder::RecordInto(queue, 2U, recording);

    EXPECT_EQ(recording.sourceItemCount, expected.sourceItemCount);
    EXPECT_EQ(recording.commands, expected.commands);
    EXPECT_EQ(recording.truncated, expected.truncated);
    const std::size_t retainedCapacity = recording.commands.capacity();
    EXPECT_GE(retainedCapacity, 8U);

    ParticleDrawRecorder::RecordInto(queue, queue.particleItems.size(), recording);
    EXPECT_EQ(recording.commands.size(), queue.particleItems.size());
    EXPECT_FALSE(recording.truncated);
    EXPECT_EQ(recording.commands.capacity(), retainedCapacity);

    ParticleDrawRecorder::RecordInto(queue, 0U, recording);
    EXPECT_EQ(recording.sourceItemCount, queue.particleItems.size());
    EXPECT_TRUE(recording.commands.empty());
    EXPECT_TRUE(recording.truncated);
    EXPECT_EQ(recording.commands.capacity(), retainedCapacity);
}

} // namespace UVE::Render::Tests
