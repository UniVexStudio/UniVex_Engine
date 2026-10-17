// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "uve/component/entity_uve.h"
#include "uve/objects/3d/object_3d_common_uve.h"

namespace UVE::Scene {

class IEntityManagerUVE;

inline constexpr std::size_t kMaximumStateTreeStates3DUVE = 64U;
inline constexpr std::size_t kMaximumStateTreeTransitions3DUVE = 128U;

struct StateTreeParameter3DUVE final {
    std::string key{};
    float value = 0.0F;

    [[nodiscard]] bool operator==(const StateTreeParameter3DUVE&) const = default;
};

struct StateTreeTransition3DUVE final {
    std::string fromStateId{};
    std::string toStateId{};
    std::string parameterKey{};
    float minimumParameterValue = 0.5F;
    float minimumDurationSeconds = 0.0F;

    [[nodiscard]] bool operator==(const StateTreeTransition3DUVE&) const = default;
};

/// Hierarchical/modular AI state-machine behavior component (`initialStateId`, `states`,
/// `transitions`, `parameters`).
struct StateTree3DComponentUVE final {
    std::string initialStateId{"Idle"};
    std::vector<std::string> states{"Idle"};
    std::vector<StateTreeTransition3DUVE> transitions{};
    std::vector<StateTreeParameter3DUVE> parameters{};
    bool enabled = true;

    // Runtime state, never persisted:
    std::string activeStateId{};
    float activeStateElapsedSeconds = 0.0F;
    std::uint32_t transitionCount = 0U;

    [[nodiscard]] bool operator==(const StateTree3DComponentUVE& other) const noexcept {
        return initialStateId == other.initialStateId && states == other.states &&
               transitions == other.transitions && parameters == other.parameters &&
               enabled == other.enabled;
    }
};

[[nodiscard]] bool IsStateTree3DComponentValidUVE(const StateTree3DComponentUVE& value) noexcept;

[[nodiscard]] StateTree3DComponentUVE SanitizeStateTree3DComponentUVE(
    const StateTree3DComponentUVE& value);

struct StateTree3DObjectDefinitionUVE final {
    static constexpr std::string_view defaultName = "StateTree3D";
    StateTree3DComponentUVE stateTree{};
};

[[nodiscard]] bool IsStateTree3DObjectDefinitionValidUVE(
    const StateTree3DObjectDefinitionUVE& value) noexcept;

void ApplyStateTree3DObjectDefinitionUVE(IEntityManagerUVE& entityManager, EntityUVE entity,
                                         const StateTree3DObjectDefinitionUVE& value);

class StateTree3DUVE final {
public:
    StateTree3DUVE() = delete;

    static void SetParameterUVE(StateTree3DComponentUVE& tree, std::string_view key, float value);

    [[nodiscard]] static float GetParameterUVE(const StateTree3DComponentUVE& tree,
                                               std::string_view key,
                                               float fallback = 0.0F) noexcept;

    /// Evaluates one step (`deltaTimeSeconds`) on `tree`, returning true when a transition fired.
    static bool StepUVE(StateTree3DComponentUVE& tree, float deltaTimeSeconds);
};

std::size_t UpdateStateTree3DObjectsUVE(IEntityManagerUVE& entityManager, float deltaTimeSeconds);

} // namespace UVE::Scene
