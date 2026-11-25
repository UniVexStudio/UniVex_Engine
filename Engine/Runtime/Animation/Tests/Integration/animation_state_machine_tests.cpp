// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "animation_state_machine.h"

#include <gtest/gtest.h>

namespace UVE::Core {
namespace {

AnimationClip MakeClip(const std::string& id, const float start, const float end) {
    AnimationClip clip;
    clip.clipId = id;
    clip.durationSeconds = 1.0;
    clip.samples = {
        PoseSample{0.0, TransformPose{{start, 0.0F, 0.0F}, {}, {1.0F, 1.0F, 1.0F}}},
        PoseSample{1.0, TransformPose{{end, 0.0F, 0.0F}, {}, {1.0F, 1.0F, 1.0F}}},
    };
    return clip;
}

AnimationStateMachine MakeMachine() {
    AnimationStateMachine machine;
    machine.initialStateId = "idle";
    machine.clips = {MakeClip("idleClip", 0.0F, 0.0F), MakeClip("runClip", 10.0F, 20.0F)};
    machine.states = {
        AnimationState{"idle", "idleClip", "locomotion", "upper", 1.0F},
        AnimationState{"run", "runClip", "locomotion", "upper", 1.0F},
    };
    machine.transitions = {
        AnimationTransition{"idle_to_run", "idle", "run", 1.0F, 0.25F, 5, AnimationInterruptionPolicy::SourceOrTarget},
    };
    return machine;
}

AnimationStateMachine MakeInterruptibleMachine(const AnimationInterruptionPolicy policy) {
    AnimationStateMachine machine = MakeMachine();
    machine.clips.push_back(MakeClip("jumpClip", 30.0F, 40.0F));
    machine.states.push_back(AnimationState{"jump", "jumpClip", "locomotion", "upper", 1.0F});
    machine.transitions[0].priority = 10;
    machine.transitions[0].exitTime = 0.0F;
    machine.transitions[0].interruption = policy;
    machine.transitions.push_back(AnimationTransition{
        "idle_to_jump", "idle", "jump", 1.0F, 0.0F, 9, AnimationInterruptionPolicy::None});
    machine.transitions.push_back(AnimationTransition{
        "run_to_jump", "run", "jump", 1.0F, 0.0F, 8, AnimationInterruptionPolicy::None});
    return machine;
}

const AnimationStateMachineEvaluationResult BeginTransition(
    AnimationStateMachineEvaluator& evaluator) {
    const AnimationStateMachineEvaluationResult result = evaluator.Evaluate(0.01);
    EXPECT_TRUE(result.IsSuccess());
    return result;
}

} // namespace

TEST(AnimationStateMachineTest, ValidateAnimationStateMachine_AcceptsBoundedStatesAndTransition) {
    const AnimationStateMachineValidationResult result = ValidateAnimationStateMachine(MakeMachine());
    EXPECT_TRUE(result.IsValid());
    EXPECT_EQ(result.code, AnimationStateMachineValidationCode::Valid);
}

TEST(AnimationStateMachineTest, Evaluate_UsesExitTimeCrossfadeAndCopiedDebugSnapshot) {
    const AnimationStateMachine machine = MakeMachine();
    AnimationStateMachineEvaluator evaluator(machine);
    const AnimationStateMachineEvaluationResult beforeTransition = evaluator.Evaluate(0.25);
    ASSERT_TRUE(beforeTransition.IsSuccess());
    EXPECT_DOUBLE_EQ(beforeTransition.debug.normalizedTime, 0.25);
    EXPECT_EQ(beforeTransition.debug.currentStateId, "idle");
    EXPECT_TRUE(beforeTransition.debug.transitioning);
    EXPECT_EQ(beforeTransition.debug.activeTransitionId, "idle_to_run");
    EXPECT_EQ(beforeTransition.debug.syncGroupId, "locomotion");
    EXPECT_EQ(beforeTransition.debug.layerMaskId, "upper");

    const AnimationStateMachineEvaluationResult crossfade = evaluator.Evaluate(0.25);
    ASSERT_TRUE(crossfade.IsSuccess());
    EXPECT_NEAR(crossfade.debug.transitionAlpha, 0.5, 1.0e-12);
    EXPECT_NEAR(crossfade.pose.position.x, 7.5F, 1.0e-5F);

    static_cast<void>(evaluator.Evaluate(0.25));
    const AnimationStateMachineEvaluationResult completed = evaluator.Evaluate(0.25);
    ASSERT_TRUE(completed.IsSuccess());
    EXPECT_FALSE(completed.debug.transitioning);
    EXPECT_EQ(completed.debug.currentStateId, "run");
}

TEST(AnimationStateMachineTest, Evaluate_SelectsHighestPriorityEligibleTransition) {
    AnimationStateMachine machine = MakeMachine();
    machine.transitions.push_back(AnimationTransition{
        "idle_to_run_low", "idle", "run", 0.0F, 0.0F, 1, AnimationInterruptionPolicy::None});
    machine.transitions[0].priority = 8;
    machine.transitions[0].exitTime = 0.0F;
    AnimationStateMachineEvaluator evaluator(machine);

    const AnimationStateMachineEvaluationResult result = evaluator.Evaluate(0.01);
    ASSERT_TRUE(result.IsSuccess());
    EXPECT_EQ(result.debug.activeTransitionId, "idle_to_run");
    EXPECT_EQ(result.debug.selectedTransitionPriority, 8);
}

TEST(AnimationStateMachineTest, Evaluate_NoneInterruptionKeepsActiveTransition) {
    const AnimationStateMachine machine =
        MakeInterruptibleMachine(AnimationInterruptionPolicy::None);
    AnimationStateMachineEvaluator evaluator(machine);
    ASSERT_EQ(BeginTransition(evaluator).debug.activeTransitionId, "idle_to_run");

    const AnimationStateMachineEvaluationResult result = evaluator.Evaluate(0.01);

    ASSERT_TRUE(result.IsSuccess());
    EXPECT_EQ(result.debug.activeTransitionId, "idle_to_run");
    EXPECT_EQ(result.debug.targetStateId, "run");
}

TEST(AnimationStateMachineTest, Evaluate_SourceInterruptionSelectsSourceTransition) {
    const AnimationStateMachine machine =
        MakeInterruptibleMachine(AnimationInterruptionPolicy::Source);
    AnimationStateMachineEvaluator evaluator(machine);
    ASSERT_EQ(BeginTransition(evaluator).debug.activeTransitionId, "idle_to_run");

    const AnimationStateMachineEvaluationResult result = evaluator.Evaluate(0.01);

    ASSERT_TRUE(result.IsSuccess());
    EXPECT_EQ(result.debug.activeTransitionId, "idle_to_jump");
    EXPECT_EQ(result.debug.targetStateId, "jump");
}

TEST(AnimationStateMachineTest, Evaluate_TargetInterruptionSelectsTargetTransition) {
    const AnimationStateMachine machine =
        MakeInterruptibleMachine(AnimationInterruptionPolicy::Target);
    AnimationStateMachineEvaluator evaluator(machine);
    ASSERT_EQ(BeginTransition(evaluator).debug.activeTransitionId, "idle_to_run");

    const AnimationStateMachineEvaluationResult result = evaluator.Evaluate(0.01);

    ASSERT_TRUE(result.IsSuccess());
    EXPECT_EQ(result.debug.activeTransitionId, "run_to_jump");
    EXPECT_EQ(result.debug.targetStateId, "jump");
}

TEST(AnimationStateMachineTest, Evaluate_SourceOrTargetUsesPriorityAcrossBothStates) {
    const AnimationStateMachine machine =
        MakeInterruptibleMachine(AnimationInterruptionPolicy::SourceOrTarget);
    AnimationStateMachineEvaluator evaluator(machine);
    ASSERT_EQ(BeginTransition(evaluator).debug.activeTransitionId, "idle_to_run");

    const AnimationStateMachineEvaluationResult result = evaluator.Evaluate(0.01);

    ASSERT_TRUE(result.IsSuccess());
    EXPECT_EQ(result.debug.activeTransitionId, "idle_to_jump");
    EXPECT_EQ(result.debug.targetStateId, "jump");
    EXPECT_EQ(result.debug.selectedTransitionPriority, 9);
}

TEST(AnimationStateMachineTest, ValidateAnimationStateMachine_RejectsUnknownClipAndInvalidExitTime) {
    AnimationStateMachine unknownClip = MakeMachine();
    unknownClip.states[1].clipId = "missing";
    EXPECT_EQ(ValidateAnimationStateMachine(unknownClip).code,
              AnimationStateMachineValidationCode::UnknownClip);

    AnimationStateMachine invalidExit = MakeMachine();
    invalidExit.transitions[0].exitTime = 1.5F;
    EXPECT_EQ(ValidateAnimationStateMachine(invalidExit).code,
              AnimationStateMachineValidationCode::InvalidExitTime);
}

} // namespace UVE::Core
