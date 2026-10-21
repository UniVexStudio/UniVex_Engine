// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cstddef>
#include <span>
#include <string>
#include <vector>

#include "uve/asset/mesh_asset_uve.h"

namespace UVE::Scene {

/// How many named weights one component may carry. Same bound as the mesh's own target cap: a
/// component cannot meaningfully address more shapes than the asset it is attached to may hold, and
/// mirroring the number here is what keeps a rejected asset from being reachable through a
/// component that outgrew it.
inline constexpr std::size_t kMaximumMorphTargetWeightsUVE = Asset::kMaximumMeshMorphTargetsUVE;

/// One blend shape's current value, addressed BY NAME rather than by index.
///
/// Name addressing is what makes the component survive the things that actually happen to assets:
/// re-exporting a face mesh with an added shape shifts every index after the insertion point, and
/// an index-addressed component would silently animate the wrong muscle. A name that no longer
/// exists is then a detectable, reportable miss rather than a silent misdrive.
struct MorphTargetWeightUVE final {
    std::string target;
    float weight = 0.0F;

    [[nodiscard]] bool operator==(const MorphTargetWeightUVE&) const noexcept = default;
};

/// The blend-shape state of one mesh instance: a set of named weights and the switch that pauses
/// their effect.
///
/// Weights are sparse over the mesh's shapes: an entry means "drive this shape", and a shape with
/// no entry stays at whatever the mesh's own defaultMorphWeights say. That is deliberate - a
/// 52-shape ARKit rig spends nearly all of its time with three or four shapes off zero, and
/// storing 52 floats per entity to express that would make every face in a crowd pay for the
/// animation of one.
///
/// `enabled` exists so a state machine, a game pause, or a "reset face" operation can stop the
/// effect without destroying the authored weights - the same reason the engine's other modifiers
/// carry theirs.
struct MorphTargetComponentUVE final {
    std::vector<MorphTargetWeightUVE> weights;
    bool enabled = true;

    [[nodiscard]] bool operator==(const MorphTargetComponentUVE&) const noexcept = default;
};

/// Validates the component's own structure: the size cap, non-empty and unique names, finite
/// weights. Says nothing about whether the names exist on any particular mesh - that is a question
/// only a mesh can answer, and it is TryResolveMorphTargetWeightsUVE that asks it.
[[nodiscard]] bool IsMorphTargetComponentValidUVE(const MorphTargetComponentUVE& component) noexcept;

/// Resolves this component into the flat, index-addressed weight array a mesh's morph evaluation
/// consumes: `outWeights[i]` is the weight of `targets[i]`.
///
/// The base array is the target weight each shape starts at - `defaults` when the caller has the
/// mesh's defaultMorphWeights, otherwise zeroes - and every entry in the component overwrites the
/// shape with the matching name. Order inside the component therefore does not matter, which is
/// what lets an author, an animation track and a gameplay script all write the same shape without
/// last-writer-wins depending on which list was built first.
///
/// Returns false, leaving `outWeights` untouched, when the component is structurally invalid, when
/// `defaults` is non-empty and does not match `targets`, when any default is non-finite, or when an
/// entry names a shape the mesh does not have. A name miss is a hard failure on purpose: silently
/// dropping it would let a mistyped "jawOpen" play as a perfectly still face with no diagnostic.
[[nodiscard]] bool TryResolveMorphTargetWeightsUVE(const MorphTargetComponentUVE& component,
                                                   std::span<const Asset::MeshMorphTargetUVE> targets,
                                                   std::span<const float> defaults,
                                                   std::vector<float>& outWeights);

/// Applies the component's weights to `bindVertices` through the mesh's shapes.
///
/// This is the one call a runtime mesh path needs: it resolves names to indices and then evaluates
/// them with the CPU authority in AccumulateMorphTargetsUVE, so nothing that drives blend shapes
/// has to know how the accumulation works. A disabled component still produces `outVertices` - it
/// produces the mesh at its DEFAULT weights, because "disabled" means "this component contributes
/// nothing", not "the mesh has no face".
[[nodiscard]] bool EvaluateMorphTargetsUVE(const MorphTargetComponentUVE& component,
                                            const Asset::MeshAssetUVE& mesh,
                                            std::span<const Asset::MeshVertexUVE> bindVertices,
                                            std::vector<Asset::MeshVertexUVE>& outVertices);

} // namespace UVE::Scene
