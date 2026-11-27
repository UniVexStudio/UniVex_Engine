// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "animation_state_machine.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace UVE::Core {
namespace {

constexpr std::size_t kMaximumIdentifierBytes = 128U;

[[nodiscard]] bool IsBoundedIdentifier(const std::string& value) noexcept {
    return !value.empty() && value.size() <= kMaximumIdentifierBytes;
}

[[nodiscard]] const AnimationClip* FindClip(const AnimationStateMachine& machine,
                                                  const std::string& clipId) noexcept {
    const auto iterator = std::find_if(machine.clips.cbegin(), machine.clips.cend(), [&clipId](const auto& clip) {
        return clip.clipId == clipId;
    });
    return iterator == machine.clips.cend() ? nullptr : &*iterator;
}

[[nodiscard]] bool IsFiniteRange(const float value, const float minimum, const float maximum) noexcept {
    return std::isfinite(value) && value >= minimum && value <= maximum;
}

} // namespace

AnimationStateMachineValidationResult ValidateAnimationStateMachine(
    const AnimationStateMachine& machine) noexcept {
    if (machine.states.empty() || machine.initialStateId.empty()) {
        return {AnimationStateMachineValidationCode::EmptyMachine, {},
                "Animation state machine requires states and an initial state."};
    }
    if (machine.states.size() > AnimationStateMachine::kMaximumStates ||
        machine.transitions.size() > AnimationStateMachine::kMaximumTransitions) {
        return {AnimationStateMachineValidationCode::CapacityExceeded, {},
                "Animation state machine exceeds the bounded state or transition limit."};
    }
    std::vector<std::string> stateIds;
    stateIds.reserve(machine.states.size());
    for (const AnimationState& state : machine.states) {
        if (!IsBoundedIdentifier(state.stateId) || !IsBoundedIdentifier(state.clipId) ||
            ((!IsBoundedIdentifier(state.syncGroupId) && !state.syncGroupId.empty())) ||
            ((!IsBoundedIdentifier(state.layerMaskId) && !state.layerMaskId.empty())) ||
            !IsFiniteRange(state.speed, 0.0F, std::numeric_limits<float>::max())) {
            return {AnimationStateMachineValidationCode::InvalidState, state.stateId,
                    "Animation state identity, optional metadata, or speed is invalid."};
        }
        if (std::find(stateIds.begin(), stateIds.end(), state.stateId) != stateIds.end()) {
            return {AnimationStateMachineValidationCode::DuplicateState, state.stateId,
                    "Animation state identifiers must be unique."};
        }
        stateIds.push_back(state.stateId);
        if (FindClip(machine, state.clipId) == nullptr) {
            return {AnimationStateMachineValidationCode::UnknownClip, state.clipId,
                    "Animation state references an unknown AnimationClip."};
        }
    }
    if (std::find(stateIds.begin(), stateIds.end(), machine.initialStateId) == stateIds.end()) {
        return {AnimationStateMachineValidationCode::MissingInitialState, machine.initialStateId,
                "Animation state machine initial state is not registered."};
    }
    std::vector<std::string> transitionIds;
    transitionIds.reserve(machine.transitions.size());
    for (const AnimationTransition& transition : machine.transitions) {
        if (!IsBoundedIdentifier(transition.transitionId) ||
            !IsBoundedIdentifier(transition.sourceStateId) ||
            !IsBoundedIdentifier(transition.targetStateId) ||
            transition.sourceStateId == transition.targetStateId) {
            return {AnimationStateMachineValidationCode::InvalidTransition, transition.transitionId,
                    "Animation transition identity or endpoints are invalid."};
        }
        if (std::find(transitionIds.begin(), transitionIds.end(), transition.transitionId) != transitionIds.end()) {
            return {AnimationStateMachineValidationCode::DuplicateTransition, transition.transitionId,
                    "Animation transition identifiers must be unique."};
        }
        if (std::find(stateIds.begin(), stateIds.end(), transition.sourceStateId) == stateIds.end() ||
            std::find(stateIds.begin(), stateIds.end(), transition.targetStateId) == stateIds.end()) {
            return {AnimationStateMachineValidationCode::UnknownState, transition.transitionId,
                    "Animation transition references an unknown state."};
        }
        if (!IsFiniteRange(transition.durationSeconds, 0.0F, std::numeric_limits<float>::max())) {
            return {AnimationStateMachineValidationCode::InvalidDuration, transition.transitionId,
                    "Animation transition duration is invalid."};
        }
        if (!IsFiniteRange(transition.exitTime, 0.0F, 1.0F)) {
            return {AnimationStateMachineValidationCode::InvalidExitTime, transition.transitionId,
                    "Animation transition exit time must be within [0, 1]."};
        }
        transitionIds.push_back(transition.transitionId);
    }
    return {AnimationStateMachineValidationCode::Valid, {}, "Animation state machine is valid."};
}

