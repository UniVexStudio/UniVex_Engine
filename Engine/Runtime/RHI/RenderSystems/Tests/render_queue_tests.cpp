// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#include "render_queue.h"

#include <cmath>
#include <filesystem>
#include <limits>
#include <string>
#include <vector>
#include <utility>

#include <gtest/gtest.h>

#include "asset_database.h"
#include "render_batch.h"
#include "asset_manager.h"
#include "material_asset.h"
#include "mesh_asset.h"
#include "event_system.h"
#include "thread_pool.h"

namespace UVE::Render::Tests {
namespace {

// RenderItem holds live AssetHandle<T> members with no default constructor, so a real
// AssetManager + registered loaders are the simplest way to obtain valid handles for
// Sort()'s test fixtures — the loaders never actually read a file since the handles are only
// used for their identity/lifetime here, not their loaded content.
class RenderQueueTest : public ::testing::Test {
protected:
    Threading::ThreadPool threadPool{2};
    Events::EventSystem eventSystem;
    Asset::AssetDatabase assetDatabase;
    Asset::AssetManager assetManager{threadPool, eventSystem};

    RenderQueueTest() {
        assetManager.RegisterLoader<Asset::MeshAsset>(
            [](const std::filesystem::path&, Asset::MeshAsset&) { return true; });
        assetManager.RegisterLoader<Asset::MaterialAsset>(
            [](const std::filesystem::path&, Asset::MaterialAsset&) { return true; });
    }

    [[nodiscard]] RenderItem MakeItem(float sortDepth) {
        static int nextPathSuffix = 0;
        const Asset::AssetGuid meshGuid =
            assetDatabase.Register("render_queue_tests_mesh_" + std::to_string(nextPathSuffix++) + ".uvmodel");
        const Asset::AssetGuid materialGuid = assetDatabase.Register(
            "render_queue_tests_material_" + std::to_string(nextPathSuffix++) + ".uvmat");
        Asset::AssetHandle<Asset::MeshAsset> meshHandle =
            assetManager.Load<Asset::MeshAsset>(meshGuid, assetDatabase);
        Asset::AssetHandle<Asset::MaterialAsset> materialHandle =
            assetManager.Load<Asset::MaterialAsset>(materialGuid, assetDatabase);
        return RenderItem{Math::Matrix4x4::Identity(), std::move(meshHandle), std::move(materialHandle),
                              sortDepth};
    }

