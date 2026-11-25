// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include "animation_clip.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace UVE::Core {

enum class AnimationInterruptionPolicy : std::uint8_t {
    None = 0,
    Source,
    Target,
    SourceOrTarget,
};

struct AnimationState final {
    std::string stateId;
    std::string clipId;
    std::string syncGroupId;
    std::string layerMaskId;
    float speed = 1.0F;
};

/// Controls whether a transition already in progress may be replaced by an eligible transition
/// from its source state, target state, both, or neither. Replacement preserves deterministic
/// priority/identifier selection and starts the replacement crossfade from zero alpha.
struct AnimationTransition final {
    std::string transitionId;
    std::string sourceStateId;
    std::string targetStateId;
    float durationSeconds = 0.0F;
    float exitTime = 0.0F;
    std::int32_t priority = 0;
    AnimationInterruptionPolicy interruption = AnimationInterruptionPolicy::None;
};

struct AnimationStateMachine final {
    static constexpr std::size_t kMaximumStates = 256U;
    static constexpr std::size_t kMaximumTransitions = 512U;

    std::vector<AnimationState> states;
    std::vector<AnimationTransition> transitions;
    std::vector<AnimationClip> clips;
    std::string initialStateId;
};

enum class AnimationStateMachineValidationCode : std::uint8_t {
    Valid = 0,
    EmptyMachine,
    CapacityExceeded,
    InvalidState,
    DuplicateState,
    UnknownClip,
    InvalidTransition,
    DuplicateTransition,
    UnknownState,
    InvalidDuration,
    InvalidExitTime,
    MissingInitialState,
};

struct AnimationStateMachineValidationResult final {
    AnimationStateMachineValidationCode code = AnimationStateMachineValidationCode::EmptyMachine;
    std::string identifier;
    std::string message;

    [[nodiscard]] bool IsValid() const noexcept {
        return code == AnimationStateMachineValidationCode::Valid;
    }
};

struct AnimationStateMachineDebugSnapshot final {
    std::string currentStateId;
    std::string targetStateId;
    std::string activeTransitionId;
    std::string syncGroupId;
    std::string layerMaskId;
    double normalizedTime = 0.0;
    double transitionAlpha = 0.0;
    std::int32_t selectedTransitionPriority = 0;
    bool transitioning = false;
};

struct AnimationStateMachineEvaluationResult final {
    TransformPose pose;
    AnimationStateMachineDebugSnapshot debug;
    bool usedOutput = false;
    std::string message;

    [[nodiscard]] bool IsSuccess() const noexcept {
        return usedOutput;
    }
};

[[nodiscard]] AnimationStateMachineValidationResult ValidateAnimationStateMachine(
    const AnimationStateMachine& machine) noexcept;

class AnimationStateMachineEvaluator final {
public:
    explicit AnimationStateMachineEvaluator(const AnimationStateMachine& machine);
    AnimationStateMachineEvaluator(AnimationStateMachine&&) = delete;

    [[nodiscard]] AnimationStateMachineEvaluationResult Evaluate(double deltaSeconds);
    [[nodiscard]] const AnimationStateMachineDebugSnapshot& GetDebugSnapshot() const noexcept;

private:
    [[nodiscard]] const AnimationState* FindState(const std::string& stateId) const noexcept;
    [[nodiscard]] const AnimationTransition* SelectTransition(
        const AnimationState& source) const noexcept;
    [[nodiscard]] const AnimationTransition* SelectInterruptionTransition(
        const AnimationTransition& activeTransition) const noexcept;
    [[nodiscard]] bool SampleState(const AnimationState& state, double timeSeconds,
                                      TransformPose& outPose) const noexcept;

    const AnimationStateMachine& m_machine;
    AnimationStateMachineDebugSnapshot m_debug;
};

} // namespace UVE::Core
