// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include <array>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "blueprint_asset.h"
#include "blueprint_asset_registry.h"
#include "blueprint_asset_types.h"
#include "blueprint_compiler.h"
#include "blueprint_core_types.h"
#include "blueprint_debugger.h"
#include "blueprint_host.h"
#include "blueprint_instance.h"
#include "blueprint_node_categories.h"
#include "blueprint_nodes.h"
#include "blueprint_platform.h"
#include "blueprint_resources.h"

namespace UVE::Blueprint::Tests {
namespace {

class RecordingGraphListenerUVE final : public Core::IBlueprintGraphListenerUVE {
public:
    void OnBlueprintGraphChangedUVE(const Core::BlueprintGraphEventUVE& event) override {
        events.push_back(event);
    }
    std::vector<Core::BlueprintGraphEventUVE> events;
};

class MinimalDebugHostUVE final : public UVScriptHostUVE {
public:
    std::optional<HostPropertyUVE> DescribePropertyUVE(std::string_view) const override { return std::nullopt; }
    std::optional<HostFunctionUVE> DescribeFunctionUVE(std::string_view) const override { return std::nullopt; }
    std::optional<std::vector<TypeUVE>> DescribeEventUVE(const std::string_view event) const override {
        if (event == "ready") return std::vector<TypeUVE>{};
        return std::nullopt;
    }
    ValueUVE GetPropertyUVE(std::string_view) override { return ValueUVE{}; }
    void SetPropertyUVE(std::string_view, const ValueUVE&) override {}
    ValueUVE CallFunctionUVE(std::string_view, std::span<const ValueUVE>) override { return ValueUVE{}; }
    void CallMethodUVE(ObjectRefUVE, std::string_view, std::span<const ValueUVE>) override {}
    void PrintUVE(const std::string_view text) override { output.emplace_back(text); }

