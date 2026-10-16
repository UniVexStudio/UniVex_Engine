// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/objects/3d/state_tree_3d_uve.h"

#include <algorithm>
#include <cmath>

#include "uve/component/disabled_component_uve.h"
#include "uve/entity/i_entity_manager_uve.h"
#include "uve/objects/3d/object_3d_uve.h"

namespace UVE::Scene {

bool IsStateTree3DComponentValidUVE(const StateTree3DComponentUVE& value) noexcept {
    if (value.initialStateId.empty() || !IsBounded3DObjectStringUVE(value.initialStateId) ||
        value.states.empty() || value.states.size() > kMaximumStateTreeStates3DUVE ||
        value.transitions.size() > kMaximumStateTreeTransitions3DUVE) {
        return false;
    }
    for (const std::string& s : value.states) {
        if (s.empty() || !IsBounded3DObjectStringUVE(s)) {
            return false;
        }
    }
    for (const StateTreeTransition3DUVE& tr : value.transitions) {
        if (tr.fromStateId.empty() || tr.toStateId.empty() ||
            !IsBounded3DObjectStringUVE(tr.fromStateId) ||
            !IsBounded3DObjectStringUVE(tr.toStateId) ||
            !IsBounded3DObjectStringUVE(tr.parameterKey) ||
            !std::isfinite(tr.minimumParameterValue) ||
            !std::isfinite(tr.minimumDurationSeconds) || tr.minimumDurationSeconds < 0.0F) {
            return false;
        }
    }
    for (const StateTreeParameter3DUVE& p : value.parameters) {
        if (p.key.empty() || !IsBounded3DObjectStringUVE(p.key) || !std::isfinite(p.value)) {
            return false;
        }
    }
    return true;
}

StateTree3DComponentUVE SanitizeStateTree3DComponentUVE(const StateTree3DComponentUVE& value) {
    StateTree3DComponentUVE out = value;
    if (out.initialStateId.empty()) {
        out.initialStateId = "Idle";
    } else if (!IsBounded3DObjectStringUVE(out.initialStateId)) {
        out.initialStateId.resize(kMaximum3DObjectStringLengthUVE);
    }
    if (out.states.empty()) {
        out.states.push_back(out.initialStateId);
    } else if (out.states.size() > kMaximumStateTreeStates3DUVE) {
        out.states.resize(kMaximumStateTreeStates3DUVE);
    }
    for (std::string& s : out.states) {
        if (s.empty()) {
            s = "Idle";
        } else if (!IsBounded3DObjectStringUVE(s)) {
            s.resize(kMaximum3DObjectStringLengthUVE);
        }
    }
    if (out.transitions.size() > kMaximumStateTreeTransitions3DUVE) {
        out.transitions.resize(kMaximumStateTreeTransitions3DUVE);
    }
    for (StateTreeTransition3DUVE& tr : out.transitions) {
        if (!std::isfinite(tr.minimumParameterValue)) {
            tr.minimumParameterValue = 0.5F;
        }
        if (!std::isfinite(tr.minimumDurationSeconds) || tr.minimumDurationSeconds < 0.0F) {
            tr.minimumDurationSeconds = 0.0F;
        }
    }
    std::erase_if(out.parameters, [](const StateTreeParameter3DUVE& p) {
        return p.key.empty() || !std::isfinite(p.value);
    });
    return out;
}

bool IsStateTree3DObjectDefinitionValidUVE(const StateTree3DObjectDefinitionUVE& value) noexcept {
    return IsStateTree3DComponentValidUVE(value.stateTree);
}

void ApplyStateTree3DObjectDefinitionUVE(IEntityManagerUVE& entityManager, const EntityUVE entity,
                                         const StateTree3DObjectDefinitionUVE& value) {
    EnsureObject3DBaselineUVE(entityManager, entity, StateTree3DObjectDefinitionUVE::defaultName);
    entityManager.AddComponentUVE<StateTree3DComponentUVE>(
        entity, SanitizeStateTree3DComponentUVE(value.stateTree));
}

void StateTree3DUVE::SetParameterUVE(StateTree3DComponentUVE& tree, const std::string_view key,
                                     const float value) {
    if (key.empty() || !std::isfinite(value)) {
        return;
    }
    for (StateTreeParameter3DUVE& p : tree.parameters) {
        if (p.key == key) {
            p.value = value;
            return;
        }
    }
    tree.parameters.push_back(StateTreeParameter3DUVE{std::string(key), value});
}

float StateTree3DUVE::GetParameterUVE(const StateTree3DComponentUVE& tree,
                                      const std::string_view key, const float fallback) noexcept {
    for (const StateTreeParameter3DUVE& p : tree.parameters) {
        if (p.key == key) {
            return p.value;
        }
    }
    return fallback;
}

bool StateTree3DUVE::StepUVE(StateTree3DComponentUVE& tree, const float deltaTimeSeconds) {
    if (!tree.enabled || !IsStateTree3DComponentValidUVE(tree)) {
        return false;
    }
    if (tree.activeStateId.empty()) {
        tree.activeStateId = tree.initialStateId;
        tree.activeStateElapsedSeconds = 0.0F;
    }
    if (std::isfinite(deltaTimeSeconds) && deltaTimeSeconds > 0.0F) {
        tree.activeStateElapsedSeconds += deltaTimeSeconds;
    }
    for (const StateTreeTransition3DUVE& tr : tree.transitions) {
        if (tr.fromStateId != tree.activeStateId) {
            continue;
        }
        if (tree.activeStateElapsedSeconds < tr.minimumDurationSeconds) {
            continue;
        }
        if (!tr.parameterKey.empty() &&
            GetParameterUVE(tree, tr.parameterKey, 0.0F) < tr.minimumParameterValue) {
            continue;
        }
        tree.activeStateId = tr.toStateId;
        tree.activeStateElapsedSeconds = 0.0F;
        ++tree.transitionCount;
        return true;
    }
    return false;
}

std::size_t UpdateStateTree3DObjectsUVE(IEntityManagerUVE& entityManager,
                                        const float deltaTimeSeconds) {
    std::size_t transitionedCount = 0U;
    entityManager.ForEachUVE<StateTree3DComponentUVE>(
        [&](const EntityUVE entity, StateTree3DComponentUVE& tree) {
            if (entityManager.HasComponentUVE<DisabledComponentUVE>(entity) || !tree.enabled) {
                return;
            }
            if (StateTree3DUVE::StepUVE(tree, deltaTimeSeconds)) {
                ++transitionedCount;
            }
        });
    return transitionedCount;
}

} // namespace UVE::Scene
