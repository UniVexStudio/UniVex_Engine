// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#include "uve/render_systems/render_batch_uve.h"

#include <cstddef>
#include <filesystem>
#include <string>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "uve/asset/asset_database_uve.h"
#include "uve/asset/asset_manager_uve.h"
#include "uve/asset/material_asset_uve.h"
#include "uve/asset/mesh_asset_uve.h"
#include "uve/events/event_system_uve.h"
#include "uve/math/matrix4x4_uve.h"
#include "uve/render_systems/render_queue_uve.h"
#include "uve/threading/thread_pool_uve.h"

namespace UVE::Render::Tests {
namespace {

// ---------------------------------------------------------------------------
// Batching is the CPU half of GPU instancing, and it is the half where a
// mistake is silent: a wrong batch boundary still draws every object, just
// with the wrong transform or in the wrong order. So these tests care far
// more about ORDER and GROUPING than about batch counts.
//
// RenderItemUVE holds live AssetHandleUVE<T> members with no default
// constructor, so a real AssetManagerUVE plus registered loaders is the
// simplest way to get valid handles - the same approach render_queue's own
// tests take. The loaders never read a file; only handle identity matters.
// ---------------------------------------------------------------------------

class RenderBatchUVETest : public ::testing::Test {
protected:
    Threading::ThreadPoolUVE threadPool{2};
    Events::EventSystemUVE eventSystem;
    Asset::AssetDatabaseUVE assetDatabase;
    Asset::AssetManagerUVE assetManager{threadPool, eventSystem};

    RenderBatchUVETest() {
        assetManager.RegisterLoaderUVE<Asset::MeshAssetUVE>(
            [](const std::filesystem::path&, Asset::MeshAssetUVE&) { return true; });
        assetManager.RegisterLoaderUVE<Asset::MaterialAssetUVE>(
            [](const std::filesystem::path&, Asset::MaterialAssetUVE&) { return true; });
    }

    /// Registers a stable named mesh/material pair, so two calls with the same names produce two
    /// items that genuinely SHOULD batch together - the fixture in render_queue's tests
    /// deliberately makes every item unique, which is the opposite of what is needed here.
    [[nodiscard]] Asset::AssetGuidUVE MeshGuidUVE(const std::string& name) {
        return assetDatabase.RegisterUVE("render_batch_tests_" + name + ".uvemodel");
    }
    [[nodiscard]] Asset::AssetGuidUVE MaterialGuidUVE(const std::string& name) {
        return assetDatabase.RegisterUVE("render_batch_tests_" + name + ".uvemat");
    }

