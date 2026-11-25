// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "character_prediction.h"

#include "entity.h"
#include "hierarchy_component.h"
#include "transform_component.h"
#include "world_transform_component.h"
#include "vector3.h"
#include "character_3d.h"
#include "character_body_motion.h"

namespace UVE::Scene {
namespace {

[[nodiscard]] std::uint32_t PackClockHalf(const float seconds) noexcept {
    const float millis = seconds * 1000.0F;
    // !(millis > 0) rather than (millis <= 0): NaN fails the comparison and packs as zero
    // instead of slipping through to the cast.
    if (!(millis > 0.0F)) {
        return std::uint32_t{0U};
    }
    if (millis >= 65535.0F) {
        return std::uint32_t{0xFFFFU};
    }
    return static_cast<std::uint32_t>(millis);
}

} // namespace

std::uint32_t PackPredictionClocks(const float coyoteSeconds,
                                       const float bufferSeconds) noexcept {
    return (PackClockHalf(coyoteSeconds) << 16U) | PackClockHalf(bufferSeconds);
}

PredictionClocks UnpackPredictionClocks(const std::uint32_t appState) noexcept {
    return PredictionClocks{static_cast<float>(appState >> 16U) / 1000.0F,
                               static_cast<float>(appState & 0xFFFFU) / 1000.0F};
}

Network::PredictionStepper MakeCharacterPredictionStepper(
    const CharacterControllerComponent& settings, const float gravityY) {
    return [settings, gravityY](const Network::PredictedState& state,
                                const Network::PredictedInput& input, const float dt) {
        CharacterControllerComponent c = settings;
        c.velocity = state.velocity;
        c.grounded = state.grounded;
        const PredictionClocks clocks = UnpackPredictionClocks(state.appState);
        c.timeSinceOnFloor = clocks.coyoteSeconds;
        c.jumpBufferRemaining = clocks.bufferSeconds;
        const Physics::CharacterMotionInput motion{input.move, input.rise, input.jumpPressed};
        static_cast<void>(Physics::StepCharacterIntent(c, motion, gravityY, dt));
        Network::PredictedState next;
        next.position = state.position + c.velocity * dt;
        next.velocity = c.velocity;
        next.grounded = c.grounded;
        next.appState = PackPredictionClocks(c.timeSinceOnFloor, c.jumpBufferRemaining);
        return next;
    };
}

Network::PredictedInput CapturePredictedInput(IEntityManager& entityManager,
                                                    const Entity entity) {
    const Physics::CharacterMotionInput motion = ResolveCharacterMotion(entityManager, entity);
    Network::PredictedInput input;
    input.move = motion.move;
    input.rise = motion.rise;
    input.jumpPressed = motion.jumpPressed;
    return input;
}

std::optional<Network::PredictedState> PackPredictedState(
    const IEntityManager& entityManager, const Entity entity) {
    if (!entityManager.IsAlive(entity) ||
        !entityManager.HasComponent<WorldTransformComponent>(entity) ||
        !entityManager.HasComponent<CharacterControllerComponent>(entity)) {
        return std::nullopt;
    }
    const WorldTransformComponent& world =
        entityManager.GetComponent<WorldTransformComponent>(entity);
    const CharacterControllerComponent& c =
        entityManager.GetComponent<CharacterControllerComponent>(entity);
    if (!Math::IsFinite(world.worldPosition) || !Math::IsFinite(c.velocity)) {
        return std::nullopt;
    }
    Network::PredictedState state;
    state.position = world.worldPosition;
    state.velocity = c.velocity;
    state.grounded = c.grounded;
    state.appState = PackPredictionClocks(c.timeSinceOnFloor, c.jumpBufferRemaining);
    return state;
}

bool ApplyPredictedState(IEntityManager& entityManager, ISceneGraph& sceneGraph,
                            const Entity entity, const Network::PredictedState& state) {
    if (!entityManager.IsAlive(entity) ||
        !entityManager.HasComponent<TransformComponent>(entity) ||
        !entityManager.HasComponent<WorldTransformComponent>(entity) ||
        !entityManager.HasComponent<CharacterControllerComponent>(entity)) {
        return false;
    }
    // Predicted pawns are roots: the state carries a world position and the write lands on
    // the local transform, which agree only without a parent. No hierarchy component means
    // never parented, which counts as root.
    if (entityManager.HasComponent<HierarchyComponent>(entity) &&
        entityManager.GetComponent<HierarchyComponent>(entity).parent != kInvalidEntity) {
        return false;
    }
    if (!Math::IsFinite(state.position) || !Math::IsFinite(state.velocity)) {
        return false;
    }
    TransformComponent transform = entityManager.GetComponent<TransformComponent>(entity);
    transform.localPosition = state.position;
    // Through the graph, not around it - the mover's own rule - with the immediate Update()
    // that makes the new pose visible to the rest of the step instead of next frame's pass.
    sceneGraph.SetLocalTransform(entityManager, entity, transform);
    CharacterControllerComponent& c =
        entityManager.GetComponent<CharacterControllerComponent>(entity);
    c.velocity = state.velocity;
    c.grounded = state.grounded;
    const PredictionClocks clocks = UnpackPredictionClocks(state.appState);
    c.timeSinceOnFloor = clocks.coyoteSeconds;
    c.jumpBufferRemaining = clocks.bufferSeconds;
    sceneGraph.Update(entityManager);
    return true;
}

Network::PredictionTick TickPredictedPawn(IEntityManager& entityManager,
                                                ISceneGraph& sceneGraph, const Entity entity,
                                                Network::NetworkPrediction& predictor) {
    return TickPredictedPawn(entityManager, sceneGraph, entity, predictor,
                                Network::kDefaultPredictionPawn);
}

Network::PredictionTick TickPredictedPawn(IEntityManager& entityManager,
                                                ISceneGraph& sceneGraph, const Entity entity,
                                                Network::NetworkPrediction& predictor,
                                                const Network::PredictionPawn pawn) {
    const Network::PredictionTick tick =
        predictor.Tick(pawn, CapturePredictedInput(entityManager, entity));
    static_cast<void>(ApplyPredictedState(entityManager, sceneGraph, entity,
                                             predictor.LocalState(pawn)));
    return tick;
}

bool SyncPredictedPawn(IEntityManager& entityManager, ISceneGraph& sceneGraph,
                           const Entity entity, Network::NetworkPrediction& predictor) {
    return SyncPredictedPawn(entityManager, sceneGraph, entity, predictor,
                                Network::kDefaultPredictionPawn);
}

bool SyncPredictedPawn(IEntityManager& entityManager, ISceneGraph& sceneGraph,
                           const Entity entity, Network::NetworkPrediction& predictor,
                           const Network::PredictionPawn pawn) {
    return ApplyPredictedState(entityManager, sceneGraph, entity, predictor.LocalState(pawn));
}

} // namespace UVE::Scene