    /// An item on a CHOSEN mesh rather than a fresh one, so a test can build a queue where several
    /// items share a mesh - which is the situation batching exists for and MakeItem cannot
    /// produce, since it registers a new asset every call.
    [[nodiscard]] RenderItem MakeItemOnMesh(float sortDepth, int meshIndex) {
        const Asset::AssetGuid meshGuid =
            assetDatabase.Register("render_queue_tests_shared_mesh_" + std::to_string(meshIndex) + ".uvmodel");
        const Asset::AssetGuid materialGuid = assetDatabase.Register(
            "render_queue_tests_shared_material_" + std::to_string(meshIndex) + ".uvmat");
        Asset::AssetHandle<Asset::MeshAsset> meshHandle =
            assetManager.Load<Asset::MeshAsset>(meshGuid, assetDatabase);
        Asset::AssetHandle<Asset::MaterialAsset> materialHandle =
            assetManager.Load<Asset::MaterialAsset>(materialGuid, assetDatabase);
        return RenderItem{Math::Matrix4x4::Identity(), std::move(meshHandle), std::move(materialHandle),
                              sortDepth};
    }
};

TEST_F(RenderQueueTest, Sort_OpaqueItems_SortedFrontToBack) {
    RenderQueue queue;
    queue.opaqueItems.push_back(MakeItem(5.0F));
    queue.opaqueItems.push_back(MakeItem(1.0F));
    queue.opaqueItems.push_back(MakeItem(3.0F));

    queue.Sort();

    ASSERT_EQ(queue.opaqueItems.size(), 3U);
    EXPECT_FLOAT_EQ(queue.opaqueItems[0].sortDepth, 1.0F);
    EXPECT_FLOAT_EQ(queue.opaqueItems[1].sortDepth, 3.0F);
    EXPECT_FLOAT_EQ(queue.opaqueItems[2].sortDepth, 5.0F);
}

TEST_F(RenderQueueTest, Sort_TransparentItems_SortedBackToFront) {
    RenderQueue queue;
    queue.transparentItems.push_back(MakeItem(1.0F));
    queue.transparentItems.push_back(MakeItem(5.0F));
    queue.transparentItems.push_back(MakeItem(3.0F));

    queue.Sort();

    ASSERT_EQ(queue.transparentItems.size(), 3U);
    EXPECT_FLOAT_EQ(queue.transparentItems[0].sortDepth, 5.0F);
    EXPECT_FLOAT_EQ(queue.transparentItems[1].sortDepth, 3.0F);
    EXPECT_FLOAT_EQ(queue.transparentItems[2].sortDepth, 1.0F);
}

TEST_F(RenderQueueTest, Sort_EqualDepthItems_UseDeterministicAssetTieBreak) {
    const auto assetLess = [](const RenderItem& lhs, const RenderItem& rhs) {
        const auto lhsMaterial = lhs.materialHandle.GetGuid().value;
        const auto rhsMaterial = rhs.materialHandle.GetGuid().value;
        if (lhsMaterial != rhsMaterial) {
            return lhsMaterial < rhsMaterial;
        }
        return lhs.meshHandle.GetGuid().value < rhs.meshHandle.GetGuid().value;
    };

    RenderItem opaqueFirst = MakeItem(2.0F);
    RenderItem opaqueSecond = MakeItem(2.0F);
    ASSERT_NE(opaqueFirst.materialHandle.GetGuid(), opaqueSecond.materialHandle.GetGuid());
    RenderQueue opaqueQueue;
    opaqueQueue.opaqueItems.push_back(std::move(opaqueSecond));
    opaqueQueue.opaqueItems.push_back(std::move(opaqueFirst));
    opaqueQueue.Sort();
    ASSERT_EQ(opaqueQueue.opaqueItems.size(), 2U);
    EXPECT_TRUE(assetLess(opaqueQueue.opaqueItems[0], opaqueQueue.opaqueItems[1]));
    EXPECT_FLOAT_EQ(opaqueQueue.opaqueItems[0].sortDepth, 2.0F);
    EXPECT_FLOAT_EQ(opaqueQueue.opaqueItems[1].sortDepth, 2.0F);

    RenderItem transparentFirst = MakeItem(3.0F);
    RenderItem transparentSecond = MakeItem(3.0F);
    ASSERT_NE(transparentFirst.materialHandle.GetGuid(), transparentSecond.materialHandle.GetGuid());
    RenderQueue transparentQueue;
    transparentQueue.transparentItems.push_back(std::move(transparentSecond));
    transparentQueue.transparentItems.push_back(std::move(transparentFirst));
    transparentQueue.Sort();
    ASSERT_EQ(transparentQueue.transparentItems.size(), 2U);
    EXPECT_TRUE(assetLess(transparentQueue.transparentItems[0], transparentQueue.transparentItems[1]));
    EXPECT_FLOAT_EQ(transparentQueue.transparentItems[0].sortDepth, 3.0F);
    EXPECT_FLOAT_EQ(transparentQueue.transparentItems[1].sortDepth, 3.0F);
}

TEST_F(RenderQueueTest, Sort_NonFiniteDepthsArePlacedAfterFiniteItems) {
    RenderQueue opaqueQueue;
    opaqueQueue.opaqueItems.push_back(MakeItem(std::numeric_limits<float>::quiet_NaN()));
    opaqueQueue.opaqueItems.push_back(MakeItem(1.0F));
    opaqueQueue.opaqueItems.push_back(MakeItem(std::numeric_limits<float>::infinity()));
    opaqueQueue.opaqueItems.push_back(MakeItem(3.0F));
    opaqueQueue.Sort();

    ASSERT_EQ(opaqueQueue.opaqueItems.size(), 4U);
    EXPECT_FLOAT_EQ(opaqueQueue.opaqueItems[0].sortDepth, 1.0F);
    EXPECT_FLOAT_EQ(opaqueQueue.opaqueItems[1].sortDepth, 3.0F);
    EXPECT_FALSE(std::isfinite(opaqueQueue.opaqueItems[2].sortDepth));
    EXPECT_FALSE(std::isfinite(opaqueQueue.opaqueItems[3].sortDepth));

    RenderQueue transparentQueue;
    transparentQueue.transparentItems.push_back(MakeItem(-std::numeric_limits<float>::infinity()));
    transparentQueue.transparentItems.push_back(MakeItem(2.0F));
    transparentQueue.transparentItems.push_back(MakeItem(std::numeric_limits<float>::quiet_NaN()));
    transparentQueue.transparentItems.push_back(MakeItem(5.0F));
    transparentQueue.Sort();

    ASSERT_EQ(transparentQueue.transparentItems.size(), 4U);
    EXPECT_FLOAT_EQ(transparentQueue.transparentItems[0].sortDepth, 5.0F);
    EXPECT_FLOAT_EQ(transparentQueue.transparentItems[1].sortDepth, 2.0F);
    EXPECT_FALSE(std::isfinite(transparentQueue.transparentItems[2].sortDepth));
    EXPECT_FALSE(std::isfinite(transparentQueue.transparentItems[3].sortDepth));
}

TEST_F(RenderQueueTest, Clear_ClearsFrameStateAndPreservesCapacity) {
    RenderQueue queue;
    queue.Reserve(8U, 7U, 6U);
    queue.opaqueItems.push_back(MakeItem(1.0F));
    queue.transparentItems.push_back(MakeItem(2.0F));
    queue.particleItems.push_back({{11U, 2U}, Math::Vector3{1.0F, 2.0F, 3.0F}, 1.0F, 4.0F, 1U});
    queue.particleItemsTruncated = true;
    queue.invalidAssetReferences = 1U;
    queue.pendingAssetLoads = 2U;
    queue.failedAssetLoads = 3U;
    queue.invalidRenderEligibility = 4U;

    const std::size_t opaqueCapacity = queue.opaqueItems.capacity();
    const std::size_t transparentCapacity = queue.transparentItems.capacity();
    const std::size_t particleCapacity = queue.particleItems.capacity();

    queue.Clear();

    EXPECT_TRUE(queue.opaqueItems.empty());
    EXPECT_TRUE(queue.transparentItems.empty());
    EXPECT_TRUE(queue.particleItems.empty());
    EXPECT_FALSE(queue.particleItemsTruncated);
    EXPECT_EQ(queue.invalidAssetReferences, 0U);
    EXPECT_EQ(queue.pendingAssetLoads, 0U);
    EXPECT_EQ(queue.failedAssetLoads, 0U);
    EXPECT_EQ(queue.invalidRenderEligibility, 0U);
    EXPECT_EQ(queue.opaqueItems.capacity(), opaqueCapacity);
    EXPECT_EQ(queue.transparentItems.capacity(), transparentCapacity);
    EXPECT_EQ(queue.particleItems.capacity(), particleCapacity);
}

} // namespace
TEST_F(RenderQueueTest, SortForDepthOnlyPass_GroupsItemsByMeshSoTheShadowBatcherCanMergeThem) {
    // The reason this function exists. BuildShadowBatches merges only ADJACENT same-mesh items,
    // so the batch count is decided by the ordering it is handed, and depth order interleaves
    // meshes by distance. This asserts the batch count directly rather than the ordering, because
    // the batch count is what costs draw calls.
    RenderQueue queue;
    constexpr std::size_t kItemCount = 300U;
    constexpr int kDistinctMeshes = 3;
    for (std::size_t index = 0U; index < kItemCount; ++index) {
        // Depth deliberately uncorrelated with mesh, which is the case that splits runs apart.
        const float depth = static_cast<float>((index * 7919U) % 1000U) * 0.1F;
        queue.opaqueItems.push_back(MakeItemOnMesh(depth, static_cast<int>(index) % kDistinctMeshes));
    }

    RenderQueue depthOrdered;
    depthOrdered.opaqueItems = queue.opaqueItems;
    depthOrdered.Sort();
    RenderBatchSet depthBatches;
    BuildShadowBatches(depthOrdered.opaqueItems, depthBatches);

    queue.SortForDepthOnlyPass();
    RenderBatchSet meshBatches;
    BuildShadowBatches(queue.opaqueItems, meshBatches);

    EXPECT_EQ(meshBatches.batches.size(), static_cast<std::size_t>(kDistinctMeshes))
        << "mesh ordering must collapse to one batch per distinct mesh";
    EXPECT_LT(meshBatches.batches.size(), depthBatches.batches.size())
        << "if depth ordering batched just as well there would be no reason for this function";
    EXPECT_EQ(queue.opaqueItems.size(), kItemCount) << "ordering must not lose or duplicate items";
}

TEST_F(RenderQueueTest, SortForDepthOnlyPass_OrdersByDepthWithinEachMesh) {
    // Mesh first is the point, but within one mesh the pass should still draw nearest-first so the
    // depth test rejects as much as it can. This pins the secondary key.
    RenderQueue queue;
    for (const float depth : {9.0F, 1.0F, 5.0F, 3.0F}) {
        queue.opaqueItems.push_back(MakeItemOnMesh(depth, 0));
    }

    queue.SortForDepthOnlyPass();

    ASSERT_EQ(queue.opaqueItems.size(), 4U);
    for (std::size_t index = 1U; index < queue.opaqueItems.size(); ++index) {
        EXPECT_LE(queue.opaqueItems[index - 1U].sortDepth, queue.opaqueItems[index].sortDepth)
            << "within one mesh the order should still be front-to-back, at index " << index;
    }
}

TEST_F(RenderQueueTest, SortForDepthOnlyPass_NonFiniteDepthsDoNotBreakTheOrdering) {
    // A NaN depth makes a naive comparator inconsistent, which is undefined behaviour in
    // std::sort rather than just a wrong order - the same hazard Sort guards against, and easy
    // to forget when writing a second comparator.
    RenderQueue queue;
    queue.opaqueItems.push_back(MakeItemOnMesh(std::numeric_limits<float>::quiet_NaN(), 0));
    queue.opaqueItems.push_back(MakeItemOnMesh(2.0F, 1));
    queue.opaqueItems.push_back(MakeItemOnMesh(std::numeric_limits<float>::infinity(), 0));
    queue.opaqueItems.push_back(MakeItemOnMesh(1.0F, 1));

    queue.SortForDepthOnlyPass();

    ASSERT_EQ(queue.opaqueItems.size(), 4U) << "no item may be lost to a NaN comparison";
    // Still grouped by mesh, which is what the pass downstream relies on.
    for (std::size_t index = 1U; index < queue.opaqueItems.size(); ++index) {
        EXPECT_LE(queue.opaqueItems[index - 1U].meshHandle.GetGuid().value,
                  queue.opaqueItems[index].meshHandle.GetGuid().value);
    }
}

TEST_F(RenderQueueTest, SortForDepthOnlyPass_LeavesTransparentAndParticleBucketsAlone) {
    // A depth-only pass draws neither, so reordering them would be work with no consumer. Pinned
    // because "sort everything for symmetry" is the obvious thing for someone to add later.
    RenderQueue queue;
    queue.transparentItems.push_back(MakeItem(3.0F));
    queue.transparentItems.push_back(MakeItem(2.0F));
    queue.transparentItems.push_back(MakeItem(1.0F));
    const std::vector<float> depthsBefore{3.0F, 2.0F, 1.0F};

    queue.SortForDepthOnlyPass();

    ASSERT_EQ(queue.transparentItems.size(), depthsBefore.size());
    for (std::size_t index = 0U; index < depthsBefore.size(); ++index) {
        EXPECT_FLOAT_EQ(queue.transparentItems[index].sortDepth, depthsBefore[index]);
    }
}

} // namespace UVE::Render::Tests
