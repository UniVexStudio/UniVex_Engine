// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "particle_render_bridge.h"
#include "render_queue.h"

#include <limits>

#include <gtest/gtest.h>

namespace UVE::Render::Tests {

TEST(ParticleRenderBridgeTest, ValidateParticleRenderSnapshot_AcceptsValidAndTruncatedBatches) {
    ParticleRenderSnapshot valid;
    valid.sourceParticleCount = 1U;
    valid.items.push_back({Scene::Entity{2U, 1U}, Math::Vector3{1.0F, 2.0F, 3.0F}, 1.0F, 3.0F, 7U});
    EXPECT_TRUE(ValidateParticleRenderSnapshot(valid));
    valid.truncated = true;
    valid.sourceParticleCount = 3U;
    EXPECT_TRUE(ValidateParticleRenderSnapshot(valid, 1U));
}

TEST(ParticleRenderBridgeTest, ValidateParticleRenderSnapshot_RejectsInconsistentOrUnsafeBatches) {
    ParticleRenderSnapshot snapshot;
    snapshot.sourceParticleCount = 1U;
    snapshot.items.push_back({Scene::Entity{2U, 1U}, {}, 1.0F, 0.0F, 1U});
    EXPECT_FALSE(ValidateParticleRenderSnapshot(snapshot, 0U));
    snapshot.items[0].sequence = 0U;
    EXPECT_FALSE(ValidateParticleRenderSnapshot(snapshot));
    snapshot.items[0].sequence = 1U;
    snapshot.items[0].entity = Scene::kInvalidEntity;
    EXPECT_FALSE(ValidateParticleRenderSnapshot(snapshot));
    snapshot.items[0].entity = Scene::Entity{2U, 1U};
    snapshot.sourceParticleCount = 0U;
    EXPECT_FALSE(ValidateParticleRenderSnapshot(snapshot));
}

TEST(ParticleRenderBridgeTest, Extract_CopiesEnabledParticlesWithStableOrdering) {
    Scene::ParticleRuntime runtime;
    const Scene::Entity first{2U, 1U};
    const Scene::Entity second{1U, 1U};
    ASSERT_TRUE(runtime.Attach(first, Scene::ParticleEmitterComponent{4U}));
    ASSERT_TRUE(runtime.Attach(second, Scene::ParticleEmitterComponent{4U}));
    ASSERT_TRUE(runtime.EmitDetailed(
                       first, Scene::ParticleEmission{1U, Math::Vector3{0.0F, 0.0F, 3.0F}, {}, 2.0F})
                    .IsAccepted());
    ASSERT_TRUE(runtime.EmitDetailed(
                       second, Scene::ParticleEmission{1U, Math::Vector3{0.0F, 0.0F, 5.0F}, {}, 2.0F})
                    .IsAccepted());

    const ParticleRenderSnapshot snapshot = ParticleRenderBridge::Extract(runtime);
    ASSERT_EQ(snapshot.sourceParticleCount, 2U);
    ASSERT_EQ(snapshot.items.size(), 2U);
    EXPECT_FALSE(snapshot.truncated);
    EXPECT_EQ(snapshot.items[0].entity, second);
    EXPECT_EQ(snapshot.items[0].sortDepth, 5.0F);
    EXPECT_EQ(snapshot.items[1].entity, first);
    EXPECT_EQ(snapshot.items[1].sortDepth, 3.0F);
}

TEST(ParticleRenderBridgeTest, Extract_FiltersDisabledAndHonorsHardCap) {
    Scene::ParticleRuntime runtime;
    const Scene::Entity enabled{3U, 1U};
    const Scene::Entity disabled{4U, 1U};
    ASSERT_TRUE(runtime.Attach(enabled, Scene::ParticleEmitterComponent{4U}));
    ASSERT_TRUE(runtime.Attach(disabled, Scene::ParticleEmitterComponent{4U}));
    ASSERT_TRUE(runtime.EmitDetailed(enabled, Scene::ParticleEmission{3U, {}, {}, 2.0F}).IsAccepted());
    ASSERT_TRUE(runtime.EmitDetailed(disabled, Scene::ParticleEmission{2U, {}, {}, 2.0F}).IsAccepted());
    ASSERT_EQ(runtime.SetEnabledDetailed(disabled, false).code, Scene::ParticleRuntimeCode::Applied);

    const ParticleRenderSnapshot snapshot = ParticleRenderBridge::Extract(runtime, 2U);
    EXPECT_EQ(snapshot.sourceParticleCount, 3U);
    EXPECT_EQ(snapshot.items.size(), 2U);
    EXPECT_TRUE(snapshot.truncated);
    for (const ParticleRenderItem& item : snapshot.items) {
        EXPECT_EQ(item.entity, enabled);
    }
}

TEST(ParticleRenderBridgeTest, Extract_ZeroCapDoesNotCopyState) {
    Scene::ParticleRuntime runtime;
    ASSERT_TRUE(runtime.Attach({5U, 1U}, Scene::ParticleEmitterComponent{2U}));
    ASSERT_TRUE(runtime.EmitDetailed({5U, 1U}, Scene::ParticleEmission{1U, {}, {}, 2.0F}).IsAccepted());

    const ParticleRenderSnapshot snapshot = ParticleRenderBridge::Extract(runtime, 0U);
    EXPECT_TRUE(snapshot.items.empty());
    EXPECT_EQ(snapshot.sourceParticleCount, 0U);
    EXPECT_TRUE(snapshot.truncated);
}

TEST(ParticleRenderBridgeTest, RenderQueue_RejectsInvalidSnapshotWithoutMutatingExistingItems) {
    RenderQueue queue;
    queue.particleItems = {{{8U, 1U}, {}, 1.0F, 2.0F, 1U}};
    const RenderQueue before = queue;

    ParticleRenderSnapshot invalidSnapshot;
    invalidSnapshot.sourceParticleCount = 1U;
    invalidSnapshot.items = {{{9U, 1U}, Math::Vector3{std::numeric_limits<float>::infinity(), 0.0F, 0.0F},
                               1.0F, 2.0F, 2U}};

    queue.AppendParticleSnapshot(invalidSnapshot);

    EXPECT_EQ(queue.particleItems, before.particleItems);
    EXPECT_TRUE(queue.particleItemsTruncated);
}

TEST(ParticleRenderBridgeTest, RenderQueue_AppendsCopiesAndSortsParticleItemsDeterministically) {
    RenderQueue queue;
    ParticleRenderSnapshot snapshot;
    snapshot.sourceParticleCount = 3U;
    snapshot.items = {
        {{9U, 1U}, {}, 1.0F, 1.0F, 2U},
        {{8U, 1U}, {}, 1.0F, 3.0F, 1U},
        {{8U, 1U}, {}, 1.0F, 3.0F, 2U},
    };
    snapshot.truncated = true;

    queue.AppendParticleSnapshot(snapshot);
    queue.Sort();
    ASSERT_EQ(queue.particleItems.size(), 3U);
    EXPECT_TRUE(queue.particleItemsTruncated);
    EXPECT_EQ(queue.particleItems[0].sequence, 1U);
    EXPECT_EQ(queue.particleItems[1].sequence, 2U);
    EXPECT_EQ(queue.particleItems[2].entity, (Scene::Entity{9U, 1U}));
}

} // namespace UVE::Render::Tests

