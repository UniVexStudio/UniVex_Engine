// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
//
// The VFX emitter graph is the single source of truth for what an emitter does: only modules wired
// into their stage's chain are compiled, wires and values change the compiled program, and every
// node of the library can be created and connected.

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "Support/test_scratch.h"

#include "editor_vfx_graph.h"
#include "particle_system_asset.h"
#include "vfx_program.h"
#include "vfx_templates.h"
#include "prism/Application.h"
#include "prism/BlueprintGraph.h"

namespace UVE::Editor::Tests {
namespace {

using Asset::ParticleModuleTypeUVE;
using Asset::ParticleSystemAssetUVE;
using VFX::ParticleTemplateKindUVE;
using PT = prism::BlueprintPinType;

constexpr std::array<ParticleTemplateKindUVE, 4> kRealTemplatesUVE{
    ParticleTemplateKindUVE::Fountain, ParticleTemplateKindUVE::Sparks, ParticleTemplateKindUVE::Fire,
    ParticleTemplateKindUVE::Smoke};

void EnsurePrismHostedUVE() {
    static prism::Application app;
    static const bool ready = [] {
        prism::HostDesc desc;
        desc.assetDir = (::UVE::Tests::RepositoryRootUVE() / "Engine/Runtime/Prism/assets").string();
        desc.width = 600;
        desc.height = 400;
        return app.initHosted(desc, std::make_shared<prism::HeadlessPlatform>());
    }();
    ASSERT_TRUE(ready);
}

struct CompiledUVE {
    bool ok = false;
    VFX::ParticleProgramUVE program;
    std::vector<VFX::ParticleCompileMessageUVE> messages;
};

[[nodiscard]] CompiledUVE CompileUVE(const ParticleSystemAssetUVE& system) {
    CompiledUVE out;
    out.ok = VFX::CompileParticleEmitterUVE(system, 0U, out.program, out.messages);
    return out;
}

[[nodiscard]] std::size_t ModuleCountUVE(const Asset::ParticleEmitterAssetUVE& e) {
    return e.emitterUpdate.size() + e.particleSpawn.size() + e.particleUpdate.size() + e.render.size();
}

/// Extracts emitter 0 of `graph` and returns `base` with it applied: what the editor does after
/// every graph edit.
[[nodiscard]] ParticleSystemAssetUVE ApplyGraphUVE(const ParticleSystemAssetUVE& base,
                                                   const prism::BlueprintGraph& graph) {
    ParticleSystemAssetUVE out = base;
    Asset::ParticleEmitterAssetUVE extracted;
    EXPECT_TRUE(ExtractEmitterFromGraphUVE(graph, base, 0U, extracted));
    out.emitters[0] = std::move(extracted);
    return out;
}

void InstallRulesUVE(prism::BlueprintGraph& graph) {
    graph.canConnectHook = [&graph](const int a, const int b, const int c, const int d, std::string* why) {
        return CanConnectVfxPinsUVE(graph, a, b, c, d, why);
    };
    graph.nodeLocked = [](const int id) { return IsVfxStageNodeIdUVE(id); };
}

[[nodiscard]] int InputPinIndexUVE(const prism::BlueprintGraph& graph, const int nodeId, const std::string& name) {
    const prism::BlueprintNode* const node = graph.findNode(nodeId);
    if (node == nullptr) {
        return -1;
    }
    for (std::size_t i = 0U; i < node->inputs.size(); ++i) {
        if (node->inputs[i].name == name) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

[[nodiscard]] const prism::BlueprintNodeSpec* FindSpecUVE(const std::vector<prism::BlueprintNodeSpec>& library,
                                                          const std::string& name) {
    for (const prism::BlueprintNodeSpec& spec : library) {
        if (spec.name == name) {
            return &spec;
        }
    }
    return nullptr;
}

TEST(VfxGraphAuthorityUVETest, TemplatesLoadAsRealNodeGraphs) {
    EnsurePrismHostedUVE();
    for (const ParticleTemplateKindUVE kind : kRealTemplatesUVE) {
        const ParticleSystemAssetUVE system = VFX::CreateParticleTemplateUVE(kind);
        const Asset::ParticleEmitterAssetUVE& emitter = system.emitters[0];
        prism::BlueprintGraph graph;
        BuildEmitterGraphUVE(system, 0U, graph);

        // One node per module, the four stage outputs, and one chain wire per module.
        EXPECT_EQ(static_cast<std::size_t>(graph.nodeCount()),
                  ModuleCountUVE(emitter) + 4U + emitter.mathNodes.size());
        std::size_t linkWires = 0U;
        for (const prism::BlueprintWire& wire : graph.wires()) {
            linkWires += wire.type == PT::Link ? 1U : 0U;
        }
        EXPECT_EQ(linkWires, ModuleCountUVE(emitter)) << "template " << static_cast<int>(kind);
        EXPECT_TRUE(emitter.detached.empty());
    }
}

TEST(VfxGraphAuthorityUVETest, RemovingEveryChainWireRemovesEveryContribution) {
    EnsurePrismHostedUVE();
    for (const ParticleTemplateKindUVE kind : kRealTemplatesUVE) {
        const ParticleSystemAssetUVE system = VFX::CreateParticleTemplateUVE(kind);
        const CompiledUVE before = CompileUVE(system);
        ASSERT_TRUE(before.ok) << "template " << static_cast<int>(kind);
        ASSERT_FALSE(before.program.renderers.empty());

        prism::BlueprintGraph graph;
        BuildEmitterGraphUVE(system, 0U, graph);
        const std::vector<prism::BlueprintWire> wires = graph.wires();
        for (const prism::BlueprintWire& wire : wires) {
            if (wire.type == PT::Link) {
                ASSERT_TRUE(graph.disconnectPin(wire.toNodeId, wire.toPinIndex, true));
            }
        }

        const ParticleSystemAssetUVE edited = ApplyGraphUVE(system, graph);
        const Asset::ParticleEmitterAssetUVE& e = edited.emitters[0];
        EXPECT_EQ(ModuleCountUVE(e), 0U) << "template " << static_cast<int>(kind);
        EXPECT_EQ(e.detached.size(), ModuleCountUVE(system.emitters[0]));

        // The unwired emitter has nothing to run: it cannot compile, and no program is produced.
        const CompiledUVE after = CompileUVE(edited);
        EXPECT_FALSE(after.ok);
        EXPECT_TRUE(after.program.renderers.empty());
        EXPECT_TRUE(after.program.particleSpawnOps.empty());
        EXPECT_TRUE(after.program.particleUpdateOps.empty());
        EXPECT_TRUE(after.program.emitterUpdateOps.empty());
    }
}

TEST(VfxGraphAuthorityUVETest, CuttingOneWireDropsExactlyTheModulesUpstreamOfTheCut) {
    EnsurePrismHostedUVE();
    const ParticleSystemAssetUVE system = VFX::CreateParticleTemplateUVE(ParticleTemplateKindUVE::Fountain);
    const Asset::ParticleEmitterAssetUVE& emitter = system.emitters[0];
    ASSERT_GE(emitter.particleSpawn.size(), 3U);
    const CompiledUVE before = CompileUVE(system);
    ASSERT_TRUE(before.ok);

    prism::BlueprintGraph graph;
    BuildEmitterGraphUVE(system, 0U, graph);
    // Cut the wire that leaves the second Particle Spawn module: the chain becomes [3rd .. last].
    const int cutNode = static_cast<int>(emitter.particleSpawn[1].id);
    ASSERT_TRUE(graph.disconnectPin(cutNode, 0, false));

    const ParticleSystemAssetUVE edited = ApplyGraphUVE(system, graph);
    const Asset::ParticleEmitterAssetUVE& e = edited.emitters[0];
    EXPECT_EQ(e.particleSpawn.size(), emitter.particleSpawn.size() - 2U);
    EXPECT_EQ(e.detached.size(), 2U);
    EXPECT_EQ(e.emitterUpdate.size(), emitter.emitterUpdate.size());
    EXPECT_EQ(e.particleUpdate.size(), emitter.particleUpdate.size());
    EXPECT_EQ(e.render.size(), emitter.render.size());

    const CompiledUVE after = CompileUVE(edited);
    ASSERT_TRUE(after.ok);
    EXPECT_LT(after.program.particleSpawnOps.size(), before.program.particleSpawnOps.size());
}

TEST(VfxGraphAuthorityUVETest, RemovingANodeRemovesItsContribution) {
    EnsurePrismHostedUVE();
    const ParticleSystemAssetUVE system = VFX::CreateParticleTemplateUVE(ParticleTemplateKindUVE::Fountain);
    const Asset::ParticleEmitterAssetUVE& emitter = system.emitters[0];
    const CompiledUVE before = CompileUVE(system);
    ASSERT_TRUE(before.ok);

    // Remove the Initial Lifetime node from the graph.
    std::uint32_t lifetimeId = 0U;
    for (const Asset::ParticleModuleUVE& m : emitter.particleSpawn) {
        if (m.type == ParticleModuleTypeUVE::InitialLifetime) {
            lifetimeId = m.id;
        }
    }
    ASSERT_NE(lifetimeId, 0U);
    prism::BlueprintGraph graph;
    BuildEmitterGraphUVE(system, 0U, graph);
    graph.removeNode(static_cast<int>(lifetimeId));
    // The removed node's neighbours are no longer joined by it: reconnect the chain around the gap.
    // (Removing the node alone breaks the chain at that point.)
    const ParticleSystemAssetUVE edited = ApplyGraphUVE(system, graph);
    for (const auto* stage : {&edited.emitters[0].emitterUpdate, &edited.emitters[0].particleSpawn,
                              &edited.emitters[0].particleUpdate, &edited.emitters[0].render,
                              &edited.emitters[0].detached}) {
        for (const Asset::ParticleModuleUVE& m : *stage) {
            EXPECT_NE(m.id, lifetimeId) << "a removed node must not survive anywhere in the asset";
        }
    }
    for (const VFX::VfxParticleSpawnOpUVE& op : CompileUVE(edited).program.particleSpawnOps) {
        EXPECT_NE(op.moduleId, lifetimeId);
    }
}

TEST(VfxGraphAuthorityUVETest, ChangingANodeValueChangesTheCompiledProgram) {
    EnsurePrismHostedUVE();
    const ParticleSystemAssetUVE system = VFX::CreateParticleTemplateUVE(ParticleTemplateKindUVE::Fountain);
    const Asset::ParticleModuleUVE& rateModule = system.emitters[0].emitterUpdate.front();
    ASSERT_EQ(rateModule.type, ParticleModuleTypeUVE::SpawnRate);

    prism::BlueprintGraph graph;
    BuildEmitterGraphUVE(system, 0U, graph);
    const int rateNode = static_cast<int>(rateModule.id);
    const int ratePin = InputPinIndexUVE(graph, rateNode, "Rate");
    ASSERT_GT(ratePin, 0);
    ASSERT_TRUE(graph.setPinDefault(rateNode, ratePin, "321"));

    const CompiledUVE before = CompileUVE(system);
    const CompiledUVE after = CompileUVE(ApplyGraphUVE(system, graph));
    ASSERT_TRUE(before.ok);
    ASSERT_TRUE(after.ok);
    ASSERT_FALSE(before.program.emitterUpdateOps.empty());
    ASSERT_FALSE(after.program.emitterUpdateOps.empty());
    EXPECT_FLOAT_EQ(before.program.emitterUpdateOps.front().rate.constant, 120.0F);
    EXPECT_FLOAT_EQ(after.program.emitterUpdateOps.front().rate.constant, 321.0F);
}

TEST(VfxGraphAuthorityUVETest, EveryLibraryNodeCanBeCreatedAndGetsADistinctId) {
    EnsurePrismHostedUVE();
    const ParticleSystemAssetUVE system = VFX::CreateParticleTemplateUVE(ParticleTemplateKindUVE::Fountain);
    const std::vector<prism::BlueprintNodeSpec> library = BuildVfxGraphNodeLibraryUVE(system, true);
    ASSERT_EQ(library.size(), Asset::GetParticleModuleCatalogUVE().size() + 21U);

    prism::BlueprintGraph graph;
    BuildEmitterGraphUVE(system, 0U, graph);
    std::vector<int> ids;
    float x = 0.0F;
    for (const prism::BlueprintNodeSpec& spec : library) {
        ASSERT_TRUE(static_cast<bool>(spec.make)) << spec.name;
        const std::size_t before = static_cast<std::size_t>(graph.nodeCount());
        const int id = SpawnVfxGraphNodeUVE(graph, spec, prism::Vec2{x, 900.0F});
        x += 40.0F;
        ASSERT_GT(id, 0) << spec.name;
        EXPECT_FALSE(IsVfxStageNodeIdUVE(id)) << spec.name;
        ASSERT_NE(graph.findNode(id), nullptr) << spec.name;
        EXPECT_EQ(static_cast<std::size_t>(graph.nodeCount()), before + 1U) << spec.name;
        EXPECT_TRUE(std::find(ids.begin(), ids.end(), id) == ids.end()) << spec.name;
        ids.push_back(id);
    }
}

TEST(VfxGraphAuthorityUVETest, ANewlyCreatedNodeDoesNothingUntilItIsConnected) {
    EnsurePrismHostedUVE();
    const ParticleSystemAssetUVE system = VFX::CreateParticleTemplateUVE(ParticleTemplateKindUVE::Fountain);
    const std::vector<prism::BlueprintNodeSpec> library = BuildVfxGraphNodeLibraryUVE(system, true);
    const prism::BlueprintNodeSpec* const gravity = FindSpecUVE(library, "Gravity");
    ASSERT_NE(gravity, nullptr);

    prism::BlueprintGraph graph;
    InstallRulesUVE(graph);
    BuildEmitterGraphUVE(system, 0U, graph);
    const CompiledUVE before = CompileUVE(system);
    ASSERT_TRUE(before.ok);

    const int id = SpawnVfxGraphNodeUVE(graph, *gravity, prism::Vec2{100.0F, 900.0F});
    ASSERT_GT(id, 0);

    // Unconnected: kept in the asset as detached, absent from the compiled program.
    const ParticleSystemAssetUVE dangling = ApplyGraphUVE(system, graph);
    ASSERT_EQ(dangling.emitters[0].detached.size(), 1U);
    EXPECT_EQ(dangling.emitters[0].detached[0].id, static_cast<std::uint32_t>(id));
    EXPECT_EQ(CompileUVE(dangling).program.particleUpdateOps.size(), before.program.particleUpdateOps.size());

    // Insert it at the end of the Particle Update chain through the graph's own validated connect().
    const Asset::ParticleModuleUVE& last = system.emitters[0].particleUpdate.back();
    ASSERT_TRUE(graph.disconnectPin(kVfxStageNodeParticleUpdateUVE, 0, true));
    std::string why;
    ASSERT_TRUE(graph.canConnect(static_cast<int>(last.id), 0, id, 0, &why)) << why;
    ASSERT_TRUE(graph.connect(static_cast<int>(last.id), 0, id, 0));
    ASSERT_TRUE(graph.connect(id, 0, kVfxStageNodeParticleUpdateUVE, 0));

    const ParticleSystemAssetUVE wired = ApplyGraphUVE(system, graph);
    EXPECT_TRUE(wired.emitters[0].detached.empty());
    ASSERT_EQ(wired.emitters[0].particleUpdate.size(), system.emitters[0].particleUpdate.size() + 1U);
    EXPECT_EQ(wired.emitters[0].particleUpdate.back().type, ParticleModuleTypeUVE::Gravity);
    const CompiledUVE after = CompileUVE(wired);
    ASSERT_TRUE(after.ok);
    EXPECT_EQ(after.program.particleUpdateOps.size(), before.program.particleUpdateOps.size() + 1U);
}

TEST(VfxGraphAuthorityUVETest, MathNodeWireDrivesAnInputAndRemovingItRestoresTheConstant) {
    EnsurePrismHostedUVE();
    const ParticleSystemAssetUVE system = VFX::CreateParticleTemplateUVE(ParticleTemplateKindUVE::Fountain);
    const std::vector<prism::BlueprintNodeSpec> library = BuildVfxGraphNodeLibraryUVE(system, true);
    const prism::BlueprintNodeSpec* const value = FindSpecUVE(library, "Value: Float");
    ASSERT_NE(value, nullptr);

    prism::BlueprintGraph graph;
    InstallRulesUVE(graph);
    BuildEmitterGraphUVE(system, 0U, graph);
    const int mathId = SpawnVfxGraphNodeUVE(graph, *value, prism::Vec2{100.0F, 950.0F});
    ASSERT_GT(mathId, 0);
    ASSERT_TRUE(graph.setPinDefault(mathId, 0, "55"));

    const int rateNode = static_cast<int>(system.emitters[0].emitterUpdate.front().id);
    const int ratePin = InputPinIndexUVE(graph, rateNode, "Rate");
    ASSERT_GT(ratePin, 0);
    std::string why;
    ASSERT_TRUE(graph.canConnect(mathId, 0, rateNode, ratePin, &why)) << why;
    ASSERT_TRUE(graph.connect(mathId, 0, rateNode, ratePin));

    const ParticleSystemAssetUVE wired = ApplyGraphUVE(system, graph);
    const Asset::ParticleModuleUVE& wiredRate = wired.emitters[0].emitterUpdate.front();
    ASSERT_TRUE(wiredRate.floats.contains("Rate"));
    EXPECT_EQ(wiredRate.floats.at("Rate").kind, Asset::ParticleValueKindUVE::Wire);
    EXPECT_EQ(wiredRate.floats.at("Rate").wireNode, static_cast<std::uint32_t>(mathId));
    ASSERT_EQ(wired.emitters[0].mathNodes.size(), 1U);
    EXPECT_FLOAT_EQ(wired.emitters[0].mathNodes[0].constant, 55.0F);
    const CompiledUVE compiledWired = CompileUVE(wired);
    ASSERT_TRUE(compiledWired.ok);
    EXPECT_EQ(compiledWired.program.emitterUpdateOps.front().rate.kind, Asset::ParticleValueKindUVE::Wire);

    ASSERT_TRUE(graph.disconnectPin(rateNode, ratePin, true));
    const ParticleSystemAssetUVE unwired = ApplyGraphUVE(wired, graph);
    EXPECT_EQ(unwired.emitters[0].emitterUpdate.front().floats.at("Rate").kind,
              Asset::ParticleValueKindUVE::Constant);
    const CompiledUVE compiledUnwired = CompileUVE(unwired);
    ASSERT_TRUE(compiledUnwired.ok);
    EXPECT_EQ(compiledUnwired.program.emitterUpdateOps.front().rate.kind, Asset::ParticleValueKindUVE::Constant);
}

TEST(VfxGraphAuthorityUVETest, ConnectionRulesRejectCrossStageChainsAndUnwirableInputs) {
    EnsurePrismHostedUVE();
    const ParticleSystemAssetUVE system = VFX::CreateParticleTemplateUVE(ParticleTemplateKindUVE::Fountain);
    const std::vector<prism::BlueprintNodeSpec> library = BuildVfxGraphNodeLibraryUVE(system, true);
    prism::BlueprintGraph graph;
    InstallRulesUVE(graph);
    BuildEmitterGraphUVE(system, 0U, graph);

    const int gravity = SpawnVfxGraphNodeUVE(graph, *FindSpecUVE(library, "Gravity"), prism::Vec2{0.0F, 900.0F});
    std::string why;
    EXPECT_FALSE(graph.canConnect(gravity, 0, kVfxStageNodeRenderUVE, 0, &why));
    EXPECT_FALSE(why.empty());
    EXPECT_TRUE(graph.canConnect(gravity, 0, kVfxStageNodeParticleUpdateUVE, 0, &why)) << why;

    // A module that already feeds the chain cannot also feed a second node.
    const int second = SpawnVfxGraphNodeUVE(graph, *FindSpecUVE(library, "Drag"), prism::Vec2{300.0F, 900.0F});
    const Asset::ParticleModuleUVE& tail = system.emitters[0].particleUpdate.back();
    EXPECT_FALSE(graph.canConnect(static_cast<int>(tail.id), 0, second, 0, &why));

    // Math results only feed Float inputs of a module.
    const int math = SpawnVfxGraphNodeUVE(graph, *FindSpecUVE(library, "Value: Float"), prism::Vec2{0.0F, 1000.0F});
    const int shapeNode = static_cast<int>(system.emitters[0].particleSpawn.front().id);
    for (std::size_t pin = 1U; pin < graph.findNode(shapeNode)->inputs.size(); ++pin) {
        const prism::BlueprintPin& p = graph.findNode(shapeNode)->inputs[pin];
        if (p.type != PT::Float) {
            EXPECT_FALSE(graph.canConnect(math, 0, shapeNode, static_cast<int>(pin), nullptr)) << p.name;
        }
    }
}

TEST(VfxGraphAuthorityUVETest, StageOutputNodesCannotBeDeleted) {
    EnsurePrismHostedUVE();
    const ParticleSystemAssetUVE system = VFX::CreateParticleTemplateUVE(ParticleTemplateKindUVE::Fountain);
    prism::BlueprintGraph graph;
    InstallRulesUVE(graph);
    BuildEmitterGraphUVE(system, 0U, graph);
    graph.select(kVfxStageNodeRenderUVE);
    graph.removeSelected();
    EXPECT_NE(graph.findNode(kVfxStageNodeRenderUVE), nullptr);
}

TEST(VfxGraphAuthorityUVETest, DetachedModulesSurviveSaveLoadAndGraphRebuild) {
    EnsurePrismHostedUVE();
    ParticleSystemAssetUVE system = VFX::CreateParticleTemplateUVE(ParticleTemplateKindUVE::Fountain);
    Asset::ParticleModuleUVE spare = Asset::MakeDefaultParticleModuleUVE(500U, ParticleModuleTypeUVE::Drag, 40.0F, 900.0F);
    spare.floats["Coefficient"].kind = Asset::ParticleValueKindUVE::Constant;
    spare.floats["Coefficient"].constant = 3.0F;
    system.emitters[0].detached.push_back(spare);
    ASSERT_TRUE(Asset::IsParticleSystemAssetValidUVE(system));

    const std::filesystem::path path = ::UVE::Tests::ScratchPathUVE("detached_roundtrip.uvfx");
    ASSERT_TRUE(Asset::SaveParticleSystemAssetUVE(system, path));
    ParticleSystemAssetUVE loaded;
    ASSERT_TRUE(Asset::LoadParticleSystemAssetUVE(path, loaded));
    ASSERT_EQ(loaded.emitters[0].detached.size(), 1U);
    EXPECT_TRUE(loaded.emitters[0].detached[0] == spare);

    // It is a node in the rebuilt graph, with no chain wire, and the compiled program ignores it.
    prism::BlueprintGraph graph;
    BuildEmitterGraphUVE(loaded, 0U, graph);
    ASSERT_NE(graph.findNode(500), nullptr);
    EXPECT_FALSE(graph.isPinConnected(500, 0, true));
    EXPECT_FALSE(graph.isPinConnected(500, 0, false));
    const CompiledUVE with = CompileUVE(loaded);
    ParticleSystemAssetUVE without = loaded;
    without.emitters[0].detached.clear();
    const CompiledUVE base = CompileUVE(without);
    ASSERT_TRUE(with.ok);
    ASSERT_TRUE(base.ok);
    EXPECT_EQ(with.program.particleUpdateOps.size(), base.program.particleUpdateOps.size());

    // A detached module still has to be a valid module.
    ParticleSystemAssetUVE bad = system;
    bad.emitters[0].detached[0].floats["NotAnInput"] = Asset::ParticleFloatInputUVE{};
    EXPECT_FALSE(Asset::IsParticleSystemAssetValidUVE(bad));
}

} // namespace
} // namespace UVE::Editor::Tests
