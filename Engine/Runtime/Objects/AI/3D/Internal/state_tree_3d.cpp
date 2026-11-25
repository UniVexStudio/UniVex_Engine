// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "state_tree_3d.h"

#include <algorithm>
#include <cmath>

#include "disabled_component.h"
#include "i_entity_manager.h"
#include "object_3d.h"

namespace UVE::Scene {

bool IsStateTree3DComponentValid(const StateTree3DComponent& value) noexcept {
    if (value.initialStateId.empty() || !IsBounded3DObjectString(value.initialStateId) ||
        value.states.empty() || value.states.size() > kMaximumStateTreeStates3D ||
        value.transitions.size() > kMaximumStateTreeTransitions3D) {
        return false;
    }
    for (const std::string& s : value.states) {
        if (s.empty() || !IsBounded3DObjectString(s)) {
            return false;
        }
    }
    for (const StateTreeTransition3D& tr : value.transitions) {
        if (tr.fromStateId.empty() || tr.toStateId.empty() ||
            !IsBounded3DObjectString(tr.fromStateId) ||
            !IsBounded3DObjectString(tr.toStateId) ||
            !IsBounded3DObjectString(tr.parameterKey) ||
            !std::isfinite(tr.minimumParameterValue) ||
            !std::isfinite(tr.minimumDurationSeconds) || tr.minimumDurationSeconds < 0.0F) {
            return false;
        }
    }
    for (const StateTreeParameter3D& p : value.parameters) {
        if (p.key.empty() || !IsBounded3DObjectString(p.key) || !std::isfinite(p.value)) {
            return false;
        }
    }
    return true;
}

StateTree3DComponent SanitizeStateTree3DComponent(const StateTree3DComponent& value) {
    StateTree3DComponent out = value;
    if (out.initialStateId.empty()) {
        out.initialStateId = "Idle";
    } else if (!IsBounded3DObjectString(out.initialStateId)) {
        out.initialStateId.resize(kMaximum3DObjectStringLength);
    }
    if (out.states.empty()) {
        out.states.push_back(out.initialStateId);
    } else if (out.states.size() > kMaximumStateTreeStates3D) {
        out.states.resize(kMaximumStateTreeStates3D);
    }
    for (std::string& s : out.states) {
        if (s.empty()) {
            s = "Idle";
        } else if (!IsBounded3DObjectString(s)) {
            s.resize(kMaximum3DObjectStringLength);
        }
    }
    if (out.transitions.size() > kMaximumStateTreeTransitions3D) {
        out.transitions.resize(kMaximumStateTreeTransitions3D);
    }
    for (StateTreeTransition3D& tr : out.transitions) {
        if (!std::isfinite(tr.minimumParameterValue)) {
            tr.minimumParameterValue = 0.5F;
        }
        if (!std::isfinite(tr.minimumDurationSeconds) || tr.minimumDurationSeconds < 0.0F) {
            tr.minimumDurationSeconds = 0.0F;
        }
    }
    std::erase_if(out.parameters, [](const StateTreeParameter3D& p) {
        return p.key.empty() || !std::isfinite(p.value);
    });
    return out;
}

bool IsStateTree3DObjectDefinitionValid(const StateTree3DObjectDefinition& value) noexcept {
    return IsStateTree3DComponentValid(value.stateTree);
}

void ApplyStateTree3DObjectDefinition(IEntityManager& entityManager, const Entity entity,
                                         const StateTree3DObjectDefinition& value) {
    EnsureObject3DBaseline(entityManager, entity, StateTree3DObjectDefinition::defaultName);
    entityManager.AddComponent<StateTree3DComponent>(
        entity, SanitizeStateTree3DComponent(value.stateTree));
}

void StateTree3D::SetParameter(StateTree3DComponent& tree, const std::string_view key,
                                     const float value) {
    if (key.empty() || !std::isfinite(value)) {
        return;
    }
    for (StateTreeParameter3D& p : tree.parameters) {
        if (p.key == key) {
            p.value = value;
            return;
        }
    }
    tree.parameters.push_back(StateTreeParameter3D{std::string(key), value});
}

float StateTree3D::GetParameter(const StateTree3DComponent& tree,
                                      const std::string_view key, const float fallback) noexcept {
    for (const StateTreeParameter3D& p : tree.parameters) {
        if (p.key == key) {
            return p.value;
        }
    }
    return fallback;
}

bool StateTree3D::Step(StateTree3DComponent& tree, const float deltaTimeSeconds) {
    if (!tree.enabled || !IsStateTree3DComponentValid(tree)) {
        return false;
    }
    if (tree.activeStateId.empty()) {
        tree.activeStateId = tree.initialStateId;
        tree.activeStateElapsedSeconds = 0.0F;
    }
    if (std::isfinite(deltaTimeSeconds) && deltaTimeSeconds > 0.0F) {
        tree.activeStateElapsedSeconds += deltaTimeSeconds;
    }
    for (const StateTreeTransition3D& tr : tree.transitions) {
        if (tr.fromStateId != tree.activeStateId) {
            continue;
        }
        if (tree.activeStateElapsedSeconds < tr.minimumDurationSeconds) {
            continue;
        }
        if (!tr.parameterKey.empty() &&
            GetParameter(tree, tr.parameterKey, 0.0F) < tr.minimumParameterValue) {
            continue;
        }
        tree.activeStateId = tr.toStateId;
        tree.activeStateElapsedSeconds = 0.0F;
        ++tree.transitionCount;
        return true;
    }
    return false;
}

std::size_t UpdateStateTree3DObjects(IEntityManager& entityManager,
                                        const float deltaTimeSeconds) {
    std::size_t transitionedCount = 0U;
    entityManager.ForEach<StateTree3DComponent>(
        [&](const Entity entity, StateTree3DComponent& tree) {
            if (entityManager.HasComponent<DisabledComponent>(entity) || !tree.enabled) {
                return;
            }
            if (StateTree3D::Step(tree, deltaTimeSeconds)) {
                ++transitionedCount;
            }
        });
    return transitionedCount;
}

} // namespace UVE::Scene