    [[nodiscard]] RenderItemUVE MakeItemUVE(const std::string& meshName,
                                            const std::string& materialName,
                                            const float translateX = 0.0F) {
        Asset::AssetHandleUVE<Asset::MeshAssetUVE> meshHandle =
            assetManager.LoadUVE<Asset::MeshAssetUVE>(MeshGuidUVE(meshName), assetDatabase);
        Asset::AssetHandleUVE<Asset::MaterialAssetUVE> materialHandle =
            assetManager.LoadUVE<Asset::MaterialAssetUVE>(MaterialGuidUVE(materialName),
                                                          assetDatabase);
        // A distinct translation per item is what makes a mis-ordered instanceMatrices array
        // detectable: identity matrices everywhere would let any permutation pass.
        Math::Matrix4x4UVE world = Math::Matrix4x4UVE::IdentityUVE();
        world.m[0][3] = translateX;
        return RenderItemUVE{world, std::move(meshHandle), std::move(materialHandle), 0.0F};
    }
};

TEST_F(RenderBatchUVETest, BuildRenderBatchesUVE_EmptyInput_ProducesNothing) {
    RenderBatchSetUVE batches;
    // Pre-fill, to prove the function clears rather than appends - it is called every frame on a
    // reused set, so appending would grow without bound.
    batches.batches.push_back(RenderBatchUVE{});
    batches.instanceMatrices.push_back(Math::Matrix4x4UVE::IdentityUVE());

    BuildRenderBatchesUVE({}, batches);

    EXPECT_TRUE(batches.batches.empty());
    EXPECT_TRUE(batches.instanceMatrices.empty());
    EXPECT_EQ(batches.GetTotalInstanceCountUVE(), 0U);
}

TEST_F(RenderBatchUVETest, BuildRenderBatchesUVE_IdenticalAdjacentItems_CollapseIntoOneBatch) {
    std::vector<RenderItemUVE> items;
    items.push_back(MakeItemUVE("cube", "stone", 1.0F));
    items.push_back(MakeItemUVE("cube", "stone", 2.0F));
    items.push_back(MakeItemUVE("cube", "stone", 3.0F));

    RenderBatchSetUVE batches;
    BuildRenderBatchesUVE(items, batches);

    ASSERT_EQ(batches.batches.size(), 1U);
    EXPECT_EQ(batches.batches[0].firstItem, 0U);
    EXPECT_EQ(batches.batches[0].itemCount, 3U);
    // Three objects, one draw call - this is the entire point of the exercise.
    EXPECT_EQ(batches.GetTotalInstanceCountUVE(), 3U);
    ASSERT_EQ(batches.instanceMatrices.size(), 3U);
    EXPECT_FLOAT_EQ(batches.instanceMatrices[0].m[0][3], 1.0F);
    EXPECT_FLOAT_EQ(batches.instanceMatrices[1].m[0][3], 2.0F);
    EXPECT_FLOAT_EQ(batches.instanceMatrices[2].m[0][3], 3.0F);
}

TEST_F(RenderBatchUVETest, BuildRenderBatchesUVE_SameMeshDifferentMaterial_DoesNotBatch) {
    // Both halves of the key must be checked. A batch keyed on mesh alone would draw these three
    // with whichever material happened to be bound - a plausible-looking frame that is wrong.
    std::vector<RenderItemUVE> items;
    items.push_back(MakeItemUVE("cube", "stone"));
    items.push_back(MakeItemUVE("cube", "metal"));
    items.push_back(MakeItemUVE("cube", "stone"));

    RenderBatchSetUVE batches;
    BuildRenderBatchesUVE(items, batches);

    ASSERT_EQ(batches.batches.size(), 3U);
    for (const RenderBatchUVE& batch : batches.batches) {
        EXPECT_EQ(batch.itemCount, 1U);
    }
}

TEST_F(RenderBatchUVETest, BuildRenderBatchesUVE_SameMaterialDifferentMesh_DoesNotBatch) {
    std::vector<RenderItemUVE> items;
    items.push_back(MakeItemUVE("cube", "stone"));
    items.push_back(MakeItemUVE("sphere", "stone"));

    RenderBatchSetUVE batches;
    BuildRenderBatchesUVE(items, batches);

    ASSERT_EQ(batches.batches.size(), 2U);
    EXPECT_NE(batches.batches[0].meshGuid, batches.batches[1].meshGuid);
    EXPECT_EQ(batches.batches[0].materialGuid, batches.batches[1].materialGuid);
}

TEST_F(RenderBatchUVETest, BuildRenderBatchesUVE_NonAdjacentMatches_AreNotRegrouped) {
    // THE load-bearing test of this module. A-B-A must stay three batches, never two.
    //
    // Merging the two A items would be a better batch count and a WRONG frame: the queue is
    // already depth-sorted (opaque front-to-back for early-z, transparent back-to-front for
    // correct blending), and pulling a distant item forward to join a batch silently reorders
    // the draw. For transparency that is visibly broken alpha. Batching is only allowed to be
    // an optimization, never a reordering.
    std::vector<RenderItemUVE> items;
    items.push_back(MakeItemUVE("cube", "stone", 1.0F));
    items.push_back(MakeItemUVE("sphere", "glass", 2.0F));
    items.push_back(MakeItemUVE("cube", "stone", 3.0F));

    RenderBatchSetUVE batches;
    BuildRenderBatchesUVE(items, batches);

    ASSERT_EQ(batches.batches.size(), 3U);
    EXPECT_EQ(batches.batches[0].firstItem, 0U);
    EXPECT_EQ(batches.batches[1].firstItem, 1U);
    EXPECT_EQ(batches.batches[2].firstItem, 2U);
    // And the matrices stay in submission order, not batch-key order.
    ASSERT_EQ(batches.instanceMatrices.size(), 3U);
    EXPECT_FLOAT_EQ(batches.instanceMatrices[0].m[0][3], 1.0F);
    EXPECT_FLOAT_EQ(batches.instanceMatrices[1].m[0][3], 2.0F);
    EXPECT_FLOAT_EQ(batches.instanceMatrices[2].m[0][3], 3.0F);
}

TEST_F(RenderBatchUVETest, BuildRenderBatchesUVE_MixedRuns_KeepFirstItemAndCountInLockstep) {
    // firstItem indexes BOTH the source bucket and instanceMatrices; the consumer relies on that
    // to need only one base offset per draw. This walks every batch and proves the invariant
    // rather than spot-checking one.
    std::vector<RenderItemUVE> items;
    items.push_back(MakeItemUVE("cube", "stone", 0.0F));
    items.push_back(MakeItemUVE("cube", "stone", 1.0F));
    items.push_back(MakeItemUVE("tree", "bark", 2.0F));
    items.push_back(MakeItemUVE("cube", "stone", 3.0F));
    items.push_back(MakeItemUVE("cube", "stone", 4.0F));
    items.push_back(MakeItemUVE("cube", "stone", 5.0F));

    RenderBatchSetUVE batches;
    BuildRenderBatchesUVE(items, batches);

    ASSERT_EQ(batches.batches.size(), 3U);
    EXPECT_EQ(batches.batches[0].itemCount, 2U);
    EXPECT_EQ(batches.batches[1].itemCount, 1U);
    EXPECT_EQ(batches.batches[2].itemCount, 3U);

    std::size_t expectedFirst = 0U;
    for (const RenderBatchUVE& batch : batches.batches) {
        EXPECT_EQ(batch.firstItem, expectedFirst);
        for (std::size_t offset = 0U; offset < batch.itemCount; ++offset) {
            const std::size_t slot = batch.firstItem + offset;
            ASSERT_LT(slot, batches.instanceMatrices.size());
            // The matrix in slot N must be the world matrix of source item N - the whole
            // lockstep contract in one assertion.
            EXPECT_FLOAT_EQ(batches.instanceMatrices[slot].m[0][3], items[slot].worldMatrix.m[0][3]);
        }
        expectedFirst += batch.itemCount;
    }
    EXPECT_EQ(expectedFirst, items.size());
    EXPECT_EQ(batches.GetTotalInstanceCountUVE(), items.size());
}

TEST_F(RenderBatchUVETest, BuildRenderBatchesUVE_EveryItemUnique_EmitsSingleInstanceBatches) {
    // The worst case for batching, and it must still be correct rather than degenerate: a run of
    // one is emitted as a one-instance batch so the consumer stays a single loop.
    std::vector<RenderItemUVE> items;
    for (int index = 0; index < 5; ++index) {
        items.push_back(MakeItemUVE("mesh" + std::to_string(index), "mat" + std::to_string(index),
                                    static_cast<float>(index)));
    }

    RenderBatchSetUVE batches;
    BuildRenderBatchesUVE(items, batches);

    ASSERT_EQ(batches.batches.size(), 5U);
    for (std::size_t index = 0U; index < batches.batches.size(); ++index) {
        EXPECT_EQ(batches.batches[index].itemCount, 1U);
        EXPECT_EQ(batches.batches[index].firstItem, index);
    }
}

TEST_F(RenderBatchUVETest, BuildRenderBatchesUVE_ReusedAcrossFrames_DoesNotAccumulate) {
    std::vector<RenderItemUVE> items;
    items.push_back(MakeItemUVE("cube", "stone", 1.0F));
    items.push_back(MakeItemUVE("cube", "stone", 2.0F));

    RenderBatchSetUVE batches;
    for (int frame = 0; frame < 3; ++frame) {
        BuildRenderBatchesUVE(items, batches);
        ASSERT_EQ(batches.batches.size(), 1U) << "frame " << frame;
        ASSERT_EQ(batches.instanceMatrices.size(), 2U) << "frame " << frame;
    }
}

TEST_F(RenderBatchUVETest, BuildRenderBatchesUVE_BatchesCarryTheGuidsTheConsumerWillBindWith) {
    // The consumer resolves GPU resources from these GUIDs, so they must be the item's own, not
    // merely non-empty.
    std::vector<RenderItemUVE> items;
    items.push_back(MakeItemUVE("cube", "stone"));
    items.push_back(MakeItemUVE("tree", "bark"));

    RenderBatchSetUVE batches;
    BuildRenderBatchesUVE(items, batches);

    ASSERT_EQ(batches.batches.size(), 2U);
    EXPECT_EQ(batches.batches[0].meshGuid, items[0].meshHandle.GetGuidUVE());
    EXPECT_EQ(batches.batches[0].materialGuid, items[0].materialHandle.GetGuidUVE());
    EXPECT_EQ(batches.batches[1].meshGuid, items[1].meshHandle.GetGuidUVE());
    EXPECT_EQ(batches.batches[1].materialGuid, items[1].materialHandle.GetGuidUVE());
}

TEST_F(RenderBatchUVETest, BuildRenderBatchesUVE_PreservesADepthSortedQueuesOrderExactly) {
    // End to end with the real sorter, because the ordering guarantee is only meaningful against
    // the order the renderer will actually hand over.
    RenderQueueUVE queue;
    queue.opaqueItems.push_back(MakeItemUVE("cube", "stone", 5.0F));
    queue.opaqueItems.back().sortDepth = 5.0F;
    queue.opaqueItems.push_back(MakeItemUVE("cube", "stone", 1.0F));
    queue.opaqueItems.back().sortDepth = 1.0F;
    queue.opaqueItems.push_back(MakeItemUVE("tree", "bark", 3.0F));
    queue.opaqueItems.back().sortDepth = 3.0F;
    queue.SortUVE();

    RenderBatchSetUVE batches;
    BuildRenderBatchesUVE(queue.opaqueItems, batches);

    // Sorted front-to-back the order is depth 1 (cube), 3 (tree), 5 (cube) - so despite two cubes
    // being present this must be three batches, and the matrices must follow the sorted order.
    ASSERT_EQ(batches.batches.size(), 3U);
    ASSERT_EQ(batches.instanceMatrices.size(), 3U);
    EXPECT_FLOAT_EQ(batches.instanceMatrices[0].m[0][3], 1.0F);
    EXPECT_FLOAT_EQ(batches.instanceMatrices[1].m[0][3], 3.0F);
    EXPECT_FLOAT_EQ(batches.instanceMatrices[2].m[0][3], 5.0F);
}

// ---------------------------------------------------------------------------
// BuildShadowBatchesUVE - the depth-only variant.
//
// Its one behavioural difference from its sibling is that it ignores the
// material, so the tests that matter are the ones that put the two functions
// side by side on the SAME input and pin the divergence. Everything they share
// - adjacency-only merging, order preservation, clear-not-append - is
// re-asserted here rather than assumed, because the two are separate
// implementations and a copy-paste drift between them would otherwise be
// silent.
// ---------------------------------------------------------------------------

TEST_F(RenderBatchUVETest, BuildShadowBatchesUVE_SameMeshDifferentMaterials_CollapseIntoOneBatch) {
    // The defining case. The main pass must split these three; the shadow pass must not, because
    // a depth-only draw binds no material and all three write identical depth.
    std::vector<RenderItemUVE> items;
    items.push_back(MakeItemUVE("cube", "stone", 1.0F));
    items.push_back(MakeItemUVE("cube", "metal", 2.0F));
    items.push_back(MakeItemUVE("cube", "glass", 3.0F));

    RenderBatchSetUVE mainBatches;
    BuildRenderBatchesUVE(items, mainBatches);
    RenderBatchSetUVE shadowBatches;
    BuildShadowBatchesUVE(items, shadowBatches);

    EXPECT_EQ(mainBatches.batches.size(), 3U);
    ASSERT_EQ(shadowBatches.batches.size(), 1U);
    EXPECT_EQ(shadowBatches.batches[0].itemCount, 3U);

    // Same objects drawn either way - the saving is in call count, never in coverage.
    EXPECT_EQ(shadowBatches.GetTotalInstanceCountUVE(), mainBatches.GetTotalInstanceCountUVE());

    // And the transforms still arrive in queue order, which is what gl_InstanceID indexes.
    ASSERT_EQ(shadowBatches.instanceMatrices.size(), 3U);
    EXPECT_FLOAT_EQ(shadowBatches.instanceMatrices[0].m[0][3], 1.0F);
    EXPECT_FLOAT_EQ(shadowBatches.instanceMatrices[1].m[0][3], 2.0F);
    EXPECT_FLOAT_EQ(shadowBatches.instanceMatrices[2].m[0][3], 3.0F);
}

TEST_F(RenderBatchUVETest, BuildShadowBatchesUVE_DifferentMeshes_StillSplit) {
    // The mesh is the one thing a depth-only pass DOES care about: it supplies the vertex buffer.
    // Merging across meshes would draw the wrong geometry, so the relaxation must stop here.
    std::vector<RenderItemUVE> items;
    items.push_back(MakeItemUVE("cube", "stone", 1.0F));
    items.push_back(MakeItemUVE("sphere", "stone", 2.0F));

    RenderBatchSetUVE batches;
    BuildShadowBatchesUVE(items, batches);

    ASSERT_EQ(batches.batches.size(), 2U);
    EXPECT_NE(batches.batches[0].meshGuid, batches.batches[1].meshGuid);
    EXPECT_EQ(batches.batches[1].firstItem, 1U);
}

TEST_F(RenderBatchUVETest, BuildShadowBatchesUVE_LeavesMaterialGuidInvalid) {
    // Deliberate: a shadow batch has no meaningful material, so it advertises none. If a consumer
    // ever tries to bind one it should get an obviously invalid guid, not whichever material
    // happened to sort first.
    std::vector<RenderItemUVE> items;
    items.push_back(MakeItemUVE("cube", "stone", 1.0F));
    items.push_back(MakeItemUVE("cube", "metal", 2.0F));

    RenderBatchSetUVE batches;
    BuildShadowBatchesUVE(items, batches);

    ASSERT_EQ(batches.batches.size(), 1U);
    EXPECT_EQ(batches.batches[0].materialGuid, Asset::kInvalidAssetGuidUVE);
    // The mesh guid, by contrast, is load-bearing - the draw binds its vertex buffer.
    EXPECT_NE(batches.batches[0].meshGuid, Asset::kInvalidAssetGuidUVE);
}

TEST_F(RenderBatchUVETest, BuildShadowBatchesUVE_DoesNotReorderANonAdjacentRepeat) {
    // cube, sphere, cube. Reordering to bring the two cubes together would save a draw call and
    // wreck the front-to-back ordering the queue sorted for - and early-z rejection is worth more
    // in a depth-only pass than the call it would save.
    std::vector<RenderItemUVE> items;
    items.push_back(MakeItemUVE("cube", "stone", 1.0F));
    items.push_back(MakeItemUVE("sphere", "stone", 2.0F));
    items.push_back(MakeItemUVE("cube", "stone", 3.0F));

    RenderBatchSetUVE batches;
    BuildShadowBatchesUVE(items, batches);

    ASSERT_EQ(batches.batches.size(), 3U);
    ASSERT_EQ(batches.instanceMatrices.size(), 3U);
    EXPECT_FLOAT_EQ(batches.instanceMatrices[0].m[0][3], 1.0F);
    EXPECT_FLOAT_EQ(batches.instanceMatrices[1].m[0][3], 2.0F);
    EXPECT_FLOAT_EQ(batches.instanceMatrices[2].m[0][3], 3.0F);
}

TEST_F(RenderBatchUVETest, BuildShadowBatchesUVE_ReusedAcrossFrames_DoesNotAccumulate) {
    // The renderer keeps one set per cascade and reuses it every frame, so appending instead of
    // clearing would grow unboundedly and silently re-draw last frame's casters.
    std::vector<RenderItemUVE> items;
    items.push_back(MakeItemUVE("cube", "stone", 1.0F));
    items.push_back(MakeItemUVE("cube", "metal", 2.0F));

    RenderBatchSetUVE batches;
    BuildShadowBatchesUVE(items, batches);
    const std::size_t firstFrameBatches = batches.batches.size();
    BuildShadowBatchesUVE(items, batches);

    EXPECT_EQ(batches.batches.size(), firstFrameBatches);
    EXPECT_EQ(batches.instanceMatrices.size(), 2U);
}

TEST_F(RenderBatchUVETest, BuildShadowBatchesUVE_EmptyInput_ProducesNothing) {
    RenderBatchSetUVE batches;
    batches.batches.push_back(RenderBatchUVE{});
    batches.instanceMatrices.push_back(Math::Matrix4x4UVE::IdentityUVE());

    BuildShadowBatchesUVE({}, batches);

    EXPECT_TRUE(batches.batches.empty());
    EXPECT_TRUE(batches.instanceMatrices.empty());
}

TEST_F(RenderBatchUVETest, BuildShadowBatchesUVE_NeverProducesMoreBatchesThanTheMainPass) {
    // The relaxation is strictly one-way: ignoring the material can only merge batches the main
    // pass split, never split one it merged. Checked on a mixed sequence rather than a contrived
    // one, and alongside the invariant that both paths still cover every caster.
    std::vector<RenderItemUVE> items;
    items.push_back(MakeItemUVE("cube", "stone", 1.0F));
    items.push_back(MakeItemUVE("cube", "stone", 2.0F));
    items.push_back(MakeItemUVE("cube", "metal", 3.0F));
    items.push_back(MakeItemUVE("sphere", "metal", 4.0F));
    items.push_back(MakeItemUVE("sphere", "stone", 5.0F));
    items.push_back(MakeItemUVE("cube", "stone", 6.0F));

    RenderBatchSetUVE mainBatches;
    BuildRenderBatchesUVE(items, mainBatches);
    RenderBatchSetUVE shadowBatches;
    BuildShadowBatchesUVE(items, shadowBatches);

    EXPECT_LE(shadowBatches.batches.size(), mainBatches.batches.size());
    EXPECT_EQ(shadowBatches.GetTotalInstanceCountUVE(), items.size());
    EXPECT_EQ(mainBatches.GetTotalInstanceCountUVE(), items.size());

    // cube,cube / cube / sphere / sphere / cube -> mesh runs are cube(3), sphere(2), cube(1).
    ASSERT_EQ(shadowBatches.batches.size(), 3U);
    EXPECT_EQ(shadowBatches.batches[0].itemCount, 3U);
    EXPECT_EQ(shadowBatches.batches[1].itemCount, 2U);
    EXPECT_EQ(shadowBatches.batches[2].itemCount, 1U);
}

TEST_F(RenderBatchUVETest, BuildShadowBatchesUVE_FirstItemIndexesBothTheQueueAndTheMatrixArray) {
    // The instanced draw uploads one base offset and lets gl_InstanceID walk forward from it, so
    // firstItem must address the source queue and instanceMatrices identically. If those two ever
    // drift, objects render with another object's transform - the exact silent failure this suite
    // exists to catch.
    std::vector<RenderItemUVE> items;
    items.push_back(MakeItemUVE("cube", "stone", 1.0F));
    items.push_back(MakeItemUVE("sphere", "stone", 2.0F));
    items.push_back(MakeItemUVE("sphere", "metal", 3.0F));

    RenderBatchSetUVE batches;
    BuildShadowBatchesUVE(items, batches);

    ASSERT_EQ(batches.batches.size(), 2U);
    for (const RenderBatchUVE& batch : batches.batches) {
        for (std::size_t offset = 0U; offset < batch.itemCount; ++offset) {
            const std::size_t index = batch.firstItem + offset;
            ASSERT_LT(index, batches.instanceMatrices.size());
            EXPECT_FLOAT_EQ(batches.instanceMatrices[index].m[0][3],
                            items[index].worldMatrix.m[0][3]);
        }
    }
}

} // namespace
} // namespace UVE::Render::Tests
