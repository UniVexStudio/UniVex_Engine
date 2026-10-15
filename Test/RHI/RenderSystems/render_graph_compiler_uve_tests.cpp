// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
#include "uve/render_systems/render_graph_uve.h"
#include "uve/rhi_null/null_render_device_uve.h"
#include <array>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>
#include <gtest/gtest.h>

namespace UVE::Render::Tests {
namespace {
RenderGraphPassDescUVE PassUVE(std::string name, std::vector<RenderGraphResourceUseUVE> uses,
                               bool sideEffects = true) {
    RenderGraphPassDescUVE pass;
    pass.debugNameUVE = std::move(name);
    pass.resources = std::move(uses);
    pass.sideEffects = sideEffects;
    pass.recordCallbackUVE = [](ICommandBufferUVE &) {};
    return pass;
}
TEST(RenderGraphCompilerUVETest, ExplicitDependenciesCanPlaceAProducerBeforeAnEarlierDeclaredConsumer) {
    RenderGraphUVE graph;
    const auto texture = graph.CreateTransientTextureUVE({2U, 2U, TextureFormatUVE::RGBA8Unorm}, "color");
    const auto reader = graph.AddPassUVE(PassUVE("read", {{texture, RenderGraphResourceAccessUVE::Read}}));
    const auto writer = graph.AddPassUVE(PassUVE("write", {{texture, RenderGraphResourceAccessUVE::Write}}));
    ASSERT_TRUE(graph.AddDependencyUVE(reader, writer));
    RenderGraphPlanUVE plan;
    ASSERT_EQ(graph.CompileUVE(plan), RenderGraphCompileCodeUVE::Valid);
    EXPECT_EQ(plan.passOrder, (std::vector<std::uint32_t>{1U, 0U}));
    EXPECT_EQ(plan.barrierBeforePass, (std::vector<bool>{false, true}));
}
TEST(RenderGraphCompilerUVETest, RawWarWawHazardsAndDuplicateImportsAreExplicit) {
    NullRenderDeviceUVE device;
    const auto texture = device.CreateTextureUVE({2U, 2U, TextureFormatUVE::RGBA8Unorm});
    RenderGraphUVE graph;
    const auto r = graph.ImportTextureUVE(texture, "first");
    EXPECT_EQ(graph.ImportTextureUVE(texture, "alias"), r);
    graph.AddPassUVE(PassUVE("read external", {{r, RenderGraphResourceAccessUVE::Read}}));
    graph.AddPassUVE(PassUVE("write", {{r, RenderGraphResourceAccessUVE::Write}}));
    graph.AddPassUVE(PassUVE("read", {{r, RenderGraphResourceAccessUVE::Read}}));
    graph.AddPassUVE(PassUVE("overwrite", {{r, RenderGraphResourceAccessUVE::Write}}));
    RenderGraphPlanUVE plan;
    ASSERT_EQ(graph.CompileUVE(plan), RenderGraphCompileCodeUVE::Valid);
    EXPECT_EQ(plan.prerequisites[1], (std::vector<std::uint32_t>{0U}));
    EXPECT_EQ(plan.prerequisites[2], (std::vector<std::uint32_t>{1U}));
    EXPECT_EQ(plan.prerequisites[3], (std::vector<std::uint32_t>{1U, 2U}));
    EXPECT_EQ(plan.barrierBeforePass, (std::vector<bool>{false, true, true, true}));
}
TEST(RenderGraphCompilerUVETest, CyclesAndForeignHandlesFailBeforeRecordingAndPreservePlan) {
    NullRenderDeviceUVE device;
    RenderGraphUVE graph;
    const auto a = graph.AddPassUVE(PassUVE("a", {}));
    const auto b = graph.AddPassUVE(PassUVE("b", {}));
    ASSERT_TRUE(graph.AddDependencyUVE(a, b));
    ASSERT_TRUE(graph.AddDependencyUVE(b, a));
    RenderGraphPlanUVE plan;
    plan.passOrder = {42U};
    EXPECT_EQ(graph.CompileUVE(plan), RenderGraphCompileCodeUVE::Cycle);
    EXPECT_EQ(plan.passOrder, (std::vector<std::uint32_t>{42U}));
    auto commands = device.CreateCommandBufferUVE();
    EXPECT_FALSE(graph.ExecuteUVE(*commands));
    graph.ClearUVE();
    RenderGraphUVE other;
    const auto native = device.CreateTextureUVE({1U, 1U, TextureFormatUVE::RGBA8Unorm});
    const auto foreign = other.ImportTextureUVE(native, "foreign");
    graph.AddPassUVE(PassUVE("wrong owner", {{foreign, RenderGraphResourceAccessUVE::Read}}));
    EXPECT_EQ(graph.CompileUVE(plan), RenderGraphCompileCodeUVE::InvalidResource);
    EXPECT_EQ(plan.passOrder, (std::vector<std::uint32_t>{42U}));
}
TEST(RenderGraphCompilerUVETest, InvalidAccessDuplicatesAndUninitializedTransientReadsAreRejected) {
    RenderGraphUVE graph;
    const auto r = graph.CreateTransientTextureUVE({1U, 1U, TextureFormatUVE::RGBA8Unorm}, "r");
    graph.AddPassUVE(PassUVE("read", {{r, RenderGraphResourceAccessUVE::Read}}));
    RenderGraphPlanUVE plan;
    EXPECT_EQ(graph.CompileUVE(plan), RenderGraphCompileCodeUVE::UninitializedRead);
    graph.ClearUVE();
    const auto live = graph.CreateTransientTextureUVE({1U, 1U, TextureFormatUVE::RGBA8Unorm}, "live");
    graph.AddPassUVE(PassUVE("bad access", {{live, static_cast<RenderGraphResourceAccessUVE>(255)}}));
    EXPECT_EQ(graph.CompileUVE(plan), RenderGraphCompileCodeUVE::InvalidResource);
    graph.ClearUVE();
    const auto next = graph.CreateTransientTextureUVE({1U, 1U, TextureFormatUVE::RGBA8Unorm}, "next");
    graph.AddPassUVE(PassUVE("duplicate", {{next, RenderGraphResourceAccessUVE::Write},
                                           {next, RenderGraphResourceAccessUVE::Read}}));
    EXPECT_EQ(graph.CompileUVE(plan), RenderGraphCompileCodeUVE::InvalidResource);
}
TEST(RenderGraphCompilerUVETest, UnusedPurePassesCullButOutputDependenciesAndSideEffectsRemain) {
    RenderGraphUVE graph;
    const auto a = graph.CreateTransientTextureUVE({2U, 2U, TextureFormatUVE::RGBA8Unorm}, "a");
    const auto b = graph.CreateTransientTextureUVE({2U, 2U, TextureFormatUVE::RGBA8Unorm}, "b");
    const auto dead = graph.CreateTransientTextureUVE({2U, 2U, TextureFormatUVE::RGBA8Unorm}, "unused");
    graph.AddPassUVE(PassUVE("producer", {{a, RenderGraphResourceAccessUVE::Write}}, false));
    graph.AddPassUVE(
        PassUVE("output", {{a, RenderGraphResourceAccessUVE::Read}, {b, RenderGraphResourceAccessUVE::Write}},
                false));
    graph.AddPassUVE(PassUVE("dead", {{dead, RenderGraphResourceAccessUVE::Write}}, false));
    graph.AddPassUVE(PassUVE("side effect", {}));
    ASSERT_TRUE(graph.MarkOutputUVE(b));
    RenderGraphPlanUVE plan;
    ASSERT_EQ(graph.CompileUVE(plan), RenderGraphCompileCodeUVE::Valid);
    EXPECT_EQ(plan.passOrder, (std::vector<std::uint32_t>{0U, 1U, 3U}));
    EXPECT_EQ(plan.lifetimes[dead.value].firstUse, UINT32_MAX);
    EXPECT_EQ(plan.lifetimes[b.value].lastUse, plan.passOrder.size());
    EXPECT_EQ(plan.transientTextures.size(), 2U);
}
TEST(RenderGraphCompilerUVETest, TransientReuseRequiresDisjointLifetimesAndExactShape) {
    RenderGraphUVE graph;
    const auto a = graph.CreateTransientTextureUVE({2U, 2U, TextureFormatUVE::RGBA8Unorm}, "a");
    const auto b = graph.CreateTransientTextureUVE({2U, 2U, TextureFormatUVE::RGBA8Unorm}, "b");
    const auto c = graph.CreateTransientTextureUVE({4U, 2U, TextureFormatUVE::RGBA8Unorm}, "c");
    graph.AddPassUVE(PassUVE("write a", {{a, RenderGraphResourceAccessUVE::Write}}));
    graph.AddPassUVE(PassUVE("read a", {{a, RenderGraphResourceAccessUVE::Read}}));
    graph.AddPassUVE(PassUVE("write b", {{b, RenderGraphResourceAccessUVE::Write}}));
    graph.AddPassUVE(PassUVE("write c", {{c, RenderGraphResourceAccessUVE::Write}}));
    RenderGraphPlanUVE plan;
    ASSERT_EQ(graph.CompileUVE(plan), RenderGraphCompileCodeUVE::Valid);
    EXPECT_EQ(plan.lifetimes[a.value].physicalSlot, plan.lifetimes[b.value].physicalSlot);
    EXPECT_NE(plan.lifetimes[b.value].physicalSlot, plan.lifetimes[c.value].physicalSlot);
    EXPECT_EQ(plan.transientTextures.size(), 2U);
    RenderGraphOptionsUVE options;
    options.aliasTransientTextures = false;
    ASSERT_EQ(graph.CompileUVE(plan, options), RenderGraphCompileCodeUVE::Valid);
    EXPECT_EQ(plan.transientTextures.size(), 3U);
}
TEST(RenderGraphCompilerUVETest, ActualTransientHandlesAliasAndSurviveGraphClearUntilCommandRetirement) {
    NullRenderDeviceUVE device;
    RenderGraphUVE graph;
    const auto a = graph.CreateTransientTextureUVE({2U, 2U, TextureFormatUVE::RGBA8Unorm}, "a");
    const auto b = graph.CreateTransientTextureUVE({2U, 2U, TextureFormatUVE::RGBA8Unorm}, "b");
    std::vector<TextureHandleUVE> seen;
    const auto add = [&](const char *name, RenderGraphResourceHandleUVE r,
                         RenderGraphResourceAccessUVE access) {
        auto pass = PassUVE(name, {{r, access}});
        pass.recordCallbackUVE = {};
        pass.executeCallbackUVE = [&, r, access](auto &commands, const auto &context) {
            const auto native = context.GetTextureUVE(r);
            EXPECT_NE(native, kInvalidTextureHandleUVE);
            seen.push_back(native);
            if (access == RenderGraphResourceAccessUVE::Write) {
                RenderPassDescUVE target;
                target.colorAttachment = native;
                commands.BeginRenderPassUVE(target);
                commands.EndRenderPassUVE();
            }
        };
        graph.AddPassUVE(std::move(pass));
    };
    add("write a", a, RenderGraphResourceAccessUVE::Write);
    add("read a", a, RenderGraphResourceAccessUVE::Read);
    add("write b", b, RenderGraphResourceAccessUVE::Write);
    add("read b", b, RenderGraphResourceAccessUVE::Read);
    auto commands = device.CreateCommandBufferUVE();
    ASSERT_TRUE(graph.ExecuteUVE(device, *commands).IsCompletedUVE());
    ASSERT_EQ(seen.size(), 4U);
    EXPECT_EQ(seen.front(), seen.back());
    EXPECT_EQ(device.GetLiveResourceCountUVE(), 1U);
    graph.ClearUVE();
    EXPECT_EQ(device.GetLiveResourceCountUVE(), 1U);
    device.SubmitUVE(std::move(commands));
    std::size_t barriers = 0U;
    for (const auto &operation : device.GetLastSubmittedCommandsUVE())
        if (std::holds_alternative<MemoryBarrierCommandUVE>(operation))
            ++barriers;
    EXPECT_EQ(barriers, 3U);
    EXPECT_EQ(device.GetLiveResourceCountUVE(),
              1U); // deletion is owner-thread retirement, not a worker callback
    EXPECT_EQ(device.WaitIdleUVE(), RenderOperationResultUVE::Recorded);
    EXPECT_EQ(device.GetLiveResourceCountUVE(), 0U);
}
TEST(RenderGraphCompilerUVETest, LegacyRouteRefusesTransientAllocationWithoutADevice) {
    NullRenderDeviceUVE device;
    RenderGraphUVE graph;
    const auto r = graph.CreateTransientTextureUVE({1U, 1U, TextureFormatUVE::RGBA8Unorm}, "r");
    bool called = false;
    auto pass = PassUVE("write", {{r, RenderGraphResourceAccessUVE::Write}});
    pass.recordCallbackUVE = [&](auto &) { called = true; };
    graph.AddPassUVE(std::move(pass));
    auto commands = device.CreateCommandBufferUVE();
    EXPECT_FALSE(graph.ExecuteUVE(*commands));
    EXPECT_FALSE(called);
    EXPECT_EQ(device.GetLiveResourceCountUVE(), 0U);
}
TEST(RenderGraphCompilerUVETest, DestroyedImportFailsDeviceValidationBeforeCallbacks) {
    NullRenderDeviceUVE device;
    const auto native = device.CreateTextureUVE({1U, 1U, TextureFormatUVE::RGBA8Unorm});
    RenderGraphUVE graph;
    const auto r = graph.ImportTextureUVE(native, "borrowed");
    bool called = false;
    auto pass = PassUVE("read", {{r, RenderGraphResourceAccessUVE::Read}});
    pass.recordCallbackUVE = [&](auto &) { called = true; };
    graph.AddPassUVE(std::move(pass));
    device.DestroyTextureUVE(native);
    auto commands = device.CreateCommandBufferUVE();
    EXPECT_EQ(graph.ExecuteUVE(device, *commands).code, RenderGraphExecuteCodeUVE::InvalidGraph);
    EXPECT_FALSE(called);
}
TEST(RenderGraphCompilerUVETest, CallbackFailureAndMutationDoNotRunSubsequentPasses) {
    NullRenderDeviceUVE device;
    RenderGraphUVE graph;
    int later = 0;
    auto bad = PassUVE("bad", {});
    bad.recordCallbackUVE = [](auto &) { throw std::runtime_error("record failed"); };
    graph.AddPassUVE(std::move(bad));
    auto next = PassUVE("next", {});
    next.recordCallbackUVE = [&](auto &) { ++later; };
    graph.AddPassUVE(next);
    auto commands = device.CreateCommandBufferUVE();
    EXPECT_EQ(graph.ExecuteUVE(device, *commands).code, RenderGraphExecuteCodeUVE::CallbackFailed);
    EXPECT_EQ(later, 0);
    graph.ClearUVE();
    auto mutate = PassUVE("mutate", {});
    mutate.recordCallbackUVE = [&](auto &) { graph.ClearUVE(); };
    graph.AddPassUVE(std::move(mutate));
    graph.AddPassUVE(next);
    EXPECT_EQ(graph.ExecuteUVE(device, *commands).code, RenderGraphExecuteCodeUVE::CallbackFailed);
    EXPECT_EQ(later, 0);
    EXPECT_EQ(graph.GetPassCountUVE(), 2U);
}
TEST(RenderGraphCompilerUVETest, CapacityFailurePoisonsTheBuildRatherThanExecutingAPartialGraph) {
    RenderGraphUVE graph;
    for (std::size_t i = 0U; i < RenderGraphUVE::kMaximumPassesUVE; ++i)
        ASSERT_TRUE(graph.AddPassUVE(PassUVE("pass", {})).IsValidUVE());
    EXPECT_FALSE(graph.AddPassUVE(PassUVE("overflow", {})).IsValidUVE());
    RenderGraphPlanUVE plan;
    EXPECT_EQ(graph.CompileUVE(plan), RenderGraphCompileCodeUVE::CapacityExceeded);
    graph.ClearUVE();
    EXPECT_EQ(graph.CompileUVE(plan), RenderGraphCompileCodeUVE::Valid);
    EXPECT_TRUE(plan.passOrder.empty());
}
} // namespace
} // namespace UVE::Render::Tests