    std::vector<std::string> output;
};

TEST(BlueprintCoreUnitUVETest, GraphModelConnectValidateAndRoundTripAsset) {
    Core::BlueprintCoreGraphUVE graph("MainGraph", "Character3D");
    RecordingGraphListenerUVE listener;
    graph.AddListenerUVE(&listener);

    Core::BlueprintCoreNodeUVE ready;
    ready.typeKey = "event_ready";
    ready.title = "Event Ready";
    ready.outputs.push_back({0U, "", Asset::BlueprintPinTypeUVE::Exec, Core::BlueprintPinDirectionUVE::Output, ""});
    const auto readyId = graph.AddNodeUVE(ready);

    Core::BlueprintCoreNodeUVE printNode;
    printNode.typeKey = "print";
    printNode.title = "Print";
    printNode.inputs.push_back({0U, "", Asset::BlueprintPinTypeUVE::Exec, Core::BlueprintPinDirectionUVE::Input, ""});
    printNode.inputs.push_back(
        {1U, "Text", Asset::BlueprintPinTypeUVE::String, Core::BlueprintPinDirectionUVE::Input, "Hello"});
    printNode.outputs.push_back({0U, "", Asset::BlueprintPinTypeUVE::Exec, Core::BlueprintPinDirectionUVE::Output, ""});
    const auto printId = graph.AddNodeUVE(printNode);

    const auto conn = graph.ConnectUVE(readyId, 0U, printId, 0U);
    ASSERT_TRUE(conn.connected);
    EXPECT_FALSE(graph.HasCycleUVE());

    const auto topo = graph.TopologicalSortNodesUVE();
    ASSERT_EQ(topo.size(), 2U);
    EXPECT_EQ(topo[0], readyId);
    EXPECT_EQ(topo[1], printId);

    // Reject self-connection and mismatched pin types.
    EXPECT_FALSE(graph.ConnectUVE(printId, 0U, printId, 0U).connected);
    EXPECT_FALSE(graph.ConnectUVE(readyId, 0U, printId, 1U).connected);

    Core::AutoLayoutBlueprintGraphUVE(graph);
    const auto bounds = Core::ComputeBlueprintGraphBoundsUVE(graph);
    EXPECT_TRUE(bounds.valid);
    EXPECT_GT(bounds.WidthUVE(), 0.0F);

    const Asset::BlueprintAssetUVE asset = Core::BlueprintCoreGraphToAssetUVE(graph);
    EXPECT_TRUE(Asset::IsBlueprintAssetValidUVE(asset));
    EXPECT_EQ(asset.nodes.size(), 2U);
    EXPECT_EQ(asset.wires.size(), 1U);

    const Core::BlueprintCoreGraphUVE roundTripped = Core::BlueprintCoreGraphFromAssetUVE(asset);
    EXPECT_EQ(roundTripped.NodesUVE().size(), 2U);
    EXPECT_EQ(roundTripped.ConnectionsUVE().size(), 1U);
    EXPECT_GE(listener.events.size(), 3U);

    EXPECT_TRUE(Core::CanConnectBlueprintPinTypesUVE(Asset::BlueprintPinTypeUVE::Int, Asset::BlueprintPinTypeUVE::Float));
    EXPECT_FALSE(Core::CanConnectBlueprintPinTypesUVE(Asset::BlueprintPinTypeUVE::String, Asset::BlueprintPinTypeUVE::Int));
    EXPECT_EQ(Core::SanitizeBlueprintIdentifierUVE("123 my-var!"), "_123_my_var_");
    EXPECT_EQ(Core::FormatBlueprintNodeTitleUVE("net_rpc_send"), "Net Rpc Send");
}

TEST(BlueprintCoreUnitUVETest, ReflectionRegistryInheritanceAndLookup) {
    Core::BlueprintReflectionRegistryUVE registry;
    Core::BlueprintClassReflectionUVE base;
    base.className = "Object3D";
    ASSERT_TRUE(registry.RegisterClassUVE(base));

    Core::BlueprintClassReflectionUVE derived;
    derived.className = "Character3D";
    derived.parentClassName = "Object3D";
    derived.properties.push_back({"speed", Asset::BlueprintPinTypeUVE::Float, false, true, "5.0", "Move speed"});
    ASSERT_TRUE(registry.RegisterClassUVE(derived));

    EXPECT_TRUE(registry.IsSubclassOfUVE("Character3D", "Object3D"));
    EXPECT_TRUE(registry.IsSubclassOfUVE("Character3D", "Character3D"));
    EXPECT_FALSE(registry.IsSubclassOfUVE("Object3D", "Character3D"));
    ASSERT_NE(registry.FindClassUVE("Character3D"), nullptr);
    EXPECT_EQ(registry.FindClassUVE("Character3D")->properties.size(), 1U);
}

TEST(BlueprintAssetsUnitUVETest, RegistryDependencyOrderAndDetailedValidation) {
    Assets::BlueprintAssetRegistryUVE registry;
    ASSERT_TRUE(registry.RegisterAssetUVE(
        {"Content/BP_Base.uvbp", Assets::BlueprintAssetKindUVE::BlueprintClass, "Object3D", 1U, {}, {}}));
    ASSERT_TRUE(registry.RegisterAssetUVE({"Content/BP_Hero.uvbp",
                                           Assets::BlueprintAssetKindUVE::BlueprintClass,
                                           "Character3D",
                                           1U,
                                           {"Content/BP_Base.uvbp"},
                                           {}}));
    ASSERT_TRUE(registry.RegisterAssetUVE({"Content/BP_Boss.uvbp",
                                           Assets::BlueprintAssetKindUVE::BlueprintClass,
                                           "Character3D",
                                           1U,
                                           {"Content/BP_Hero.uvbp"},
                                           {}}));

    EXPECT_FALSE(registry.HasDependencyCycleUVE());
    const auto order = registry.ComputeBuildOrderUVE();
    ASSERT_EQ(order.size(), 3U);
    EXPECT_EQ(order[0], "Content/BP_Base.uvbp");
    EXPECT_EQ(order[1], "Content/BP_Hero.uvbp");
    EXPECT_EQ(order[2], "Content/BP_Boss.uvbp");
    EXPECT_EQ(registry.GetTransitiveDependenciesUVE("Content/BP_Boss.uvbp").size(), 2U);
    EXPECT_EQ(registry.GetDependentsUVE("Content/BP_Base.uvbp").size(), 1U);

    // Inject a cycle and confirm detection.
    ASSERT_TRUE(registry.RegisterAssetUVE({"Content/BP_Base.uvbp",
                                           Assets::BlueprintAssetKindUVE::BlueprintClass,
                                           "Object3D",
                                           1U,
                                           {"Content/BP_Boss.uvbp"},
                                           {}}));
    EXPECT_TRUE(registry.HasDependencyCycleUVE());

    // Detailed validation detects unknown node type and dangling wire.
    Asset::BlueprintAssetUVE bad;
    bad.nodes.push_back({1U, "nonexistent_node", {}, 0.0F, 0.0F, {}});
    bad.wires.push_back({1U, 0U, 99U, 0U});
    const auto report = Assets::ValidateBlueprintAssetDetailedUVE(bad);
    EXPECT_FALSE(report.IsValidUVE());
    EXPECT_GE(report.ErrorCountUVE(), 2U);
}

TEST(BlueprintNodesUnitUVETest, DomainClassificationAndSearchFilters) {
    const auto netNodes = Nodes::GetBlueprintNodesByDomainUVE(Nodes::BlueprintNodeDomainUVE::Networking);
    EXPECT_GE(netNodes.size(), 20U);

    Nodes::BlueprintNodeQueryUVE query;
    query.searchText = "rpc";
    query.domain = Nodes::BlueprintNodeDomainUVE::Networking;
    const auto rpcHits = Nodes::SearchBlueprintNodesUVE(query);
    EXPECT_GE(rpcHits.size(), 10U);

    const auto counts = Nodes::GetBlueprintNodeDomainCountsUVE();
    EXPECT_GT(counts.size(), 5U);
}

TEST(BlueprintDebugUnitUVETest, DebugSessionBreakpointsWatchAndTrace) {
    constexpr const char* kSource =
        "var counter = 7\n"
        "fn compute(x: int) -> int:\n"
        "    let doubled = x * 2\n"
        "    return doubled + counter\n";

    MinimalDebugHostUVE host;
    const CompileResultUVE compiled = CompileUVScriptSourceUVE(kSource, host);
    ASSERT_TRUE(compiled.IsSuccessUVE());

    ScriptInstanceUVE instance(compiled.program, host, ExecutionUVE::Interpreted);
    Debug::BlueprintDebugSessionUVE session;
    const std::uint32_t bpId = session.BreakpointsUVE().AddLineBreakpointUVE(3U, 42U);
    ASSERT_NE(bpId, 0U);
    ASSERT_TRUE(session.AttachUVE(instance));
    session.AddWatchUVE("counter");
    session.AddWatchUVE("local[0]");

    const std::array<ValueUVE, 1> args{std::int64_t{10}};
    EXPECT_EQ(instance.CallUVE("compute", args), std::nullopt);
    ASSERT_TRUE(instance.IsDebugPausedUVE());

    const auto live = session.CaptureLiveValuesUVE();
    ASSERT_TRUE(live.has_value());
    EXPECT_EQ(live->sourceLine, 3U);
    EXPECT_EQ(live->functionName, "compute");
    ASSERT_EQ(live->watches.size(), 2U);
    EXPECT_TRUE(live->watches[0].resolved);
    EXPECT_EQ(live->watches[0].formattedValue, "7");
    EXPECT_TRUE(live->watches[1].resolved);
    EXPECT_EQ(live->watches[1].formattedValue, "10");
    EXPECT_FALSE(session.GetCallStackUVE().empty());

    const auto resumed = session.ContinueUVE();
    EXPECT_EQ(resumed.state, ScriptDebugStateUVE::Completed);
    ASSERT_TRUE(resumed.value.has_value());
    EXPECT_EQ(std::get<std::int64_t>(*resumed.value), 27);
    EXPECT_FALSE(session.TraceUVE().EntriesUVE().empty());

    session.DetachUVE();
    EXPECT_FALSE(session.IsAttachedUVE());
}

TEST(BlueprintResourcesAndPlatformUnitUVETest, ThemesIconsTemplatesAndPlatformPaths) {
    const auto darkTheme = Resources::GetBlueprintThemeUVE(Resources::BlueprintThemeKindUVE::DarkStudio);
    EXPECT_EQ(darkTheme.PinStyleForUVE(Asset::BlueprintPinTypeUVE::Exec).shape, Resources::BlueprintPinShapeUVE::Arrow);
    EXPECT_EQ(darkTheme.PinStyleForUVE(Asset::BlueprintPinTypeUVE::IntArray).shape,
              Resources::BlueprintPinShapeUVE::Grid);
    EXPECT_FALSE(Resources::GetAllBlueprintIconsUVE().empty());

    for (const auto& tmpl : Resources::GetBlueprintTemplatesUVE()) {
        const Asset::BlueprintAssetUVE asset = Resources::InstantiateBlueprintTemplateUVE(tmpl.kind);
        EXPECT_TRUE(Asset::IsBlueprintAssetValidUVE(asset)) << tmpl.id;
        EXPECT_TRUE(Assets::ValidateBlueprintAssetDetailedUVE(asset).IsValidUVE()) << tmpl.id;
        const auto translation = Asset::TranslateBlueprintToUVScriptUVE(asset, "TestObj", asset.parentKind);
        EXPECT_TRUE(translation.problems.empty()) << tmpl.id;
    }

    const auto caps = Platform::GetCurrentBlueprintPlatformCapabilitiesUVE();
    EXPECT_FALSE(caps.name.empty());
    EXPECT_EQ(Platform::GetBlueprintPlatformCapabilitiesForUVE(Platform::BlueprintPlatformKindUVE::Windows)
                  .sharedLibraryExtension,
              ".dll");
    EXPECT_FALSE(Platform::GetBlueprintPlatformCapabilitiesForUVE(Platform::BlueprintPlatformKindUVE::Android)
                     .supportsDynamicPluginLoading);

    EXPECT_EQ(Platform::NormalizeBlueprintAssetPathUVE("./Scripts\\Player\\BP_Hero.uvbp"),
              std::optional<std::string>("Scripts/Player/BP_Hero.uvbp"));
    EXPECT_EQ(Platform::NormalizeBlueprintAssetPathUVE("../escape.uvbp"), std::nullopt);
    EXPECT_EQ(Platform::NormalizeBlueprintAssetPathUVE("/abs/path.uvbp"), std::nullopt);
    const std::filesystem::path cacheDir =
        Platform::GetBlueprintCacheDirectoryUVE("/tmp/project", Platform::BlueprintPlatformKindUVE::Linux);
    EXPECT_EQ(cacheDir.filename(), "linux");
}

} // namespace
} // namespace UVE::Blueprint::Tests
