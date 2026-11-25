// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "entity.h"
#include "object_3d_common.h"

namespace UVE::Scene {

class IEntityManager;

inline constexpr std::size_t kMaximumStateTreeStates3D = 64U;
inline constexpr std::size_t kMaximumStateTreeTransitions3D = 128U;

struct StateTreeParameter3D final {
    std::string key{};
    float value = 0.0F;

    [[nodiscard]] bool operator==(const StateTreeParameter3D&) const = default;
};

struct StateTreeTransition3D final {
    std::string fromStateId{};
    std::string toStateId{};
    std::string parameterKey{};
    float minimumParameterValue = 0.5F;
    float minimumDurationSeconds = 0.0F;

    [[nodiscard]] bool operator==(const StateTreeTransition3D&) const = default;
};

/// Hierarchical/modular AI state-machine behavior component (`initialStateId`, `states`,
/// `transitions`, `parameters`).
struct StateTree3DComponent final {
    std::string initialStateId{"Idle"};
    std::vector<std::string> states{"Idle"};
    std::vector<StateTreeTransition3D> transitions{};
    std::vector<StateTreeParameter3D> parameters{};
    bool enabled = true;

    // Runtime state, never persisted:
    std::string activeStateId{};
    float activeStateElapsedSeconds = 0.0F;
    std::uint32_t transitionCount = 0U;

    [[nodiscard]] bool operator==(const StateTree3DComponent& other) const noexcept {
        return initialStateId == other.initialStateId && states == other.states &&
               transitions == other.transitions && parameters == other.parameters &&
               enabled == other.enabled;
    }
};

[[nodiscard]] bool IsStateTree3DComponentValid(const StateTree3DComponent& value) noexcept;

[[nodiscard]] StateTree3DComponent SanitizeStateTree3DComponent(
    const StateTree3DComponent& value);

struct StateTree3DObjectDefinition final {
    static constexpr std::string_view defaultName = "StateTree3D";
    StateTree3DComponent stateTree{};
};

[[nodiscard]] bool IsStateTree3DObjectDefinitionValid(
    const StateTree3DObjectDefinition& value) noexcept;

void ApplyStateTree3DObjectDefinition(IEntityManager& entityManager, Entity entity,
                                         const StateTree3DObjectDefinition& value);

class StateTree3D final {
public:
    StateTree3D() = delete;

    static void SetParameter(StateTree3DComponent& tree, std::string_view key, float value);

    [[nodiscard]] static float GetParameter(const StateTree3DComponent& tree,
                                               std::string_view key,
                                               float fallback = 0.0F) noexcept;

    /// Evaluates one step (`deltaTimeSeconds`) on `tree`, returning true when a transition fired.
    static bool Step(StateTree3DComponent& tree, float deltaTimeSeconds);
};

std::size_t UpdateStateTree3DObjects(IEntityManager& entityManager, float deltaTimeSeconds);

} // namespace UVE::Scene
