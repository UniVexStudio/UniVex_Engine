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

#include "blueprint_ai_nodes.h"
#include "blueprint_animation_nodes.h"
#include "blueprint_audio_nodes.h"
#include "blueprint_character_nodes.h"
#include "blueprint_events_nodes.h"
#include "blueprint_flow_nodes.h"
#include "blueprint_game_nodes.h"
#include "blueprint_gameplay_nodes.h"
#include "blueprint_input_nodes.h"
#include "blueprint_math_nodes.h"
#include "blueprint_physics_nodes.h"
#include "blueprint_rendering_nodes.h"
#include "blueprint_transform_nodes.h"
#include "blueprint_ui_nodes.h"
#include "blueprint_utilities_nodes.h"
#include "blueprint_variables_nodes.h"
#include "blueprint_world_nodes.h"

namespace UVE::Blueprint::Tests {
namespace {

class RecordingGraphListener final : public Core::IBlueprintGraphListener {
public:
    void OnBlueprintGraphChanged(const Core::BlueprintGraphEvent& event) override {
        events.push_back(event);
    }
    std::vector<Core::BlueprintGraphEvent> events;
};

class MinimalDebugHost final : public UVScriptHost {
public:
    std::optional<HostProperty> DescribeProperty(std::string_view) const override { return std::nullopt; }
    std::optional<HostFunction> DescribeFunction(std::string_view) const override { return std::nullopt; }
    std::optional<std::vector<Type>> DescribeEvent(const std::string_view event) const override {
        if (event == "ready") return std::vector<Type>{};
        return std::nullopt;
    }
    Value GetProperty(std::string_view) override { return Value{}; }
    void SetProperty(std::string_view, const Value&) override {}
    Value CallFunction(std::string_view, std::span<const Value>) override { return Value{}; }
    void CallMethod(ObjectRef, std::string_view, std::span<const Value>) override {}
    void Print(const std::string_view text) override { output.emplace_back(text); }

