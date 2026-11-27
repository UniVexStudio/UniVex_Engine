// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#include "scene_serializer.h"
#include "perception.h"
#include "utility_ai.h"
#include "cinematic.h"
#include "gameplay_attributes.h"
#include "gameplay_tags.h"
#include "status_effects.h"
#include "trigger_volume.h"
#include "pawn.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <limits>
#include <optional>
#include <stdexcept>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <system_error>
#include <type_traits>
#include <typeindex>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <variant>
#include <vector>

#include <nlohmann/json.hpp>

#include "asset_guid.h"
#include "uve_file_envelope.h"
#include "logging_macros.h"
#include "quaternion.h"
#include "vector2.h"
#include "vector3.h"
#include "color.h"
#include "rect.h"
#include "animation_driver_component.h"
#include "animation_sequencer_component.h"
#include "animation_graph_component.h"
#include "montage_sequencer_component.h"
#include "area_component.h"
#include "audio_source_component.h"
#include "bone_modifier_component.h"
#include "camera_component.h"
#include "camera_follow_component.h"
#include "canvas_component.h"
#include "character_controller_component.h"
#include "collider_component.h"
#include "morph_target_component.h"
#include "skin_component.h"
#include "all_objects_3d.h"
#include "scene_object_type.h"
#include "scene_folder.h"
#include "scene_root.h"
#include "hierarchy_component.h"
#include "light_component.h"
#include "light_emitter_component.h"
#include "mesh_component.h"
#include "name_component.h"
#include "auto_translate_component.h"
#include "editor_description_component.h"
#include "object_metadata_component.h"
#include "process_component.h"
#include "thread_group_component.h"
#include "particle_emitter_component.h"
#include "physics_interpolation_component.h"
#include "physics_object_component.h"
#include "primitive_mesh_component.h"
#include "render_instance_component.h"
#include "solid_body_component.h"
#include "prefab_instance_component.h"
#include "rigid_3d_component.h"
#include "script_component.h"
#include "surface_instance_component.h"
#include "transform_component.h"
#include "disabled_component.h"
#include "visibility_component.h"
#include "ui_button_component.h"
#include "ui_image_component.h"
#include "ui_text_component.h"
#include "world_transform_component.h"
#include "component_type_info.h"
#include "scene_component_metadata.h"

