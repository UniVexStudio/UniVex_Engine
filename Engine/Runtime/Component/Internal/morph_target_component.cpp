// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "morph_target_component.h"

#include <algorithm>
#include <cmath>
#include <iterator>

#include "logging_macros.h"

namespace UVE::Scene {

bool IsMorphTargetComponentValid(const MorphTargetComponent& component) noexcept {
    if (component.weights.size() > kMaximumMorphTargetWeights) {
        return false;
    }
    // A std::unordered_set allocates, and this validator is noexcept. A linear scan is both
    // allocation-free and faster at the sizes that matter: the cap is the mesh's shape cap, and a
    // real face rig drives a handful of shapes at a time.
    for (std::size_t index = 0U; index < component.weights.size(); ++index) {
        const MorphTargetWeight& entry = component.weights[index];
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

bool TryResolveMorphTargetWeights(const MorphTargetComponent& component,
                                     const std::span<const Asset::MeshMorphTarget> targets,
                                     const std::span<const float> defaults,
                                     std::vector<float>& outWeights) {
    if (!IsMorphTargetComponentValid(component)) {
        UVE_ERROR("MorphTargetComponent: {} weights is structurally invalid (empty or duplicate "
                  "names, non-finite values, or past the {} cap)",
                  component.weights.size(), kMaximumMorphTargetWeights);
        return false;
    }
    if (!defaults.empty() && defaults.size() != targets.size()) {
        UVE_ERROR("MorphTargetComponent: {} default weights for {} morph targets",
                  defaults.size(), targets.size());
        return false;
    }
    std::vector<float> resolved(targets.size(), 0.0F);
    if (!defaults.empty()) {
        for (std::size_t index = 0U; index < defaults.size(); ++index) {
            if (!std::isfinite(defaults[index])) {
                UVE_ERROR("MorphTargetComponent: default weight {} is non-finite", index);
                return false;
            }
            resolved[index] = defaults[index];
        }
    }
    for (const MorphTargetWeight& entry : component.weights) {
        const auto match = std::find_if(targets.begin(), targets.end(),
                                        [&entry](const Asset::MeshMorphTarget& target) {
                                            return target.name == entry.target;
                                        });
        if (match == targets.end()) {
            UVE_ERROR("MorphTargetComponent: no morph target named \"{}\" on this mesh",
                      entry.target);
            return false;
        }
        resolved[static_cast<std::size_t>(std::distance(targets.begin(), match))] = entry.weight;
    }
    outWeights = std::move(resolved);
    return true;
}

bool EvaluateMorphTargets(const MorphTargetComponent& component,
                             const Asset::MeshAsset& mesh,
                             const std::span<const Asset::MeshVertex> bindVertices,
                             std::vector<Asset::MeshVertex>& outVertices) {
    // A disabled component contributes nothing, so every shape sits at the mesh's own default and
    // the component's names are never consulted. Resolving them first would fail a paused face
    // against a mesh it merely happens to be stale against, which is the opposite of what
    // "disabled" should mean.
    if (!component.enabled) {
        std::vector<float> defaults(mesh.morphTargets.size(), 0.0F);
        if (!mesh.defaultMorphWeights.empty()) {
            defaults = mesh.defaultMorphWeights;
        }
        return Asset::AccumulateMorphTargets(bindVertices, mesh.morphTargets, defaults, outVertices);
    }
    std::vector<float> weights;
    if (!TryResolveMorphTargetWeights(component, mesh.morphTargets, mesh.defaultMorphWeights, weights)) {
        return false;
    }
    return Asset::AccumulateMorphTargets(bindVertices, mesh.morphTargets, weights, outVertices);
}

} // namespace UVE::Scene
