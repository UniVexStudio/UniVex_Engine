// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
#include "render_graph.h"
#include "null_render_device.h"
#include <array>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>
#include <gtest/gtest.h>

namespace UVE::Render::Tests {
namespace {
RenderGraphPassDesc Pass(std::string name, std::vector<RenderGraphResourceUse> uses,
                               bool sideEffects = true) {
    RenderGraphPassDesc pass;
    pass.debugName = std::move(name);
    pass.resources = std::move(uses);
    pass.sideEffects = sideEffects;
    pass.recordCallback = [](ICommandBuffer &) {};
    return pass;
}
TEST(RenderGraphCompilerTest, ExplicitDependenciesCanPlaceAProducerBeforeAnEarlierDeclaredConsumer) {
    RenderGraph graph;
    const auto texture = graph.CreateTransientTexture({2U, 2U, TextureFormat::RGBA8Unorm}, "color");
    const auto reader = graph.AddPass(Pass("read", {{texture, RenderGraphResourceAccess::Read}}));
    const auto writer = graph.AddPass(Pass("write", {{texture, RenderGraphResourceAccess::Write}}));
    ASSERT_TRUE(graph.AddDependency(reader, writer));
    RenderGraphPlan plan;
    ASSERT_EQ(graph.Compile(plan), RenderGraphCompileCode::Valid);
    EXPECT_EQ(plan.passOrder, (std::vector<std::uint32_t>{1U, 0U}));
    EXPECT_EQ(plan.barrierBeforePass, (std::vector<bool>{false, true}));
}
TEST(RenderGraphCompilerTest, RawWarWawHazardsAndDuplicateImportsAreExplicit) {
    NullRenderDevice device;
    const auto texture = device.CreateTexture({2U, 2U, TextureFormat::RGBA8Unorm});
    RenderGraph graph;
    const auto r = graph.ImportTexture(texture, "first");
    EXPECT_EQ(graph.ImportTexture(texture, "alias"), r);
    graph.AddPass(Pass("read external", {{r, RenderGraphResourceAccess::Read}}));
    graph.AddPass(Pass("write", {{r, RenderGraphResourceAccess::Write}}));
    graph.AddPass(Pass("read", {{r, RenderGraphResourceAccess::Read}}));
    graph.AddPass(Pass("overwrite", {{r, RenderGraphResourceAccess::Write}}));
    RenderGraphPlan plan;
    ASSERT_EQ(graph.Compile(plan), RenderGraphCompileCode::Valid);
    EXPECT_EQ(plan.prerequisites[1], (std::vector<std::uint32_t>{0U}));
    EXPECT_EQ(plan.prerequisites[2], (std::vector<std::uint32_t>{1U}));
    EXPECT_EQ(plan.prerequisites[3], (std::vector<std::uint32_t>{1U, 2U}));
    EXPECT_EQ(plan.barrierBeforePass, (std::vector<bool>{false, true, true, true}));
}
TEST(RenderGraphCompilerTest, CyclesAndForeignHandlesFailBeforeRecordingAndPreservePlan) {
    NullRenderDevice device;
    RenderGraph graph;
    const auto a = graph.AddPass(Pass("a", {}));
    const auto b = graph.AddPass(Pass("b", {}));
    ASSERT_TRUE(graph.AddDependency(a, b));
    ASSERT_TRUE(graph.AddDependency(b, a));
    RenderGraphPlan plan;
    plan.passOrder = {42U};
    EXPECT_EQ(graph.Compile(plan), RenderGraphCompileCode::Cycle);
    EXPECT_EQ(plan.passOrder, (std::vector<std::uint32_t>{42U}));
    auto commands = device.CreateCommandBuffer();
    EXPECT_FALSE(graph.Execute(*commands));
    graph.Clear();
    RenderGraph other;
    const auto native = device.CreateTexture({1U, 1U, TextureFormat::RGBA8Unorm});
    const auto foreign = other.ImportTexture(native, "foreign");
    graph.AddPass(Pass("wrong owner", {{foreign, RenderGraphResourceAccess::Read}}));
    EXPECT_EQ(graph.Compile(plan), RenderGraphCompileCode::InvalidResource);
    EXPECT_EQ(plan.passOrder, (std::vector<std::uint32_t>{42U}));
}
TEST(RenderGraphCompilerTest, InvalidAccessDuplicatesAndUninitializedTransientReadsAreRejected) {
    RenderGraph graph;
    const auto r = graph.CreateTransientTexture({1U, 1U, TextureFormat::RGBA8Unorm}, "r");
    graph.AddPass(Pass("read", {{r, RenderGraphResourceAccess::Read}}));
    RenderGraphPlan plan;
    EXPECT_EQ(graph.Compile(plan), RenderGraphCompileCode::UninitializedRead);
    graph.Clear();
    const auto live = graph.CreateTransientTexture({1U, 1U, TextureFormat::RGBA8Unorm}, "live");
    graph.AddPass(Pass("bad access", {{live, static_cast<RenderGraphResourceAccess>(255)}}));
    EXPECT_EQ(graph.Compile(plan), RenderGraphCompileCode::InvalidResource);
    graph.Clear();
    const auto next = graph.CreateTransientTexture({1U, 1U, TextureFormat::RGBA8Unorm}, "next");
    graph.AddPass(Pass("duplicate", {{next, RenderGraphResourceAccess::Write},
                                           {next, RenderGraphResourceAccess::Read}}));
    EXPECT_EQ(graph.Compile(plan), RenderGraphCompileCode::InvalidResource);
}
TEST(RenderGraphCompilerTest, UnusedPurePassesCullButOutputDependenciesAndSideEffectsRemain) {
    RenderGraph graph;
    const auto a = graph.CreateTransientTexture({2U, 2U, TextureFormat::RGBA8Unorm}, "a");
    const auto b = graph.CreateTransientTexture({2U, 2U, TextureFormat::RGBA8Unorm}, "b");
    const auto dead = graph.CreateTransientTexture({2U, 2U, TextureFormat::RGBA8Unorm}, "unused");
    graph.AddPass(Pass("producer", {{a, RenderGraphResourceAccess::Write}}, false));
    graph.AddPass(
        Pass("output", {{a, RenderGraphResourceAccess::Read}, {b, RenderGraphResourceAccess::Write}},
                false));
    graph.AddPass(Pass("dead", {{dead, RenderGraphResourceAccess::Write}}, false));
    graph.AddPass(Pass("side effect", {}));
    ASSERT_TRUE(graph.MarkOutput(b));
    RenderGraphPlan plan;
    ASSERT_EQ(graph.Compile(plan), RenderGraphCompileCode::Valid);
    EXPECT_EQ(plan.passOrder, (std::vector<std::uint32_t>{0U, 1U, 3U}));
    EXPECT_EQ(plan.lifetimes[dead.value].firstUse, UINT32_MAX);
    EXPECT_EQ(plan.lifetimes[b.value].lastUse, plan.passOrder.size());
    EXPECT_EQ(plan.transientTextures.size(), 2U);
}
TEST(RenderGraphCompilerTest, TransientReuseRequiresDisjointLifetimesAndExactShape) {
    RenderGraph graph;
    const auto a = graph.CreateTransientTexture({2U, 2U, TextureFormat::RGBA8Unorm}, "a");
    const auto b = graph.CreateTransientTexture({2U, 2U, TextureFormat::RGBA8Unorm}, "b");
    const auto c = graph.CreateTransientTexture({4U, 2U, TextureFormat::RGBA8Unorm}, "c");
    graph.AddPass(Pass("write a", {{a, RenderGraphResourceAccess::Write}}));
    graph.AddPass(Pass("read a", {{a, RenderGraphResourceAccess::Read}}));
    graph.AddPass(Pass("write b", {{b, RenderGraphResourceAccess::Write}}));
    graph.AddPass(Pass("write c", {{c, RenderGraphResourceAccess::Write}}));
    RenderGraphPlan plan;
    ASSERT_EQ(graph.Compile(plan), RenderGraphCompileCode::Valid);
    EXPECT_EQ(plan.lifetimes[a.value].physicalSlot, plan.lifetimes[b.value].physicalSlot);
    EXPECT_NE(plan.lifetimes[b.value].physicalSlot, plan.lifetimes[c.value].physicalSlot);
    EXPECT_EQ(plan.transientTextures.size(), 2U);
    RenderGraphOptions options;
    options.aliasTransientTextures = false;
    ASSERT_EQ(graph.Compile(plan, options), RenderGraphCompileCode::Valid);
    EXPECT_EQ(plan.transientTextures.size(), 3U);
}
TEST(RenderGraphCompilerTest, ActualTransientHandlesAliasAndSurviveGraphClearUntilCommandRetirement) {
    NullRenderDevice device;
    RenderGraph graph;
    const auto a = graph.CreateTransientTexture({2U, 2U, TextureFormat::RGBA8Unorm}, "a");
    const auto b = graph.CreateTransientTexture({2U, 2U, TextureFormat::RGBA8Unorm}, "b");
    std::vector<TextureHandle> seen;
    const auto add = [&](const char *name, RenderGraphResourceHandle r,
                         RenderGraphResourceAccess access) {
        auto pass = Pass(name, {{r, access}});
        pass.recordCallback = {};
        pass.executeCallback = [&, r, access](auto &commands, const auto &context) {
            const auto native = context.GetTexture(r);
            EXPECT_NE(native, kInvalidTextureHandle);
            seen.push_back(native);
            if (access == RenderGraphResourceAccess::Write) {
                RenderPassDesc target;
                target.colorAttachment = native;
                commands.BeginRenderPass(target);
                commands.EndRenderPass();
            }
        };
        graph.AddPass(std::move(pass));
    };
    add("write a", a, RenderGraphResourceAccess::Write);
    add("read a", a, RenderGraphResourceAccess::Read);
    add("write b", b, RenderGraphResourceAccess::Write);
    add("read b", b, RenderGraphResourceAccess::Read);
    auto commands = device.CreateCommandBuffer();
    ASSERT_TRUE(graph.Execute(device, *commands).IsCompleted());
    ASSERT_EQ(seen.size(), 4U);
    EXPECT_EQ(seen.front(), seen.back());
    EXPECT_EQ(device.GetLiveResourceCount(), 1U);
    graph.Clear();
    EXPECT_EQ(device.GetLiveResourceCount(), 1U);
    device.Submit(std::move(commands));
    std::size_t barriers = 0U;
    for (const auto &operation : device.GetLastSubmittedCommands())
        if (std::holds_alternative<MemoryBarrierCommand>(operation))
            ++barriers;
    EXPECT_EQ(barriers, 3U);
    EXPECT_EQ(device.GetLiveResourceCount(),
              1U); // deletion is owner-thread retirement, not a worker callback
    EXPECT_EQ(device.WaitIdle(), RenderOperationResult::Recorded);
    EXPECT_EQ(device.GetLiveResourceCount(), 0U);
}
TEST(RenderGraphCompilerTest, LegacyRouteRefusesTransientAllocationWithoutADevice) {
    NullRenderDevice device;
    RenderGraph graph;
    const auto r = graph.CreateTransientTexture({1U, 1U, TextureFormat::RGBA8Unorm}, "r");
    bool called = false;
    auto pass = Pass("write", {{r, RenderGraphResourceAccess::Write}});
    pass.recordCallback = [&](auto &) { called = true; };
    graph.AddPass(std::move(pass));
    auto commands = device.CreateCommandBuffer();
    EXPECT_FALSE(graph.Execute(*commands));
    EXPECT_FALSE(called);
    EXPECT_EQ(device.GetLiveResourceCount(), 0U);
}
TEST(RenderGraphCompilerTest, DestroyedImportFailsDeviceValidationBeforeCallbacks) {
    NullRenderDevice device;
    const auto native = device.CreateTexture({1U, 1U, TextureFormat::RGBA8Unorm});
    RenderGraph graph;
    const auto r = graph.ImportTexture(native, "borrowed");
    bool called = false;
    auto pass = Pass("read", {{r, RenderGraphResourceAccess::Read}});
    pass.recordCallback = [&](auto &) { called = true; };
    graph.AddPass(std::move(pass));
    device.DestroyTexture(native);
    auto commands = device.CreateCommandBuffer();
    EXPECT_EQ(graph.Execute(device, *commands).code, RenderGraphExecuteCode::InvalidGraph);
    EXPECT_FALSE(called);
}
TEST(RenderGraphCompilerTest, CallbackFailureAndMutationDoNotRunSubsequentPasses) {
    NullRenderDevice device;
    RenderGraph graph;
    int later = 0;
    auto bad = Pass("bad", {});
    bad.recordCallback = [](auto &) { throw std::runtime_error("record failed"); };
    graph.AddPass(std::move(bad));
    auto next = Pass("next", {});
    next.recordCallback = [&](auto &) { ++later; };
    graph.AddPass(next);
    auto commands = device.CreateCommandBuffer();
    EXPECT_EQ(graph.Execute(device, *commands).code, RenderGraphExecuteCode::CallbackFailed);
    EXPECT_EQ(later, 0);
    graph.Clear();
    auto mutate = Pass("mutate", {});
    mutate.recordCallback = [&](auto &) { graph.Clear(); };
    graph.AddPass(std::move(mutate));
    graph.AddPass(next);
    EXPECT_EQ(graph.Execute(device, *commands).code, RenderGraphExecuteCode::CallbackFailed);
    EXPECT_EQ(later, 0);
    EXPECT_EQ(graph.GetPassCount(), 2U);
}
TEST(RenderGraphCompilerTest, CapacityFailurePoisonsTheBuildRatherThanExecutingAPartialGraph) {
    RenderGraph graph;
    for (std::size_t i = 0U; i < RenderGraph::kMaximumPasses; ++i)
        ASSERT_TRUE(graph.AddPass(Pass("pass", {})).IsValid());
    EXPECT_FALSE(graph.AddPass(Pass("overflow", {})).IsValid());
    RenderGraphPlan plan;
    EXPECT_EQ(graph.Compile(plan), RenderGraphCompileCode::CapacityExceeded);
    graph.Clear();
    EXPECT_EQ(graph.Compile(plan), RenderGraphCompileCode::Valid);
    EXPECT_TRUE(plan.passOrder.empty());
}
} // namespace
} // namespace UVE::Render::Tests