namespace UVE::Scene {

namespace {

// --- Math JSON helpers ---------------------------------------------------------------------

[[nodiscard]] nlohmann::json ToJson(const Math::Vector3& vector) {
    return nlohmann::json::array({vector.x, vector.y, vector.z});
}

[[nodiscard]] Math::Vector3 Vector3FromJson(const nlohmann::json& json) {
    return Math::Vector3{json.at(0).get<float>(), json.at(1).get<float>(), json.at(2).get<float>()};
}

// Colors persist as the same [r, g, b] triple arrays vectors use: old scenes load
// byte-identical, and the linear interpretation comes from the Color type.
[[nodiscard]] nlohmann::json ToJson(const Math::Color& color) {
    return nlohmann::json::array({color.r, color.g, color.b});
}

[[nodiscard]] Math::Color ColorFromJson(const nlohmann::json& json) {
    return Math::Color{json.at(0).get<float>(), json.at(1).get<float>(), json.at(2).get<float>()};
}

[[nodiscard]] nlohmann::json ToJson(const Math::Vector2& vector) {
    return nlohmann::json::array({vector.x, vector.y});
}

[[nodiscard]] Math::Vector2 Vector2FromJson(const nlohmann::json& json) {
    return Math::Vector2{json.at(0).get<float>(), json.at(1).get<float>()};
}

[[nodiscard]] nlohmann::json ToJson(const Math::Quaternion& rotation) {
    return nlohmann::json::array({rotation.x, rotation.y, rotation.z, rotation.w});
}

[[nodiscard]] Math::Quaternion QuaternionFromJson(const nlohmann::json& json) {
    return Math::Quaternion{json.at(0).get<float>(), json.at(1).get<float>(), json.at(2).get<float>(),
                                json.at(3).get<float>()};
}

// --- Per-component-type JSON (de)serialization ---------------------------------------------
//
// One ToJson(const T&) overload plus one fromJson lambda per serializable component type.
// HierarchyComponent and WorldTransformComponent are deliberately absent here: the former
// needs the file-local-id remapping only Save()/Load() itself has the context to do, and
// the latter is derived/cached data that is never serialized at all.
//
// Adding a new built-in component type? Register its JSON (de)serialization here too.

/// Reads an enum stored as a small integer, rejecting anything outside the known range rather
/// than casting it through. A hand-edited or future-version scene can carry a value this build
/// does not have, and silently reinterpreting it as XYZ would rotate the object without saying
/// so - the same class of failure as a mismatched icon name, but with geometry.
[[nodiscard]] Math::EulerOrder ReadEulerOrder(const nlohmann::json& json) {
    const auto raw = json.value("eulerOrder", static_cast<std::uint8_t>(0));
    switch (raw) {
        case 0: return Math::EulerOrder::XYZ;
        case 1: return Math::EulerOrder::YXZ;
        case 2: return Math::EulerOrder::ZYX;
        case 3: return Math::EulerOrder::XZY;
        case 4: return Math::EulerOrder::YZX;
        case 5: return Math::EulerOrder::ZXY;
        default: return Math::EulerOrder::XYZ;
    }
}

[[nodiscard]] RotationEditMode ReadRotationEditMode(const nlohmann::json& json) {
    const auto raw = json.value("rotationEditMode", static_cast<std::uint8_t>(0));
    return raw == 1 ? RotationEditMode::Quaternion : RotationEditMode::Euler;
}

[[nodiscard]] nlohmann::json ToJson(const TransformComponent& component) {
    // localRotation is written even in Euler mode, and that is deliberate: it is what every
    // consumer reads, and a reader that does not know about the Euler fields must still get a
    // correct rotation out of the file. The Euler fields are the AUTHORED source beside it - the
    // exact angles typed, including turns past 360 and the pole poses a quaternion cannot
    // distinguish - so reopening a scene shows what was typed rather than a re-derived guess.
    return {
        {"localPosition", ToJson(component.localPosition)},
        {"localRotation", ToJson(component.localRotation)},
        {"localScale", ToJson(component.localScale)},
        {"localEulerRadians", ToJson(component.localEulerRadians)},
        {"eulerOrder", static_cast<std::uint8_t>(component.eulerOrder)},
        {"rotationEditMode", static_cast<std::uint8_t>(component.rotationEditMode)},
        {"topLevel", component.topLevel},
    };
}

// The target is an entity reference and is written beside these by the entity-aware encoder
// (targetLocalId), because only it knows the file-local ids. Runtime state is never written.
[[nodiscard]] nlohmann::json ToJson(const AnimationSequencerComponent& component) {
    nlohmann::json library = nlohmann::json::array();
    for (const Asset::AssetGuid& guid : component.library) {
        library.push_back(guid.value);
    }
    return {{"clip", component.clip.value},
            {"library", std::move(library)},
            {"libraryRef", component.libraryRef.value},
            {"autoplay", component.autoplay},
            {"speed", component.speed},
            {"loopMode", static_cast<std::uint8_t>(component.loopMode)},
            {"onFinish", static_cast<std::uint8_t>(component.onFinish)},
            {"startOffsetSeconds", component.startOffsetSeconds},
            {"blendInSeconds", component.blendInSeconds},
            {"relative", component.relative}};
}

// The target is written beside these as targetLocalId, like the players' used to be.
[[nodiscard]] nlohmann::json ToJson(const AnimationDriverComponent& component) {
    return {{"active", component.active},
            {"speedScale", component.speedScale},
            {"processCallback", static_cast<std::uint8_t>(component.processCallback)},
            {"animatePosition", component.animatePosition},
            {"animateRotation", component.animateRotation},
            {"animateScale", component.animateScale},
            {"transition", static_cast<std::uint8_t>(component.transition)},
            {"rootMotion", static_cast<std::uint8_t>(component.rootMotion)},
            {"rootMotionBone", component.rootMotionBone}};
}

/// A driver from its own payload, or - for a player or tree saved before AnimationDriver existed -
/// from the same keys on that component's payload. The target is resolved by the caller.
[[nodiscard]] AnimationDriverComponent AnimationDriverFromJson(const nlohmann::json& json) {
    AnimationDriverComponent driver;
    driver.active = json.value("active", true);
    driver.speedScale = json.value("speedScale", 1.0F);
    driver.processCallback = static_cast<AnimationProcessCallback>(json.value("processCallback", std::uint8_t{0}));
    driver.animatePosition = json.value("animatePosition", true);
    driver.animateRotation = json.value("animateRotation", true);
    driver.animateScale = json.value("animateScale", true);
    driver.transition = static_cast<AnimationTransitionMode>(json.value("transition", std::uint8_t{0}));
    driver.rootMotion = static_cast<AnimationRootMotionMode>(json.value("rootMotion", std::uint8_t{0}));
    driver.rootMotionBone = json.value("rootMotionBone", std::string{});
    return driver;
}

[[nodiscard]] nlohmann::json ToJson(const GameplayAttributesComponent& component) {
    nlohmann::json attributes = nlohmann::json::array();
    for (const GameplayAttribute& attribute : component.attributes) {
        attributes.push_back({{"id", attribute.id},
                              {"current", attribute.current},
                              {"maximum", attribute.maximum},
                              {"regenPerSecond", attribute.regenPerSecond}});
    }
    return {{"attributes", std::move(attributes)}};
}

[[nodiscard]] nlohmann::json ToJson(const MontageSequencerComponent& component) {
    nlohmann::json montages = nlohmann::json::array();
    for (std::size_t i = 0U; i < 8U; ++i) {
        montages.push_back(component.MontageAt(i).value);
    }
    nlohmann::json out = {{"montages", std::move(montages)}};
    if (component.libraryRef != Asset::AssetGuid{}) {
        out["library"] = component.libraryRef.value;
    }
    return out;
}

[[nodiscard]] bool IsMontageSequencerComponentValid(const MontageSequencerComponent&) noexcept {
    return true;
}

[[nodiscard]] nlohmann::json ToJson(const GameplayTagComponent& component) {
    return {{"tags", component.tags}};
}

[[nodiscard]] nlohmann::json ToJson(const StatusEffectsComponent& component) {
    nlohmann::json effects = nlohmann::json::array();
    for (const StatusEffect& effect : component.effects) {
        effects.push_back({{"effectId", effect.effectId},
                           {"attributeId", effect.attributeId},
                           {"magnitudePerSecond", effect.magnitudePerSecond},
                           {"remainingSeconds", effect.remainingSeconds},
                           {"stacking", static_cast<unsigned int>(effect.stacking)},
                           {"stacks", effect.stacks},
                           {"maxStacks", effect.maxStacks},
                           {"maxDelta", effect.maxDelta},
                           {"maxApplied", effect.maxApplied}});
    }
    return {{"effects", std::move(effects)}};
}

[[nodiscard]] nlohmann::json ToJson(const ControllerComponent& component) {
    // The pawn link is deliberately absent: entity references resolve through file-local ids.
    return {{"kind", static_cast<unsigned int>(component.kind)}};
}

[[nodiscard]] nlohmann::json ToJson(const PawnComponent& component) {
    // The controller link is deliberately absent: entity references resolve through file-local ids.
    return {{"input",
             {{"move", ToJson(component.input.move)},
              {"rise", component.input.rise},
              {"jumpPressed", component.input.jumpPressed},
              {"lookPointer", ToJson(component.input.lookPointer)},
              {"lookStick", ToJson(component.input.lookStick)},
              {"interactPressed", component.input.interactPressed}}}};
}

[[nodiscard]] nlohmann::json ToJson(const CameraFollowComponent& component) {
    // The target link is deliberately absent: entity references resolve through file-local ids.
    return {{"offset", ToJson(component.offset)},
            {"followPossessedPawn", component.followPossessedPawn}};
}

[[nodiscard]] nlohmann::json ToJson(const TriggerVolumeComponent& component) {
    // Runtime state rides along on purpose: a spent one-shot must stay spent across save/load.
    return {{"policy", static_cast<unsigned int>(component.policy)},
            {"intervalSeconds", component.intervalSeconds},
            {"cooldownSeconds", component.cooldownSeconds},
            {"armed", component.armed},
            {"firedCount", component.firedCount},
            {"cooldownRemaining", component.cooldownRemaining},
            {"intervalRemaining", component.intervalRemaining}};
}

[[nodiscard]] nlohmann::json ToJson(const PerceptionComponent& component) {
    return {{"watchedTag", component.watchedTag},
            {"sightRangeMetres", component.sightRangeMetres},
            {"sightFieldOfViewDegrees", component.sightFieldOfViewDegrees},
            {"hearingRadiusMetres", component.hearingRadiusMetres},
            {"requiresLineOfSight", component.requiresLineOfSight}};
}

[[nodiscard]] nlohmann::json ToJson(const NoiseEmitterComponent& component) {
    return {{"loudness", component.loudness}, {"decayPerSecond", component.decayPerSecond}};
}

[[nodiscard]] nlohmann::json ToJson(const BlackboardComponent& component) {
    nlohmann::json entries = nlohmann::json::array();
    for (const AiBlackboardEntry& entry : component.entries) {
        entries.push_back({{"key", entry.key}, {"value", entry.value}});
    }
    nlohmann::json vectors = nlohmann::json::array();
    for (const AiBlackboardVectorEntry& entry : component.vectors) {
        vectors.push_back({{"key", entry.key}, {"value", ToJson(entry.value)}});
    }
    return {{"entries", std::move(entries)}, {"vectors", std::move(vectors)}};
}

[[nodiscard]] nlohmann::json ToJson(const AiBrainComponent& component) {
    nlohmann::json actions = nlohmann::json::array();
    for (const AiAction& action : component.actions) {
        nlohmann::json considerations = nlohmann::json::array();
        for (const AiConsideration& consideration : action.considerations) {
            considerations.push_back({{"inputId", consideration.inputId},
                                      {"curve", static_cast<std::uint8_t>(consideration.curve)},
                                      {"weight", consideration.weight}});
        }
        actions.push_back({{"actionId", action.actionId},
                           {"baseScore", action.baseScore},
                           {"considerations", std::move(considerations)}});
    }
    // Selection state (currentAction, currentScore) is deliberately not written: a loaded brain
    // rethinks on its first frame, the same reseed rule as cinematics and sequencers.
    return {{"hysteresis", component.hysteresis}, {"actions", std::move(actions)}};
}

[[nodiscard]] nlohmann::json ToJson(const CinematicComponent& component) {
    nlohmann::json events = nlohmann::json::array();
    for (const CinematicEventKey& key : component.events) {
        events.push_back({{"timeSeconds", key.timeSeconds}, {"eventId", key.eventId}});
    }
    nlohmann::json cuts = nlohmann::json::array();
    for (const CinematicCameraCut& cut : component.cuts) {
        cuts.push_back({{"timeSeconds", cut.timeSeconds}});
    }
    nlohmann::json cameraKeys = nlohmann::json::array();
    for (const CinematicCameraKey& key : component.cameraKeys) {
        cameraKeys.push_back({{"timeSeconds", key.timeSeconds},
                              {"position", ToJson(key.position)},
                              {"rotation", ToJson(key.rotation)}});
    }
    nlohmann::json animationKeys = nlohmann::json::array();
    for (const CinematicAnimationKey& key : component.animationKeys) {
        // The target rides the parallel animTargetLocalIds array (entity reference); the clip is a
        // plain guid, the same {"clip", value} spelling the sequencer codec uses.
        animationKeys.push_back({{"timeSeconds", key.timeSeconds}, {"clip", key.clip.value}});
    }
    nlohmann::json audioKeys = nlohmann::json::array();
    for (const CinematicAudioKey& key : component.audioKeys) {
        audioKeys.push_back({{"timeSeconds", key.timeSeconds},
                             {"audioAssetPath", key.audioAssetPath},
                             {"volume", key.volume},
                             {"spatial", key.spatial},
                             {"position", ToJson(key.position)},
                             {"minDistance", key.minDistance},
                             {"maxDistance", key.maxDistance}});
    }
    // Player state (isPlaying, finished, currentTime) is deliberately not written: a loaded
    // cinematic reseeds parked at zero, the same rule as Decal3D and Health.
    return {{"durationSeconds", component.durationSeconds},
            {"events", std::move(events)},
            {"cuts", std::move(cuts)},
            {"cameraKeys", std::move(cameraKeys)},
            {"animationKeys", std::move(animationKeys)},
            {"audioKeys", std::move(audioKeys)},
            {"autoplay", component.autoplay},
            {"speed", component.speed},
            {"loopMode", static_cast<std::uint8_t>(component.loopMode)}};
}

/// The cuts here carry times only: the cameras are entity references, and only the save pass can
/// turn one into a file-local id (see the reference pass in the shared encode path, which writes
/// them as the parallel cutCameraLocalIds array for every saved cinematic).
[[nodiscard]] CinematicComponent CinematicObjectFromJson(const nlohmann::json& json) {
    CinematicComponent value;
    value.durationSeconds = json.value("durationSeconds", 0.0);
    value.autoplay = json.value("autoplay", false);
    value.speed = json.value("speed", 1.0F);
    value.loopMode = static_cast<CinematicLoopMode>(
        json.value("loopMode", static_cast<std::uint8_t>(CinematicLoopMode::Once)));
    if (const auto events = json.find("events"); events != json.end() && events->is_array()) {
        for (const nlohmann::json& entry : *events) {
            CinematicEventKey key;
            key.timeSeconds = entry.value("timeSeconds", 0.0);
            key.eventId = entry.value("eventId", std::string{});
            value.events.push_back(std::move(key));
        }
    }
    if (const auto cuts = json.find("cuts"); cuts != json.end() && cuts->is_array()) {
        for (const nlohmann::json& entry : *cuts) {
            CinematicCameraCut cut;
            cut.timeSeconds = entry.value("timeSeconds", 0.0);
            value.cuts.push_back(std::move(cut));
        }
    }
    if (const auto keys = json.find("cameraKeys"); keys != json.end() && keys->is_array()) {
        for (const nlohmann::json& entry : *keys) {
            CinematicCameraKey key;
            key.timeSeconds = entry.value("timeSeconds", 0.0);
            key.position = Vector3FromJson(entry.at("position"));
            key.rotation = QuaternionFromJson(entry.at("rotation"));
            value.cameraKeys.push_back(std::move(key));
        }
    }
    if (const auto animKeys = json.find("animationKeys");
         animKeys != json.end() && animKeys->is_array()) {
        for (const nlohmann::json& entry : *animKeys) {
            CinematicAnimationKey key;
            key.timeSeconds = entry.value("timeSeconds", 0.0);
            key.clip = Asset::AssetGuid{entry.value("clip", std::uint64_t{0})};
            value.animationKeys.push_back(std::move(key));
        }
    }
    if (const auto soundKeys = json.find("audioKeys");
         soundKeys != json.end() && soundKeys->is_array()) {
        for (const nlohmann::json& entry : *soundKeys) {
            CinematicAudioKey key;
            key.timeSeconds = entry.value("timeSeconds", 0.0);
            key.audioAssetPath = entry.value("audioAssetPath", std::string{});
            key.volume = entry.value("volume", 1.0F);
            key.spatial = entry.value("spatial", false);
            if (const auto position = entry.find("position"); position != entry.end()) {
                key.position = Vector3FromJson(*position);
            }
            key.minDistance = entry.value("minDistance", 1.0F);
            key.maxDistance = entry.value("maxDistance", 25.0F);
            value.audioKeys.push_back(std::move(key));
        }
    }
    return value;
}

template <typename Key>
void ResolveParallelTargets(std::vector<Key>& keys, Entity Key::*member,
                               const nlohmann::json& json, const char* sidecar,
                               const std::unordered_map<std::uint32_t, Entity>& localIdToEntity) {
    const nlohmann::json ids = json.value(sidecar, nlohmann::json::array());
    std::vector<Key> resolved;
    resolved.reserve(keys.size());
    for (std::size_t index = 0U; index < keys.size(); ++index) {
        const std::uint32_t localId = (ids.is_array() && index < ids.size())
                                          ? ids.at(index).get<std::uint32_t>()
                                          : std::numeric_limits<std::uint32_t>::max();
        const auto found = localIdToEntity.find(localId);
        if (found == localIdToEntity.end()) {
            continue;
        }
        keys[index].*member = found->second;
        resolved.push_back(keys[index]);
    }
    keys = std::move(resolved);
}

/// The load-side half of every entity reference a cinematic holds: each parallel file-local id
/// becomes the camera/target it names, and a key naming nothing in this file (reference outside
/// the saved set, or a document predating the sidecar) is dropped - a cut or cue to nowhere is
/// invalid, and must not nuke the load.
[[nodiscard]] CinematicComponent CinematicComponentWithResolvedReferences(
    const nlohmann::json& json, const std::unordered_map<std::uint32_t, Entity>& localIdToEntity) {
    CinematicComponent value = CinematicObjectFromJson(json);
    ResolveParallelTargets(value.cuts, &CinematicCameraCut::camera, json, "cutCameraLocalIds",
                              localIdToEntity);
    ResolveParallelTargets(value.animationKeys, &CinematicAnimationKey::target, json,
                              "animTargetLocalIds", localIdToEntity);
    return value;
}

[[nodiscard]] nlohmann::json ToJson(const AnimationGraphComponent& component) {
    nlohmann::json parameters = nlohmann::json::array();
    for (const AnimationParameter& parameter : component.parameters) {
        nlohmann::json entry = {{"name", parameter.name},
                                {"type", static_cast<std::uint8_t>(parameter.type)},
                                {"value", parameter.value}};
        if (parameter.type == AnimationParameterType::Vector3) {
            entry["vector"] = {parameter.vector.x, parameter.vector.y, parameter.vector.z};
        }
        if (parameter.source != AnimationParameterSource::Script) {
            entry["source"] = static_cast<std::uint8_t>(parameter.source);
        }
        parameters.push_back(std::move(entry));
    }
    nlohmann::json nodes = nlohmann::json::array();
    for (const AnimationGraphNode& object : component.nodes) {
        nlohmann::json statePositions = nlohmann::json::array();
        for (const Math::Vector2& at : object.statePositions) {
            statePositions.push_back({at.x, at.y});
        }
        nlohmann::json transitions = nlohmann::json::array();
        for (const AnimationGraphTransition& transition : object.transitions) {
            nlohmann::json conditions = nlohmann::json::array();
            for (const AnimationTransitionCondition& test : transition.conditions) {
                conditions.push_back({{"condition", static_cast<std::uint8_t>(test.condition)},
                                      {"parameter", test.parameter},
                                      {"threshold", test.threshold}});
            }
            transitions.push_back({{"from", transition.fromState},
                                   {"to", transition.toState},
                                   {"conditions", std::move(conditions)},
                                   {"exitPhase", transition.exitPhase},
                                   {"start", static_cast<std::uint8_t>(transition.start)},
                                   {"fadeSeconds", transition.fadeSeconds},
                                   {"curve", static_cast<std::uint8_t>(transition.curve)},
                                   {"interruptible", transition.interruptible},
                                   {"enabled", transition.enabled}});
        }
        nlohmann::json blendPoints = nlohmann::json::array();
        for (const AnimationBlendPoint& point : object.blendPoints) {
            blendPoints.push_back({{"position", {point.position.x, point.position.y}},
                                   {"clip", point.clip.value},
                                   {"speed", point.speed},
                                   {"loop", point.loop}});
        }
        nodes.push_back({{"id", object.id},
                         {"kind", static_cast<std::uint8_t>(object.kind)},
                         {"name", object.name},
                         {"position", {object.position.x, object.position.y}},
                         {"inputs", object.inputs},
                         {"clip", object.clip.value},
                         {"loop", object.loop},
                         {"speed", object.speed},
                         {"parameter", object.parameter},
                         {"value", object.value},
                         {"fadeSeconds", object.fadeSeconds},
                         {"sync", object.sync},
                         {"blendMode", static_cast<std::uint8_t>(object.blendMode)},
                         {"smoothingSeconds", object.smoothingSeconds},
                         {"blendPoints", std::move(blendPoints)},
                         {"parameterY", object.parameterY},
                         {"valueY", object.valueY},
                         {"bones", object.bones},
                         {"restart", object.restart},
                         {"areaMin", {object.areaMin.x, object.areaMin.y}},
                         {"areaMax", {object.areaMax.x, object.areaMax.y}},
                         {"entryState", object.entryState},
                         {"transitions", std::move(transitions)},
                         {"statePositions", std::move(statePositions)},
                         {"entryPosition", {object.entryPosition.x, object.entryPosition.y}},
                         {"anyPosition", {object.anyPosition.x, object.anyPosition.y}},
                         {"stateNames", object.stateNames},
                         {"owner", object.owner},
                         {"ownerState", object.ownerState},
                         {"gridX", object.gridX},
                         {"gridY", object.gridY},
                         {"snapToGrid", object.snapToGrid},
                         {"axisNameX", object.axisNameX},
                         {"axisNameY", object.axisNameY},
                         {"target", {object.target.x, object.target.y, object.target.z}},
                         {"pole", {object.pole.x, object.pole.y, object.pole.z}},
                         {"rotationDegrees", {object.rotationDegrees.x, object.rotationDegrees.y, object.rotationDegrees.z}},
                         {"boneScale", {object.boneScale.x, object.boneScale.y, object.boneScale.z}},
                         {"targetSource", static_cast<std::uint8_t>(object.targetSource)},
                         {"targetBone", object.targetBone},
                         {"modifyMode", static_cast<std::uint8_t>(object.modifyMode)},
                         {"space", static_cast<std::uint8_t>(object.space)},
                         {"clampDegrees", object.clampDegrees},
                         {"alphaParameter", object.alphaParameter},
                         {"fadeBackSeconds", object.fadeBackSeconds},
                         {"startSeconds", object.startSeconds},
                         {"fadeOutSeconds", object.fadeOutSeconds},
                         {"autoRestart", object.autoRestart},
                         {"autoRestartDelaySeconds", object.autoRestartDelaySeconds}});
    }
    nlohmann::json root = {{"parameters", std::move(parameters)},
            {"nodes", std::move(nodes)}};
    if (!component.comments.empty()) {
        nlohmann::json comments = nlohmann::json::array();
        for (const AnimationGraphComment& comment : component.comments) {
            comments.push_back({{"title", comment.title},
                                {"position", {comment.position.x, comment.position.y}},
                                {"size", {comment.size.x, comment.size.y}},
                                {"color", comment.color},
                                {"owner", comment.owner},
                                {"ownerState", comment.ownerState}});
        }
        root["comments"] = std::move(comments);
    }
    if (component.sequencer != kInvalidEntity) {
        root["sequencer"] = {{"index", component.sequencer.index}, {"generation", component.sequencer.generation}};
    }
    if (component.montageSequencer != kInvalidEntity) {
        root["montageSequencer"] = {{"index", component.montageSequencer.index}, {"generation", component.montageSequencer.generation}};
    }
    return root;
}

/// Reads a tree. A tree saved as the earlier two-clip blend becomes the same thing as a graph:
/// Output fed by a Blend2 of Clip A and Clip B, weighted by a "blend" parameter.
[[nodiscard]] AnimationGraphComponent AnimationGraphFromJson(const nlohmann::json& json) {
    AnimationGraphComponent tree;
    if (json.contains("sequencer") && json["sequencer"].is_object()) {
        tree.sequencer = Entity{
            json["sequencer"].value("index", std::numeric_limits<std::uint32_t>::max()),
            json["sequencer"].value("generation", 0U)};
    }
    const char* const montageKey = json.contains("montageSequencer") ? "montageSequencer" : "montagePlayer";
    if (json.contains(montageKey) && json[montageKey].is_object()) {
        tree.montageSequencer = Entity{
            json[montageKey].value("index", std::numeric_limits<std::uint32_t>::max()),
            json[montageKey].value("generation", 0U)};
    }
    if (!json.contains("nodes") && (json.contains("clipA") || json.contains("clipB"))) {
        tree.parameters = {AnimationParameter{"blend", AnimationParameterType::Float, json.value("blend", 0.0F)}};
        const auto makeObject = [](const std::uint32_t id, const AnimationGraphNodeKind kind, std::string name) {
            AnimationGraphNode object;
            object.id = id;
            object.kind = kind;
            object.name = std::move(name);
            return object;
        };
        AnimationGraphNode output = makeObject(1U, AnimationGraphNodeKind::Output, "Output");
        output.inputs = {2U};
        output.position = Math::Vector2{480.0F, 0.0F};
        AnimationGraphNode blend = makeObject(2U, AnimationGraphNodeKind::Blend2, "Blend");
        blend.inputs = {3U, 4U};
        blend.parameter = "blend";
        blend.position = Math::Vector2{240.0F, 0.0F};
        const float speed = json.value("speed", 1.0F);
        AnimationGraphNode clipA = makeObject(3U, AnimationGraphNodeKind::Clip, "Clip A");
        clipA.clip = Asset::AssetGuid{json.value("clipA", std::uint64_t{0})};
        clipA.speed = speed;
        AnimationGraphNode clipB = makeObject(4U, AnimationGraphNodeKind::Clip, "Clip B");
        clipB.clip = Asset::AssetGuid{json.value("clipB", std::uint64_t{0})};
        clipB.speed = speed;
        clipB.position = Math::Vector2{0.0F, 120.0F};
        tree.nodes = {output, blend, clipA, clipB};
        return tree;
    }
    tree.parameters.clear();
    for (const nlohmann::json& item : json.value("parameters", nlohmann::json::array())) {
        AnimationParameter parameter;
        parameter.name = item.at("name").get<std::string>();
        parameter.type = static_cast<AnimationParameterType>(item.value("type", std::uint8_t{0}));
        parameter.value = item.value("value", 0.0F);
        const std::vector<float> vector = item.value("vector", std::vector<float>{});
        if (vector.size() == 3U) {
            parameter.vector = Math::Vector3{vector[0], vector[1], vector[2]};
        }
        parameter.source = static_cast<AnimationParameterSource>(item.value("source", std::uint8_t{0}));
        tree.parameters.push_back(std::move(parameter));
    }
    if (json.contains("nodes")) {
        tree.nodes.clear();
        // Older saves placed a blend space's animations on its inputs, positioned by "points" (1D)
        // or "points2D"; they are folded into its own points once every object is read.
        std::vector<std::vector<Math::Vector2>> legacyPositions;
        std::vector<std::uint32_t> fitArea; // ids of spaces saved before they had an area
        for (const nlohmann::json& item : json.at("nodes")) {
            AnimationGraphNode object;
            object.id = item.at("id").get<std::uint32_t>();
            object.kind = static_cast<AnimationGraphNodeKind>(item.at("kind").get<std::uint8_t>());
            object.name = item.value("name", std::string{});
            const std::vector<float> position = item.value("position", std::vector<float>{0.0F, 0.0F});
            if (position.size() == 2U) {
                object.position = Math::Vector2{position[0], position[1]};
            }
            object.inputs = item.value("inputs", std::vector<std::uint32_t>{});
            object.clip = Asset::AssetGuid{item.value("clip", std::uint64_t{0})};
            object.loop = item.value("loop", true);
            object.speed = item.value("speed", 1.0F);
            object.parameter = item.value("parameter", std::string{});
            object.value = item.value("value", 0.5F);
            object.fadeSeconds = item.value("fadeSeconds", 0.2F);
            object.sync = item.value("sync", false);
            object.blendMode = static_cast<AnimationBlendMode>(item.value("blendMode", std::uint8_t{0}));
            object.smoothingSeconds = item.value("smoothingSeconds", 0.0F);
            object.parameterY = item.value("parameterY", std::string{});
            object.valueY = item.value("valueY", 0.0F);
            object.bones = item.value("bones", std::vector<std::string>{});
            object.restart = item.value("restart", true);
            const auto readPair = [&item](const char* key, Math::Vector2& out) {
                if (const auto found = item.find(key); found != item.end() && found->is_array() && found->size() == 2U) {
                    out = Math::Vector2{(*found)[0].get<float>(), (*found)[1].get<float>()};
                }
            };
            readPair("areaMin", object.areaMin);
            readPair("areaMax", object.areaMax);
            std::vector<Math::Vector2> legacy;
            for (const float x : item.value("points", std::vector<float>{})) {
                legacy.push_back(Math::Vector2{x, 0.0F});
            }
            if (const auto found = item.find("points2D"); found != item.end() && found->is_array()) {
                for (const auto& point : *found) {
                    if (point.is_array() && point.size() == 2U) {
                        legacy.push_back(Math::Vector2{point[0].get<float>(), point[1].get<float>()});
                    }
                }
            }
            legacyPositions.push_back(std::move(legacy));
            if (!item.contains("areaMin")) {
                fitArea.push_back(object.id);
            }
            if (const auto found = item.find("blendPoints"); found != item.end() && found->is_array()) {
                for (const auto& pointJson : *found) {
                    AnimationBlendPoint point;
                    const std::vector<float> at = pointJson.value("position", std::vector<float>{0.0F, 0.0F});
                    if (at.size() == 2U) {
                        point.position = Math::Vector2{at[0], at[1]};
                    }
                    point.clip = Asset::AssetGuid{pointJson.value("clip", std::uint64_t{0})};
                    point.speed = pointJson.value("speed", 1.0F);
                    point.loop = pointJson.value("loop", true);
                    object.blendPoints.push_back(point);
                }
            }
            object.entryState = item.value("entryState", std::uint32_t{0});
            for (const nlohmann::json& transitionJson : item.value("transitions", nlohmann::json::array())) {
                AnimationGraphTransition transition;
                transition.fromState = transitionJson.value("from", kAnyAnimationState);
                transition.toState = transitionJson.value("to", std::uint32_t{0});
                if (const auto list = transitionJson.find("conditions"); list != transitionJson.end() && list->is_array()) {
                    for (const nlohmann::json& testJson : *list) {
                        AnimationTransitionCondition test;
                        test.condition = static_cast<AnimationCondition>(testJson.value("condition", std::uint8_t{4}));
                        test.parameter = testJson.value("parameter", std::string{});
                        test.threshold = testJson.value("threshold", 0.0F);
                        transition.conditions.push_back(std::move(test));
                    }
                } else {
                    // Saved when a transition had one condition; Always is no condition at all.
                    const auto condition =
                        static_cast<AnimationCondition>(transitionJson.value("condition", std::uint8_t{0}));
                    if (condition != AnimationCondition::Always) {
                        transition.conditions.push_back(AnimationTransitionCondition{
                            condition, transitionJson.value("parameter", std::string{}), transitionJson.value("threshold", 0.0F)});
                    }
                }
                transition.exitPhase = transitionJson.value("exitPhase", -1.0F);
                transition.start = static_cast<AnimationTransitionStart>(transitionJson.value("start", std::uint8_t{0}));
                transition.curve = static_cast<AnimationTransitionCurve>(transitionJson.value("curve", std::uint8_t{0}));
                transition.interruptible = transitionJson.value("interruptible", true);
                transition.enabled = transitionJson.value("enabled", true);
                transition.fadeSeconds = transitionJson.value("fadeSeconds", 0.2F);
                object.transitions.push_back(std::move(transition));
            }
            for (const nlohmann::json& at : item.value("statePositions", nlohmann::json::array())) {
                if (at.is_array() && at.size() == 2U) {
                    object.statePositions.push_back(Math::Vector2{at[0].get<float>(), at[1].get<float>()});
                }
            }
            readPair("entryPosition", object.entryPosition);
            readPair("anyPosition", object.anyPosition);
            const auto readTriple = [&item](const char* key, Math::Vector3& out) {
                if (const auto found = item.find(key); found != item.end() && found->is_array() && found->size() == 3U) {
                    out = Math::Vector3{(*found)[0].get<float>(), (*found)[1].get<float>(), (*found)[2].get<float>()};
                }
            };
            readTriple("target", object.target);
            readTriple("pole", object.pole);
            readTriple("rotationDegrees", object.rotationDegrees);
            readTriple("boneScale", object.boneScale);
            object.targetSource = static_cast<AnimationGraphTargetSource>(item.value("targetSource", std::uint8_t{0}));
            object.targetBone = item.value("targetBone", std::string{});
            object.modifyMode = static_cast<AnimationBoneModifyMode>(item.value("modifyMode", std::uint8_t{0}));
            object.space = static_cast<AnimationBoneSpace>(item.value("space", std::uint8_t{0}));
            object.clampDegrees = item.value("clampDegrees", 180.0F);
            object.alphaParameter = item.value("alphaParameter", std::string{});
            object.fadeBackSeconds = item.value("fadeBackSeconds", 0.2F);
            object.startSeconds = item.value("startSeconds", 0.0F);
            object.fadeOutSeconds = item.value("fadeOutSeconds", -1.0F);
            object.autoRestart = item.value("autoRestart", false);
            object.autoRestartDelaySeconds = item.value("autoRestartDelaySeconds", 1.0F);
            if (const auto names = item.find("stateNames"); names != item.end() && names->is_array()) {
                for (const nlohmann::json& name : *names) {
                    object.stateNames.push_back(name.is_string() ? name.get<std::string>() : std::string{});
                }
            }
            object.owner = item.value("owner", std::uint32_t{0});
            object.ownerState = item.value("ownerState", std::uint32_t{0});
            object.gridX = item.value("gridX", std::uint32_t{4});
            object.gridY = item.value("gridY", std::uint32_t{4});
            object.snapToGrid = item.value("snapToGrid", true);
            object.axisNameX = item.value("axisNameX", std::string{});
            object.axisNameY = item.value("axisNameY", std::string{});
            tree.nodes.push_back(std::move(object));
        }
        static_cast<void>(MigrateBlendSpaceInputs(tree.nodes, legacyPositions));
        // A space saved before it had an area gets one around its points.
        for (AnimationGraphNode& object : tree.nodes) {
            if (std::find(fitArea.begin(), fitArea.end(), object.id) == fitArea.end() || object.blendPoints.empty() ||
                (object.kind != AnimationGraphNodeKind::BlendSpace1D && object.kind != AnimationGraphNodeKind::BlendSpace2D)) {
                continue;
            }
            Math::Vector2 lo = object.blendPoints.front().position;
            Math::Vector2 hi = lo;
            for (const AnimationBlendPoint& point : object.blendPoints) {
                lo = Math::Vector2{std::min(lo.x, point.position.x), std::min(lo.y, point.position.y)};
                hi = Math::Vector2{std::max(hi.x, point.position.x), std::max(hi.y, point.position.y)};
            }
            const float marginX = std::max((hi.x - lo.x) * 0.15F, 0.5F);
            const float marginY = std::max((hi.y - lo.y) * 0.15F, 0.5F);
            object.areaMin = Math::Vector2{lo.x - marginX, lo.y - marginY};
            object.areaMax = Math::Vector2{hi.x + marginX, hi.y + marginY};
        }
    }
    if (json.contains("comments") && json["comments"].is_array()) {
        for (const nlohmann::json& item : json["comments"]) {
            if (!item.is_object()) {
                continue;
            }
            AnimationGraphComment comment;
            comment.title = item.value("title", std::string{"Comment"});
            if (const auto p = item.find("position"); p != item.end() && p->is_array() && p->size() == 2U) {
                comment.position = Math::Vector2{(*p)[0].get<float>(), (*p)[1].get<float>()};
            }
            if (const auto z = item.find("size"); z != item.end() && z->is_array() && z->size() == 2U) {
                comment.size = Math::Vector2{(*z)[0].get<float>(), (*z)[1].get<float>()};
            }
            comment.color = item.value("color", comment.color);
            comment.owner = item.value("owner", std::uint32_t{0});
            comment.ownerState = item.value("ownerState", std::uint32_t{0});
            tree.comments.push_back(std::move(comment));
        }
    }
    return tree;
}









[[nodiscard]] nlohmann::json ToJson(const CharacterControllerComponent& component) {
    return {{"motionMode", static_cast<std::uint8_t>(component.motionMode)},
            {"upDirection", ToJson(component.upDirection)},
            {"gravityScale", component.gravityScale},
            {"builtInMovement", component.builtInMovement},
            {"moveSpeed", component.moveSpeed},
            {"jumpHeight", component.jumpHeight},
            {"airControl", component.airControl},
            {"coyoteTimeSeconds", component.coyoteTimeSeconds},
            {"jumpBufferSeconds", component.jumpBufferSeconds},
            {"floorMaxAngleDegrees", component.floorMaxAngleDegrees},
            {"wallMinSlideAngleDegrees", component.wallMinSlideAngleDegrees},
            {"safeMargin", component.safeMargin},
            {"floorStopOnSlope", component.floorStopOnSlope},
            {"floorConstantSpeed", component.floorConstantSpeed},
            {"floorSnapLength", component.floorSnapLength},
            {"maxStepHeight", component.maxStepHeight},
            {"minStepWidth", component.minStepWidth},
            {"slideOnCeiling", component.slideOnCeiling},
            {"floorBlockOnWall", component.floorBlockOnWall},
            {"platformOnLeave", static_cast<std::uint8_t>(component.platformOnLeave)},
            {"maximumPlatformSpeed", component.maximumPlatformSpeed},
            {"pushRigidBodies", component.pushRigidBodies},
            {"pushStrength", component.pushStrength},
            {"maxPushSpeed", component.maxPushSpeed},
            {"maxSlides", component.maxSlides},
            {"maximumContacts", component.maximumContacts},
            {"velocity", ToJson(component.velocity)},
            {"grounded", component.grounded},
            {"isOnCeiling", component.isOnCeiling},
            {"floorNormal", ToJson(component.floorNormal)},
            {"timeSinceOnFloor", component.timeSinceOnFloor},
            {"jumpBufferRemaining", component.jumpBufferRemaining}};
}



[[nodiscard]] nlohmann::json ToJson(const UIButtonComponent& component) {
    return {{"positionPixels", ToJson(component.rect.position)},
            {"sizePixels", ToJson(component.rect.size)},
            {"normalColor", ToJson(component.normalColor)},
            {"hoverColor", ToJson(component.hoverColor)},
            {"pressedColor", ToJson(component.pressedColor)},
            {"isHovered", component.isHovered},
            {"wasClickedThisFrame", component.wasClickedThisFrame}};
}


[[nodiscard]] nlohmann::json ToJson(const ScriptComponent& component) {
    nlohmann::json json{{"scriptAssetPath", component.scriptAssetPath}};
    if (!component.exportValues.empty()) {
        json["exportValues"] = component.exportValues;
    }
    return json;
}










// ---- Variant <-> JSON ------------------------------------------------------------------------
//
// {"type": "<name>", "value": <payload>}. The type is written by name (GetVariantTypeName), never
// by enumerator number, so the Variant type list can grow or reorder without invalidating a single
// saved scene. Vectors are arrays of numbers; Dictionaries are arrays of {key, value} so authored
// order survives, for the same reason metadata entries are.

[[nodiscard]] nlohmann::json VariantToJson(const Core::Variant& value);

template <typename VectorT>
[[nodiscard]] nlohmann::json ComponentsToJson(const VectorT& vector) {
    if constexpr (std::is_same_v<VectorT, Math::Vector2>) {
        return nlohmann::json::array({vector.x, vector.y});
    } else if constexpr (std::is_same_v<VectorT, Math::Vector3>) {
        return nlohmann::json::array({vector.x, vector.y, vector.z});
    } else if constexpr (std::is_same_v<VectorT, Core::VariantColor>) {
        return nlohmann::json::array({vector.r, vector.g, vector.b, vector.a});
    } else {
        return nlohmann::json::array({vector.x, vector.y, vector.z, vector.w});
    }
}

[[nodiscard]] nlohmann::json VariantPayloadToJson(const Core::Variant& value) {
    return std::visit(
        [](const auto& stored) -> nlohmann::json {
            using StoredT = std::decay_t<decltype(stored)>;
            if constexpr (std::is_same_v<StoredT, Math::Vector2> || std::is_same_v<StoredT, Math::Vector3> ||
                          std::is_same_v<StoredT, Core::VariantVector4> ||
                          std::is_same_v<StoredT, Math::Quaternion> ||
                          std::is_same_v<StoredT, Core::VariantColor>) {
                return ComponentsToJson(stored);
            } else if constexpr (std::is_same_v<StoredT, std::vector<Core::Variant>>) {
                nlohmann::json elements = nlohmann::json::array();
                for (const Core::Variant& element : stored) {
                    elements.push_back(VariantToJson(element));
                }
                return elements;
            } else if constexpr (std::is_same_v<StoredT, std::vector<Core::VariantDictionaryEntry>>) {
                nlohmann::json entries = nlohmann::json::array();
                for (const Core::VariantDictionaryEntry& entry : stored) {
                    entries.push_back({{"key", entry.key}, {"value", VariantToJson(entry.value)}});
                }
                return entries;
            } else if constexpr (std::is_same_v<StoredT, std::vector<Math::Vector2>> ||
                                 std::is_same_v<StoredT, std::vector<Math::Vector3>> ||
                                 std::is_same_v<StoredT, std::vector<Core::VariantColor>>) {
                nlohmann::json elements = nlohmann::json::array();
                for (const auto& element : stored) {
                    elements.push_back(ComponentsToJson(element));
                }
                return elements;
            } else {
                // bool, integers, double, string, and packed arrays of those: nlohmann writes each as
                // its natural JSON form.
                return stored;
            }
        },
        value.GetStorage());
}

[[nodiscard]] nlohmann::json VariantToJson(const Core::Variant& value) {
    return {{"type", std::string{Core::GetVariantTypeName(value.GetType())}},
            {"value", VariantPayloadToJson(value)}};
}

[[nodiscard]] float ReadFloat(const nlohmann::json& json) {
    if (!json.is_number()) {
        throw std::runtime_error("Variant component is not a number");
    }
    return json.get<float>();
}

template <typename VectorT>
[[nodiscard]] VectorT ComponentsFromJson(const nlohmann::json& json) {
    constexpr std::size_t count =
        std::is_same_v<VectorT, Math::Vector2> ? 2U : (std::is_same_v<VectorT, Math::Vector3> ? 3U : 4U);
    if (!json.is_array() || json.size() != count) {
        throw std::runtime_error("Variant vector has the wrong number of components");
    }
    if constexpr (std::is_same_v<VectorT, Math::Vector2>) {
        return {ReadFloat(json[0]), ReadFloat(json[1])};
    } else if constexpr (std::is_same_v<VectorT, Math::Vector3>) {
        return {ReadFloat(json[0]), ReadFloat(json[1]), ReadFloat(json[2])};
    } else if constexpr (std::is_same_v<VectorT, Core::VariantColor>) {
        return {ReadFloat(json[0]), ReadFloat(json[1]), ReadFloat(json[2]), ReadFloat(json[3])};
    } else {
        return {ReadFloat(json[0]), ReadFloat(json[1]), ReadFloat(json[2]), ReadFloat(json[3])};
    }
}

/// Decodes one Variant, throwing on anything malformed - the serializer turns that into a rolled-back
/// load. `depth` stops the recursion at the Variant depth bound, so a hostile file nested thousands
/// deep cannot exhaust the stack before validation ever runs.
[[nodiscard]] Core::Variant VariantFromJson(const nlohmann::json& json, const std::size_t depth) {
    if (depth > Core::kMaximumVariantDepth) {
        throw std::runtime_error("Variant nesting exceeds the depth limit");
    }
    if (!json.is_object() || !json.contains("type") || !json.contains("value")) {
        throw std::runtime_error("Variant is not a {type, value} object");
    }
    const std::optional<Core::VariantType> type = Core::TryParseVariantTypeName(json.at("type").get<std::string>());
    if (!type.has_value()) {
        throw std::runtime_error("Variant has an unknown type");
    }
    const nlohmann::json& payload = json.at("value");
    Core::Variant value = Core::Variant::MakeDefault(*type);
    const auto readList = [&payload](auto& list, const auto& readElement) {
        if (!payload.is_array() || payload.size() > Core::kMaximumVariantElements) {
            throw std::runtime_error("Variant array payload is not a bounded array");
        }
        list.reserve(payload.size());
        for (const nlohmann::json& element : payload) {
            list.push_back(readElement(element));
        }
    };
    value.VisitMutable(
        [&](auto& stored) {
            using StoredT = std::decay_t<decltype(stored)>;
            if constexpr (std::is_same_v<StoredT, bool>) {
                stored = payload.get<bool>();
            } else if constexpr (std::is_same_v<StoredT, std::int64_t>) {
                stored = payload.get<std::int64_t>();
            } else if constexpr (std::is_same_v<StoredT, std::uint64_t>) {
                stored = payload.get<std::uint64_t>();
            } else if constexpr (std::is_same_v<StoredT, double>) {
                if (!payload.is_number()) {
                    throw std::runtime_error("Variant float is not a number");
                }
                stored = payload.get<double>();
            } else if constexpr (std::is_same_v<StoredT, std::string>) {
                stored = payload.get<std::string>();
            } else if constexpr (std::is_same_v<StoredT, Math::Vector2> ||
                                 std::is_same_v<StoredT, Math::Vector3> ||
                                 std::is_same_v<StoredT, Core::VariantVector4> ||
                                 std::is_same_v<StoredT, Math::Quaternion> ||
                                 std::is_same_v<StoredT, Core::VariantColor>) {
                stored = ComponentsFromJson<StoredT>(payload);
            } else if constexpr (std::is_same_v<StoredT, std::vector<Core::Variant>>) {
                readList(stored, [depth](const nlohmann::json& element) {
                    return VariantFromJson(element, depth + 1U);
                });
            } else if constexpr (std::is_same_v<StoredT, std::vector<Core::VariantDictionaryEntry>>) {
                readList(stored, [depth](const nlohmann::json& entry) {
                    return Core::VariantDictionaryEntry{entry.at("key").get<std::string>(),
                                                          VariantFromJson(entry.at("value"), depth + 1U)};
                });
            } else if constexpr (std::is_same_v<StoredT, std::vector<Math::Vector2>> ||
                                 std::is_same_v<StoredT, std::vector<Math::Vector3>> ||
                                 std::is_same_v<StoredT, std::vector<Core::VariantColor>>) {
                using ElementT = typename StoredT::value_type;
                readList(stored, [](const nlohmann::json& element) { return ComponentsFromJson<ElementT>(element); });
            } else {
                using ElementT = typename StoredT::value_type;
                readList(stored, [](const nlohmann::json& element) { return element.get<ElementT>(); });
            }
        });
    return value;
}

[[nodiscard]] nlohmann::json ToJson(const ObjectMetadataComponent& component) {
    // An array of key/value objects rather than one JSON object, so authored order survives a
    // round trip - a JSON object's member order is not something a reader is obliged to keep, and
    // losing it would reorder an inspector's rows and dirty a scene file for no reason.
    nlohmann::json entries = nlohmann::json::array();
    for (const ObjectMetadataEntry& entry : component.entries) {
        entries.push_back({{"key", entry.key}, {"value", VariantToJson(entry.value)}});
    }
    return {{"entries", std::move(entries)}};
}

[[nodiscard]] nlohmann::json ToJson(const EditorDescriptionComponent& component) {
    return {{"description", component.description}};
}

[[nodiscard]] nlohmann::json ToJson(const FolderComponent&) {
    return nlohmann::json::object();
}

[[nodiscard]] nlohmann::json ToJson(const OutlinerViewport3DComponent&) {
    return nlohmann::json::object();
}

[[nodiscard]] nlohmann::json ToJson(const SceneRootComponent&) {
    return nlohmann::json::object(); // pure marker: no authored state to persist
}

// The type is written by its stable id ("box_mesh_3d"), never the enum's number, so reordering
// the enum can never retype a saved object.
[[nodiscard]] nlohmann::json ToJson(const SceneObjectTypeComponent& value) {
    return {{"type", std::string{Objects::GetSceneObjectTypeId(value.kind)}}};
}

[[nodiscard]] nlohmann::json ToJson(const RayCast3DComponent& value) {
    // `exclusions` is deliberately absent: it holds entity references, and only the entity-aware
    // encoder below knows which file-local id each referenced entity has. Writing the raw handles
    // here is what the old format did, and a raw handle is meaningless in the next session - the
    // pool is free to hand that index to anything.
    return {{"direction", ToJson(value.direction)},
            {"length", value.length},
            {"collisionMask", value.collisionMask},
            {"enabled", value.enabled}};
}

[[nodiscard]] RayCast3DComponent RayCast3DObjectFromJson(const nlohmann::json& json) {
    // The registration reader is used everywhere a RayCast3D payload is validated WITHOUT an entity
    // id table - including the scene decoder's validation pass, which only needs to know the
    // payload is well-formed. Entity references are resolved by the entity-aware restore path
    // below, which is the only code that knows which file-local id maps to which restored entity;
    // a legacy `exclusions` array of raw handles is tolerated and dropped, because a raw handle
    // cannot be mapped to anything meaningful.
    RayCast3DComponent value;
    value.direction = Vector3FromJson(json.at("direction"));
    value.length = json.value("length", 100.0F);
    value.collisionMask = json.value("collisionMask", std::uint32_t{0xFFFFFFFFU});
    value.enabled = json.value("enabled", true);
    return value;
}

[[nodiscard]] ControllerComponent ControllerObjectFromJson(const nlohmann::json& json) {
    ControllerComponent value;
    value.kind = static_cast<ControllerKind>(json.value("kind", 0U));
    return value;
}

/// The entity-aware half of reading a Controller: its pawn is an entity reference, so only the
/// caller that owns the file's local-id table can resolve it. An unresolvable link drops to
/// invalid - an unpossessed controller loads fine, a guessed one would drive a stranger.
[[nodiscard]] ControllerComponent ControllerComponentWithResolvedPawn(
    const nlohmann::json& json,
    const std::unordered_map<std::uint32_t, Entity>& localIdToEntity) {
    ControllerComponent value = ControllerObjectFromJson(json);
    const std::int64_t localId =
        json.value("pawnLocalId", static_cast<std::int64_t>(std::numeric_limits<std::uint32_t>::max()));
    if (localId < 0 ||
        static_cast<std::uint64_t>(localId) > std::numeric_limits<std::uint32_t>::max()) {
        throw std::runtime_error("ControllerComponent pawn local ID is outside the uint32 range");
    }
    const auto targetIt = localIdToEntity.find(static_cast<std::uint32_t>(localId));
    value.pawn = targetIt != localIdToEntity.end() ? targetIt->second : kInvalidEntity;
    return value;
}

[[nodiscard]] PawnComponent PawnObjectFromJson(const nlohmann::json& json) {
    PawnComponent value;
    if (const auto input = json.find("input"); input != json.end() && input->is_object()) {
        value.input.move = Vector3FromJson(input->at("move"));
        value.input.rise = input->value("rise", 0.0F);
        value.input.jumpPressed = input->value("jumpPressed", false);
        value.input.lookPointer = Vector2FromJson(input->at("lookPointer"));
        value.input.lookStick = Vector2FromJson(input->at("lookStick"));
        value.input.interactPressed = input->value("interactPressed", false);
    }
    return value;
}

/// The entity-aware half of reading a Pawn, mirroring the controller's: the link drops to
/// invalid when the file does not contain it.
[[nodiscard]] PawnComponent PawnComponentWithResolvedController(
    const nlohmann::json& json,
    const std::unordered_map<std::uint32_t, Entity>& localIdToEntity) {
    PawnComponent value = PawnObjectFromJson(json);
    const std::int64_t localId = json.value(
        "controllerLocalId", static_cast<std::int64_t>(std::numeric_limits<std::uint32_t>::max()));
    if (localId < 0 ||
        static_cast<std::uint64_t>(localId) > std::numeric_limits<std::uint32_t>::max()) {
        throw std::runtime_error("PawnComponent controller local ID is outside the uint32 range");
    }
    const auto targetIt = localIdToEntity.find(static_cast<std::uint32_t>(localId));
    value.controller = targetIt != localIdToEntity.end() ? targetIt->second : kInvalidEntity;
    return value;
}

[[nodiscard]] CameraFollowComponent CameraFollowObjectFromJson(const nlohmann::json& json) {
    CameraFollowComponent value;
    value.offset =
        json.contains("offset") ? Vector3FromJson(json.at("offset")) : Math::Vector3{};
    value.followPossessedPawn = json.value("followPossessedPawn", false);
    return value;
}

/// The entity-aware half of reading a CameraFollow, mirroring the pawn's: the link drops to
/// invalid when the file does not contain it.
[[nodiscard]] CameraFollowComponent CameraFollowComponentWithResolvedTarget(
    const nlohmann::json& json,
    const std::unordered_map<std::uint32_t, Entity>& localIdToEntity) {
    CameraFollowComponent value = CameraFollowObjectFromJson(json);
    const std::int64_t localId = json.value(
        "targetLocalId", static_cast<std::int64_t>(std::numeric_limits<std::uint32_t>::max()));
    if (localId < 0 ||
        static_cast<std::uint64_t>(localId) > std::numeric_limits<std::uint32_t>::max()) {
        throw std::runtime_error("CameraFollowComponent target local ID is outside the uint32 range");
    }
    const auto targetIt = localIdToEntity.find(static_cast<std::uint32_t>(localId));
    value.target = targetIt != localIdToEntity.end() ? targetIt->second : kInvalidEntity;
    return value;
}

/// The entity-aware half of reading a RayCast3D: its exclusions are entity references, so only the
/// caller that owns the file's local-id table can resolve them. A reference the file does not
/// contain is DROPPED rather than turned back into a raw handle - the same rule the animation
/// target and the visibility parent follow - and the exclusions that did resolve keep their order,
/// so a scene that lost one still loads with the rest instead of losing the whole component.
[[nodiscard]] RayCast3DComponent RayCast3DComponentWithResolvedExclusions(
    const nlohmann::json& json,
    const std::unordered_map<std::uint32_t, Entity>& localIdToEntity) {
    RayCast3DComponent value = RayCast3DObjectFromJson(json);
    const nlohmann::json exclusionIds = json.value("exclusionsLocalIds", nlohmann::json::array());
    if (!exclusionIds.is_array() || exclusionIds.size() > kMaximumRayCastExclusions) {
        throw std::runtime_error("RayCast3DComponent exclusionsLocalIds must be a bounded array");
    }
    std::size_t resolvedCount = 0U;
    for (const auto& exclusionId : exclusionIds) {
        const std::int64_t localId = exclusionId.get<std::int64_t>();
        if (localId < 0 || static_cast<std::uint64_t>(localId) > std::numeric_limits<std::uint32_t>::max()) {
            throw std::runtime_error("RayCast3DComponent exclusion local ID is outside the uint32 range");
        }
        const auto targetIt = localIdToEntity.find(static_cast<std::uint32_t>(localId));
        if (targetIt == localIdToEntity.end()) {
            continue; // outside what this file contains: dropped, not guessed at
        }
        // Dropped references are compacted away, not left as holes: the list is a prefix, and a
        // hole would make the component fail its own validator (a live reference behind an empty
        // slot) and take the whole ray down with it.
        value.exclusions[resolvedCount] = targetIt->second;
        ++resolvedCount;
    }
    if (!IsRayCast3DObjectComponentValid(value)) {
        throw std::runtime_error("Invalid RayCast3DComponent payload");
    }
    return value;
}



[[nodiscard]] nlohmann::json ToJson(const NavMeshVolume3DComponent& value) {
    // `rebuildRequested` is a request to the running navigation runtime, not a fact about the level:
    // a scene reloaded tomorrow has nothing to rebuild, so it is not written.
    return {{"boundsHalfExtents", ToJson(value.boundsHalfExtents)},
            {"navigationMeshAssetPath", value.navigationMeshAssetPath},
            {"navigationLayers", value.navigationLayers},
            {"cellSize", value.cellSize},
            {"agentRadius", value.agentRadius},
            {"agentHeight", value.agentHeight},
            {"maximumSlopeDegrees", value.maximumSlopeDegrees},
            {"maximumStepHeight", value.maximumStepHeight},
            {"enabled", value.enabled}};
}

[[nodiscard]] NavMeshVolume3DComponent NavMeshVolume3DObjectFromJson(const nlohmann::json& json) {
    NavMeshVolume3DComponent value;
    value.boundsHalfExtents = Vector3FromJson(json.at("boundsHalfExtents"));
    value.navigationMeshAssetPath = json.value("navigationMeshAssetPath", std::string{});
    value.navigationLayers = json.value("navigationLayers", std::uint32_t{1});
    value.cellSize = json.value("cellSize", 0.5F);
    value.agentRadius = json.value("agentRadius", 0.5F);
    value.agentHeight = json.value("agentHeight", 1.8F);
    value.maximumSlopeDegrees = json.value("maximumSlopeDegrees", 45.0F);
    value.maximumStepHeight = json.value("maximumStepHeight", 0.4F);
    value.enabled = json.value("enabled", true);
    return value;
}



[[nodiscard]] nlohmann::json ToJson(const Skeleton3DComponent& value) {
    nlohmann::json bones = nlohmann::json::array();
    for (const SkeletonBone& bone : value.bones) {
        bones.push_back({{"name", bone.name},
                         {"parentIndex", bone.parentIndex},
                         {"localPosition", ToJson(bone.localPosition)},
                         {"localRotation", ToJson(bone.localRotation)},
                         {"localScale", ToJson(bone.localScale)}});
    }
    return {{"skeletonAssetPath", value.skeletonAssetPath}, {"bones", std::move(bones)}, {"enabled", value.enabled}};
}

[[nodiscard]] Skeleton3DComponent Skeleton3DObjectFromJson(const nlohmann::json& json) {
    Skeleton3DComponent value;
    value.skeletonAssetPath = json.value("skeletonAssetPath", std::string{});
    const nlohmann::json bones = json.value("bones", nlohmann::json::array());
    if (!bones.is_array() || bones.size() > kMaximumSkeletonBones) {
        throw std::runtime_error("Skeleton3DComponent bones must be a bounded array");
    }
    value.bones.reserve(bones.size());
    for (const nlohmann::json& boneJson : bones) {
        value.bones.push_back(SkeletonBone{boneJson.at("name").get<std::string>(),
                                              boneJson.value("parentIndex", -1),
                                              Vector3FromJson(boneJson.at("localPosition")),
                                              QuaternionFromJson(boneJson.at("localRotation")),
                                              Vector3FromJson(boneJson.at("localScale"))});
    }
    value.enabled = json.value("enabled", true);
    return value;
}

/// The `skeletonLocalId` key is deliberately NOT written here: the skeleton is an entity reference,
/// and only the save pass can turn one into a file-local id (see the reference pass in Save,
/// which writes that key from the component's resolved reference for every saved attachment).
[[nodiscard]] nlohmann::json ToJson(const BoneAttachment3DComponent& value) {
    return {{"boneIndex", value.boneIndex},
            {"boneName", value.boneName},
            {"localPosition", ToJson(value.localPosition)},
            {"localRotation", ToJson(value.localRotation)},
            {"localScale", ToJson(value.localScale)},
            {"enabled", value.enabled}};
}

[[nodiscard]] BoneAttachment3DComponent BoneAttachment3DObjectFromJson(const nlohmann::json& json) {
    BoneAttachment3DComponent value;
    // The skeleton reference itself is left unset here: it is a file-local id in the document, and
    // the load pass below is what turns it into an entity. An id this file does not contain leaves
    // the attachment inert rather than binding it to whatever entity shares that id elsewhere.
    value.boneIndex = json.value("boneIndex", kInvalidSkeletonBoneIndex);
    value.boneName = json.value("boneName", std::string{});
    value.localPosition = Vector3FromJson(json.at("localPosition"));
    value.localRotation = QuaternionFromJson(json.at("localRotation"));
    value.localScale = Vector3FromJson(json.at("localScale"));
    value.enabled = json.value("enabled", true);
    return value;
}

/// The load-side half of the reference: the authored file-local id becomes the entity it names.
[[nodiscard]] BoneAttachment3DComponent BoneAttachment3DComponentWithResolvedSkeleton(
    const nlohmann::json& json, const std::unordered_map<std::uint32_t, Entity>& localIdToEntity) {
    BoneAttachment3DComponent value = BoneAttachment3DObjectFromJson(json);
    const std::uint32_t skeletonLocalId =
        json.value("skeletonLocalId", std::numeric_limits<std::uint32_t>::max());
    const auto skeletonIt = localIdToEntity.find(skeletonLocalId);
    value.skeleton = skeletonIt != localIdToEntity.end() ? skeletonIt->second : kInvalidEntity;
    return value;
}

/// The three `*LocalId` keys are deliberately NOT written here - they are entity references, and
/// only the save pass can turn one into a file-local id (see the reference pass in Save, which
/// writes them from the component's resolved references for every saved chain).
[[nodiscard]] nlohmann::json ToJson(const TwoBoneIK3DComponent& value) {
    return {{"rootBoneIndex", value.rootBoneIndex},
            {"rootBoneName", value.rootBoneName},
            {"middleBoneIndex", value.middleBoneIndex},
            {"middleBoneName", value.middleBoneName},
            {"endBoneIndex", value.endBoneIndex},
            {"endBoneName", value.endBoneName},
            {"targetPosition", ToJson(value.targetPosition)},
            {"poleDirection", ToJson(value.poleDirection)},
            {"enabled", value.enabled}};
}

[[nodiscard]] TwoBoneIK3DComponent TwoBoneIK3DObjectFromJson(const nlohmann::json& json) {
    TwoBoneIK3DComponent value;
    // The references themselves are left unset here: they are file-local ids in the document, and
    // the load pass below turns them into entities. An id this file does not contain leaves that
    // reference inert rather than pointing at whatever entity shares the id elsewhere.
    value.rootBoneIndex = json.value("rootBoneIndex", kInvalidSkeletonBoneIndex);
    value.rootBoneName = json.value("rootBoneName", std::string{});
    value.middleBoneIndex = json.value("middleBoneIndex", kInvalidSkeletonBoneIndex);
    value.middleBoneName = json.value("middleBoneName", std::string{});
    value.endBoneIndex = json.value("endBoneIndex", kInvalidSkeletonBoneIndex);
    value.endBoneName = json.value("endBoneName", std::string{});
    value.targetPosition = Vector3FromJson(json.at("targetPosition"));
    value.poleDirection = Vector3FromJson(json.at("poleDirection"));
    value.enabled = json.value("enabled", true);
    return value;
}

/// The load-side half of every reference a chain has: an authored file-local id becomes the entity
/// it names, and one that names nothing in this file stays the sentinel - which the solver reads as
/// "the authored target position is what I solve toward".
[[nodiscard]] TwoBoneIK3DComponent TwoBoneIK3DComponentWithResolvedReferences(
    const nlohmann::json& json, const std::unordered_map<std::uint32_t, Entity>& localIdToEntity) {
    TwoBoneIK3DComponent value = TwoBoneIK3DObjectFromJson(json);
    const auto resolve = [&json, &localIdToEntity](const char* key) {
        const std::uint32_t localId =
            json.value(key, std::numeric_limits<std::uint32_t>::max());
        const auto found = localIdToEntity.find(localId);
        return found != localIdToEntity.end() ? found->second : kInvalidEntity;
    };
    value.skeleton = resolve("skeletonLocalId");
    value.target = resolve("targetLocalId");
    value.poleTarget = resolve("poleLocalId");
    return value;
}

[[nodiscard]] nlohmann::json ToJson(const FullBodyIK3DComponent& value) {
    nlohmann::json effectors = nlohmann::json::array();
    for (const FullBodyIKEffector& effector : value.effectors) {
        effectors.push_back({{"boneIndex", effector.boneIndex},
                             {"boneName", effector.boneName},
                             {"targetPosition", ToJson(effector.targetPosition)},
                             {"positionWeight", effector.positionWeight}});
    }
    return {{"rootBoneIndex", value.rootBoneIndex},
            {"rootBoneName", value.rootBoneName},
            {"effectors", std::move(effectors)},
            {"maxIterations", value.maxIterations},
            {"toleranceMetres", value.toleranceMetres},
            {"rootTranslationWeight", value.rootTranslationWeight},
            {"maxJointStepRadians", value.maxJointStepRadians},
            {"enabled", value.enabled}};
}

[[nodiscard]] FullBodyIK3DComponent FullBodyIK3DObjectFromJson(const nlohmann::json& json) {
    FullBodyIK3DComponent value;
    // The references themselves are left unset here: they are file-local ids in the document, and
    // the load pass below turns them into entities. An id this file does not contain leaves that
    // reference inert rather than pointing at whatever entity shares the id elsewhere.
    value.rootBoneIndex = json.value("rootBoneIndex", kInvalidSkeletonBoneIndex);
    value.rootBoneName = json.value("rootBoneName", std::string{});
    const auto effectors = json.find("effectors");
    if (effectors != json.end() && effectors->is_array()) {
        for (const nlohmann::json& entry : *effectors) {
            FullBodyIKEffector effector;
            effector.boneIndex = entry.value("boneIndex", kInvalidSkeletonBoneIndex);
            effector.boneName = entry.value("boneName", std::string{});
            effector.targetPosition = Vector3FromJson(entry.at("targetPosition"));
            effector.positionWeight = entry.value("positionWeight", 1.0F);
            value.effectors.push_back(std::move(effector));
        }
    }
    value.maxIterations = json.value("maxIterations", FullBodyIK3DComponent{}.maxIterations);
    value.toleranceMetres = json.value("toleranceMetres", FullBodyIK3DComponent{}.toleranceMetres);
    value.rootTranslationWeight =
        json.value("rootTranslationWeight", FullBodyIK3DComponent{}.rootTranslationWeight);
    value.maxJointStepRadians =
        json.value("maxJointStepRadians", FullBodyIK3DComponent{}.maxJointStepRadians);
    value.enabled = json.value("enabled", true);
    return value;
}

/// The load-side half of every reference a solve has: an authored file-local id becomes the entity
/// it names, and one that names nothing in this file stays the sentinel - which the solver reads as
/// "the authored target position is what I solve toward". Effector targets resolve inside their own
/// entries so one gone target leaves that limb inert instead of shifting every other one.
[[nodiscard]] FullBodyIK3DComponent FullBodyIK3DComponentWithResolvedReferences(
    const nlohmann::json& json, const std::unordered_map<std::uint32_t, Entity>& localIdToEntity) {
    FullBodyIK3DComponent value = FullBodyIK3DObjectFromJson(json);
    const auto resolve = [&json, &localIdToEntity](const char* key) {
        const std::uint32_t localId =
            json.value(key, std::numeric_limits<std::uint32_t>::max());
        const auto found = localIdToEntity.find(localId);
        return found != localIdToEntity.end() ? found->second : kInvalidEntity;
    };
    value.skeleton = resolve("skeletonLocalId");
    const auto effectors = json.find("effectors");
    if (effectors != json.end() && effectors->is_array()) {
        for (std::size_t index = 0U; index < value.effectors.size() && index < effectors->size(); ++index) {
            const std::uint32_t localId =
                (*effectors)[index].value("targetLocalId", std::numeric_limits<std::uint32_t>::max());
            const auto found = localIdToEntity.find(localId);
            value.effectors[index].target =
                found != localIdToEntity.end() ? found->second : kInvalidEntity;
        }
    }
    return value;
}

[[nodiscard]] nlohmann::json ToJson(const Ragdoll3DComponent& value) {
    nlohmann::json bonesJson = nlohmann::json::array();
    for (const RagdollBoneSetup& bone : value.bones) {
        bonesJson.push_back({{"boneName", bone.boneName},
                             {"parentBoneName", bone.parentBoneName},
                             {"mass", bone.mass},
                             {"radius", bone.radius},
                             {"halfHeight", bone.halfHeight},
                             {"linearDamping", bone.linearDamping},
                             {"angularDamping", bone.angularDamping},
                             {"friction", bone.friction},
                             {"swing1Motion", static_cast<std::uint8_t>(bone.swing1Motion)},
                             {"swing2Motion", static_cast<std::uint8_t>(bone.swing2Motion)},
                             {"twistMotion", static_cast<std::uint8_t>(bone.twistMotion)},
                             {"swing1LimitDegrees", bone.swing1LimitDegrees},
                             {"swing2LimitDegrees", bone.swing2LimitDegrees},
                             {"twistLimitDegrees", bone.twistLimitDegrees},
                             {"twistAsymmetric", bone.twistAsymmetric},
                             {"twistLowerLimitDegrees", bone.twistLowerLimitDegrees},
                             {"twistUpperLimitDegrees", bone.twistUpperLimitDegrees},
                             {"jointAxisLocal", ToJson(bone.jointAxisLocal)},
                             {"motorStrengthScale", bone.motorStrengthScale},
                             {"blendWeight", bone.blendWeight},
                             {"simulate", bone.simulate}});
    }
    return {{"bones", std::move(bonesJson)},
            {"gravity", ToJson(value.gravity)},
            {"initialVelocity", ToJson(value.initialVelocity)},
            {"mode", static_cast<std::uint8_t>(value.mode)},
            {"qualityPreset", static_cast<std::uint8_t>(value.qualityPreset)},
            {"physicsBlendWeight", value.physicsBlendWeight},
            {"enableSelfCollision", value.enableSelfCollision},
            {"enableDynamicPushback", value.enableDynamicPushback},
            {"driveEntityTransform", value.driveEntityTransform},
            {"inheritAnimatedVelocity", value.inheritAnimatedVelocity},
            {"maxSubstepsPerFrame", value.maxSubstepsPerFrame},
            {"motorStiffness", value.motorStiffness},
            {"motorDamping", value.motorDamping},
            {"maxMotorTorque", value.maxMotorTorque},
            {"rootDriveLinearStiffness", value.rootDriveLinearStiffness},
            {"rootDriveLinearDamping", value.rootDriveLinearDamping},
            {"rootDriveMaxLinearForce", value.rootDriveMaxLinearForce},
            {"rootDriveAngularStiffness", value.rootDriveAngularStiffness},
            {"rootDriveAngularDamping", value.rootDriveAngularDamping},
            {"rootDriveMaxAngularTorque", value.rootDriveMaxAngularTorque},
            {"partialRootBoneName", value.partialRootBoneName},
            {"layerMask", value.filter.layerMask},
            {"enabled", value.enabled}};
}

[[nodiscard]] Ragdoll3DComponent Ragdoll3DObjectFromJson(const nlohmann::json& json) {
    Ragdoll3DComponent value;
    const auto clampMotion = [](const std::uint8_t raw) -> Physics::RagdollMotion {
        return raw <= static_cast<std::uint8_t>(Physics::RagdollMotion::Locked)
                   ? static_cast<Physics::RagdollMotion>(raw)
                   : Physics::RagdollMotion::Limited;
    };
    const auto bonesIt = json.find("bones");
    if (bonesIt != json.end() && bonesIt->is_array()) {
        if (bonesIt->size() > kMaximumRagdollBones) {
            throw std::runtime_error("Ragdoll3DComponent bones exceed bounded cap");
        }
        for (const nlohmann::json& entry : *bonesIt) {
            RagdollBoneSetup bone;
            bone.boneName = entry.value("boneName", std::string{});
            bone.parentBoneName = entry.value("parentBoneName", std::string{});
            bone.mass = entry.value("mass", bone.mass);
            bone.radius = entry.value("radius", bone.radius);
            bone.halfHeight = entry.value("halfHeight", bone.halfHeight);
            bone.linearDamping = entry.value("linearDamping", bone.linearDamping);
            bone.angularDamping = entry.value("angularDamping", bone.angularDamping);
            bone.friction = entry.value("friction", bone.friction);
            bone.swing1Motion = clampMotion(entry.value("swing1Motion", static_cast<std::uint8_t>(bone.swing1Motion)));
            bone.swing2Motion = clampMotion(entry.value("swing2Motion", static_cast<std::uint8_t>(bone.swing2Motion)));
            bone.twistMotion = clampMotion(entry.value("twistMotion", static_cast<std::uint8_t>(bone.twistMotion)));
            bone.swing1LimitDegrees = entry.value("swing1LimitDegrees", bone.swing1LimitDegrees);
            bone.swing2LimitDegrees = entry.value("swing2LimitDegrees", bone.swing2LimitDegrees);
            bone.twistLimitDegrees = entry.value("twistLimitDegrees", bone.twistLimitDegrees);
            bone.twistAsymmetric = entry.value("twistAsymmetric", bone.twistAsymmetric);
            bone.twistLowerLimitDegrees = entry.value("twistLowerLimitDegrees", bone.twistLowerLimitDegrees);
            bone.twistUpperLimitDegrees = entry.value("twistUpperLimitDegrees", bone.twistUpperLimitDegrees);
            if (entry.contains("jointAxisLocal")) {
                bone.jointAxisLocal = Vector3FromJson(entry.at("jointAxisLocal"));
            }
            bone.motorStrengthScale = entry.value("motorStrengthScale", bone.motorStrengthScale);
            bone.blendWeight = entry.value("blendWeight", bone.blendWeight);
            bone.simulate = entry.value("simulate", bone.simulate);
            value.bones.push_back(std::move(bone));
        }
    }
    if (json.contains("gravity")) value.gravity = Vector3FromJson(json.at("gravity"));
    if (json.contains("initialVelocity")) value.initialVelocity = Vector3FromJson(json.at("initialVelocity"));
    const std::uint8_t rawMode = json.value("mode", static_cast<std::uint8_t>(value.mode));
    if (rawMode <= static_cast<std::uint8_t>(RagdollMode::BlendingOut)) {
        value.mode = static_cast<RagdollMode>(rawMode);
    }
    const std::uint8_t rawPreset = json.value("qualityPreset", static_cast<std::uint8_t>(value.qualityPreset));
    if (rawPreset <= static_cast<std::uint8_t>(RagdollQualityPreset::Custom)) {
        value.qualityPreset = static_cast<RagdollQualityPreset>(rawPreset);
    }
    value.physicsBlendWeight = json.value("physicsBlendWeight", value.physicsBlendWeight);
    value.enableSelfCollision = json.value("enableSelfCollision", value.enableSelfCollision);
    value.enableDynamicPushback = json.value("enableDynamicPushback", value.enableDynamicPushback);
    value.driveEntityTransform = json.value("driveEntityTransform", value.driveEntityTransform);
    value.inheritAnimatedVelocity = json.value("inheritAnimatedVelocity", value.inheritAnimatedVelocity);
    value.maxSubstepsPerFrame = json.value("maxSubstepsPerFrame", value.maxSubstepsPerFrame);
    value.motorStiffness = json.value("motorStiffness", value.motorStiffness);
    value.motorDamping = json.value("motorDamping", value.motorDamping);
    value.maxMotorTorque = json.value("maxMotorTorque", value.maxMotorTorque);
    value.rootDriveLinearStiffness = json.value("rootDriveLinearStiffness", value.rootDriveLinearStiffness);
    value.rootDriveLinearDamping = json.value("rootDriveLinearDamping", value.rootDriveLinearDamping);
    value.rootDriveMaxLinearForce = json.value("rootDriveMaxLinearForce", value.rootDriveMaxLinearForce);
    value.rootDriveAngularStiffness = json.value("rootDriveAngularStiffness", value.rootDriveAngularStiffness);
    value.rootDriveAngularDamping = json.value("rootDriveAngularDamping", value.rootDriveAngularDamping);
    value.rootDriveMaxAngularTorque = json.value("rootDriveMaxAngularTorque", value.rootDriveMaxAngularTorque);
    value.partialRootBoneName = json.value("partialRootBoneName", std::string{});
    value.filter.layerMask = json.value("layerMask", value.filter.layerMask);
    value.enabled = json.value("enabled", true);
    return value;
}

[[nodiscard]] Ragdoll3DComponent Ragdoll3DComponentWithResolvedReferences(
    const nlohmann::json& json, const std::unordered_map<std::uint32_t, Entity>& localIdToEntity) {
    Ragdoll3DComponent value = Ragdoll3DObjectFromJson(json);
    const std::uint32_t localId = json.value("skeletonLocalId", std::numeric_limits<std::uint32_t>::max());
    const auto found = localIdToEntity.find(localId);
    value.skeleton = found != localIdToEntity.end() ? found->second : kInvalidEntity;
    return value;
}



[[nodiscard]] nlohmann::json ToJson(const Marker3DComponent& value) {
    return {{"markerName", value.markerName},
            {"localPosition", ToJson(value.localPosition)},
            {"localRotation", ToJson(value.localRotation)},
            {"enabled", value.enabled}};
}

[[nodiscard]] Marker3DComponent Marker3DObjectFromJson(const nlohmann::json& json) {
    return Marker3DComponent{json.value("markerName", std::string{"Marker"}),
                                    Vector3FromJson(json.at("localPosition")),
                                    QuaternionFromJson(json.at("localRotation")),
                                    json.value("enabled", true)};
}

[[nodiscard]] nlohmann::json ToJson(const Hitbox3DComponent& value) {
    return {{"halfExtents", ToJson(value.halfExtents)},
            {"collisionLayer", value.collisionLayer},
            {"collisionMask", value.collisionMask},
            {"damageChannel", value.damageChannel},
            {"enabled", value.enabled}};
}

[[nodiscard]] Hitbox3DComponent Hitbox3DObjectFromJson(const nlohmann::json& json) {
    return Hitbox3DComponent{Vector3FromJson(json.at("halfExtents")),
                                    json.value("collisionLayer", std::uint32_t{1}),
                                    json.value("collisionMask", std::uint32_t{0xFFFFFFFFU}),
                                    json.value("damageChannel", std::string{"default"}),
                                    json.value("enabled", true)};
}

[[nodiscard]] Hitbox3DComponent Hitbox3DComponentWithResolvedIgnore(
    const nlohmann::json& json, const std::unordered_map<std::uint32_t, Entity>& localIdToEntity) {
    Hitbox3DComponent value = Hitbox3DObjectFromJson(json);
    const std::int64_t ignoreLocalId = json.value("ignoreLocalId", static_cast<std::int64_t>(-1));
    if (ignoreLocalId >= 0 &&
        static_cast<std::uint64_t>(ignoreLocalId) <= std::numeric_limits<std::uint32_t>::max()) {
        const auto ignoreIt = localIdToEntity.find(static_cast<std::uint32_t>(ignoreLocalId));
        if (ignoreIt != localIdToEntity.end()) {
            value.ignoreEntity = ignoreIt->second;
        }
    }
    if (!IsHitbox3DObjectComponentValid(value)) {
        throw std::runtime_error("Invalid Hitbox3DComponent payload");
    }
    return value;
}

[[nodiscard]] nlohmann::json ToJson(const Hurtbox3DComponent& value) {
    return {{"halfExtents", ToJson(value.halfExtents)},
            {"collisionLayer", value.collisionLayer},
            {"collisionMask", value.collisionMask},
            {"damageChannel", value.damageChannel},
            {"enabled", value.enabled}};
}

[[nodiscard]] Hurtbox3DComponent Hurtbox3DObjectFromJson(const nlohmann::json& json) {
    return Hurtbox3DComponent{Vector3FromJson(json.at("halfExtents")),
                                     json.value("collisionLayer", std::uint32_t{1}),
                                     json.value("collisionMask", std::uint32_t{0xFFFFFFFFU}),
                                     json.value("damageChannel", std::string{"default"}),
                                     json.value("enabled", true)};
}

[[nodiscard]] Hurtbox3DComponent Hurtbox3DComponentWithResolvedIgnore(
    const nlohmann::json& json, const std::unordered_map<std::uint32_t, Entity>& localIdToEntity) {
    Hurtbox3DComponent value = Hurtbox3DObjectFromJson(json);
    const std::int64_t ignoreLocalId = json.value("ignoreLocalId", static_cast<std::int64_t>(-1));
    if (ignoreLocalId >= 0 &&
        static_cast<std::uint64_t>(ignoreLocalId) <= std::numeric_limits<std::uint32_t>::max()) {
        const auto ignoreIt = localIdToEntity.find(static_cast<std::uint32_t>(ignoreLocalId));
        if (ignoreIt != localIdToEntity.end()) {
            value.ignoreEntity = ignoreIt->second;
        }
    }
    if (!IsHurtbox3DObjectComponentValid(value)) {
        throw std::runtime_error("Invalid Hurtbox3DComponent payload");
    }
    return value;
}

[[nodiscard]] nlohmann::json ToJson(const Projectile3DComponent& value) {
    // `remainingLifetime` and the runtime hit result are state, not authored data: the countdown
    // is re-armed from `maxLifetime` on load, and where this projectile last landed says nothing
    // about the one a freshly loaded scene creates.
    return {{"velocity", ToJson(value.velocity)},
            {"acceleration", ToJson(value.acceleration)},
            {"radius", value.radius},
            {"maxLifetime", value.maxLifetime},
            {"collisionMask", value.collisionMask},
            {"active", value.active},
            {"hitPolicy", static_cast<std::uint32_t>(value.hitPolicy)},
            {"restitution", value.restitution},
            {"friction", value.friction}};
}

[[nodiscard]] Projectile3DComponent Projectile3DObjectFromJson(const nlohmann::json& json) {
    Projectile3DComponent value;
    value.velocity = Vector3FromJson(json.at("velocity"));
    value.acceleration = Vector3FromJson(json.at("acceleration"));
    value.radius = json.value("radius", 0.1F);
    value.maxLifetime = json.value("maxLifetime", 10.0F);
    value.remainingLifetime = value.maxLifetime;
    value.collisionMask = json.value("collisionMask", std::uint32_t{0xFFFFFFFFU});
    value.active = json.value("active", true);
    // A file written before these fields existed loads with the authored defaults; an unknown
    // policy is left as it decoded and then refused by the component's validator, never guessed
    // at, so a hand-edited file cannot make a projectile behave like a policy that is not a policy.
    value.hitPolicy = static_cast<Projectile3DHitPolicy>(json.value("hitPolicy", std::uint32_t{0}));
    value.restitution = json.value("restitution", 0.5F);
    value.friction = json.value("friction", 0.2F);
    return value;
}

[[nodiscard]] Projectile3DComponent Projectile3DComponentWithResolvedIgnore(
    const nlohmann::json& json, const std::unordered_map<std::uint32_t, Entity>& localIdToEntity) {
    Projectile3DComponent value = Projectile3DObjectFromJson(json);
    const std::int64_t ignoreLocalId = json.value("ignoreLocalId", static_cast<std::int64_t>(-1));
    if (ignoreLocalId >= 0 &&
        static_cast<std::uint64_t>(ignoreLocalId) <= std::numeric_limits<std::uint32_t>::max()) {
        const auto ignoreIt = localIdToEntity.find(static_cast<std::uint32_t>(ignoreLocalId));
        if (ignoreIt != localIdToEntity.end()) {
            value.ignoreEntity = ignoreIt->second;
        }
    }
    if (!IsProjectile3DObjectComponentValid(value)) {
        throw std::runtime_error("Invalid Projectile3DComponent payload");
    }
    return value;
}

[[nodiscard]] nlohmann::json ToJson(const InteractionArea3DComponent& value) {
    return {{"halfExtents", ToJson(value.halfExtents)},
            {"collisionLayer", value.collisionLayer},
            {"collisionMask", value.collisionMask},
            {"interactionTag", value.interactionTag},
            {"maximumCandidates", value.maximumCandidates},
            {"enabled", value.enabled}};
}

[[nodiscard]] InteractionArea3DComponent InteractionArea3DObjectFromJson(const nlohmann::json& json) {
    return InteractionArea3DComponent{Vector3FromJson(json.at("halfExtents")),
                                             json.value("collisionLayer", std::uint32_t{1}),
                                             json.value("collisionMask", std::uint32_t{0xFFFFFFFFU}),
                                             json.value("interactionTag", std::string{"interactable"}),
                                             json.value("maximumCandidates", std::uint32_t{16}),
                                             json.value("enabled", true)};
}

[[nodiscard]] InteractionArea3DComponent InteractionArea3DComponentWithResolvedIgnore(
    const nlohmann::json& json, const std::unordered_map<std::uint32_t, Entity>& localIdToEntity) {
    InteractionArea3DComponent value = InteractionArea3DObjectFromJson(json);
    const std::int64_t ignoreLocalId = json.value("ignoreLocalId", static_cast<std::int64_t>(-1));
    if (ignoreLocalId >= 0 &&
        static_cast<std::uint64_t>(ignoreLocalId) <= std::numeric_limits<std::uint32_t>::max()) {
        const auto ignoreIt = localIdToEntity.find(static_cast<std::uint32_t>(ignoreLocalId));
        if (ignoreIt != localIdToEntity.end()) {
            value.ignoreEntity = ignoreIt->second;
        }
    }
    if (!IsInteractionArea3DObjectComponentValid(value)) {
        throw std::runtime_error("Invalid InteractionArea3DComponent payload");
    }
    return value;
}



[[nodiscard]] nlohmann::json ToJson(const Sky3DComponent& value) {
    return {{"source", static_cast<std::uint8_t>(value.source)},
            {"skyColor", ToJson(value.skyColor)},
            {"horizonColor", ToJson(value.horizonColor)},
            {"groundColor", ToJson(value.groundColor)},
            {"skyCurve", value.skyCurve},
            {"groundCurve", value.groundCurve},
            {"panoramaPath", value.panoramaPath},
            {"panoramaRotationDegrees", value.panoramaRotationDegrees},
            {"panoramaExposure", value.panoramaExposure},
            {"atmosphereRayleigh", value.atmosphereRayleigh},
            {"atmosphereMie", value.atmosphereMie},
            {"atmosphereOzone", value.atmosphereOzone},
            {"atmosphereMieAnisotropy", value.atmosphereMieAnisotropy},
            {"atmosphereMultiScatter", value.atmosphereMultiScatter},
            {"atmosphereIntensity", value.atmosphereIntensity},
            {"atmosphereSamples", value.atmosphereSamples},
            {"starDensity", value.starDensity},
            {"starBrightness", value.starBrightness},
            {"starSeed", value.starSeed},
            {"milkyWayStrength", value.milkyWayStrength},
            {"cloudsEnabled", value.cloudsEnabled},
            {"cloudCoverage", value.cloudCoverage},
            {"cloudDensity", value.cloudDensity},
            {"cloudAltitude", value.cloudAltitude},
            {"cloudWindSpeed", value.cloudWindSpeed},
            {"cloudWindDirectionDegrees", value.cloudWindDirectionDegrees},
            {"cloudSoftness", value.cloudSoftness},
            {"cloudSilverLining", value.cloudSilverLining},
            {"ambientFromSky", value.ambientFromSky},
            {"ambientStrength", value.ambientStrength},
            {"fogFromSky", value.fogFromSky},
            {"fogStrength", value.fogStrength},
            {"reflectionsFromSky", value.reflectionsFromSky},
            {"luminance", value.luminance},
            {"animate", value.animate},
            {"enabled", value.enabled}};
}

[[nodiscard]] Sky3DComponent Sky3DFromJson(const nlohmann::json& json) {
    Sky3DComponent value;
    value.source = static_cast<SkySource>(json.value("source", std::uint8_t{0}));
    value.skyColor = Vector3FromJson(json.at("skyColor"));
    value.horizonColor = Vector3FromJson(json.at("horizonColor"));
    value.groundColor = Vector3FromJson(json.at("groundColor"));
    value.skyCurve = json.value("skyCurve", 0.15F);
    value.groundCurve = json.value("groundCurve", 0.02F);
    value.panoramaPath = json.value("panoramaPath", std::string{});
    value.panoramaRotationDegrees = json.value("panoramaRotationDegrees", 0.0F);
    value.panoramaExposure = json.value("panoramaExposure", 0.0F);
    // Every atmosphere value falls back to its default, so a document written before the model
    // existed loads as the calibrated Earth rather than as a zeroed atmosphere - which would
    // render as a black sky with no hint that a field was missing.
    value.atmosphereRayleigh = json.value("atmosphereRayleigh", 1.0F);
    value.atmosphereMie = json.value("atmosphereMie", 1.0F);
    value.atmosphereOzone = json.value("atmosphereOzone", 1.0F);
    value.atmosphereMieAnisotropy = json.value("atmosphereMieAnisotropy", 0.76F);
    value.atmosphereMultiScatter = json.value("atmosphereMultiScatter", 1.0F);
    value.atmosphereIntensity = json.value("atmosphereIntensity", 1.0F);
    value.atmosphereSamples = json.value("atmosphereSamples", std::uint32_t{16U});
    value.starDensity = json.value("starDensity", 420.0F);
    value.starBrightness = json.value("starBrightness", 0.0F);
    value.starSeed = json.value("starSeed", 0.0F);
    value.milkyWayStrength = json.value("milkyWayStrength", 0.0F);
    value.cloudsEnabled = json.value("cloudsEnabled", false);
    value.cloudCoverage = json.value("cloudCoverage", 0.45F);
    value.cloudDensity = json.value("cloudDensity", 1.0F);
    value.cloudAltitude = json.value("cloudAltitude", 1500.0F);
    value.cloudWindSpeed = json.value("cloudWindSpeed", 8.0F);
    value.cloudWindDirectionDegrees = json.value("cloudWindDirectionDegrees", 45.0F);
    value.cloudSoftness = json.value("cloudSoftness", 0.35F);
    value.cloudSilverLining = json.value("cloudSilverLining", 1.0F);
    value.ambientFromSky = json.value("ambientFromSky", false);
    value.ambientStrength = json.value("ambientStrength", 1.0F);
    value.fogFromSky = json.value("fogFromSky", false);
    value.fogStrength = json.value("fogStrength", 1.0F);
    value.reflectionsFromSky = json.value("reflectionsFromSky", false);
    value.luminance = json.value("luminance", 1.0F);
    value.animate = json.value("animate", true);
    value.enabled = json.value("enabled", true);
    return value;
}

[[nodiscard]] nlohmann::json ToJson(const ReflectionProbe3DComponent& value) {
    return {{"size", ToJson(value.size)},
            {"visibilityLayers", value.visibilityLayers},
            {"updateMode", static_cast<std::uint8_t>(value.updateMode)},
            {"resolution", static_cast<std::uint8_t>(value.resolution)},
            {"enabled", value.enabled}};
}

[[nodiscard]] ReflectionProbe3DComponent ReflectionProbe3DObjectFromJson(const nlohmann::json& json) {
    ReflectionProbe3DComponent value;
    value.size = Vector3FromJson(json.at("size"));
    value.visibilityLayers = json.value("visibilityLayers", std::uint32_t{0xFFFFFFFFU});
    value.updateMode = static_cast<ReflectionProbeUpdateMode>(json.value("updateMode", std::uint8_t{0}));
    value.resolution = static_cast<ReflectionProbeResolution>(json.value(
        "resolution", static_cast<std::uint8_t>(ReflectionProbeResolution::Medium)));
    value.enabled = json.value("enabled", true);
    return value;
}











[[nodiscard]] nlohmann::json ToJson(const LodGroup3DComponent& value) {
    // Both level-indexed arrays are written as the authored prefix, len == levelCount: one array
    // of thresholds and one of the per-level mesh guids, index for index. An unset level writes the
    // invalid GUID rather than being skipped, so the two arrays can never drift out of alignment by
    // a scene file being hand-edited or written by an older build.
    nlohmann::json thresholds = nlohmann::json::array();
    nlohmann::json lodMeshes = nlohmann::json::array();
    for (std::size_t index = 0U; index < value.levelCount; ++index) {
        thresholds.push_back(value.distanceThresholds[index]);
        lodMeshes.push_back(value.lodMeshGuids[index].value);
    }
    return {{"distanceThresholds", std::move(thresholds)},
            {"lodMeshGuids", std::move(lodMeshes)},
            {"levelCount", value.levelCount},
            {"hysteresis", value.hysteresis},
            {"enabled", value.enabled}};
}

[[nodiscard]] LodGroup3DComponent LodGroup3DObjectFromJson(const nlohmann::json& json) {
    LodGroup3DComponent value;
    const nlohmann::json thresholds = json.value("distanceThresholds", nlohmann::json::array());
    if (!thresholds.is_array() || thresholds.empty() || thresholds.size() > kMaximumLodLevels) {
        throw std::runtime_error("LodGroup3DComponent thresholds must be a bounded non-empty array");
    }
    value.levelCount = static_cast<std::uint8_t>(thresholds.size());
    for (std::size_t index = 0U; index < thresholds.size(); ++index) {
        value.distanceThresholds[index] = thresholds.at(index).get<float>();
    }
    // A level mesh array is optional - a group that overrides no levels writes none - but it is
    // bounded when present, and shorter than levelCount simply leaves the remaining levels on the
    // MeshComponent fallback.
    const nlohmann::json lodMeshes = json.value("lodMeshGuids", nlohmann::json::array());
    if (!lodMeshes.is_array() || lodMeshes.size() > kMaximumLodLevels) {
        throw std::runtime_error("LodGroup3DComponent lodMeshGuids must be a bounded array");
    }
    for (std::size_t index = 0U; index < lodMeshes.size(); ++index) {
        value.lodMeshGuids[index] = Asset::AssetGuid{lodMeshes.at(index).get<std::uint64_t>()};
    }
    value.hysteresis = json.value("hysteresis", 0.0F);
    value.enabled = json.value("enabled", true);
    return value;
}











[[nodiscard]] nlohmann::json ToJson(const LevelStreamer3DComponent& value) {
    return {{"levelPath", value.levelPath},
            {"loadDistance", value.loadDistance},
            {"unloadDistance", value.unloadDistance},
            {"enabled", value.enabled}};
}

[[nodiscard]] LevelStreamer3DComponent LevelStreamer3DObjectFromJson(const nlohmann::json& json) {
    return LevelStreamer3DComponent{json.value("levelPath", std::string{}),
                                           json.value("loadDistance", 250.0F),
                                           json.value("unloadDistance", 300.0F),
                                           json.value("enabled", false), false, false};
}

[[nodiscard]] nlohmann::json ToJson(const WorldPartition3DComponent& value) {
    return {{"cellSize", value.cellSize},
            {"cellCounts", {value.cellCounts[0], value.cellCounts[1], value.cellCounts[2]}},
            {"maximumLoadedCells", value.maximumLoadedCells},
            {"enabled", value.enabled}};
}

[[nodiscard]] WorldPartition3DComponent WorldPartition3DObjectFromJson(const nlohmann::json& json) {
    WorldPartition3DComponent value;
    value.cellSize = json.value("cellSize", 128.0F);
    const nlohmann::json counts = json.value("cellCounts", nlohmann::json::array({16U, 1U, 16U}));
    if (!counts.is_array() || counts.size() != 3U) {
        throw std::runtime_error("WorldPartition3DComponent cellCounts must contain three values");
    }
    for (std::size_t index = 0U; index < value.cellCounts.size(); ++index) {
        value.cellCounts[index] = counts.at(index).get<std::uint32_t>();
    }
    value.maximumLoadedCells = json.value("maximumLoadedCells", std::uint32_t{64});
    value.enabled = json.value("enabled", true);
    return value;
}

[[nodiscard]] nlohmann::json ToJson(const PrefabPropertyOverride& override) {
    return {{"propertyPath", override.propertyPath}, {"serializedValue", override.serializedValue}};
}

[[nodiscard]] nlohmann::json ToJson(const PrefabInstanceComponent& component) {
    nlohmann::json overrides = nlohmann::json::array();
    for (const PrefabPropertyOverride& override : component.overrides) {
        overrides.push_back(ToJson(override));
    }
    return {{"sourcePrefabGuid", component.sourcePrefabGuid.value},
            {"sourceRevision", component.sourceRevision},
            {"instanceRevision", component.instanceRevision},
            {"overrides", std::move(overrides)}};
}

[[nodiscard]] PrefabInstanceComponent PrefabInstanceFromJson(const nlohmann::json& json) {
    const nlohmann::json overridesJson = json.value("overrides", nlohmann::json::array());
    if (!overridesJson.is_array()) {
        throw std::runtime_error("PrefabInstanceComponent overrides must be an array");
    }

    std::vector<PrefabPropertyOverride> overrides;
    overrides.reserve(overridesJson.size());
    for (const nlohmann::json& overrideJson : overridesJson) {
        if (!overrideJson.is_object()) {
            throw std::runtime_error("PrefabInstanceComponent override must be an object");
        }
        overrides.push_back(PrefabPropertyOverride{
            overrideJson.at("propertyPath").get<std::string>(),
            overrideJson.at("serializedValue").get<std::string>()});
    }

    const PrefabInstanceComponent instance{
        Asset::AssetGuid{json.at("sourcePrefabGuid").get<std::uint64_t>()}, std::move(overrides),
        json.value("sourceRevision", 1ULL), json.value("instanceRevision", 1ULL)};
    if (!IsPrefabInstanceComponentValid(instance)) {
        throw std::runtime_error("Invalid PrefabInstanceComponent payload");
    }
    return instance;
}

/// One entry in the component-serializer table: `isValid` checks the authored component before
/// `toJson` reads it; `fromJson` adds a fresh component (built from `json`) to `entity`.
struct ComponentRegistration {
    std::type_index typeIndex;
    std::function<nlohmann::json(IEntityManager&, Entity)> toJson;
    std::function<bool(IEntityManager&, Entity)> isValid;
    std::function<void(IEntityManager&, Entity, const nlohmann::json&)> fromJson;
};

// Restored hand-written registrations (Tier 1.6 reseed holdouts - see the ledger in
// GetRegistrationsByName): these readers reseed runtime state from authored values on
// load, which the property codec cannot express. They live here as one cluster so the
// next migration audit finds them in one place.

[[nodiscard]] nlohmann::json ToJson(const Decal3DComponent& value) {
    return {{"materialAssetPath", value.materialAssetPath},
            {"size", ToJson(value.size)},
            {"projection", static_cast<std::uint8_t>(value.projection)},
            {"lifetime", value.lifetime},
            {"enabled", value.enabled},
            {"modulate", ToJson(value.modulate)},
            {"emissionEnergy", value.emissionEnergy},
            {"albedoMix", value.albedoMix},
            {"normalFade", value.normalFade},
            {"upperFade", value.upperFade},
            {"lowerFade", value.lowerFade},
            {"distanceFadeEnabled", value.distanceFadeEnabled},
            {"distanceFadeBegin", value.distanceFadeBegin},
            {"distanceFadeLength", value.distanceFadeLength},
            {"cullMask", value.cullMask}};
}

[[nodiscard]] Decal3DComponent Decal3DObjectFromJson(const nlohmann::json& json) {
    // Every field after `enabled` arrived later; a decal saved before them reads with its defaults.
    const Decal3DComponent defaults{};
    Decal3DComponent value{};
    value.materialAssetPath = json.value("materialAssetPath", std::string{});
    value.size = Vector3FromJson(json.at("size"));
    value.projection = static_cast<DecalProjectionMode>(json.value("projection", std::uint8_t{0}));
    value.lifetime = json.value("lifetime", 0.0F);
    value.enabled = json.value("enabled", true);
    value.modulate = json.contains("modulate") ? Vector3FromJson(json.at("modulate")) : defaults.modulate;
    value.emissionEnergy = json.value("emissionEnergy", defaults.emissionEnergy);
    value.albedoMix = json.value("albedoMix", defaults.albedoMix);
    value.normalFade = json.value("normalFade", defaults.normalFade);
    value.upperFade = json.value("upperFade", defaults.upperFade);
    value.lowerFade = json.value("lowerFade", defaults.lowerFade);
    value.distanceFadeEnabled = json.value("distanceFadeEnabled", defaults.distanceFadeEnabled);
    value.distanceFadeBegin = json.value("distanceFadeBegin", defaults.distanceFadeBegin);
    value.distanceFadeLength = json.value("distanceFadeLength", defaults.distanceFadeLength);
    value.cullMask = json.value("cullMask", defaults.cullMask);
    // The countdown is re-armed from the authored lifetime rather than restored: how much of a
    // decal's life was left when the scene was saved is a fact about that session, and a restored
    // decal starts its life whole - the same rule a restored projectile's remaining flight follows.
    value.remainingLifetime = value.lifetime;
    value.expired = false;
    return value;
}

[[nodiscard]] nlohmann::json ToJson(const HealthComponent& value) {
    return {{"maxHealth", value.maxHealth}, {"invulnerable", value.invulnerable}};
}

[[nodiscard]] HealthComponent HealthFromJson(const nlohmann::json& json) {
    HealthComponent value{};
    value.maxHealth = json.value("maxHealth", value.maxHealth);
    value.invulnerable = json.value("invulnerable", value.invulnerable);
    value.health = value.maxHealth;
    return value;
}

[[nodiscard]] nlohmann::json ToJson(const SpringArm3DComponent& value) {
    return {{"armLength", value.armLength},
            {"margin", value.margin},
            {"smoothing", value.smoothing},
            {"collisionMask", value.collisionMask},
            {"enabled", value.enabled}};
}

/// Blend shapes serialize as a name/value list rather than parallel arrays: a scene file is read by
/// people as often as by the engine, and `{"target": "jawOpen", "weight": 0.4}` cannot get its two
/// halves out of step the way two arrays can. Sparse by construction - a shape at rest is simply
/// absent, which is also what keeps a 52-shape rig's scene payload small.
[[nodiscard]] nlohmann::json ToJson(const MorphTargetComponent& value) {
    nlohmann::json weightsJson = nlohmann::json::array();
    for (const MorphTargetWeight& entry : value.weights) {
        weightsJson.push_back({{"target", entry.target}, {"weight", entry.weight}});
    }
    return nlohmann::json{{"enabled", value.enabled}, {"weights", std::move(weightsJson)}};
}

[[nodiscard]] MorphTargetComponent MorphTargetObjectFromJson(const nlohmann::json& json) {
    MorphTargetComponent value;
    value.enabled = json.value("enabled", true);
    if (json.contains("weights")) {
        if (!json.at("weights").is_array() || json.at("weights").size() > kMaximumMorphTargetWeights) {
            throw std::runtime_error("MorphTargetComponent payload has an impossible weights array");
        }
        value.weights.reserve(json.at("weights").size());
        for (const auto& entry : json.at("weights")) {
            if (!entry.is_object() || !entry.contains("target") || !entry.at("target").is_string()) {
                throw std::runtime_error("MorphTargetComponent payload has a weight without a target");
            }
            MorphTargetWeight weight;
            weight.target = entry.at("target").get<std::string>();
            if (entry.contains("weight")) {
                if (!entry.at("weight").is_number()) {
                    throw std::runtime_error("MorphTargetComponent payload has a non-numeric weight");
                }
                weight.weight = entry.at("weight").get<float>();
            }
            value.weights.push_back(std::move(weight));
        }
    }
    if (!IsMorphTargetComponentValid(value)) {
        throw std::runtime_error("Invalid MorphTargetComponent payload");
    }
    return value;
}

[[nodiscard]] SpringArm3DComponent SpringArm3DObjectFromJson(const nlohmann::json& json) {
    SpringArm3DComponent value;
    value.armLength = json.value("armLength", 4.0F);
    value.margin = json.value("margin", 0.1F);
    value.smoothing = json.value("smoothing", 8.0F);
    value.collisionMask = json.value("collisionMask", std::uint32_t{0xFFFFFFFFU});
    value.currentLength = value.armLength;
    value.enabled = json.value("enabled", true);
    return value;
}

template <typename T, typename FromJsonFunc, typename ValidateFunc>
[[nodiscard]] ComponentRegistration MakeRegistration(FromJsonFunc fromJsonFunc, ValidateFunc validateFunc) {
    return ComponentRegistration{
        std::type_index(typeid(T)),
        [](IEntityManager& entityManager, Entity entity) -> nlohmann::json {
            return ToJson(entityManager.GetComponent<T>(entity));
        },
        [validateFunc](IEntityManager& entityManager, Entity entity) {
            return validateFunc(entityManager.GetComponent<T>(entity));
        },
        [fromJsonFunc](IEntityManager& entityManager, Entity entity, const nlohmann::json& json) {
            entityManager.AddComponent<T>(entity, fromJsonFunc(json));
        },
    };
}

/// Older names of components, mapped to the name they are registered under now. Reading a saved
/// document goes through this, so a component renamed in code still loads files written before the
/// rename; writing always uses the current name. Same rule as the scene-object registry's legacy
/// type ids: every rename leaves its previous name readable here forever.
[[nodiscard]] std::string_view CanonicalComponentName(const std::string_view name) noexcept {
    static const std::unordered_map<std::string_view, std::string_view> kLegacyNames{
        // The scene-object pass: the word "Node" left these names, nothing else changed.
        {"SceneNodeTypeComponent", "SceneObjectTypeComponent"},
        {"NodeMetadataComponent", "ObjectMetadataComponent"},
        {"AnimatableBody3DNodeComponent", "Kinematic3DComponent"},
        {"BoneAttachment3DNodeComponent", "BoneAttachment3DComponent"},
        {"Decal3DNodeComponent", "Decal3DComponent"},
        {"FogVolume3DNodeComponent", "FogVolume3DComponent"},
        {"Hitbox3DNodeComponent", "Hitbox3DComponent"},
        {"Hurtbox3DNodeComponent", "Hurtbox3DComponent"},
        {"InteractionArea3DNodeComponent", "InteractionArea3DComponent"},
        {"LevelStreamer3DNodeComponent", "LevelStreamer3DComponent"},
        {"LodGroup3DNodeComponent", "LodGroup3DComponent"},
        {"Marker3DNodeComponent", "Marker3DComponent"},
        {"NavigationAgent3DNodeComponent", "NavSeeker3DComponent"},
        {"NavigationRegion3DNodeComponent", "NavMeshVolume3DComponent"},
        {"Occluder3DNodeComponent", "Occluder3DComponent"},
        {"Projectile3DNodeComponent", "Projectile3DComponent"},
        {"RayCast3DNodeComponent", "RayCast3DComponent"},
        {"ReflectionProbe3DNodeComponent", "ReflectionProbe3DComponent"},
        {"Skeleton3DNodeComponent", "Skeleton3DComponent"},
        {"SpawnPoint3DNodeComponent", "SpawnPoint3DComponent"},
        {"SpringArm3DNodeComponent", "SpringArm3DComponent"},
        {"VisibilityRegion3DNodeComponent", "VisibilityRegion3DComponent"},
        {"WorldEnvironment3DNodeComponent", "WorldEnvironment3DComponent"},
        {"WorldPartition3DNodeComponent", "WorldPartition3DComponent"},
        // The component pass: four components kept the name their scene-object kind had already
        // dropped, so the Add-Object list said "AnimationGraph" while the row it created stored an
        // "AnimationTreeComponent". The kinds' names won. A document written before this rename
        // carries the left-hand name and still loads; writing always uses the right-hand one.
        {"AnimationTreeComponent", "AnimationGraphComponent"},
        {"AnimationPlayerComponent", "AnimationSequencerComponent"},
        {"MontagePlayerComponent", "MontageSequencerComponent"},
        {"RigidBodyComponent", "Rigid3DComponent"},
        {"AnimatableBody3DComponent", "Kinematic3DComponent"},
        // The AnimationMixer pass: it never mixed anything, it is the base AnimationSequencer and
        // AnimationGraph share - what they move, which channels, on which clock, how fast. The
        // engine's other base components are named for what they make an object (PhysicsObject3D,
        // RenderInstance3D, LightEmitter3D), so this one follows them instead of borrowing a name.
        {"AnimationMixerComponent", "AnimationDriverComponent"},
        // The navigation pass: those two were the last kinds named after another engine's own
        // navigation classes. A document written before this carries the left-hand name.
        {"NavigationRegion3DComponent", "NavMeshVolume3DComponent"},
        {"NavigationAgent3DComponent", "NavSeeker3DComponent"},
        // The viewport pass: the 2D arrival renamed the level's Viewport to Viewport3D, and its
        // marker component followed. A document written before this carries the left-hand name.
        {"OutlinerViewportComponent", "OutlinerViewport3DComponent"},
    };
    const auto it = kLegacyNames.find(name);
    return it != kLegacyNames.end() ? it->second : name;
}

// --- Metadata-driven (de)serialization ------------------------------------------------------
//
// Components with a generic registration need no hand-written ToJson/fromJson: the component
// metadata registry already describes every property (name, type id, accessors), and the entry's
// factory builds and attaches the instance. Tier 1.5 proved it on Canvas; Tier 1.6 discovers
// every qualifying entry automatically (see the loop at the end of GetRegistrationsByName),
// so adding a component is a metadata declaration and never a serializer edit. What stays
// hand-written - entity references, lists, legacy-shape readers - is documented per component at
// its registration.
//
// JSON shapes are the leaf helpers' existing shapes (scalars as numbers/strings, Vector2/3 and
// colours as [x, y(, z)] arrays, quaternions as [x, y, z, w], AssetGuid as its uint64 value,
// BitMask32 as its uint32 value), so a migrated component emits bytes identical to its
// hand-written predecessor. A Rect named exactly "rect" keeps the established UI pair
// (positionPixels/sizePixels), each half defaulting independently, exactly like the UI readers
// it replaces; a Rect with any other name - a component's second Rect onward - nests as
// {"position", "size"} under its own key, since the legacy pair has room for exactly one. The skip rule is exactly TypeMetadataProperty::IsSerialized() (no
// runtime state, no editor-only authoring, no unbound properties). A missing key on read keeps
// the factory default - the same leniency as the hand-written json.value(key, default) reads,
// and what lets old files load after a property is added. A property whose type id has no codec
// yet throws naming the property: silent data loss is worse than a loud migration error. (The
// qualification check below means that throw only fires when a hand-written reader is deleted
// for an entry the codec cannot express - a migration bug, caught by the round-trip tests.)

void MetadataPropertyWrite(const Core::TypeMetadataEntry& entry,
                              const Core::TypeMetadataProperty& property, const void* instance,
                              nlohmann::json& json) {
    if (property.typeId == kPropertyTypeBool) {
        bool value = false;
        property.getValue(instance, &value);
        json[property.name] = value;
    } else if (property.typeId == kPropertyTypeFloat) {
        float value = 0.0F;
        property.getValue(instance, &value);
        json[property.name] = value;
    } else if (property.typeId == kPropertyTypeInt32) {
        std::int32_t value = 0;
        property.getValue(instance, &value);
        json[property.name] = value;
    } else if (property.typeId == kPropertyTypeUInt32) {
        std::uint32_t value = 0U;
        property.getValue(instance, &value);
        json[property.name] = value;
    } else if (property.typeId == kPropertyTypeUInt8) {
        std::uint8_t value = 0U;
        property.getValue(instance, &value);
        json[property.name] = value;
    } else if (property.typeId == kPropertyTypeBitMask32) {
        std::uint32_t value = 0U;
        property.getValue(instance, &value);
        json[property.name] = value;
    } else if (property.typeId == kPropertyTypeString) {
        std::string value;
        property.getValue(instance, &value);
        json[property.name] = value;
    } else if (property.typeId == kPropertyTypeVector2) {
        Math::Vector2 value{};
        property.getValue(instance, &value);
        json[property.name] = ToJson(value);
    } else if (property.typeId == kPropertyTypeVector3) {
        Math::Vector3 value{};
        property.getValue(instance, &value);
        json[property.name] = ToJson(value);
    } else if (property.typeId == kPropertyTypeColor || property.typeId == kPropertyTypeLinearColor) {
        // One JSON shape ([x, y, z] linear values) for both colour ids: they differ only in the
        // inspector, and the byte-stability rule serializes linear values raw.
        Math::Color value{};
        property.getValue(instance, &value);
        json[property.name] = ToJson(value);
    } else if (property.typeId == kPropertyTypeQuaternion) {
        Math::Quaternion value{};
        property.getValue(instance, &value);
        json[property.name] = ToJson(value);
    } else if (property.typeId == kPropertyTypeEnum) {
        // Enum accessors speak std::int64_t (see MakeEnumProperty): the stored shape is a
        // plain number, the enumerator mapping lives in the bound accessors.
        std::int64_t value = 0;
        property.getValue(instance, &value);
        json[property.name] = value;
    } else if (property.typeId == kPropertyTypeStringList) {
        // Written exactly as the component holds it: a save/load pair that changed a value would
        // make every round-trip comparison a special case. The canonical form - no trailing
        // empties - is enforced where the list is edited and by the component's sanitizer, not
        // here, because a codec that rewrites its input is not a codec.
        std::vector<std::string> value;
        property.getValue(instance, &value);
        json[property.name] = value;
    } else if (property.typeId == kPropertyTypeAssetGuid) {
        Asset::AssetGuid value{};
        property.getValue(instance, &value);
        json[property.name] = value.value;
    } else if (property.typeId == kPropertyTypeRect) {
        Math::Rect value{};
        property.getValue(instance, &value);
        if (property.name == "rect") {
            // The legacy pair: a Rect writes two keys, not one, keeping the established UI
            // encoding byte-identical (see the Tier 1.6 decision: positionPixels/sizePixels).
            json["positionPixels"] = ToJson(value.position);
            json["sizePixels"] = ToJson(value.size);
        } else {
            // A named Rect - a component's second Rect onward - nests under its own key: the
            // legacy pair above has room for exactly one, and sharing it would silently corrupt
            // every Rect but the last.
            json[property.name] = {{"position", ToJson(value.position)}, {"size", ToJson(value.size)}};
        }
    } else {
        throw std::runtime_error("Cannot serialize property '" + property.name + "' of '" + entry.displayName +
                                 "': no metadata codec for its type");
    }
}

/// A property counts as present when the document carries its key - or, for a Rect, either half
/// of its legacy pair. Absent keys keep their factory defaults (see the block comment above).
[[nodiscard]] bool MetadataPropertyPresent(const Core::TypeMetadataProperty& property,
                                              const nlohmann::json& json) {
    if (property.typeId == kPropertyTypeRect && property.name == "rect") {
        return json.contains("positionPixels") || json.contains("sizePixels");
    }
    return json.contains(property.name);
}

void MetadataPropertyRead(const Core::TypeMetadataEntry& entry,
                             const Core::TypeMetadataProperty& property, const nlohmann::json& json,
                             void* instance) {
    if (property.typeId == kPropertyTypeBool) {
        const bool value = json.at(property.name).get<bool>();
        property.setValue(instance, &value);
    } else if (property.typeId == kPropertyTypeFloat) {
        const float value = json.at(property.name).get<float>();
        property.setValue(instance, &value);
    } else if (property.typeId == kPropertyTypeInt32) {
        const std::int32_t value = json.at(property.name).get<std::int32_t>();
        property.setValue(instance, &value);
    } else if (property.typeId == kPropertyTypeUInt32) {
        const std::uint32_t value = json.at(property.name).get<std::uint32_t>();
        property.setValue(instance, &value);
    } else if (property.typeId == kPropertyTypeUInt8) {
        const std::uint8_t value = json.at(property.name).get<std::uint8_t>();
        property.setValue(instance, &value);
    } else if (property.typeId == kPropertyTypeBitMask32) {
        const std::uint32_t value = json.at(property.name).get<std::uint32_t>();
        property.setValue(instance, &value);
    } else if (property.typeId == kPropertyTypeString) {
        const std::string value = json.at(property.name).get<std::string>();
        property.setValue(instance, &value);
    } else if (property.typeId == kPropertyTypeVector2) {
        const Math::Vector2 value = Vector2FromJson(json.at(property.name));
        property.setValue(instance, &value);
    } else if (property.typeId == kPropertyTypeVector3) {
        const Math::Vector3 value = Vector3FromJson(json.at(property.name));
        property.setValue(instance, &value);
    } else if (property.typeId == kPropertyTypeColor || property.typeId == kPropertyTypeLinearColor) {
        const Math::Color value = ColorFromJson(json.at(property.name));
        property.setValue(instance, &value);
    } else if (property.typeId == kPropertyTypeQuaternion) {
        const Math::Quaternion value = QuaternionFromJson(json.at(property.name));
        property.setValue(instance, &value);
    } else if (property.typeId == kPropertyTypeEnum) {
        const std::int64_t value = json.at(property.name).get<std::int64_t>();
        property.setValue(instance, &value);
    } else if (property.typeId == kPropertyTypeStringList) {
        std::vector<std::string> value;
        property.getValue(instance, &value);
        if (json.at(property.name).is_array()) {
            value.clear();
            for (const nlohmann::json& element : json.at(property.name)) {
                // A list of paths is read as text and nothing else: a non-string is skipped rather
                // than coerced, because a number that became a path would name no file.
                if (element.is_string()) {
                    value.push_back(element.get<std::string>());
                }
            }
        }
        property.setValue(instance, &value);
    } else if (property.typeId == kPropertyTypeAssetGuid) {
        const Asset::AssetGuid value{json.at(property.name).get<std::uint64_t>()};
        property.setValue(instance, &value);
    } else if (property.typeId == kPropertyTypeRect) {
        // Each half keeps its factory value when its key is absent, exactly like the hand-written
        // UI readers this replaces.
        Math::Rect value{};
        property.getValue(instance, &value);
        if (property.name == "rect") {
            if (json.contains("positionPixels")) {
                value.position = Vector2FromJson(json.at("positionPixels"));
            }
            if (json.contains("sizePixels")) {
                value.size = Vector2FromJson(json.at("sizePixels"));
            }
        } else if (json.contains(property.name)) {
            const nlohmann::json& nested = json.at(property.name);
            if (nested.contains("position")) {
                value.position = Vector2FromJson(nested.at("position"));
            }
            if (nested.contains("size")) {
                value.size = Vector2FromJson(nested.at("size"));
            }
        }
        property.setValue(instance, &value);
    } else {
        throw std::runtime_error("Cannot deserialize property '" + property.name + "' of '" +
                                 entry.displayName + "': no metadata codec for its type");
    }
}

/// True when the codec above can express `property` in both directions. Kept as an explicit list
/// (not a negated "not Entity, not a list") so a newly introduced property type id fails closed:
/// its components keep their hand-written registrations until the codec learns the id.
[[nodiscard]] bool MetadataPropertyHasCodec(const Core::TypeMetadataProperty& property) {
    const auto typeId = property.typeId;
    return typeId == kPropertyTypeBool || typeId == kPropertyTypeFloat ||
           typeId == kPropertyTypeInt32 || typeId == kPropertyTypeUInt32 ||
           typeId == kPropertyTypeUInt8 || typeId == kPropertyTypeBitMask32 ||
           typeId == kPropertyTypeString || typeId == kPropertyTypeVector2 ||
           typeId == kPropertyTypeVector3 || typeId == kPropertyTypeColor ||
           typeId == kPropertyTypeLinearColor || typeId == kPropertyTypeQuaternion ||
           typeId == kPropertyTypeEnum || typeId == kPropertyTypeAssetGuid ||
           typeId == kPropertyTypeRect || typeId == kPropertyTypeStringList;
}

/// An entry earns a generic registration when it is a component with a C++ name, a bound factory
/// plus the archetype-slot trio, at least one serialized property, and serialized properties the
/// codec can all express. Anything else - entity references, lists, a missing factory, a
/// describe-only entry, an entry with nothing to persist (its EditorDescription-style data stays
/// out of the file entirely) - keeps its hand-written registration, or stays unregistered
/// exactly as today.
[[nodiscard]] bool MetadataComponentQualifies(const Core::TypeMetadataEntry& entry) {
    if (entry.kind != Core::TypeMetadataKind::Component || entry.cppName.empty() ||
        !entry.HasFactory() || entry.typeIndex == std::type_index(typeid(void)) ||
        entry.instanceSize == 0 || entry.constructDefaultInPlace == nullptr ||
        entry.moveConstructInPlace == nullptr || entry.destroyInPlace == nullptr) {
        return false;
    }
    bool hasSerializedProperty = false;
    for (const Core::TypeMetadataProperty& property : entry.properties) {
        if (!property.IsSerialized()) {
            continue;
        }
        hasSerializedProperty = true;
        if (!MetadataPropertyHasCodec(property)) {
            return false;
        }
    }
    return hasSerializedProperty;
}

/// Builds a component registration from a metadata entry instead of hand-written JSON: `toJson`
/// walks the entry's serialized properties off the live component, `fromJson` factory-builds a
/// default, overwrites the keys the document carries, validates, and attaches through the
/// type-erased AddComponent + assignInstance pair. `entry` is a shared copy owned by the
/// registration table (the registry snapshot it came from is long gone); an empty `entry` or one
/// without a bound factory throws naming the component.
[[nodiscard]] ComponentRegistration
MakeMetadataRegistration(const std::string& componentName,
                            std::shared_ptr<const Core::TypeMetadataEntry> entry) {
    if (!entry || !entry->HasFactory() || entry->constructDefaultInPlace == nullptr ||
        entry->moveConstructInPlace == nullptr || entry->destroyInPlace == nullptr ||
        entry->typeIndex == std::type_index(typeid(void))) {
        throw std::runtime_error("Cannot build a metadata registration for '" + componentName +
                                 "': its metadata entry is missing or has no factory");
    }
    return ComponentRegistration{
        entry->typeIndex,
        [entry](IEntityManager& entityManager, Entity entity) -> nlohmann::json {
            const void* const instance = entityManager.GetComponentPointer(entity, entry->typeIndex);
            nlohmann::json json = nlohmann::json::object();
            for (const Core::TypeMetadataProperty& property : entry->properties) {
                if (!property.IsSerialized()) {
                    continue;
                }
                MetadataPropertyWrite(*entry, property, instance, json);
            }
            return json;
        },
        [entry](IEntityManager& entityManager, Entity entity) {
            if (entry->isInstanceValid == nullptr) {
                return true;
            }
            return entry->isInstanceValid(entityManager.GetComponentPointer(entity, entry->typeIndex));
        },
        [entry, componentName](IEntityManager& entityManager, Entity entity, const nlohmann::json& json) {
            Core::TypeInstance instance = Core::TypeInstance::MakeDefault(*entry);
            for (const Core::TypeMetadataProperty& property : entry->properties) {
                if (!property.IsSerialized() || !MetadataPropertyPresent(property, json)) {
                    continue;
                }
                MetadataPropertyRead(*entry, property, json, instance.GetMutable());
            }
            if (entry->isInstanceValid != nullptr && !entry->isInstanceValid(instance.Get())) {
                throw std::runtime_error("Invalid " + componentName + " payload");
            }
            const ComponentTypeInfo typeInfo{entry->instanceSize, entry->instanceAlignment,
                                                entry->constructDefaultInPlace, entry->moveConstructInPlace,
                                                entry->destroyInPlace};
            void* const slot = entityManager.AddComponent(entity, entry->typeIndex, typeInfo);
            entry->assignInstance(slot, instance.Get());
        },
    };
}

[[nodiscard]] const std::unordered_map<std::string, ComponentRegistration>& GetRegistrationsByName() {
    static const std::unordered_map<std::string, ComponentRegistration> registrations = [] {
        std::unordered_map<std::string, ComponentRegistration> table;

        // Hand-written registrations that stay (Tier 1.6 ledger). Everything else is discovered
        // from metadata by the loop at the end of this function; each entry below names what it
        // would take to migrate it. Delete the registration when you migrate one - the generic
        // path picks it up with no further edit here.
        // - Entity references (the registrations never see the file-local id tables, and the
        //   bespoke *LocalId keys have no generic spelling yet): AnimationDriver, BoneAttachment3D,
        //   Hitbox3D, Hurtbox3D, InteractionArea3D, Projectile3D, RayCast3D (plus its exclusions
        //   list), TwoBoneIK3D, FullBodyIK3D (per-effector target local ids), Cinematic (parallel
//   cut-camera and animation-target local ids;
//   unresolvable keys drop on load). Unlock: metadata-driven remap plumbed through Save/Load.
        // - Custom JSON shapes (nested objects, derived counts, dynamic lists): AnimationGraph,
        //   LodGroup3D (prefix-encoded level arrays with a derived levelCount), ObjectMetadata,
        //   Script, Skeleton3D (nested bones array), GameplayAttributes, GameplayTags,
        //   StatusEffects (dynamic gameplay lists), AiBrain, Blackboard (dynamic AI lists),
//   Perception, NoiseEmitter (flat AI structs beside them). Unlock: a list/struct codec
//   decision per shape.
        // - Legacy-compat readers (old keys migrate on load): Transform (euler degrees),
        //   AnimationSequencer (clip paths, playOnAwake-era keys). Unlock: a compat horizon.
        // - Reseed-on-load readers (runtime state is recomputed from authored values, never
        //   restored stale): Decal3D (remainingLifetime = lifetime, expired = false), Health
        //   (health = maxHealth), SpringArm3D (currentLength = armLength). The property codec
        //   only copies keys, so these stay hand-written. Unlock: a reseed-rule vocabulary.
        // - Metadata/JSON disagreements, each a product call, not a mechanical migration: UIButton
        //   (hover/clicked are RuntimeState but persist), EditorDescription (EditorOnly but persists),
        //   CharacterController (sim state persists but is undeclared), NavMeshVolume3D (a serialized
        //   rebuild flag JSON ignores, and a persisted path metadata lacks), ReflectionProbe3D
        //   (visibilityLayers undeclared), WorldPartition3D (cellCounts undeclared).
        // - No metadata entry at all: Folder, LevelStreamer3D, Marker3D, OutlinerViewport3D,
        //   PrefabInstance (deeply custom besides), SceneObjectType, SceneRoot. Unlock: declare them.

        table.emplace("TransformComponent", MakeRegistration<TransformComponent>([](const nlohmann::json& json) {
                          TransformComponent transform{Vector3FromJson(json.at("localPosition")),
                                                          QuaternionFromJson(json.at("localRotation")),
                                                          Vector3FromJson(json.at("localScale"))};
                          // Absent in every scene written before Euler authoring existed. Those
                          // documents get the angles derived from the rotation they DO have, which
                          // is the best available answer and matches what the Inspector used to
                          // show them; from then on the angles are authored and stop drifting.
                          // Absent in scenes written before the flag existed, and false is what
                          // they meant: every transform in them composed from its parent.
                          transform.topLevel = json.value("topLevel", false);
                          if (json.contains("localEulerRadians")) {
                              transform.localEulerRadians = Vector3FromJson(json.at("localEulerRadians"));
                              transform.eulerOrder = ReadEulerOrder(json);
                              transform.rotationEditMode = ReadRotationEditMode(json);
                          } else {
                              transform.eulerOrder = Math::EulerOrder::XYZ;
                              transform.rotationEditMode = RotationEditMode::Euler;
                              if (!Math::TryToEulerOrdered(transform.localRotation, Math::EulerOrder::XYZ,
                                                              transform.localEulerRadians)) {
                                  transform.localEulerRadians = Math::Vector3{};
                              }
                          }
                          if (!IsTransformComponentValid(transform)) {
                              throw std::runtime_error("Invalid TransformComponent payload");
                          }
                          return transform;
                      }, IsTransformComponentValid));
        table.emplace("AnimationSequencerComponent",
                      MakeRegistration<AnimationSequencerComponent>([](const nlohmann::json& json) {
                          AnimationSequencerComponent animation;
                          animation.clip = Asset::AssetGuid{json.value("clip", std::uint64_t{0})};
                          animation.libraryRef = Asset::AssetGuid{json.value("libraryRef", std::uint64_t{0})};
                          if (const auto library = json.find("library"); library != json.end() && library->is_array()) {
                              for (const nlohmann::json& guid : *library) {
                                  if (guid.is_number_unsigned() && guid.get<std::uint64_t>() != 0U) {
                                      animation.library.push_back(Asset::AssetGuid{guid.get<std::uint64_t>()});
                                  }
                              }
                          }
                          // Players saved before the object was rebuilt: speed, looping and play-on-awake
                          // carry over; a disabled one no longer autoplays. Their clip was a path no
                          // runtime ever played, and cannot be resolved to an asset here, so it is dropped.
                          if (json.contains("clipAssetPath") && !json.value("clipAssetPath", std::string{}).empty()) {
                              UVE_WARNING("SceneSerializer: AnimationSequencer clip path \"{}\" is no longer "
                                          "supported; pick the clip again in the Inspector",
                                          json.value("clipAssetPath", std::string{}));
                          }
                          const bool legacyEnabled = json.value("enabled", true);
                          animation.autoplay = json.value("autoplay", json.value("playOnAwake", true)) && legacyEnabled;
                          animation.speed = json.value("speed", json.value("playbackSpeed", 1.0F));
                          const bool legacyLooping = json.value("looping", true);
                          animation.loopMode = static_cast<AnimationLoopMode>(json.value(
                              "loopMode", static_cast<std::uint8_t>(legacyLooping ? AnimationLoopMode::Loop
                                                                                  : AnimationLoopMode::Once)));
                          animation.onFinish = static_cast<AnimationFinishAction>(
                              json.value("onFinish", std::uint8_t{0}));
                          animation.startOffsetSeconds = json.value("startOffsetSeconds", 0.0F);
                          animation.blendInSeconds = json.value("blendInSeconds", 0.0F);
                          animation.relative = json.value("relative", false);
                          if (!IsAnimationSequencerComponentValid(animation)) {
                              throw std::runtime_error("Invalid AnimationSequencerComponent payload");
                          }
                          return animation;
                      }, IsAnimationSequencerComponentValid));
        table.emplace("GameplayAttributesComponent",
                      MakeRegistration<GameplayAttributesComponent>([](const nlohmann::json& json) {
                          GameplayAttributesComponent attributes;
                          if (const auto list = json.find("attributes");
                              list != json.end() && list->is_array()) {
                              for (const nlohmann::json& entry : *list) {
                                  GameplayAttribute attribute;
                                  attribute.id = entry.value("id", std::string{});
                                  attribute.current = entry.value("current", 0.0F);
                                  attribute.maximum = entry.value("maximum", 0.0F);
                                  attribute.regenPerSecond = entry.value("regenPerSecond", 0.0F);
                                  attributes.attributes.push_back(std::move(attribute));
                              }
                          }
                          if (!IsGameplayAttributesComponentValid(attributes)) {
                              throw std::runtime_error("Invalid GameplayAttributesComponent payload");
                          }
                          return attributes;
                      }, IsGameplayAttributesComponentValid));
        table.emplace("MontageSequencerComponent",
                      MakeRegistration<MontageSequencerComponent>([](const nlohmann::json& json) {
                          MontageSequencerComponent slot;
                          Asset::AssetGuid* const fields[] = {&slot.montage1, &slot.montage2, &slot.montage3,
                                                                 &slot.montage4, &slot.montage5, &slot.montage6,
                                                                 &slot.montage7, &slot.montage8};
                          if (const auto list = json.find("montages"); list != json.end() && list->is_array()) {
                              for (std::size_t i = 0U; i < list->size() && i < 8U; ++i) {
                                  if ((*list)[i].is_number_unsigned()) {
                                      fields[i]->value = (*list)[i].get<std::uint64_t>();
                                  }
                              }
                          }
                          slot.libraryRef = Asset::AssetGuid{json.value("library", std::uint64_t{0})};
                          return slot;
                      }, IsMontageSequencerComponentValid));
        table.emplace("GameplayTagComponent",
                      MakeRegistration<GameplayTagComponent>([](const nlohmann::json& json) {
                          GameplayTagComponent tags;
                          if (const auto list = json.find("tags"); list != json.end() && list->is_array()) {
                              for (const nlohmann::json& entry : *list) {
                                  if (entry.is_string()) {
                                      tags.tags.push_back(entry.get<std::string>());
                                  }
                              }
                          }
                          if (!IsGameplayTagComponentValid(tags)) {
                              throw std::runtime_error("Invalid GameplayTagComponent payload");
                          }
                          return tags;
                      }, IsGameplayTagComponentValid));
        table.emplace("ControllerComponent",
                      MakeRegistration<ControllerComponent>(
                          [](const nlohmann::json& json) {
                              ControllerComponent value = ControllerObjectFromJson(json);
                              if (!IsControllerComponentValid(value)) {
                                  throw std::runtime_error("Invalid ControllerComponent payload");
                              }
                              return value;
                          },
                          IsControllerComponentValid));
        table.emplace("PawnComponent",
                      MakeRegistration<PawnComponent>(
                          [](const nlohmann::json& json) {
                              PawnComponent value = PawnObjectFromJson(json);
                              if (!IsPawnComponentValid(value)) {
                                  throw std::runtime_error("Invalid PawnComponent payload");
                              }
                              return value;
                          },
                          IsPawnComponentValid));
        table.emplace("CameraFollowComponent",
                      MakeRegistration<CameraFollowComponent>(
                          [](const nlohmann::json& json) {
                              CameraFollowComponent value = CameraFollowObjectFromJson(json);
                              if (!IsCameraFollowComponentValid(value)) {
                                  throw std::runtime_error("Invalid CameraFollowComponent payload");
                              }
                              return value;
                          },
                          IsCameraFollowComponentValid));
        table.emplace("TriggerVolumeComponent",
                      MakeRegistration<TriggerVolumeComponent>([](const nlohmann::json& json) {
                          TriggerVolumeComponent volume;
                          volume.policy =
                              static_cast<TriggerFirePolicy>(json.value("policy", 0U));
                          volume.intervalSeconds = json.value("intervalSeconds", 1.0F);
                          volume.cooldownSeconds = json.value("cooldownSeconds", 0.0F);
                          volume.armed = json.value("armed", true);
                          volume.firedCount = json.value("firedCount", 0U);
                          volume.cooldownRemaining = json.value("cooldownRemaining", 0.0F);
                          volume.intervalRemaining = json.value("intervalRemaining", 0.0F);
                          if (!IsTriggerVolumeComponentValid(volume)) {
                              throw std::runtime_error("Invalid TriggerVolumeComponent payload");
                          }
                          return volume;
                      }, IsTriggerVolumeComponentValid));
        table.emplace("StatusEffectsComponent",
                      MakeRegistration<StatusEffectsComponent>([](const nlohmann::json& json) {
                          StatusEffectsComponent effects;
                          if (const auto list = json.find("effects");
                              list != json.end() && list->is_array()) {
                              for (const nlohmann::json& entry : *list) {
                                  StatusEffect effect;
                                  effect.effectId = entry.value("effectId", std::string{});
                                  effect.attributeId = entry.value("attributeId", std::string{});
                                  effect.magnitudePerSecond = entry.value("magnitudePerSecond", 0.0F);
                                  effect.remainingSeconds = entry.value("remainingSeconds", 0.0F);
                                  effect.stacking = static_cast<StatusEffectStacking>(
                                      entry.value("stacking", 0U));
                                  effect.stacks = entry.value("stacks", 1U);
                                  effect.maxStacks = entry.value("maxStacks", 1U);
                                  effect.maxDelta = entry.value("maxDelta", 0.0F);
                                  effect.maxApplied = entry.value("maxApplied", false);
                                  effects.effects.push_back(std::move(effect));
                              }
                          }
                          if (!IsStatusEffectsComponentValid(effects)) {
                              throw std::runtime_error("Invalid StatusEffectsComponent payload");
                          }
                          return effects;
                      }, IsStatusEffectsComponentValid));
        table.emplace("CinematicComponent",
                      MakeRegistration<CinematicComponent>([](const nlohmann::json& json) {
                          // The load pass resolves references against the real id table before this
                          // registration ever sees the document; the empty table here degrades to
                          // authored data minus cameras and animation targets rather than throwing.
                          CinematicComponent cinematic =
                              CinematicComponentWithResolvedReferences(json, {});
                          if (!IsCinematicComponentValid(cinematic)) {
                              throw std::runtime_error("Invalid CinematicComponent payload");
                          }
                          return cinematic;
                      }, IsCinematicComponentValid));
        table.emplace("BlackboardComponent",
                      MakeRegistration<BlackboardComponent>([](const nlohmann::json& json) {
                          BlackboardComponent board;
                          if (const auto entries = json.find("entries");
                              entries != json.end() && entries->is_array()) {
                              for (const nlohmann::json& entry : *entries) {
                                  AiBlackboardEntry item;
                                  item.key = entry.value("key", std::string{});
                                  item.value = entry.value("value", 0.0F);
                                  board.entries.push_back(std::move(item));
                              }
                          }
                          if (const auto vectors = json.find("vectors");
                              vectors != json.end() && vectors->is_array()) {
                              for (const nlohmann::json& entry : *vectors) {
                                  AiBlackboardVectorEntry item;
                                  item.key = entry.value("key", std::string{});
                                  item.value = Vector3FromJson(entry.at("value"));
                                  board.vectors.push_back(std::move(item));
                              }
                          }
                          if (!IsBlackboardComponentValid(board)) {
                              throw std::runtime_error("Invalid BlackboardComponent payload");
                          }
                          return board;
                      }, IsBlackboardComponentValid));
        table.emplace("AiBrainComponent",
                      MakeRegistration<AiBrainComponent>([](const nlohmann::json& json) {
                          AiBrainComponent brain;
                          brain.hysteresis = json.value("hysteresis", 0.1F);
                          if (const auto actions = json.find("actions");
                              actions != json.end() && actions->is_array()) {
                              for (const nlohmann::json& entry : *actions) {
                                  AiAction action;
                                  action.actionId = entry.value("actionId", std::string{});
                                  action.baseScore = entry.value("baseScore", 1.0F);
                                  if (const auto considerations = entry.find("considerations");
                                      considerations != entry.end() && considerations->is_array()) {
                                      for (const nlohmann::json& item : *considerations) {
                                          AiConsideration consideration;
                                          consideration.inputId =
                                              item.value("inputId", std::string{});
                                          consideration.curve = static_cast<AiResponseCurve>(
                                              item.value("curve", static_cast<std::uint8_t>(
                                                                     AiResponseCurve::Linear)));
                                          consideration.weight = item.value("weight", 1.0F);
                                          action.considerations.push_back(std::move(consideration));
                                      }
                                  }
                                  brain.actions.push_back(std::move(action));
                              }
                          }
                          if (!IsAiBrainComponentValid(brain)) {
                              throw std::runtime_error("Invalid AiBrainComponent payload");
                          }
                          return brain;
                      }, IsAiBrainComponentValid));
        table.emplace("PerceptionComponent",
                      MakeRegistration<PerceptionComponent>([](const nlohmann::json& json) {
                          PerceptionComponent sensor;
                          sensor.watchedTag = json.value("watchedTag", std::string{});
                          sensor.sightRangeMetres = json.value("sightRangeMetres", 20.0F);
                          sensor.sightFieldOfViewDegrees =
                              json.value("sightFieldOfViewDegrees", 90.0F);
                          sensor.hearingRadiusMetres = json.value("hearingRadiusMetres", 15.0F);
                          sensor.requiresLineOfSight = json.value("requiresLineOfSight", true);
                          if (!IsPerceptionComponentValid(sensor)) {
                              throw std::runtime_error("Invalid PerceptionComponent payload");
                          }
                          return sensor;
                      }, IsPerceptionComponentValid));
        table.emplace("NoiseEmitterComponent",
                      MakeRegistration<NoiseEmitterComponent>([](const nlohmann::json& json) {
                          NoiseEmitterComponent emitter;
                          emitter.loudness = json.value("loudness", 1.0F);
                          emitter.decayPerSecond = json.value("decayPerSecond", 0.0F);
                          if (!IsNoiseEmitterComponentValid(emitter)) {
                              throw std::runtime_error("Invalid NoiseEmitterComponent payload");
                          }
                          return emitter;
                      }, IsNoiseEmitterComponentValid));
        table.emplace("AnimationDriverComponent",
                      MakeRegistration<AnimationDriverComponent>([](const nlohmann::json& json) {
                          const AnimationDriverComponent driver = AnimationDriverFromJson(json);
                          if (!IsAnimationDriverComponentValid(driver)) {
                              throw std::runtime_error("Invalid AnimationDriverComponent payload");
                          }
                          return driver;
                      }, IsAnimationDriverComponentValid));
        table.emplace("AnimationGraphComponent",
                      MakeRegistration<AnimationGraphComponent>([](const nlohmann::json& json) {
                          AnimationGraphComponent tree = AnimationGraphFromJson(json);
                          const std::string problem = DescribeAnimationGraphProblem(tree);
                          if (!problem.empty()) {
                              throw std::runtime_error("Invalid AnimationGraphComponent payload: " + problem);
                          }
                          return tree;
                      }, IsAnimationGraphComponentValid));
        table.emplace("RayCast3DComponent", MakeRegistration<RayCast3DComponent>(
            [](const nlohmann::json& json) {
                const RayCast3DComponent value = RayCast3DObjectFromJson(json);
                if (!IsRayCast3DObjectComponentValid(value)) {
                    throw std::runtime_error("Invalid RayCast3DComponent payload");
                }
                return value;
            }, IsRayCast3DObjectComponentValid));
        table.emplace("NavMeshVolume3DComponent", MakeRegistration<NavMeshVolume3DComponent>(
            [](const nlohmann::json& json) {
                const NavMeshVolume3DComponent value = NavMeshVolume3DObjectFromJson(json);
                if (!IsNavMeshVolume3DObjectComponentValid(value)) {
                    throw std::runtime_error("Invalid NavMeshVolume3DComponent payload");
                }
                return value;
            }, IsNavMeshVolume3DObjectComponentValid));
        table.emplace("Skeleton3DComponent", MakeRegistration<Skeleton3DComponent>(
            [](const nlohmann::json& json) {
                const Skeleton3DComponent value = Skeleton3DObjectFromJson(json);
                if (!IsSkeleton3DObjectComponentValid(value)) {
                    throw std::runtime_error("Invalid Skeleton3DComponent payload");
                }
                return value;
            }, IsSkeleton3DObjectComponentValid));
        table.emplace("BoneAttachment3DComponent", MakeRegistration<BoneAttachment3DComponent>(
            [](const nlohmann::json& json) {
                const BoneAttachment3DComponent value = BoneAttachment3DObjectFromJson(json);
                if (!IsBoneAttachment3DObjectComponentValid(value)) {
                    throw std::runtime_error("Invalid BoneAttachment3DComponent payload");
                }
                return value;
            }, IsBoneAttachment3DObjectComponentValid));
        table.emplace("TwoBoneIK3DComponent", MakeRegistration<TwoBoneIK3DComponent>(
            [](const nlohmann::json& json) {
                const TwoBoneIK3DComponent value = TwoBoneIK3DObjectFromJson(json);
                if (!IsTwoBoneIK3DObjectComponentValid(value)) {
                    throw std::runtime_error("Invalid TwoBoneIK3DComponent payload");
                }
                return value;
            }, IsTwoBoneIK3DObjectComponentValid));
        table.emplace("FullBodyIK3DComponent", MakeRegistration<FullBodyIK3DComponent>(
            [](const nlohmann::json& json) {
                const FullBodyIK3DComponent value = FullBodyIK3DObjectFromJson(json);
                if (!IsFullBodyIK3DObjectComponentValid(value)) {
                    throw std::runtime_error("Invalid FullBodyIK3DComponent payload");
                }
                return value;
            }, IsFullBodyIK3DObjectComponentValid));
        table.emplace("Ragdoll3DComponent", MakeRegistration<Ragdoll3DComponent>(
            [](const nlohmann::json& json) {
                const Ragdoll3DComponent value = Ragdoll3DObjectFromJson(json);
                if (!IsRagdoll3DComponentAuthoredValid(value)) {
                    throw std::runtime_error("Invalid Ragdoll3DComponent payload");
                }
                return value;
            }, IsRagdoll3DComponentAuthoredValid));
        table.emplace("SpringArm3DComponent", MakeRegistration<SpringArm3DComponent>(
            [](const nlohmann::json& json) {
                const SpringArm3DComponent value = SpringArm3DObjectFromJson(json);
                if (!IsSpringArm3DObjectComponentValid(value)) {
                    throw std::runtime_error("Invalid SpringArm3DComponent payload");
                }
                return value;
            }, IsSpringArm3DObjectComponentValid));
        table.emplace("Marker3DComponent", MakeRegistration<Marker3DComponent>(
            [](const nlohmann::json& json) {
                const Marker3DComponent value = Marker3DObjectFromJson(json);
                if (!IsMarker3DObjectComponentValid(value)) {
                    throw std::runtime_error("Invalid Marker3DComponent payload");
                }
                return value;
            }, IsMarker3DObjectComponentValid));
        // An id this build does not know (a scene from a newer one) adds nothing rather than failing
        // the load: the object is then read from its components, like a scene saved before types were.
        table.emplace("SceneObjectTypeComponent",
                      ComponentRegistration{
                          std::type_index(typeid(SceneObjectTypeComponent)),
                          [](IEntityManager& entityManager, Entity entity) -> nlohmann::json {
                              return ToJson(entityManager.GetComponent<SceneObjectTypeComponent>(entity));
                          },
                          [](IEntityManager& entityManager, Entity entity) {
                              return IsSceneObjectTypeComponentValid(
                                  entityManager.GetComponent<SceneObjectTypeComponent>(entity));
                          },
                          [](IEntityManager& entityManager, Entity entity, const nlohmann::json& json) {
                              const auto type = json.find("type");
                              const Objects::SceneObjectDescriptor* const descriptor =
                                  type != json.end() && type->is_string()
                                      ? Objects::FindSceneObjectDescriptor(type->get<std::string>())
                                      : nullptr;
                              if (descriptor != nullptr) {
                                  entityManager.AddComponent<SceneObjectTypeComponent>(
                                      entity, SceneObjectTypeComponent{descriptor->kind});
                              }
                          },
                      });
        table.emplace("FolderComponent", MakeRegistration<FolderComponent>([](const nlohmann::json&) {
                          return FolderComponent{};
                      }, IsFolderComponentValid));
        table.emplace("OutlinerViewport3DComponent", MakeRegistration<OutlinerViewport3DComponent>([](const nlohmann::json&) {
                          // Pure 3D marker; any legacy "dimension" field is ignored gracefully so
                          // older scene files continue to load without error.
                          return OutlinerViewport3DComponent{};
                      }, IsOutlinerViewport3DComponentValid));
        table.emplace("SceneRootComponent",
                    MakeRegistration<SceneRootComponent>([](const nlohmann::json&) {
                        return SceneRootComponent{};
                    }, IsSceneRootComponentValid));
        table.emplace("SkinComponent",
                      ComponentRegistration{
                          std::type_index(typeid(SkinComponent)),
                          [](IEntityManager& entityManager, Entity entity) -> nlohmann::json {
                              const auto& skin = entityManager.GetComponent<SkinComponent>(entity);
                              nlohmann::json bindsJson = nlohmann::json::array();
                              for (const auto& bind : skin.boneBinds) {
                                  nlohmann::json matJson = nlohmann::json::array();
                                  for (std::size_t r = 0U; r < 4U; ++r) {
                                      for (std::size_t c = 0U; c < 4U; ++c) {
                                          matJson.push_back(bind.inverseBindPose.m[r][c]);
                                      }
                                  }
                                  bindsJson.push_back({
                                      {"boneName", bind.boneName},
                                      {"boneIndex", bind.boneIndex},
                                      {"inverseBindPose", matJson}
                                  });
                              }
                              return nlohmann::json{
                                  {"bindCount", skin.bindCount},
                                  {"boneBinds", bindsJson}
                              };
                          },
                          [](IEntityManager& entityManager, Entity entity) {
                              return IsSkinComponentValid(entityManager.GetComponent<SkinComponent>(entity));
                          },
                          [](IEntityManager& entityManager, Entity entity, const nlohmann::json& json) {
                              SkinComponent skin{};
                              skin.bindCount = json.value("bindCount", 0U);
                              if (json.contains("boneBinds") && json["boneBinds"].is_array()) {
                                  for (const auto& bindJson : json["boneBinds"]) {
                                      SkinBoneBind bind{};
                                      bind.boneName = bindJson.value("boneName", "");
                                      bind.boneIndex = bindJson.value("boneIndex", -1);
                                      if (bindJson.contains("inverseBindPose") && bindJson["inverseBindPose"].is_array() &&
                                          bindJson["inverseBindPose"].size() == 16U) {
                                          for (std::size_t r = 0U; r < 4U; ++r) {
                                              for (std::size_t c = 0U; c < 4U; ++c) {
                                                  bind.inverseBindPose.m[r][c] =
                                                      bindJson["inverseBindPose"][r * 4U + c].get<float>();
                                              }
                                          }
                                      }
                                      skin.boneBinds.push_back(std::move(bind));
                                  }
                              }
                              if (skin.boneBinds.empty() && skin.bindCount > 0U) {
                                  skin.boneBinds.resize(skin.bindCount);
                                  for (std::size_t i = 0; i < skin.boneBinds.size(); ++i) {
                                      skin.boneBinds[i].boneIndex = static_cast<std::int32_t>(i);
                                  }
                              } else {
                                  skin.bindCount = static_cast<std::uint32_t>(skin.boneBinds.size());
                              }
                              entityManager.AddComponent<SkinComponent>(entity, std::move(skin));
                          },
                      });
        table.emplace("MorphTargetComponent",
                      MakeRegistration<MorphTargetComponent>(MorphTargetObjectFromJson,
                                                                   IsMorphTargetComponentValid));
        table.emplace("Hitbox3DComponent", MakeRegistration<Hitbox3DComponent>(
            [](const nlohmann::json& json) {
                const Hitbox3DComponent value = Hitbox3DObjectFromJson(json);
                if (!IsHitbox3DObjectComponentValid(value)) {
                    throw std::runtime_error("Invalid Hitbox3DComponent payload");
                }
                return value;
            }, IsHitbox3DObjectComponentValid));
        table.emplace("Hurtbox3DComponent", MakeRegistration<Hurtbox3DComponent>(
            [](const nlohmann::json& json) {
                const Hurtbox3DComponent value = Hurtbox3DObjectFromJson(json);
                if (!IsHurtbox3DObjectComponentValid(value)) {
                    throw std::runtime_error("Invalid Hurtbox3DComponent payload");
                }
                return value;
            }, IsHurtbox3DObjectComponentValid));
        table.emplace("Projectile3DComponent", MakeRegistration<Projectile3DComponent>(
            [](const nlohmann::json& json) {
                const Projectile3DComponent value = Projectile3DObjectFromJson(json);
                if (!IsProjectile3DObjectComponentValid(value)) {
                    throw std::runtime_error("Invalid Projectile3DComponent payload");
                }
                return value;
            }, IsProjectile3DObjectComponentValid));
        table.emplace("InteractionArea3DComponent", MakeRegistration<InteractionArea3DComponent>(
            [](const nlohmann::json& json) {
                const InteractionArea3DComponent value = InteractionArea3DObjectFromJson(json);
                if (!IsInteractionArea3DObjectComponentValid(value)) {
                    throw std::runtime_error("Invalid InteractionArea3DComponent payload");
                }
                return value;
            }, IsInteractionArea3DObjectComponentValid));
        table.emplace("Sky3DComponent", MakeRegistration<Sky3DComponent>(
            [](const nlohmann::json& json) {
                const Sky3DComponent value = Sky3DFromJson(json);
                if (!IsSky3DObjectComponentValid(value)) {
                    throw std::runtime_error("Invalid Sky3DComponent payload");
                }
                return value;
            }, IsSky3DObjectComponentValid));
        table.emplace("ReflectionProbe3DComponent", MakeRegistration<ReflectionProbe3DComponent>(
            [](const nlohmann::json& json) {
                const ReflectionProbe3DComponent value = ReflectionProbe3DObjectFromJson(json);
                if (!IsReflectionProbe3DObjectComponentValid(value)) {
                    throw std::runtime_error("Invalid ReflectionProbe3DComponent payload");
                }
                return value;
            }, IsReflectionProbe3DObjectComponentValid));
        table.emplace("Decal3DComponent", MakeRegistration<Decal3DComponent>(
            [](const nlohmann::json& json) {
                const Decal3DComponent value = Decal3DObjectFromJson(json);
                if (!IsDecal3DObjectComponentValid(value)) {
                    throw std::runtime_error("Invalid Decal3DComponent payload");
                }
                return value;
            }, IsDecal3DObjectComponentValid));
        table.emplace("LodGroup3DComponent", MakeRegistration<LodGroup3DComponent>(
            [](const nlohmann::json& json) {
                const LodGroup3DComponent value = LodGroup3DObjectFromJson(json);
                if (!IsLodGroup3DObjectComponentValid(value)) {
                    throw std::runtime_error("Invalid LodGroup3DComponent payload");
                }
                return value;
            }, IsLodGroup3DObjectComponentValid));
        table.emplace("HealthComponent", MakeRegistration<HealthComponent>(
            [](const nlohmann::json& json) {
                const HealthComponent value = HealthFromJson(json);
                if (!IsHealthComponentValid(value)) {
                    throw std::runtime_error("Invalid HealthComponent payload");
                }
                return value;
            }, IsHealthComponentValid));
        table.emplace("LevelStreamer3DComponent", MakeRegistration<LevelStreamer3DComponent>(
            [](const nlohmann::json& json) {
                const LevelStreamer3DComponent value = LevelStreamer3DObjectFromJson(json);
                if (!IsLevelStreamer3DObjectComponentValid(value)) {
                    throw std::runtime_error("Invalid LevelStreamer3DComponent payload");
                }
                return value;
            }, IsLevelStreamer3DObjectComponentValid));
        table.emplace("WorldPartition3DComponent", MakeRegistration<WorldPartition3DComponent>(
            [](const nlohmann::json& json) {
                const WorldPartition3DComponent value = WorldPartition3DObjectFromJson(json);
                if (!IsWorldPartition3DObjectComponentValid(value)) {
                    throw std::runtime_error("Invalid WorldPartition3DComponent payload");
                }
                return value;
            }, IsWorldPartition3DObjectComponentValid));
        table.emplace("CharacterControllerComponent",
                      MakeRegistration<CharacterControllerComponent>([](const nlohmann::json& json) {
                          // Every field falls back to its default, so a file from before a field
                          // existed loads as if it had been left at that default.
                          CharacterControllerComponent c;
                          c.motionMode = static_cast<CharacterMotionMode>(json.value("motionMode", std::uint8_t{0}));
                          c.upDirection = json.contains("upDirection")
                                              ? Vector3FromJson(json.at("upDirection"))
                                              : Math::Vector3{0.0F, 1.0F, 0.0F};
                          c.gravityScale = json.value("gravityScale", c.gravityScale);
                          c.builtInMovement = json.value("builtInMovement", c.builtInMovement);
                          c.moveSpeed = json.value("moveSpeed", c.moveSpeed);
                          c.jumpHeight = json.value("jumpHeight", c.jumpHeight);
                          c.airControl = json.value("airControl", c.airControl);
                          c.coyoteTimeSeconds = json.value("coyoteTimeSeconds", c.coyoteTimeSeconds);
                          c.jumpBufferSeconds = json.value("jumpBufferSeconds", c.jumpBufferSeconds);
                          c.floorMaxAngleDegrees =
                              json.value("floorMaxAngleDegrees", c.floorMaxAngleDegrees);
                          c.wallMinSlideAngleDegrees =
                              json.value("wallMinSlideAngleDegrees", c.wallMinSlideAngleDegrees);
                          c.safeMargin = json.value("safeMargin", c.safeMargin);
                          c.floorStopOnSlope = json.value("floorStopOnSlope", c.floorStopOnSlope);
                          c.floorConstantSpeed = json.value("floorConstantSpeed", c.floorConstantSpeed);
                          c.floorSnapLength = json.value("floorSnapLength", c.floorSnapLength);
                          c.maxStepHeight = json.value("maxStepHeight", c.maxStepHeight);
                          c.minStepWidth = json.value("minStepWidth", c.minStepWidth);
                          c.slideOnCeiling = json.value("slideOnCeiling", c.slideOnCeiling);
                          c.floorBlockOnWall = json.value("floorBlockOnWall", c.floorBlockOnWall);
                          c.platformOnLeave = static_cast<CharacterPlatformLeaveMode>(
                              json.value("platformOnLeave", static_cast<std::uint8_t>(c.platformOnLeave)));
                          c.maximumPlatformSpeed =
                              json.value("maximumPlatformSpeed", c.maximumPlatformSpeed);
                          c.pushRigidBodies = json.value("pushRigidBodies", c.pushRigidBodies);
                          c.pushStrength = json.value("pushStrength", c.pushStrength);
                          c.maxPushSpeed = json.value("maxPushSpeed", c.maxPushSpeed);
                          c.maxSlides = json.value("maxSlides", c.maxSlides);
                          c.maximumContacts = json.value("maximumContacts", c.maximumContacts);
                          // Before velocity was a vector, only its vertical part was kept.
                          c.velocity = json.contains("velocity")
                                           ? Vector3FromJson(json.at("velocity"))
                                           : Math::Vector3{0.0F, json.value("verticalVelocity", 0.0F), 0.0F};
                          // Three names, one field, in the order they were used: documents have been
                          // written with each. "grounded" is current, "isOnFloor" is what the field
                          // was called when it carried another engine's name, and "isGrounded" is
                          // older still - the serializer already accepted it before this pass.
                          c.grounded = json.value("grounded",
                                                  json.value("isOnFloor", json.value("isGrounded", false)));
                          c.isOnCeiling = json.value("isOnCeiling", false);
                          if (json.contains("floorNormal")) {
                              c.floorNormal = Vector3FromJson(json.at("floorNormal"));
                          }
                          c.timeSinceOnFloor = json.value("timeSinceOnFloor", 0.0F);
                          c.jumpBufferRemaining = json.value("jumpBufferRemaining", 0.0F);
                          CharacterControllerComponent& characterController = c;
                          if (!IsCharacterControllerComponentValid(characterController)) {
                              throw std::runtime_error("Invalid CharacterControllerComponent payload");
                          }
                          return characterController;
                      }, IsCharacterControllerComponentValid));
        table.emplace("UIButtonComponent",
                      MakeRegistration<UIButtonComponent>([](const nlohmann::json& json) {
                          UIButtonComponent button;
                          button.rect.position = json.contains("positionPixels")
                              ? Vector2FromJson(json.at("positionPixels")) : Math::Vector2{};
                          button.rect.size = json.contains("sizePixels") ? Vector2FromJson(json.at("sizePixels"))
                                                                       : Math::Vector2{120.0F, 32.0F};
                          button.normalColor = json.contains("normalColor")
                              ? Vector3FromJson(json.at("normalColor")) : Math::Vector3{0.25F, 0.25F, 0.28F};
                          button.hoverColor = json.contains("hoverColor")
                              ? Vector3FromJson(json.at("hoverColor")) : Math::Vector3{0.35F, 0.35F, 0.40F};
                          button.pressedColor = json.contains("pressedColor")
                              ? Vector3FromJson(json.at("pressedColor")) : Math::Vector3{0.18F, 0.18F, 0.20F};
                          button.isHovered = json.value("isHovered", false);
                          button.wasClickedThisFrame = json.value("wasClickedThisFrame", false);
                          if (!IsUIButtonComponentValid(button)) {
                              throw std::runtime_error("Invalid UIButtonComponent payload");
                          }
                          return button;
                      }, IsUIButtonComponentValid));
        table.emplace("ScriptComponent", MakeRegistration<ScriptComponent>([](const nlohmann::json& json) {
                          // exportValues came later; a scene saved before it has none.
                          const ScriptComponent script{
                              json.at("scriptAssetPath").get<std::string>(),
                              json.value("exportValues", std::map<std::string, std::string>{})};
                          if (!IsScriptComponentValid(script)) {
                              throw std::runtime_error("Invalid ScriptComponent payload");
                          }
                          return script;
                      }, IsScriptComponentValid));
        table.emplace("ObjectMetadataComponent",
                      MakeRegistration<ObjectMetadataComponent>([](const nlohmann::json& json) {
                          ObjectMetadataComponent metadata{};
                          for (const nlohmann::json& entry : json.at("entries")) {
                              const nlohmann::json& value = entry.at("value");
                              // A plain string is the format metadata was saved in before values
                              // were typed; it loads as a String, so no older scene is lost.
                              metadata.entries.push_back(ObjectMetadataEntry{
                                  entry.at("key").get<std::string>(),
                                  value.is_string()
                                      ? Core::Variant::MakeText(Core::VariantType::String,
                                                                      value.get<std::string>())
                                      : VariantFromJson(value, 0U)});
                          }
                          // The validator rejects a duplicate key, an oversized one and an
                          // over-long list, so a hand-edited or hostile file cannot load an entity
                          // whose metadata lookups would depend on iteration order.
                          if (!IsObjectMetadataComponentValid(metadata)) {
                              throw std::runtime_error("Invalid ObjectMetadataComponent payload");
                          }
                          return metadata;
                      }, IsObjectMetadataComponentValid));
        table.emplace("EditorDescriptionComponent",
                      MakeRegistration<EditorDescriptionComponent>([](const nlohmann::json& json) {
                          const EditorDescriptionComponent description{
                              json.at("description").get<std::string>()};
                          if (!IsEditorDescriptionComponentValid(description)) {
                              throw std::runtime_error("Invalid EditorDescriptionComponent payload");
                          }
                          return description;
                      }, IsEditorDescriptionComponentValid));
        table.emplace("PrefabInstanceComponent",
                      MakeRegistration<PrefabInstanceComponent>(
                          [](const nlohmann::json& json) { return PrefabInstanceFromJson(json); },
                          IsPrefabInstanceComponentValid));

        // --- Metadata auto-discovery (Tier 1.6) -------------------------------------------
        // Adding a component no longer touches this function: every component metadata entry
        // that qualifies gets a generic registration here, unless a hand-written one above
        // already claimed its C++ name (emplace keeps the existing registration, so explicit
        // wins). The snapshot entries are copies; the table takes shared ownership so the
        // registrations' lambdas stay valid for the table's process lifetime.
        const Core::TypeMetadataSnapshot snapshot =
            GetSceneComponentMetadataRegistry().GetSnapshot();
        for (const Core::TypeMetadataEntry& snapshotEntry : snapshot.entries) {
            if (!MetadataComponentQualifies(snapshotEntry) ||
                table.find(snapshotEntry.cppName) != table.end()) {
                continue;
            }
            auto entryCopy = std::make_shared<Core::TypeMetadataEntry>(snapshotEntry);
            table.emplace(snapshotEntry.cppName,
                          MakeMetadataRegistration(snapshotEntry.cppName, std::move(entryCopy)));
        }

        return table;
    }();
    return registrations;
}

[[nodiscard]] const std::string* FindNameForTypeIndex(std::type_index typeIndex) {
    static const std::unordered_map<std::type_index, std::string> namesByType = [] {
        std::unordered_map<std::type_index, std::string> map;
        for (const auto& [name, registration] : GetRegistrationsByName()) {
            map.emplace(registration.typeIndex, name);
        }
        return map;
    }();
    const auto it = namesByType.find(typeIndex);
    return it == namesByType.end() ? nullptr : &it->second;
}

/// Appends `root` and every descendant reachable via HierarchyComponent.parent to
/// `outEntities`, depth-first. The visited set deduplicates overlapping requested roots and makes
/// malformed hierarchy cycles fail closed rather than recursing indefinitely.
[[nodiscard]] bool CollectSubtree(IEntityManager& entityManager, const Entity root,
                                     std::unordered_set<Entity>& visited,
                                     std::vector<Entity>& outEntities) {
    if (!entityManager.IsAlive(root)) {
        return false;
    }
    if (!visited.emplace(root).second) {
        return true;
    }

    outEntities.push_back(root);
    // Siblings are written in their order: the file keeps no order number, so loading hands out
    // orders in the sequence the entities appear.
    std::vector<std::pair<std::int64_t, Entity>> ordered;
    entityManager.ForEach<HierarchyComponent>(
        [&ordered, root](const Entity entity, HierarchyComponent& hierarchy) {
            if (hierarchy.parent == root) {
                ordered.emplace_back(hierarchy.siblingOrder, entity);
            }
        });
    std::sort(ordered.begin(), ordered.end(), [](const auto& a, const auto& b) {
        if (a.first != b.first) {
            return a.first < b.first;
        }
        return a.second.index != b.second.index ? a.second.index < b.second.index
                                                : a.second.generation < b.second.generation;
    });
    std::vector<Entity> children;
    children.reserve(ordered.size());
    for (const auto& [order, child] : ordered) {
        children.push_back(child);
    }
    for (const Entity child : children) {
        if (!CollectSubtree(entityManager, child, visited, outEntities)) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool IsSceneAssetType(const SceneAssetType assetType) noexcept {
    return assetType == SceneAssetType::Scene || assetType == SceneAssetType::Prefab;
}

[[nodiscard]] std::optional<std::vector<std::byte>> EncodeScenePayload(
    IEntityManager& entityManager, const std::vector<Entity>& rootEntities,
    const std::string_view sourceDescription) {
    std::vector<Entity> allEntities;
    std::unordered_set<Entity> visited;
    for (const Entity root : rootEntities) {
        if (!CollectSubtree(entityManager, root, visited, allEntities)) {
            UVE_ERROR("SceneSerializer: \"{}\" includes an invalid root entity", sourceDescription);
            return std::nullopt;
        }
    }
    if (allEntities.size() > std::numeric_limits<std::uint32_t>::max()) {
        UVE_ERROR("SceneSerializer: \"{}\" contains too many entities to serialize", sourceDescription);
        return std::nullopt;
    }

    std::unordered_map<Entity, std::uint32_t> entityToLocalId;
    entityToLocalId.reserve(allEntities.size());
    for (std::uint32_t index = 0; index < allEntities.size(); ++index) {
        entityToLocalId.emplace(allEntities[index], index);
    }

    nlohmann::json entitiesJson = nlohmann::json::array();
    for (const Entity entity : allEntities) {
        nlohmann::json componentsJson = nlohmann::json::object();
        for (const std::type_index type : entityManager.GetComponentTypes(entity)) {
            if (type == std::type_index(typeid(WorldTransformComponent))) {
                continue; // Derived/cached state is rebuilt after restore.
            }
            if (type == std::type_index(typeid(DisabledComponent))) {
                // Presence is the data: an empty object marks the entity disabled on load.
                // Hand-written (not generic) because the registration table skips property-less
                // entries, and there is nothing here to put in the table.
                componentsJson["DisabledComponent"] = nlohmann::json::object();
                continue;
            }
            if (type == std::type_index(typeid(VisibilityComponent))) {
                // Written here rather than through the registration table for the same reason
                // HierarchyComponent is: it holds an entity reference, and only this function
                // knows the file-local id each entity was assigned.
                const VisibilityComponent& visibility =
                    entityManager.GetComponent<VisibilityComponent>(entity);
                std::int64_t visibilityParentLocalId = -1;
                if (visibility.visibilityParent != kInvalidEntity) {
                    const auto targetIt = entityToLocalId.find(visibility.visibilityParent);
                    if (targetIt != entityToLocalId.end()) {
                        visibilityParentLocalId = static_cast<std::int64_t>(targetIt->second);
                    }
                    // A target outside the captured subtree is dropped, exactly as an outside
                    // transform parent becomes a restored root: saving a reference to an entity
                    // the file does not contain would dangle on every load.
                }
                // visibleInHierarchy is deliberately absent - it is derived from ancestors and
                // recomputed by the first update after load.
                componentsJson["VisibilityComponent"] = {
                    {"visible", visibility.visible},
                    {"visibilityParentLocalId", visibilityParentLocalId}};
                continue;
            }
            if (type == std::type_index(typeid(HierarchyComponent))) {
                const HierarchyComponent& hierarchy = entityManager.GetComponent<HierarchyComponent>(entity);
                std::int64_t parentLocalId = -1;
                if (hierarchy.parent != kInvalidEntity) {
                    const auto parentIt = entityToLocalId.find(hierarchy.parent);
                    if (parentIt != entityToLocalId.end()) {
                        parentLocalId = static_cast<std::int64_t>(parentIt->second);
                    }
                    // A parent outside the captured subtree intentionally becomes a restored root.
                }
                componentsJson["HierarchyComponent"] = {{"parentLocalId", parentLocalId}};
                continue;
            }

            const std::string* const name = FindNameForTypeIndex(type);
            if (name == nullptr) {
                UVE_ERROR("SceneSerializer: no registered serializer for a component type on entity index {} "
                          "while encoding \"{}\"",
                          entity.index, sourceDescription);
                return std::nullopt;
            }
            const ComponentRegistration& registration = GetRegistrationsByName().at(*name);
            if (!registration.isValid(entityManager, entity)) {
                UVE_ERROR("SceneSerializer: component type \"{}\" on entity index {} failed authored validation "
                          "while encoding \"{}\"",
                          *name, entity.index, sourceDescription);
                return std::nullopt;
            }
            componentsJson[*name] = registration.toJson(entityManager, entity);
            // Animation targets and bone attachments are entity references: remapped to file-local
            // ids like the visibility parent, and dropped when the target is outside what is being
            // saved.
            const auto writeTarget = [&](const Entity target) {
                std::int64_t targetLocalId = -1;
                if (target != kInvalidEntity) {
                    const auto targetIt = entityToLocalId.find(target);
                    if (targetIt != entityToLocalId.end()) {
                        targetLocalId = static_cast<std::int64_t>(targetIt->second);
                    }
                }
                componentsJson[*name]["targetLocalId"] = targetLocalId;
            };
            if (type == std::type_index(typeid(AnimationDriverComponent))) {
                writeTarget(entityManager.GetComponent<AnimationDriverComponent>(entity).target);
            }
            if (type == std::type_index(typeid(BoneAttachment3DComponent))) {
                // A bone attachment's skeleton is an entity reference too, written under the id key
                // this component has always used so documents saved before it resolved still load.
                const Entity skeleton =
                    entityManager.GetComponent<BoneAttachment3DComponent>(entity).skeleton;
                std::uint32_t skeletonLocalId = std::numeric_limits<std::uint32_t>::max();
                if (skeleton != kInvalidEntity) {
                    const auto skeletonIt = entityToLocalId.find(skeleton);
                    if (skeletonIt != entityToLocalId.end()) {
                        skeletonLocalId = skeletonIt->second;
                    }
                }
                componentsJson[*name]["skeletonLocalId"] = skeletonLocalId;
            }
            if (type == std::type_index(typeid(TwoBoneIK3DComponent))) {
                // A chain's skeleton, target and pole are entity references like an attachment's
                // skeleton, written as file-local ids and resolved back on load.
                const TwoBoneIK3DComponent& chain =
                    entityManager.GetComponent<TwoBoneIK3DComponent>(entity);
                const auto localIdOf = [&entityToLocalId](const Entity referenced) {
                    std::uint32_t localId = std::numeric_limits<std::uint32_t>::max();
                    if (referenced != kInvalidEntity) {
                        const auto found = entityToLocalId.find(referenced);
                        if (found != entityToLocalId.end()) {
                            localId = found->second;
                        }
                    }
                    return localId;
                };
                componentsJson[*name]["skeletonLocalId"] = localIdOf(chain.skeleton);
                componentsJson[*name]["targetLocalId"] = localIdOf(chain.target);
                componentsJson[*name]["poleLocalId"] = localIdOf(chain.poleTarget);
            }
            if (type == std::type_index(typeid(FullBodyIK3DComponent))) {
                // A solve's skeleton and every effector's target are entity references like a
                // chain's, written as file-local ids and resolved back on load. Effector targets
                // ride inside their own entries so one unresolvable id leaves that limb inert
                // instead of shifting every other one.
                const FullBodyIK3DComponent& solve =
                    entityManager.GetComponent<FullBodyIK3DComponent>(entity);
                const auto localIdOf = [&entityToLocalId](const Entity referenced) {
                    std::uint32_t localId = std::numeric_limits<std::uint32_t>::max();
                    if (referenced != kInvalidEntity) {
                        const auto found = entityToLocalId.find(referenced);
                        if (found != entityToLocalId.end()) {
                            localId = found->second;
                        }
                    }
                    return localId;
                };
                componentsJson[*name]["skeletonLocalId"] = localIdOf(solve.skeleton);
                nlohmann::json& effectorsJson = componentsJson[*name]["effectors"];
                if (effectorsJson.is_array() && effectorsJson.size() == solve.effectors.size()) {
                    for (std::size_t index = 0U; index < solve.effectors.size(); ++index) {
                        effectorsJson[index]["targetLocalId"] = localIdOf(solve.effectors[index].target);
                    }
                }
            }
            if (type == std::type_index(typeid(Ragdoll3DComponent))) {
                const Ragdoll3DComponent& ragdoll =
                    entityManager.GetComponent<Ragdoll3DComponent>(entity);
                std::uint32_t skeletonLocalId = std::numeric_limits<std::uint32_t>::max();
                if (ragdoll.skeleton != kInvalidEntity) {
                    const auto found = entityToLocalId.find(ragdoll.skeleton);
                    if (found != entityToLocalId.end()) {
                        skeletonLocalId = found->second;
                    }
                }
                componentsJson[*name]["skeletonLocalId"] = skeletonLocalId;
            }
            if (type == std::type_index(typeid(RayCast3DComponent))) {
                // The ray's exclusions are entity references too: written as file-local ids, with
                // targets outside the saved set dropped exactly like an animation target. The
                // registration's own toJson JSON deliberately carries no exclusions key at all, so
                // there is only ever one place a reader can find them.
                const RayCast3DComponent& rayCast =
                    entityManager.GetComponent<RayCast3DComponent>(entity);
                nlohmann::json exclusionLocalIds = nlohmann::json::array();
                const std::size_t exclusionCount = CountRayCast3DExclusions(rayCast);
                for (std::size_t index = 0U; index < exclusionCount; ++index) {
                    const auto targetIt = entityToLocalId.find(rayCast.exclusions[index]);
                    if (targetIt != entityToLocalId.end()) {
                        exclusionLocalIds.push_back(targetIt->second);
                    }
                }
                componentsJson[*name]["exclusionsLocalIds"] = std::move(exclusionLocalIds);
            }
            if (type == std::type_index(typeid(ControllerComponent))) {
                // A controller's pawn is an entity reference: written as a file-local id, with a
                // target outside the saved set dropped exactly like an animation target.
                const ControllerComponent& controller =
                    entityManager.GetComponent<ControllerComponent>(entity);
                std::uint32_t pawnLocalId = std::numeric_limits<std::uint32_t>::max();
                if (controller.pawn != kInvalidEntity) {
                    const auto found = entityToLocalId.find(controller.pawn);
                    if (found != entityToLocalId.end()) {
                        pawnLocalId = found->second;
                    }
                }
                componentsJson[*name]["pawnLocalId"] = pawnLocalId;
            }
            if (type == std::type_index(typeid(PawnComponent))) {
                // A pawn's controller resolves the same way, from the other side of the link.
                const PawnComponent& pawn =
                    entityManager.GetComponent<PawnComponent>(entity);
                std::uint32_t controllerLocalId = std::numeric_limits<std::uint32_t>::max();
                if (pawn.controller != kInvalidEntity) {
                    const auto found = entityToLocalId.find(pawn.controller);
                    if (found != entityToLocalId.end()) {
                        controllerLocalId = found->second;
                    }
                }
                componentsJson[*name]["controllerLocalId"] = controllerLocalId;
            }
            if (type == std::type_index(typeid(CameraFollowComponent))) {
                // A follow camera's target is an entity reference: written as a file-local id,
                // with a target outside the saved set dropped exactly like a pawn link.
                const CameraFollowComponent& follow =
                    entityManager.GetComponent<CameraFollowComponent>(entity);
                std::uint32_t targetLocalId = std::numeric_limits<std::uint32_t>::max();
                if (follow.target != kInvalidEntity) {
                    const auto found = entityToLocalId.find(follow.target);
                    if (found != entityToLocalId.end()) {
                        targetLocalId = found->second;
                    }
                }
                componentsJson[*name]["targetLocalId"] = targetLocalId;
            }
            if (type == std::type_index(typeid(CinematicComponent))) {
                // Each cut's camera rides in a parallel file-local-id array (the cuts in toJson
                // carry times only): the sentinel marks a camera outside the saved set, and the
                // load pass drops those cuts rather than pointing them at a stranger.
                const CinematicComponent& cinematic =
                    entityManager.GetComponent<CinematicComponent>(entity);
                nlohmann::json cutCameraLocalIds = nlohmann::json::array();
                for (const CinematicCameraCut& cut : cinematic.cuts) {
                    std::uint32_t localId = std::numeric_limits<std::uint32_t>::max();
                    if (cut.camera != kInvalidEntity) {
                        const auto targetIt = entityToLocalId.find(cut.camera);
                        if (targetIt != entityToLocalId.end()) {
                            localId = targetIt->second;
                        }
                    }
                    cutCameraLocalIds.push_back(localId);
                }
                componentsJson[*name]["cutCameraLocalIds"] = std::move(cutCameraLocalIds);
                nlohmann::json animTargetLocalIds = nlohmann::json::array();
                for (const CinematicAnimationKey& key : cinematic.animationKeys) {
                    std::uint32_t localId = std::numeric_limits<std::uint32_t>::max();
                    if (key.target != kInvalidEntity) {
                        const auto targetIt = entityToLocalId.find(key.target);
                        if (targetIt != entityToLocalId.end()) {
                            localId = targetIt->second;
                        }
                    }
                    animTargetLocalIds.push_back(localId);
                }
                componentsJson[*name]["animTargetLocalIds"] = std::move(animTargetLocalIds);
            }
            if (type == std::type_index(typeid(Hitbox3DComponent))) {
                const Hitbox3DComponent& hitbox = entityManager.GetComponent<Hitbox3DComponent>(entity);
                std::int64_t ignoreLocalId = -1;
                if (hitbox.ignoreEntity != kInvalidEntity) {
                    const auto ignoreIt = entityToLocalId.find(hitbox.ignoreEntity);
                    if (ignoreIt != entityToLocalId.end()) {
                        ignoreLocalId = static_cast<std::int64_t>(ignoreIt->second);
                    }
                }
                componentsJson[*name]["ignoreLocalId"] = ignoreLocalId;
            }
            if (type == std::type_index(typeid(Hurtbox3DComponent))) {
                const Hurtbox3DComponent& hurtbox = entityManager.GetComponent<Hurtbox3DComponent>(entity);
                std::int64_t ignoreLocalId = -1;
                if (hurtbox.ignoreEntity != kInvalidEntity) {
                    const auto ignoreIt = entityToLocalId.find(hurtbox.ignoreEntity);
                    if (ignoreIt != entityToLocalId.end()) {
                        ignoreLocalId = static_cast<std::int64_t>(ignoreIt->second);
                    }
                }
                componentsJson[*name]["ignoreLocalId"] = ignoreLocalId;
            }
            if (type == std::type_index(typeid(InteractionArea3DComponent))) {
                const InteractionArea3DComponent& area =
                    entityManager.GetComponent<InteractionArea3DComponent>(entity);
                std::int64_t ignoreLocalId = -1;
                if (area.ignoreEntity != kInvalidEntity) {
                    const auto ignoreIt = entityToLocalId.find(area.ignoreEntity);
                    if (ignoreIt != entityToLocalId.end()) {
                        ignoreLocalId = static_cast<std::int64_t>(ignoreIt->second);
                    }
                }
                componentsJson[*name]["ignoreLocalId"] = ignoreLocalId;
            }
            if (type == std::type_index(typeid(Projectile3DComponent))) {
                const Projectile3DComponent& projectile =
                    entityManager.GetComponent<Projectile3DComponent>(entity);
                std::int64_t ignoreLocalId = -1;
                if (projectile.ignoreEntity != kInvalidEntity) {
                    const auto ignoreIt = entityToLocalId.find(projectile.ignoreEntity);
                    if (ignoreIt != entityToLocalId.end()) {
                        ignoreLocalId = static_cast<std::int64_t>(ignoreIt->second);
                    }
                }
                componentsJson[*name]["ignoreLocalId"] = ignoreLocalId;
            }
        }
        entitiesJson.push_back({{"localId", entityToLocalId.at(entity)}, {"components", std::move(componentsJson)}});
    }

    nlohmann::json payload;
    payload["entities"] = std::move(entitiesJson);
    const std::string payloadText = payload.dump();
    const auto* const payloadBytes = reinterpret_cast<const std::byte*>(payloadText.data());
    return std::vector<std::byte>{payloadBytes, payloadBytes + payloadText.size()};
}

void RollbackRestoredEntities(IEntityManager& entityManager, std::vector<Entity>& createdEntities) {
    for (auto entity = createdEntities.rbegin(); entity != createdEntities.rend(); ++entity) {
        if (entityManager.IsAlive(*entity)) {
            entityManager.DestroyEntity(*entity);
        }
    }
}

[[nodiscard]] std::optional<std::vector<Entity>> DecodeScenePayload(
    IEntityManager& entityManager, const std::vector<std::byte>& payloadBuffer,
    const std::string_view sourceDescription, const std::optional<std::size_t> expectedRootCount = std::nullopt) {
    const std::string payloadText(reinterpret_cast<const char*>(payloadBuffer.data()), payloadBuffer.size());
    nlohmann::json payload;
    try {
        payload = nlohmann::json::parse(payloadText);
    } catch (const nlohmann::json::parse_error& parseError) {
        UVE_ERROR("SceneSerializer: failed to parse \"{}\": {}", sourceDescription, parseError.what());
        return std::nullopt;
    }

    std::vector<std::pair<std::uint32_t, nlohmann::json>> orderedEntities;
    std::unordered_set<std::uint32_t> localIds;
    try {
        for (const nlohmann::json& entityJson : payload.at("entities")) {
            const std::uint32_t localId = entityJson.at("localId").get<std::uint32_t>();
            const nlohmann::json& components = entityJson.at("components");
            if (!components.is_object() || !localIds.emplace(localId).second) {
                UVE_ERROR("SceneSerializer: malformed entity list in \"{}\"", sourceDescription);
                return std::nullopt;
            }
            for (const auto& [componentName, componentJson] : components.items()) {
                if (componentName == "DisabledComponent") {
                    if (!componentJson.is_object()) {
                        UVE_ERROR("SceneSerializer: malformed disabled-component data in \"{}\"", sourceDescription);
                        return std::nullopt;
                    }
                    continue;
                }
                if (componentName == "VisibilityComponent") {
                    // Validated here and restored by the entity-aware path below, never through
                    // the registration table: it carries an entity reference, and only this
                    // function knows which file-local id maps to which restored entity.
                    if (!componentJson.is_object()) {
                        UVE_ERROR("SceneSerializer: malformed visibility data in \"{}\"", sourceDescription);
                        return std::nullopt;
                    }
                    if (componentJson.contains("visibilityParentLocalId")) {
                        const auto& targetJson = componentJson.at("visibilityParentLocalId");
                        if (!targetJson.is_number_integer()) {
                            UVE_ERROR("SceneSerializer: malformed visibility parent id in \"{}\"",
                                      sourceDescription);
                            return std::nullopt;
                        }
                        const std::int64_t targetLocalId = targetJson.get<std::int64_t>();
                        if (targetLocalId < -1 ||
                            (targetLocalId >= 0 && static_cast<std::uint64_t>(targetLocalId) >
                                                       std::numeric_limits<std::uint32_t>::max())) {
                            UVE_ERROR("SceneSerializer: visibility parent local ID is outside the "
                                      "uint32 range in \"{}\"", sourceDescription);
                            return std::nullopt;
                        }
                    }
                    continue;
                }
                if (componentName == "HierarchyComponent") {
                    if (!componentJson.is_object() || !componentJson.contains("parentLocalId") ||
                        !componentJson.at("parentLocalId").is_number_integer()) {
                        UVE_ERROR("SceneSerializer: malformed hierarchy data in \"{}\"", sourceDescription);
                        return std::nullopt;
                    }
                    const std::int64_t parentLocalId = componentJson.at("parentLocalId").get<std::int64_t>();
                    if (parentLocalId < -1 ||
                        (parentLocalId >= 0 &&
                         static_cast<std::uint64_t>(parentLocalId) > std::numeric_limits<std::uint32_t>::max())) {
                        UVE_ERROR("SceneSerializer: hierarchy parent local ID is outside the uint32 range in \"{}\"",
                                  sourceDescription);
                        return std::nullopt;
                    }
                    continue;
                }
                if (CanonicalComponentName(componentName) == "RayCast3DComponent") {
                    // Validated here and restored by the entity-aware path below, never through the
                    // registration table: the exclusions are local ids only this function's table
                    // can resolve. The remaining fields are the table reader's business, checked
                    // during restore.
                    if (!componentJson.is_object()) {
                        UVE_ERROR("SceneSerializer: malformed ray cast data in \"{}\"", sourceDescription);
                        return std::nullopt;
                    }
                    const nlohmann::json exclusionIds =
                        componentJson.value("exclusionsLocalIds", nlohmann::json::array());
                    if (!exclusionIds.is_array() || exclusionIds.size() > kMaximumRayCastExclusions) {
                        UVE_ERROR("SceneSerializer: malformed ray cast exclusions in \"{}\"",
                                  sourceDescription);
                        return std::nullopt;
                    }
                    for (const auto& exclusionId : exclusionIds) {
                        if (!exclusionId.is_number_integer()) {
                            UVE_ERROR("SceneSerializer: malformed ray cast exclusion id in \"{}\"",
                                      sourceDescription);
                            return std::nullopt;
                        }
                        const std::int64_t exclusionLocalId = exclusionId.get<std::int64_t>();
                        if (exclusionLocalId < 0 ||
                            static_cast<std::uint64_t>(exclusionLocalId) >
                                std::numeric_limits<std::uint32_t>::max()) {
                            UVE_ERROR("SceneSerializer: ray cast exclusion id is outside the uint32 "
                                      "range in \"{}\"",
                                      sourceDescription);
                            return std::nullopt;
                        }
                    }
                    continue;
                }
                if (GetRegistrationsByName().find(std::string{CanonicalComponentName(componentName)}) ==
                    GetRegistrationsByName().end()) {
                    UVE_ERROR("SceneSerializer: \"{}\" references unknown component type \"{}\"",
                              sourceDescription, componentName);
                    return std::nullopt;
                }
            }
            orderedEntities.emplace_back(localId, entityJson);
        }
    } catch (const nlohmann::json::exception& jsonError) {
        UVE_ERROR("SceneSerializer: malformed entity list in \"{}\": {}", sourceDescription, jsonError.what());
        return std::nullopt;
    }

    if (expectedRootCount.has_value()) {
        std::size_t rootCount = 0U;
        for (const auto& [unusedLocalId, entityJson] : orderedEntities) {
            static_cast<void>(unusedLocalId);
            const auto& components = entityJson.at("components");
            bool hasKnownParent = false;
            if (components.contains("HierarchyComponent")) {
                const std::int64_t parentLocalId =
                    components.at("HierarchyComponent").at("parentLocalId").get<std::int64_t>();
                hasKnownParent = parentLocalId >= 0 &&
                                 static_cast<std::uint64_t>(parentLocalId) <=
                                     std::numeric_limits<std::uint32_t>::max() &&
                                 localIds.contains(static_cast<std::uint32_t>(parentLocalId));
            }
            if (!hasKnownParent) {
                ++rootCount;
            }
        }
        if (rootCount != *expectedRootCount) {
            UVE_ERROR("SceneSerializer: \"{}\" has {} roots but requires {} before entity creation",
                      sourceDescription, rootCount, *expectedRootCount);
            return std::nullopt;
        }
    }

    // Restore bypasses SceneGraph::SetParent(), so reject parent cycles before creating any
    // ECS entities. The bounded step count is sufficient because a cycle cannot contain more
    // distinct local IDs than this already parsed entity vector; a missing parent terminates at a
    // restored root, matching the existing external-parent-to-root behavior.
    for (const auto& [startingLocalId, unusedEntityJson] : orderedEntities) {
        static_cast<void>(unusedEntityJson);
        std::uint32_t currentLocalId = startingLocalId;
        std::size_t steps = 0U;
        for (; steps < orderedEntities.size(); ++steps) {
            const auto entityIt = std::find_if(
                orderedEntities.begin(), orderedEntities.end(),
                [currentLocalId](const auto& entry) { return entry.first == currentLocalId; });
            if (entityIt == orderedEntities.end()) {
                break;
            }
            const auto& components = entityIt->second.at("components");
            if (!components.contains("HierarchyComponent")) {
                break;
            }
            const std::int64_t parentLocalId =
                components.at("HierarchyComponent").at("parentLocalId").get<std::int64_t>();
            if (parentLocalId < 0 ||
                static_cast<std::uint64_t>(parentLocalId) > std::numeric_limits<std::uint32_t>::max()) {
                break;
            }
            currentLocalId = static_cast<std::uint32_t>(parentLocalId);
        }
        if (steps == orderedEntities.size()) {
            UVE_ERROR("SceneSerializer: \"{}\" contains a cyclic hierarchy", sourceDescription);
            return std::nullopt;
        }
    }

    std::unordered_map<std::uint32_t, Entity> localIdToEntity;
    localIdToEntity.reserve(orderedEntities.size());
    std::vector<Entity> createdEntities;
    createdEntities.reserve(orderedEntities.size());
    for (const auto& [localId, unusedEntityJson] : orderedEntities) {
        static_cast<void>(unusedEntityJson);
        const Entity entity = entityManager.CreateEntity();
        localIdToEntity.emplace(localId, entity);
        createdEntities.push_back(entity);
    }

    std::vector<Entity> roots;
    try {
        for (const auto& [localId, entityJson] : orderedEntities) {
            const Entity entity = localIdToEntity.at(localId);
            bool isRoot = true;
            bool hasTransform = false;
            const nlohmann::json& componentsJson = entityJson.at("components");
            // Whether this entity's own document carries a driver, read before the loop so the
            // legacy-migration branch below cannot depend on key order. It used to: "AnimationMixer"
            // (the name AnimationDriverComponent had then) sorted ahead of "AnimationPlayer" and
            // "AnimationTree", so a saved driver was always visited first. Renaming those two to
            // AnimationSequencer and AnimationGraph moved AnimationGraph AHEAD of the driver, and a
            // graph then synthesized a throwaway driver that the real one collided with.
            const bool documentHasMixer =
                componentsJson.contains("AnimationDriverComponent") ||
                componentsJson.contains("AnimationMixerComponent");
            for (const auto& [componentName, componentJson] : componentsJson.items()) {
                if (componentName == "DisabledComponent") {
                    entityManager.AddComponent<DisabledComponent>(entity);
                    continue;
                }
                if (componentName == "VisibilityComponent") {
                    VisibilityComponent visibility;
                    // Absent in documents written before the flag existed, and visible is what
                    // they meant.
                    visibility.visible = componentJson.value("visible", true);
                    // Seeded from the authored value so nothing is briefly drawn between restore
                    // and the first scene-graph update, which then computes the inherited answer.
                    visibility.visibleInHierarchy = visibility.visible;
                    const std::int64_t targetLocalId = componentJson.value("visibilityParentLocalId",
                                                                           static_cast<std::int64_t>(-1));
                    if (targetLocalId >= 0) {
                        const auto targetIt = localIdToEntity.find(static_cast<std::uint32_t>(targetLocalId));
                        if (targetIt != localIdToEntity.end()) {
                            visibility.visibilityParent = targetIt->second;
                        }
                        // A target the file does not contain leaves the redirect unset, which
                        // means "inherit from the transform parent" - the same fallback the
                        // resolver uses for a dangling reference at runtime.
                    }
                    entityManager.AddComponent<VisibilityComponent>(entity, visibility);
                    continue;
                }
                if (CanonicalComponentName(componentName) == "RayCast3DComponent") {
                    entityManager.AddComponent<RayCast3DComponent>(
                        entity, RayCast3DComponentWithResolvedExclusions(componentJson, localIdToEntity));
                    continue;
                }
                if (CanonicalComponentName(componentName) == "ControllerComponent") {
                    entityManager.AddComponent<ControllerComponent>(
                        entity, ControllerComponentWithResolvedPawn(componentJson, localIdToEntity));
                    continue;
                }
                if (CanonicalComponentName(componentName) == "PawnComponent") {
                    entityManager.AddComponent<PawnComponent>(
                        entity, PawnComponentWithResolvedController(componentJson, localIdToEntity));
                    continue;
                }
                if (CanonicalComponentName(componentName) == "CameraFollowComponent") {
                    entityManager.AddComponent<CameraFollowComponent>(
                        entity, CameraFollowComponentWithResolvedTarget(componentJson, localIdToEntity));
                    continue;
                }
                if (CanonicalComponentName(componentName) == "Hitbox3DComponent") {
                    entityManager.AddComponent<Hitbox3DComponent>(
                        entity, Hitbox3DComponentWithResolvedIgnore(componentJson, localIdToEntity));
                    continue;
                }
                if (CanonicalComponentName(componentName) == "Hurtbox3DComponent") {
                    entityManager.AddComponent<Hurtbox3DComponent>(
                        entity, Hurtbox3DComponentWithResolvedIgnore(componentJson, localIdToEntity));
                    continue;
                }
                if (CanonicalComponentName(componentName) == "InteractionArea3DComponent") {
                    entityManager.AddComponent<InteractionArea3DComponent>(
                        entity, InteractionArea3DComponentWithResolvedIgnore(componentJson, localIdToEntity));
                    continue;
                }
                if (CanonicalComponentName(componentName) == "Projectile3DComponent") {
                    entityManager.AddComponent<Projectile3DComponent>(
                        entity, Projectile3DComponentWithResolvedIgnore(componentJson, localIdToEntity));
                    continue;
                }
                if (CanonicalComponentName(componentName) == "BoneAttachment3DComponent") {
                    entityManager.AddComponent<BoneAttachment3DComponent>(
                        entity, BoneAttachment3DComponentWithResolvedSkeleton(componentJson, localIdToEntity));
                    continue;
                }
                if (CanonicalComponentName(componentName) == "TwoBoneIK3DComponent") {
                    entityManager.AddComponent<TwoBoneIK3DComponent>(
                        entity, TwoBoneIK3DComponentWithResolvedReferences(componentJson, localIdToEntity));
                    continue;
                }
                if (CanonicalComponentName(componentName) == "FullBodyIK3DComponent") {
                    entityManager.AddComponent<FullBodyIK3DComponent>(
                        entity, FullBodyIK3DComponentWithResolvedReferences(componentJson, localIdToEntity));
                    continue;
                }
                if (CanonicalComponentName(componentName) == "Ragdoll3DComponent") {
                    Ragdoll3DComponent ragdoll =
                        Ragdoll3DComponentWithResolvedReferences(componentJson, localIdToEntity);
                    if (!IsRagdoll3DComponentAuthoredValid(ragdoll)) {
                        throw std::runtime_error("Invalid Ragdoll3DComponent payload");
                    }
                    entityManager.AddComponent<Ragdoll3DComponent>(entity, std::move(ragdoll));
                    continue;
                }
                if (CanonicalComponentName(componentName) == "CinematicComponent") {
                    CinematicComponent cinematic =
                        CinematicComponentWithResolvedReferences(componentJson, localIdToEntity);
                    if (!IsCinematicComponentValid(cinematic)) {
                        throw std::runtime_error("Invalid CinematicComponent payload");
                    }
                    entityManager.AddComponent<CinematicComponent>(entity, std::move(cinematic));
                    continue;
                }
                if (componentName == "HierarchyComponent") {
                    const std::int64_t parentLocalId = componentJson.at("parentLocalId").get<std::int64_t>();
                    Entity parent = kInvalidEntity;
                    if (parentLocalId >= 0) {
                        const auto parentIt = localIdToEntity.find(static_cast<std::uint32_t>(parentLocalId));
                        if (parentIt != localIdToEntity.end()) {
                            parent = parentIt->second;
                            isRoot = false;
                        }
                    }
                    entityManager.AddComponent<HierarchyComponent>(entity, HierarchyComponent{parent});
                    continue;
                }

                const std::string canonicalName{CanonicalComponentName(componentName)};
                const auto registrationIt = GetRegistrationsByName().find(canonicalName);
                if (registrationIt == GetRegistrationsByName().end()) {
                    throw std::runtime_error("unknown scene component: " + componentName);
                }
                registrationIt->second.fromJson(entityManager, entity, componentJson);
                hasTransform = hasTransform || canonicalName == "TransformComponent";
                const bool isMixer = canonicalName == "AnimationDriverComponent";
                const bool isMixerChild =
                    canonicalName == "AnimationSequencerComponent" || canonicalName == "AnimationGraphComponent";
                if (isMixer || isMixerChild) {
                    // A target the file does not contain stays unset, which means "the parent".
                    Entity target = kInvalidEntity;
                    const std::int64_t targetLocalId = componentJson.value("targetLocalId", static_cast<std::int64_t>(-1));
                    if (targetLocalId >= 0 &&
                        static_cast<std::uint64_t>(targetLocalId) <= std::numeric_limits<std::uint32_t>::max()) {
                        const auto targetIt = localIdToEntity.find(static_cast<std::uint32_t>(targetLocalId));
                        if (targetIt != localIdToEntity.end()) {
                            target = targetIt->second;
                        }
                    }
                    // A sequencer or graph from before AnimationDriver existed brings its old settings
                    // into a new one. Only when the document has no driver of its own: otherwise the
                    // real one, already added by its registration, is the truth and synthesizing a
                    // second one would collide with it.
                    if (isMixerChild && !documentHasMixer &&
                        !entityManager.HasComponent<AnimationDriverComponent>(entity)) {
                        AnimationDriverComponent legacy = AnimationDriverFromJson(componentJson);
                        if (!IsAnimationDriverComponentValid(legacy)) {
                            legacy = AnimationDriverComponent{};
                        }
                        legacy.target = target;
                        entityManager.AddComponent<AnimationDriverComponent>(entity, legacy);
                    } else if (isMixer && entityManager.HasComponent<AnimationDriverComponent>(entity)) {
                        entityManager.GetComponent<AnimationDriverComponent>(entity).target = target;
                    }
                }
            }

            // AttachTransform creates Transform+WorldTransform+Hierarchy together. Recreate the
            // derived component here so the next SceneGraph update sees restored transforms.
            if (hasTransform && !entityManager.HasComponent<WorldTransformComponent>(entity)) {
                entityManager.AddComponent<WorldTransformComponent>(entity);
            }
            if (isRoot) {
                roots.push_back(entity);
            }
        }
    } catch (const nlohmann::json::exception& jsonError) {
        UVE_ERROR("SceneSerializer: malformed component data in \"{}\": {}", sourceDescription,
                  jsonError.what());
        RollbackRestoredEntities(entityManager, createdEntities);
        return std::nullopt;
    } catch (const std::exception& validationError) {
        UVE_ERROR("SceneSerializer: invalid component data in \"{}\": {}", sourceDescription,
                  validationError.what());
        RollbackRestoredEntities(entityManager, createdEntities);
        return std::nullopt;
    }

    return roots;
}

[[nodiscard]] bool ValidateSceneAssetType(const SceneAssetType assetType,
                                             const std::string_view sourceDescription) {
    if (IsSceneAssetType(assetType)) {
        return true;
    }
    UVE_ERROR("SceneSerializer: \"{}\" has unexpected asset type {}", sourceDescription,
              static_cast<std::uint32_t>(assetType));
    return false;
}

} // namespace

std::optional<SceneSnapshot> SceneSerializer::Capture(
    IEntityManager& entityManager, const std::vector<Entity>& rootEntities,
    const SceneAssetType assetType) const {
    if (!ValidateSceneAssetType(assetType, "scene snapshot")) {
        return std::nullopt;
    }
    if (assetType == SceneAssetType::Prefab && rootEntities.size() != 1U) {
        UVE_ERROR("SceneSerializer: prefab snapshot requires exactly one root entity");
        return std::nullopt;
    }
    const std::optional<std::vector<std::byte>> payload =
        EncodeScenePayload(entityManager, rootEntities, "scene snapshot");
    if (!payload.has_value()) {
        return std::nullopt;
    }
    return SceneSnapshot{Asset::EncodeUveFileEnvelope(assetType, *payload), assetType};
}

std::vector<Entity> SceneSerializer::Restore(IEntityManager& entityManager,
                                                       const SceneSnapshot& snapshot) const {
    const auto envelope = Asset::DecodeUveFileEnvelope(snapshot.bytes, "scene snapshot");
    if (!envelope.has_value()) {
        return {};
    }
    const auto& [header, payload] = *envelope;
    if (!ValidateSceneAssetType(header.assetType, "scene snapshot") || header.assetType != snapshot.assetType) {
        if (header.assetType != snapshot.assetType) {
            UVE_ERROR("SceneSerializer: scene snapshot asset type metadata does not match its envelope");
        }
        return {};
    }
    const std::optional<std::vector<Entity>> restored = DecodeScenePayload(
        entityManager, payload, "scene snapshot",
        header.assetType == SceneAssetType::Prefab ? std::optional<std::size_t>{1U} : std::nullopt);
    return restored.value_or(std::vector<Entity>{});
}

bool SceneSerializer::Save(IEntityManager& entityManager, const std::vector<Entity>& rootEntities,
                                  const std::filesystem::path& path, const SceneAssetType assetType) {
    if (!ValidateSceneAssetType(assetType, path.string())) {
        return false;
    }
    if (assetType == SceneAssetType::Prefab && rootEntities.size() != 1U) {
        UVE_ERROR("SceneSerializer: prefab save requires exactly one root entity for \"{}\"", path.string());
        return false;
    }
    const std::optional<std::vector<std::byte>> payload = EncodeScenePayload(entityManager, rootEntities, path.string());
    if (!payload.has_value()) {
        return false;
    }

    const std::filesystem::path temporaryPath = path.string() + ".uve_scene_tmp";
    std::error_code errorCode;
    std::filesystem::remove(temporaryPath, errorCode);
    errorCode.clear();
    if (!Asset::WriteUveFile(temporaryPath, assetType, *payload)) {
        std::filesystem::remove(temporaryPath, errorCode);
        return false;
    }
    std::filesystem::rename(temporaryPath, path, errorCode);
    if (errorCode) {
        UVE_ERROR("SceneSerializer: failed to publish temporary scene \"{}\" as \"{}\": {}",
                  temporaryPath.string(), path.string(), errorCode.message());
        std::filesystem::remove(temporaryPath, errorCode);
        return false;
    }
    return true;
}

std::vector<Entity> SceneSerializer::Load(IEntityManager& entityManager,
                                                    const std::filesystem::path& path) {
    const auto file = Asset::ReadUveFile(path);
    if (!file.has_value()) {
        return {};
    }
    const auto& [header, payload] = *file;
    if (!ValidateSceneAssetType(header.assetType, path.string())) {
        return {};
    }
    const std::optional<std::vector<Entity>> restored = DecodeScenePayload(
        entityManager, payload, path.string(),
        header.assetType == SceneAssetType::Prefab ? std::optional<std::size_t>{1U} : std::nullopt);
    return restored.value_or(std::vector<Entity>{});
}

} // namespace UVE::Scene
