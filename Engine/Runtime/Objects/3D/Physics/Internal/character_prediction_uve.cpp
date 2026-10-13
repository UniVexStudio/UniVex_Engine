// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/objects/3d/character_prediction_uve.h"

#include "uve/component/entity_uve.h"
#include "uve/component/hierarchy_component_uve.h"
#include "uve/component/transform_component_uve.h"
#include "uve/component/world_transform_component_uve.h"
#include "uve/math/vector3_uve.h"
#include "uve/objects/3d/character_3d_uve.h"
#include "uve/physics/character_body_motion_uve.h"

namespace UVE::Scene {
namespace {

[[nodiscard]] std::uint32_t PackClockHalfUVE(const float seconds) noexcept {
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

std::uint32_t PackPredictionClocksUVE(const float coyoteSeconds,
                                       const float bufferSeconds) noexcept {
    return (PackClockHalfUVE(coyoteSeconds) << 16U) | PackClockHalfUVE(bufferSeconds);
}

PredictionClocksUVE UnpackPredictionClocksUVE(const std::uint32_t appState) noexcept {
    return PredictionClocksUVE{static_cast<float>(appState >> 16U) / 1000.0F,
                               static_cast<float>(appState & 0xFFFFU) / 1000.0F};
}

Network::PredictionStepperUVE MakeCharacterPredictionStepperUVE(
    const CharacterControllerComponentUVE& settings, const float gravityY) {
    return [settings, gravityY](const Network::PredictedStateUVE& state,
                                const Network::PredictedInputUVE& input, const float dt) {
        CharacterControllerComponentUVE c = settings;
        c.velocity = state.velocity;
        c.grounded = state.grounded;
        const PredictionClocksUVE clocks = UnpackPredictionClocksUVE(state.appState);
        c.timeSinceOnFloor = clocks.coyoteSeconds;
        c.jumpBufferRemaining = clocks.bufferSeconds;
        const Physics::CharacterMotionInputUVE motion{input.move, input.rise, input.jumpPressed};
        static_cast<void>(Physics::StepCharacterIntentUVE(c, motion, gravityY, dt));
        Network::PredictedStateUVE next;
        next.position = state.position + c.velocity * dt;
        next.velocity = c.velocity;
        next.grounded = c.grounded;
        next.appState = PackPredictionClocksUVE(c.timeSinceOnFloor, c.jumpBufferRemaining);
        return next;
    };
}

Network::PredictedInputUVE CapturePredictedInputUVE(IEntityManagerUVE& entityManager,
                                                    const EntityUVE entity) {
    const Physics::CharacterMotionInputUVE motion = ResolveCharacterMotionUVE(entityManager, entity);
    Network::PredictedInputUVE input;
    input.move = motion.move;
    input.rise = motion.rise;
    input.jumpPressed = motion.jumpPressed;
    return input;
}

std::optional<Network::PredictedStateUVE> PackPredictedStateUVE(
    const IEntityManagerUVE& entityManager, const EntityUVE entity) {
    if (!entityManager.IsAliveUVE(entity) ||
        !entityManager.HasComponentUVE<WorldTransformComponentUVE>(entity) ||
        !entityManager.HasComponentUVE<CharacterControllerComponentUVE>(entity)) {
        return std::nullopt;
    }
    const WorldTransformComponentUVE& world =
        entityManager.GetComponentUVE<WorldTransformComponentUVE>(entity);
    const CharacterControllerComponentUVE& c =
        entityManager.GetComponentUVE<CharacterControllerComponentUVE>(entity);
    if (!Math::IsFiniteUVE(world.worldPosition) || !Math::IsFiniteUVE(c.velocity)) {
        return std::nullopt;
    }
    Network::PredictedStateUVE state;
    state.position = world.worldPosition;
    state.velocity = c.velocity;
    state.grounded = c.grounded;
    state.appState = PackPredictionClocksUVE(c.timeSinceOnFloor, c.jumpBufferRemaining);
    return state;
}

bool ApplyPredictedStateUVE(IEntityManagerUVE& entityManager, ISceneGraphUVE& sceneGraph,
                            const EntityUVE entity, const Network::PredictedStateUVE& state) {
    if (!entityManager.IsAliveUVE(entity) ||
        !entityManager.HasComponentUVE<TransformComponentUVE>(entity) ||
        !entityManager.HasComponentUVE<WorldTransformComponentUVE>(entity) ||
        !entityManager.HasComponentUVE<CharacterControllerComponentUVE>(entity)) {
        return false;
    }
    // Predicted pawns are roots: the state carries a world position and the write lands on
    // the local transform, which agree only without a parent. No hierarchy component means
    // never parented, which counts as root.
    if (entityManager.HasComponentUVE<HierarchyComponentUVE>(entity) &&
        entityManager.GetComponentUVE<HierarchyComponentUVE>(entity).parent != kInvalidEntityUVE) {
        return false;
    }
    if (!Math::IsFiniteUVE(state.position) || !Math::IsFiniteUVE(state.velocity)) {
        return false;
    }
    TransformComponentUVE transform = entityManager.GetComponentUVE<TransformComponentUVE>(entity);
    transform.localPosition = state.position;
    // Through the graph, not around it - the mover's own rule - with the immediate UpdateUVE()
    // that makes the new pose visible to the rest of the step instead of next frame's pass.
    sceneGraph.SetLocalTransformUVE(entityManager, entity, transform);
    CharacterControllerComponentUVE& c =
        entityManager.GetComponentUVE<CharacterControllerComponentUVE>(entity);
    c.velocity = state.velocity;
    c.grounded = state.grounded;
    const PredictionClocksUVE clocks = UnpackPredictionClocksUVE(state.appState);
    c.timeSinceOnFloor = clocks.coyoteSeconds;
    c.jumpBufferRemaining = clocks.bufferSeconds;
    sceneGraph.UpdateUVE(entityManager);
    return true;
}

Network::PredictionTickUVE TickPredictedPawnUVE(IEntityManagerUVE& entityManager,
                                                ISceneGraphUVE& sceneGraph, const EntityUVE entity,
                                                Network::NetworkPredictionUVE& predictor) {
    return TickPredictedPawnUVE(entityManager, sceneGraph, entity, predictor,
                                Network::kDefaultPredictionPawnUVE);
}

Network::PredictionTickUVE TickPredictedPawnUVE(IEntityManagerUVE& entityManager,
                                                ISceneGraphUVE& sceneGraph, const EntityUVE entity,
                                                Network::NetworkPredictionUVE& predictor,
                                                const Network::PredictionPawnUVE pawn) {
    const Network::PredictionTickUVE tick =
        predictor.TickUVE(pawn, CapturePredictedInputUVE(entityManager, entity));
    static_cast<void>(ApplyPredictedStateUVE(entityManager, sceneGraph, entity,
                                             predictor.LocalStateUVE(pawn)));
    return tick;
}

bool SyncPredictedPawnUVE(IEntityManagerUVE& entityManager, ISceneGraphUVE& sceneGraph,
                           const EntityUVE entity, Network::NetworkPredictionUVE& predictor) {
    return SyncPredictedPawnUVE(entityManager, sceneGraph, entity, predictor,
                                Network::kDefaultPredictionPawnUVE);
}

bool SyncPredictedPawnUVE(IEntityManagerUVE& entityManager, ISceneGraphUVE& sceneGraph,
                           const EntityUVE entity, Network::NetworkPredictionUVE& predictor,
                           const Network::PredictionPawnUVE pawn) {
    return ApplyPredictedStateUVE(entityManager, sceneGraph, entity, predictor.LocalStateUVE(pawn));
}

} // namespace UVE::Scene