    std::vector<std::string> output;
};

TEST(BlueprintCoreUnitTest, GraphModelConnectValidateAndRoundTripAsset) {
    Core::BlueprintCoreGraph graph("MainGraph", "Character3D");
    RecordingGraphListener listener;
    graph.AddListener(&listener);

    Core::BlueprintCoreNode ready;
    ready.typeKey = "event_ready";
    ready.title = "Event Ready";
    ready.outputs.push_back({0U, "", Asset::BlueprintPinType::Exec, Core::BlueprintPinDirection::Output, ""});
    const auto readyId = graph.AddNode(ready);

    Core::BlueprintCoreNode printNode;
    printNode.typeKey = "print";
    printNode.title = "Print";
    printNode.inputs.push_back({0U, "", Asset::BlueprintPinType::Exec, Core::BlueprintPinDirection::Input, ""});
    printNode.inputs.push_back(
        {1U, "Text", Asset::BlueprintPinType::String, Core::BlueprintPinDirection::Input, "Hello"});
    printNode.outputs.push_back({0U, "", Asset::BlueprintPinType::Exec, Core::BlueprintPinDirection::Output, ""});
    const auto printId = graph.AddNode(printNode);

    const auto conn = graph.Connect(readyId, 0U, printId, 0U);
    ASSERT_TRUE(conn.connected);
    EXPECT_FALSE(graph.HasCycle());

    const auto topo = graph.TopologicalSortNodes();
    ASSERT_EQ(topo.size(), 2U);
    EXPECT_EQ(topo[0], readyId);
    EXPECT_EQ(topo[1], printId);

    // Reject self-connection and mismatched pin types.
    EXPECT_FALSE(graph.Connect(printId, 0U, printId, 0U).connected);
    EXPECT_FALSE(graph.Connect(readyId, 0U, printId, 1U).connected);

    Core::AutoLayoutBlueprintGraph(graph);
    const auto bounds = Core::ComputeBlueprintGraphBounds(graph);
    EXPECT_TRUE(bounds.valid);
    EXPECT_GT(bounds.Width(), 0.0F);

    const Asset::BlueprintAsset asset = Core::BlueprintCoreGraphToAsset(graph);
    EXPECT_TRUE(Asset::IsBlueprintAssetValid(asset));
    EXPECT_EQ(asset.nodes.size(), 2U);
    EXPECT_EQ(asset.wires.size(), 1U);

    const Core::BlueprintCoreGraph roundTripped = Core::BlueprintCoreGraphFromAsset(asset);
    EXPECT_EQ(roundTripped.Nodes().size(), 2U);
    EXPECT_EQ(roundTripped.Connections().size(), 1U);
    EXPECT_GE(listener.events.size(), 3U);

    EXPECT_TRUE(Core::CanConnectBlueprintPinTypes(Asset::BlueprintPinType::Int, Asset::BlueprintPinType::Float));
    EXPECT_FALSE(Core::CanConnectBlueprintPinTypes(Asset::BlueprintPinType::String, Asset::BlueprintPinType::Int));
    EXPECT_EQ(Core::SanitizeBlueprintIdentifier("123 my-var!"), "_123_my_var_");
    EXPECT_EQ(Core::FormatBlueprintNodeTitle("net_rpc_send"), "Net Rpc Send");
}

TEST(BlueprintCoreUnitTest, ReflectionRegistryInheritanceAndLookup) {
    Core::BlueprintReflectionRegistry registry;
    Core::BlueprintClassReflection base;
    base.className = "Object3D";
    ASSERT_TRUE(registry.RegisterClass(base));

    Core::BlueprintClassReflection derived;
    derived.className = "Character3D";
    derived.parentClassName = "Object3D";
    derived.properties.push_back({"speed", Asset::BlueprintPinType::Float, false, true, "5.0", "Move speed"});
    ASSERT_TRUE(registry.RegisterClass(derived));

    EXPECT_TRUE(registry.IsSubclassOf("Character3D", "Object3D"));
    EXPECT_TRUE(registry.IsSubclassOf("Character3D", "Character3D"));
    EXPECT_FALSE(registry.IsSubclassOf("Object3D", "Character3D"));
    ASSERT_NE(registry.FindClass("Character3D"), nullptr);
    EXPECT_EQ(registry.FindClass("Character3D")->properties.size(), 1U);
}

TEST(BlueprintAssetsUnitTest, RegistryDependencyOrderAndDetailedValidation) {
    Assets::BlueprintAssetRegistry registry;
    ASSERT_TRUE(registry.RegisterAsset(
        {"Content/BP_Base.uvbp", Assets::BlueprintAssetKind::BlueprintClass, "Object3D", 1U, {}, {}}));
    ASSERT_TRUE(registry.RegisterAsset({"Content/BP_Hero.uvbp",
                                           Assets::BlueprintAssetKind::BlueprintClass,
                                           "Character3D",
                                           1U,
                                           {"Content/BP_Base.uvbp"},
                                           {}}));
    ASSERT_TRUE(registry.RegisterAsset({"Content/BP_Boss.uvbp",
                                           Assets::BlueprintAssetKind::BlueprintClass,
                                           "Character3D",
                                           1U,
                                           {"Content/BP_Hero.uvbp"},
                                           {}}));

    EXPECT_FALSE(registry.HasDependencyCycle());
    const auto order = registry.ComputeBuildOrder();
    ASSERT_EQ(order.size(), 3U);
    EXPECT_EQ(order[0], "Content/BP_Base.uvbp");
    EXPECT_EQ(order[1], "Content/BP_Hero.uvbp");
    EXPECT_EQ(order[2], "Content/BP_Boss.uvbp");
    EXPECT_EQ(registry.GetTransitiveDependencies("Content/BP_Boss.uvbp").size(), 2U);
    EXPECT_EQ(registry.GetDependents("Content/BP_Base.uvbp").size(), 1U);

    // Inject a cycle and confirm detection.
    ASSERT_TRUE(registry.RegisterAsset({"Content/BP_Base.uvbp",
                                           Assets::BlueprintAssetKind::BlueprintClass,
                                           "Object3D",
                                           1U,
                                           {"Content/BP_Boss.uvbp"},
                                           {}}));
    EXPECT_TRUE(registry.HasDependencyCycle());

    // Detailed validation detects unknown node type and dangling wire.
    Asset::BlueprintAsset bad;
    bad.nodes.push_back({1U, "nonexistent_node", {}, 0.0F, 0.0F, {}});
    bad.wires.push_back({1U, 0U, 99U, 0U});
    const auto report = Assets::ValidateBlueprintAssetDetailed(bad);
    EXPECT_FALSE(report.IsValid());
    EXPECT_GE(report.ErrorCount(), 2U);
}

TEST(BlueprintNodesUnitTest, DomainClassificationAndSearchFilters) {
    const auto netNodes = Nodes::GetBlueprintNodesByDomain(Nodes::BlueprintNodeDomain::Networking);
    EXPECT_GE(netNodes.size(), 20U);

    Nodes::BlueprintNodeQuery query;
    query.searchText = "rpc";
    query.domain = Nodes::BlueprintNodeDomain::Networking;
    const auto rpcHits = Nodes::SearchBlueprintNodes(query);
    EXPECT_GE(rpcHits.size(), 10U);

    const auto counts = Nodes::GetBlueprintNodeDomainCounts();
    EXPECT_GT(counts.size(), 5U);
}

TEST(BlueprintDebugUnitTest, DebugSessionBreakpointsWatchAndTrace) {
    constexpr const char* kSource =
        "var counter = 7\n"
        "fn compute(x: int) -> int:\n"
        "    let doubled = x * 2\n"
        "    return doubled + counter\n";

    MinimalDebugHost host;
    const CompileResult compiled = CompileUVScriptSource(kSource, host);
    ASSERT_TRUE(compiled.IsSuccess());

    ScriptInstance instance(compiled.program, host, Execution::Interpreted);
    Debug::BlueprintDebugSession session;
    const std::uint32_t bpId = session.Breakpoints().AddLineBreakpoint(3U, 42U);
    ASSERT_NE(bpId, 0U);
    ASSERT_TRUE(session.Attach(instance));
    session.AddWatch("counter");
    session.AddWatch("local[0]");

    const std::array<Value, 1> args{std::int64_t{10}};
    EXPECT_EQ(instance.Call("compute", args), std::nullopt);
    ASSERT_TRUE(instance.IsDebugPaused());

    const auto live = session.CaptureLiveValues();
    ASSERT_TRUE(live.has_value());
    EXPECT_EQ(live->sourceLine, 3U);
    EXPECT_EQ(live->functionName, "compute");
    ASSERT_EQ(live->watches.size(), 2U);
    EXPECT_TRUE(live->watches[0].resolved);
    EXPECT_EQ(live->watches[0].formattedValue, "7");
    EXPECT_TRUE(live->watches[1].resolved);
    EXPECT_EQ(live->watches[1].formattedValue, "10");
    EXPECT_FALSE(session.GetCallStack().empty());

    const auto resumed = session.Continue();
    EXPECT_EQ(resumed.state, ScriptDebugState::Completed);
    ASSERT_TRUE(resumed.value.has_value());
    EXPECT_EQ(std::get<std::int64_t>(*resumed.value), 27);
    EXPECT_FALSE(session.Trace().Entries().empty());

    session.Detach();
    EXPECT_FALSE(session.IsAttached());
}

TEST(BlueprintResourcesAndPlatformUnitTest, ThemesIconsTemplatesAndPlatformPaths) {
    const auto darkTheme = Resources::GetBlueprintTheme(Resources::BlueprintThemeKind::DarkStudio);
    EXPECT_EQ(darkTheme.PinStyleFor(Asset::BlueprintPinType::Exec).shape, Resources::BlueprintPinShape::Arrow);
    EXPECT_EQ(darkTheme.PinStyleFor(Asset::BlueprintPinType::IntArray).shape,
              Resources::BlueprintPinShape::Grid);
    EXPECT_FALSE(Resources::GetAllBlueprintIcons().empty());

    for (const auto& tmpl : Resources::GetBlueprintTemplates()) {
        const Asset::BlueprintAsset asset = Resources::InstantiateBlueprintTemplate(tmpl.kind);
        EXPECT_TRUE(Asset::IsBlueprintAssetValid(asset)) << tmpl.id;
        EXPECT_TRUE(Assets::ValidateBlueprintAssetDetailed(asset).IsValid()) << tmpl.id;
        const auto translation = Asset::TranslateBlueprintToUVScript(asset, "TestObj", asset.parentKind);
        EXPECT_TRUE(translation.problems.empty()) << tmpl.id;
    }

    const auto caps = Platform::GetCurrentBlueprintPlatformCapabilities();
    EXPECT_FALSE(caps.name.empty());
    EXPECT_EQ(Platform::GetBlueprintPlatformCapabilitiesFor(Platform::BlueprintPlatformKind::Windows)
                  .sharedLibraryExtension,
              ".dll");
    EXPECT_FALSE(Platform::GetBlueprintPlatformCapabilitiesFor(Platform::BlueprintPlatformKind::Android)
                     .supportsDynamicPluginLoading);

    EXPECT_EQ(Platform::NormalizeBlueprintAssetPath("./Scripts\\Player\\BP_Hero.uvbp"),
              std::optional<std::string>("Scripts/Player/BP_Hero.uvbp"));
    EXPECT_EQ(Platform::NormalizeBlueprintAssetPath("../escape.uvbp"), std::nullopt);
    EXPECT_EQ(Platform::NormalizeBlueprintAssetPath("/abs/path.uvbp"), std::nullopt);
    const std::filesystem::path cacheDir =
        Platform::GetBlueprintCacheDirectory("/tmp/project", Platform::BlueprintPlatformKind::Linux);
    EXPECT_EQ(cacheDir.filename(), "linux");
}

TEST(BlueprintNodesUnitTest, PerCategoryNodeModulesRegisterUniqueCompleteCatalogue) {
    struct CategoryModuleCheck final {
        const char* label;
        void (*registerFn)(std::vector<Asset::BlueprintNodeType>&);
        std::size_t expectedCount;
    };
    constexpr std::array<CategoryModuleCheck, 17> kModules{{
        {"Events", &Nodes::RegisterEventsBlueprintNodes, 20U},
        {"Flow", &Nodes::RegisterFlowBlueprintNodes, 18U},
        {"Utilities", &Nodes::RegisterUtilitiesBlueprintNodes, 63U},
        {"Transform", &Nodes::RegisterTransformBlueprintNodes, 19U},
        {"Physics", &Nodes::RegisterPhysicsBlueprintNodes, 6U},
        {"Input", &Nodes::RegisterInputBlueprintNodes, 6U},
        {"Character", &Nodes::RegisterCharacterBlueprintNodes, 8U},
        {"Rendering", &Nodes::RegisterRenderingBlueprintNodes, 13U},
        {"Audio", &Nodes::RegisterAudioBlueprintNodes, 7U},
        {"Animation", &Nodes::RegisterAnimationBlueprintNodes, 18U},
        {"World", &Nodes::RegisterWorldBlueprintNodes, 28U},
        {"Game", &Nodes::RegisterGameBlueprintNodes, 23U},
        {"AI", &Nodes::RegisterAiBlueprintNodes, 4U},
        {"UI", &Nodes::RegisterUiBlueprintNodes, 32U},
        {"Gameplay", &Nodes::RegisterGameplayBlueprintNodes, 8U},
        {"Math", &Nodes::RegisterMathBlueprintNodes, 59U},
        {"Variables", &Nodes::RegisterVariablesBlueprintNodes, 2U},
    }};

    std::size_t categorySum = 0U;
    for (const CategoryModuleCheck& mod : kModules) {
        std::vector<Asset::BlueprintNodeType> local;
        mod.registerFn(local);
        EXPECT_EQ(local.size(), mod.expectedCount) << mod.label;
        categorySum += local.size();
    }
    EXPECT_EQ(categorySum, 334U);

    const auto& all = Asset::GetBlueprintNodeTypes();
    EXPECT_GT(all.size(), categorySum);
    std::set<std::string> uniqueKeys;
    for (const Asset::BlueprintNodeType& type : all) {
        EXPECT_FALSE(type.key.empty());
        EXPECT_FALSE(type.title.empty()) << type.key;
        EXPECT_FALSE(type.category.empty()) << type.key;
        EXPECT_TRUE(uniqueKeys.insert(type.key).second) << "Duplicate Blueprint node key: " << type.key;
        EXPECT_EQ(Asset::FindBlueprintNodeType(type.key), &type) << type.key;
    }
    EXPECT_EQ(Asset::FindBlueprintNodeType("definitely_not_a_registered_node_key"), nullptr);
}

} // namespace
} // namespace UVE::Blueprint::Tests