AnimationStateMachineEvaluator::AnimationStateMachineEvaluator(
    const AnimationStateMachine& machine) : m_machine(machine) {
    m_debug.currentStateId = machine.initialStateId;
}

AnimationStateMachineEvaluationResult AnimationStateMachineEvaluator::Evaluate(
    const double deltaSeconds) {
    AnimationStateMachineEvaluationResult result;
    if (!ValidateAnimationStateMachine(m_machine).IsValid() || !std::isfinite(deltaSeconds) || deltaSeconds < 0.0) {
        result.message = "Animation state-machine evaluation rejected invalid input.";
        result.debug = m_debug;
        return result;
    }
    const AnimationState* current = FindState(m_debug.currentStateId);
    if (current == nullptr) {
        result.message = "Animation state-machine current state is unavailable.";
        result.debug = m_debug;
        return result;
    }
    const AnimationClip* currentClip = FindClip(m_machine, current->clipId);
    const double currentDuration =
        (currentClip != nullptr && currentClip->durationSeconds > 0.0) ? currentClip->durationSeconds : 1.0;
    const double clampedDelta = std::min(deltaSeconds, 0.25);
    m_debug.normalizedTime += (clampedDelta * static_cast<double>(current->speed)) / currentDuration;
    m_debug.normalizedTime = std::fmod(m_debug.normalizedTime, 1.0);
    if (m_debug.normalizedTime < 0.0) {
        m_debug.normalizedTime += 1.0;
    }

    const bool transitionWasActiveAtFrameStart = m_debug.transitioning;
    if (!m_debug.transitioning) {
        const AnimationTransition* transition = SelectTransition(*current);
        if (transition != nullptr) {
            m_debug.activeTransitionId = transition->transitionId;
            m_debug.targetStateId = transition->targetStateId;
            m_debug.transitionAlpha = transition->durationSeconds <= 0.0F ? 1.0 : 0.0;
            m_debug.selectedTransitionPriority = transition->priority;
            m_debug.transitioning = transition->durationSeconds > 0.0F;
            if (!m_debug.transitioning) {
                m_debug.currentStateId = transition->targetStateId;
                m_debug.targetStateId.clear();
                m_debug.activeTransitionId.clear();
                m_debug.normalizedTime = 0.0;
                current = FindState(m_debug.currentStateId);
            }
        }
    }
    if (m_debug.transitioning) {
        const auto transitionIterator =
            std::find_if(m_machine.transitions.cbegin(), m_machine.transitions.cend(), [this](const auto& candidate) {
                return candidate.transitionId == m_debug.activeTransitionId;
            });
        if (transitionIterator == m_machine.transitions.cend()) {
            result.message = "Animation state-machine active transition disappeared.";
            result.debug = m_debug;
            return result;
        }
        const AnimationTransition& transition = *transitionIterator;
        const AnimationTransition* transitionToAdvance = &transition;
        if (transitionWasActiveAtFrameStart) {
            if (const AnimationTransition* interruption = SelectInterruptionTransition(transition);
                interruption != nullptr) {
                m_debug.activeTransitionId = interruption->transitionId;
                m_debug.targetStateId = interruption->targetStateId;
                m_debug.transitionAlpha = interruption->durationSeconds <= 0.0F ? 1.0 : 0.0;
                m_debug.selectedTransitionPriority = interruption->priority;
                transitionToAdvance = interruption;
                if (interruption->durationSeconds <= 0.0F) {
                    m_debug.currentStateId = interruption->targetStateId;
                    m_debug.targetStateId.clear();
                    m_debug.activeTransitionId.clear();
                    m_debug.transitioning = false;
                    m_debug.transitionAlpha = 0.0;
                    m_debug.normalizedTime = 0.0;
                    current = FindState(m_debug.currentStateId);
                }
            }
        }
        if (m_debug.transitioning) {
            m_debug.transitionAlpha = std::min(1.0, m_debug.transitionAlpha +
                (transitionToAdvance->durationSeconds <= 0.0F ? 1.0 :
                 clampedDelta / transitionToAdvance->durationSeconds));
        }
        if (m_debug.transitioning && m_debug.transitionAlpha >= 1.0) {
            m_debug.currentStateId = m_debug.targetStateId;
            m_debug.targetStateId.clear();
            m_debug.activeTransitionId.clear();
            m_debug.transitioning = false;
            m_debug.transitionAlpha = 0.0;
            m_debug.normalizedTime = 0.0;
            current = FindState(m_debug.currentStateId);
        }
    }

    TransformPose currentPose;
    if (!SampleState(*current, m_debug.normalizedTime, currentPose)) {
        result.message = "Animation state-machine current clip sampling failed.";
        result.debug = m_debug;
        return result;
    }
    result.pose = currentPose;
    if (m_debug.transitioning) {
        const AnimationState* target = FindState(m_debug.targetStateId);
        TransformPose targetPose;
        if (target == nullptr || !SampleState(*target, m_debug.normalizedTime, targetPose)) {
            result.message = "Animation state-machine target clip sampling failed.";
            result.debug = m_debug;
            return result;
        }
        const float alpha = static_cast<float>(m_debug.transitionAlpha);
        result.pose.position = currentPose.position * (1.0F - alpha) + targetPose.position * alpha;
        result.pose.scale = currentPose.scale * (1.0F - alpha) + targetPose.scale * alpha;
        Math::Quaternion targetRot = targetPose.rotation;
        const float dot = currentPose.rotation.x * targetRot.x + currentPose.rotation.y * targetRot.y +
                          currentPose.rotation.z * targetRot.z + currentPose.rotation.w * targetRot.w;
        if (dot < 0.0F) {
            targetRot = Math::Quaternion{-targetRot.x, -targetRot.y, -targetRot.z, -targetRot.w};
        }
        result.pose.rotation = Math::Quaternion{
            currentPose.rotation.x * (1.0F - alpha) + targetRot.x * alpha,
            currentPose.rotation.y * (1.0F - alpha) + targetRot.y * alpha,
            currentPose.rotation.z * (1.0F - alpha) + targetRot.z * alpha,
            currentPose.rotation.w * (1.0F - alpha) + targetRot.w * alpha,
        };
        TransformPose normalized;
        if (!TryNormalizeTransformPose(result.pose, normalized)) {
            result.message = "Animation state-machine blend produced an invalid pose.";
            result.debug = m_debug;
            return result;
        }
        result.pose = normalized;
    }
    m_debug.syncGroupId = current->syncGroupId;
    m_debug.layerMaskId = current->layerMaskId;
    result.debug = m_debug;
    result.usedOutput = true;
    result.message = "Animation state-machine evaluated successfully.";
    return result;
}

