// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
#pragma once

#include "particle_system_asset.h"
#include "vfx_program.h"

#include "prism/BlueprintGraph.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace UVE::Editor {

/// Fixed stage output node IDs inside the VFX Emitter Graph (`BlueprintGraphPreset::ParticleGraph`).
inline constexpr int kVfxStageNodeEmitterUpdateUVE = 9001;
inline constexpr int kVfxStageNodeParticleSpawnUVE = 9002;
inline constexpr int kVfxStageNodeParticleUpdateUVE = 9003;
inline constexpr int kVfxStageNodeRenderUVE = 9004;

/// Returns true when `nodeId` is one of the 4 fixed stage output nodes.
[[nodiscard]] constexpr bool IsVfxStageNodeIdUVE(const int nodeId) noexcept {
    return nodeId == kVfxStageNodeEmitterUpdateUVE || nodeId == kVfxStageNodeParticleSpawnUVE ||
           nodeId == kVfxStageNodeParticleUpdateUVE || nodeId == kVfxStageNodeRenderUVE;
}

/// Builds the PRISM `BlueprintGraph` node/wire representation for `system.emitters[emitterIndex]`,
/// placing the four fixed stage output nodes and chaining each stage's modules into them.
void BuildEmitterGraphUVE(const Asset::ParticleSystemAssetUVE& system,
                          std::size_t emitterIndex,
                          prism::BlueprintGraph& graph);

/// Extracts an emitter's module chains, input defaults, wire connections, and positions from
/// `graph`, preserving curves, gradients, random ranges, and non-pin module properties from
/// `currentSystem.emitters[emitterIndex]`.
[[nodiscard]] bool ExtractEmitterFromGraphUVE(const prism::BlueprintGraph& graph,
                                              const Asset::ParticleSystemAssetUVE& currentSystem,
                                              std::size_t emitterIndex,
                                              Asset::ParticleEmitterAssetUVE& outEmitter);

/// Builds the searchable node palette for the VFX Graph (`Emitter Update`, `Particle Spawn`,
/// `Particle Update`, `Render`, and optionally `Math / Value` when `includeMathNodes` is true).
[[nodiscard]] std::vector<prism::BlueprintNodeSpec> BuildVfxGraphNodeLibraryUVE(
    const Asset::ParticleSystemAssetUVE& system,
    bool includeMathNodes = false);

/// Creates the node described by `spec` (from `BuildVfxGraphNodeLibraryUVE`) at `world` and returns its
/// id, or -1 if `spec` makes no node. The id never collides with a stage output node or another node.
int SpawnVfxGraphNodeUVE(prism::BlueprintGraph& graph, const prism::BlueprintNodeSpec& spec, prism::Vec2 world);

/// The VFX graph's own connection rules, installed as `BlueprintGraph::canConnectHook`:
///  - a module's chain `Out` only feeds a module of the same stage, or that stage's output node;
///  - a chain `Out` feeds one node, and a stage output node takes one chain;
///  - only Float inputs may be wired from Math / Value nodes (other input kinds have no wire form).
[[nodiscard]] bool CanConnectVfxPinsUVE(const prism::BlueprintGraph& graph,
                                        int fromNode, int fromPin, int toNode, int toPin,
                                        std::string* why);

/// Applies compiler diagnostics (`ParticleCompileMessageUVE`) onto `graph` as a `BlueprintCompileReport`
/// so nodes with errors or warnings display red/yellow badges on canvas.
void ApplyVfxCompileMessagesToGraphUVE(prism::BlueprintGraph& graph,
                                       const std::vector<VFX::ParticleCompileMessageUVE>& messages);

} // namespace UVE::Editor
