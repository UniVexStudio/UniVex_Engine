// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/component/morph_target_component_uve.h"

#include <algorithm>
#include <cmath>
#include <iterator>

#include "uve/logging/logging_macros_uve.h"

namespace UVE::Scene {

bool IsMorphTargetComponentValidUVE(const MorphTargetComponentUVE& component) noexcept {
    if (component.weights.size() > kMaximumMorphTargetWeightsUVE) {
        return false;
    }
    // A std::unordered_set allocates, and this validator is noexcept. A linear scan is both
    // allocation-free and faster at the sizes that matter: the cap is the mesh's shape cap, and a
    // real face rig drives a handful of shapes at a time.
    for (std::size_t index = 0U; index < component.weights.size(); ++index) {
        const MorphTargetWeightUVE& entry = component.weights[index];
        if (entry.target.empty() || !std::isfinite(entry.weight)) {
            return false;
        }
        for (std::size_t other = 0U; other < index; ++other) {
            if (component.weights[other].target == entry.target) {
                return false;
            }
        }
    }
    return true;
}

bool TryResolveMorphTargetWeightsUVE(const MorphTargetComponentUVE& component,
                                     const std::span<const Asset::MeshMorphTargetUVE> targets,
                                     const std::span<const float> defaults,
                                     std::vector<float>& outWeights) {
    if (!IsMorphTargetComponentValidUVE(component)) {
        UVE_ERROR("MorphTargetComponentUVE: {} weights is structurally invalid (empty or duplicate "
                  "names, non-finite values, or past the {} cap)",
                  component.weights.size(), kMaximumMorphTargetWeightsUVE);
        return false;
    }
    if (!defaults.empty() && defaults.size() != targets.size()) {
        UVE_ERROR("MorphTargetComponentUVE: {} default weights for {} morph targets",
                  defaults.size(), targets.size());
        return false;
    }
    std::vector<float> resolved(targets.size(), 0.0F);
    if (!defaults.empty()) {
        for (std::size_t index = 0U; index < defaults.size(); ++index) {
            if (!std::isfinite(defaults[index])) {
                UVE_ERROR("MorphTargetComponentUVE: default weight {} is non-finite", index);
                return false;
            }
            resolved[index] = defaults[index];
        }
    }
    for (const MorphTargetWeightUVE& entry : component.weights) {
        const auto match = std::find_if(targets.begin(), targets.end(),
                                        [&entry](const Asset::MeshMorphTargetUVE& target) {
                                            return target.name == entry.target;
                                        });
        if (match == targets.end()) {
            UVE_ERROR("MorphTargetComponentUVE: no morph target named \"{}\" on this mesh",
                      entry.target);
            return false;
        }
        resolved[static_cast<std::size_t>(std::distance(targets.begin(), match))] = entry.weight;
    }
    outWeights = std::move(resolved);
    return true;
}

bool EvaluateMorphTargetsUVE(const MorphTargetComponentUVE& component,
                             const Asset::MeshAssetUVE& mesh,
                             const std::span<const Asset::MeshVertexUVE> bindVertices,
                             std::vector<Asset::MeshVertexUVE>& outVertices) {
    // A disabled component contributes nothing, so every shape sits at the mesh's own default and
    // the component's names are never consulted. Resolving them first would fail a paused face
    // against a mesh it merely happens to be stale against, which is the opposite of what
    // "disabled" should mean.
    if (!component.enabled) {
        std::vector<float> defaults(mesh.morphTargets.size(), 0.0F);
        if (!mesh.defaultMorphWeights.empty()) {
            defaults = mesh.defaultMorphWeights;
        }
        return Asset::AccumulateMorphTargetsUVE(bindVertices, mesh.morphTargets, defaults, outVertices);
    }
    std::vector<float> weights;
    if (!TryResolveMorphTargetWeightsUVE(component, mesh.morphTargets, mesh.defaultMorphWeights, weights)) {
        return false;
    }
    return Asset::AccumulateMorphTargetsUVE(bindVertices, mesh.morphTargets, weights, outVertices);
}

} // namespace UVE::Scene