const AnimationStateMachineDebugSnapshot& AnimationStateMachineEvaluator::GetDebugSnapshot() const noexcept {
    return m_debug;
}

const AnimationState* AnimationStateMachineEvaluator::FindState(const std::string& stateId) const noexcept {
    const auto iterator = std::find_if(m_machine.states.cbegin(), m_machine.states.cend(), [&stateId](const auto& state) {
        return state.stateId == stateId;
    });
    return iterator == m_machine.states.cend() ? nullptr : &*iterator;
}

const AnimationTransition* AnimationStateMachineEvaluator::SelectInterruptionTransition(
    const AnimationTransition& activeTransition) const noexcept {
    if (activeTransition.interruption == AnimationInterruptionPolicy::None) {
        return nullptr;
    }
    const auto allowsSource = activeTransition.interruption == AnimationInterruptionPolicy::Source ||
                              activeTransition.interruption == AnimationInterruptionPolicy::SourceOrTarget;
    const auto allowsTarget = activeTransition.interruption == AnimationInterruptionPolicy::Target ||
                              activeTransition.interruption == AnimationInterruptionPolicy::SourceOrTarget;
    const AnimationTransition* selected = nullptr;
    for (const AnimationTransition& candidate : m_machine.transitions) {
        const bool sourceMatch = allowsSource && candidate.sourceStateId == activeTransition.sourceStateId;
        const bool targetMatch = allowsTarget && candidate.sourceStateId == activeTransition.targetStateId;
        if (candidate.transitionId == activeTransition.transitionId || (!sourceMatch && !targetMatch) ||
            m_debug.normalizedTime + 1.0e-9 < static_cast<double>(candidate.exitTime)) {
            continue;
        }
        if (selected == nullptr || candidate.priority > selected->priority ||
            (candidate.priority == selected->priority && candidate.transitionId < selected->transitionId)) {
            selected = &candidate;
        }
    }
    return selected;
}

const AnimationTransition* AnimationStateMachineEvaluator::SelectTransition(
    const AnimationState& source) const noexcept {
    const AnimationTransition* selected = nullptr;
    for (const AnimationTransition& transition : m_machine.transitions) {
        if (transition.sourceStateId != source.stateId ||
            m_debug.normalizedTime + 1.0e-9 < static_cast<double>(transition.exitTime)) {
            continue;
        }
        if (selected == nullptr || transition.priority > selected->priority ||
            (transition.priority == selected->priority && transition.transitionId < selected->transitionId)) {
            selected = &transition;
        }
    }
    return selected;
}

bool AnimationStateMachineEvaluator::SampleState(const AnimationState& state,
                                                       const double normalizedTime,
                                                       TransformPose& outPose) const noexcept {
    const AnimationClip* clip = FindClip(m_machine, state.clipId);
    if (clip == nullptr) {
        return false;
    }
    const double sampleTimeSeconds = normalizedTime * clip->durationSeconds;
    return TrySampleAnimationClip(*clip, sampleTimeSeconds, true, outPose);
}

} // namespace UVE::Core
