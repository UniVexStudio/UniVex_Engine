// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#include "uve/scene/scene_serializer_uve.h"
#include "uve/ai/perception_uve.h"
#include "uve/ai/utility_ai_uve.h"
#include "uve/gameplay/cinematic_uve.h"
#include "uve/gameplay/gameplay_attributes_uve.h"
#include "uve/gameplay/gameplay_tags_uve.h"
#include "uve/gameplay/status_effects_uve.h"
#include "uve/gameplay/trigger_volume_uve.h"
#include "uve/gameplay/pawn_uve.h"

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

#include "uve/asset/asset_guid_uve.h"
#include "uve/asset/uve_file_envelope_uve.h"
#include "uve/logging/logging_macros_uve.h"
#include "uve/math/quaternion_uve.h"
#include "uve/math/vector2_uve.h"
#include "uve/math/vector3_uve.h"
#include "uve/math/color_uve.h"
#include "uve/math/rect_uve.h"
#include "uve/component/animation_driver_component_uve.h"
#include "uve/component/animation_sequencer_component_uve.h"
#include "uve/component/animation_graph_component_uve.h"
#include "uve/component/area_component_uve.h"
#include "uve/component/audio_source_component_uve.h"
#include "uve/component/bone_modifier_component_uve.h"
#include "uve/component/camera_component_uve.h"
#include "uve/component/camera_follow_component_uve.h"
#include "uve/component/canvas_component_uve.h"
#include "uve/component/character_controller_component_uve.h"
#include "uve/component/collider_component_uve.h"
#include "uve/component/morph_target_component_uve.h"
#include "uve/component/skin_component_uve.h"
#include "uve/objects/3d/all_objects_3d_uve.h"
#include "uve/scene/objects/scene_object_type_uve.h"
#include "uve/scene/objects/scene_folder_uve.h"
#include "uve/scene/objects/scene_root_uve.h"
#include "uve/component/hierarchy_component_uve.h"
#include "uve/component/light_component_uve.h"
#include "uve/component/light_emitter_component_uve.h"
#include "uve/component/mesh_component_uve.h"
#include "uve/component/name_component_uve.h"
#include "uve/component/auto_translate_component_uve.h"
#include "uve/component/editor_description_component_uve.h"
#include "uve/component/object_metadata_component_uve.h"
#include "uve/component/process_component_uve.h"
#include "uve/component/thread_group_component_uve.h"
#include "uve/component/particle_emitter_component_uve.h"
#include "uve/component/physics_interpolation_component_uve.h"
#include "uve/component/physics_object_component_uve.h"
#include "uve/component/primitive_mesh_component_uve.h"
#include "uve/component/render_instance_component_uve.h"
#include "uve/component/solid_body_component_uve.h"
#include "uve/component/prefab_instance_component_uve.h"
#include "uve/component/rigid_3d_component_uve.h"
#include "uve/component/script_component_uve.h"
#include "uve/component/surface_instance_component_uve.h"
#include "uve/component/transform_component_uve.h"
#include "uve/component/disabled_component_uve.h"
#include "uve/component/visibility_component_uve.h"
#include "uve/component/ui_button_component_uve.h"
#include "uve/component/ui_image_component_uve.h"
#include "uve/component/ui_text_component_uve.h"
#include "uve/component/world_transform_component_uve.h"
#include "uve/component/component_type_info_uve.h"
#include "uve/scene/scene_component_metadata_uve.h"

namespace UVE::Scene {

namespace {

// --- Math JSON helpers ---------------------------------------------------------------------

[[nodiscard]] nlohmann::json ToJsonUVE(const Math::Vector3UVE& vector) {
    return nlohmann::json::array({vector.x, vector.y, vector.z});
}

[[nodiscard]] Math::Vector3UVE Vector3FromJsonUVE(const nlohmann::json& json) {
    return Math::Vector3UVE{json.at(0).get<float>(), json.at(1).get<float>(), json.at(2).get<float>()};
}

// Colors persist as the same [r, g, b] triple arrays vectors use: old scenes load
// byte-identical, and the linear interpretation comes from the ColorUVE type.
[[nodiscard]] nlohmann::json ToJsonUVE(const Math::ColorUVE& color) {
    return nlohmann::json::array({color.r, color.g, color.b});
}

[[nodiscard]] Math::ColorUVE ColorFromJsonUVE(const nlohmann::json& json) {
    return Math::ColorUVE{json.at(0).get<float>(), json.at(1).get<float>(), json.at(2).get<float>()};
}

[[nodiscard]] nlohmann::json ToJsonUVE(const Math::Vector2UVE& vector) {
    return nlohmann::json::array({vector.x, vector.y});
}

[[nodiscard]] Math::Vector2UVE Vector2FromJsonUVE(const nlohmann::json& json) {
    return Math::Vector2UVE{json.at(0).get<float>(), json.at(1).get<float>()};
}

[[nodiscard]] nlohmann::json ToJsonUVE(const Math::QuaternionUVE& rotation) {
    return nlohmann::json::array({rotation.x, rotation.y, rotation.z, rotation.w});
}

[[nodiscard]] Math::QuaternionUVE QuaternionFromJsonUVE(const nlohmann::json& json) {
    return Math::QuaternionUVE{json.at(0).get<float>(), json.at(1).get<float>(), json.at(2).get<float>(),
                                json.at(3).get<float>()};
}

// --- Per-component-type JSON (de)serialization ---------------------------------------------
//
// One ToJsonUVE(const T&) overload plus one fromJson lambda per serializable component type.
// HierarchyComponentUVE and WorldTransformComponentUVE are deliberately absent here: the former
// needs the file-local-id remapping only SaveUVE()/LoadUVE() itself has the context to do, and
// the latter is derived/cached data that is never serialized at all.
//
// Adding a new built-in component type? Register its JSON (de)serialization here too.

/// Reads an enum stored as a small integer, rejecting anything outside the known range rather
/// than casting it through. A hand-edited or future-version scene can carry a value this build
/// does not have, and silently reinterpreting it as XYZ would rotate the object without saying
/// so - the same class of failure as a mismatched icon name, but with geometry.
[[nodiscard]] Math::EulerOrderUVE ReadEulerOrderUVE(const nlohmann::json& json) {
    const auto raw = json.value("eulerOrder", static_cast<std::uint8_t>(0));
    switch (raw) {
        case 0: return Math::EulerOrderUVE::XYZ;
        case 1: return Math::EulerOrderUVE::YXZ;
        case 2: return Math::EulerOrderUVE::ZYX;
        case 3: return Math::EulerOrderUVE::XZY;
        case 4: return Math::EulerOrderUVE::YZX;
        case 5: return Math::EulerOrderUVE::ZXY;
        default: return Math::EulerOrderUVE::XYZ;
    }
}

[[nodiscard]] RotationEditModeUVE ReadRotationEditModeUVE(const nlohmann::json& json) {
    const auto raw = json.value("rotationEditMode", static_cast<std::uint8_t>(0));
    return raw == 1 ? RotationEditModeUVE::Quaternion : RotationEditModeUVE::Euler;
}

[[nodiscard]] nlohmann::json ToJsonUVE(const TransformComponentUVE& component) {
    // localRotation is written even in Euler mode, and that is deliberate: it is what every
    // consumer reads, and a reader that does not know about the Euler fields must still get a
    // correct rotation out of the file. The Euler fields are the AUTHORED source beside it - the
    // exact angles typed, including turns past 360 and the pole poses a quaternion cannot
    // distinguish - so reopening a scene shows what was typed rather than a re-derived guess.
    return {
        {"localPosition", ToJsonUVE(component.localPosition)},
        {"localRotation", ToJsonUVE(component.localRotation)},
        {"localScale", ToJsonUVE(component.localScale)},
        {"localEulerRadians", ToJsonUVE(component.localEulerRadians)},
        {"eulerOrder", static_cast<std::uint8_t>(component.eulerOrder)},
        {"rotationEditMode", static_cast<std::uint8_t>(component.rotationEditMode)},
        {"topLevel", component.topLevel},
    };
}

// The target is an entity reference and is written beside these by the entity-aware encoder
// (targetLocalId), because only it knows the file-local ids. Runtime state is never written.
[[nodiscard]] nlohmann::json ToJsonUVE(const AnimationSequencerComponentUVE& component) {
    nlohmann::json library = nlohmann::json::array();
    for (const Asset::AssetGuidUVE& guid : component.library) {
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
[[nodiscard]] nlohmann::json ToJsonUVE(const AnimationDriverComponentUVE& component) {
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
[[nodiscard]] AnimationDriverComponentUVE AnimationDriverFromJsonUVE(const nlohmann::json& json) {
    AnimationDriverComponentUVE driver;
    driver.active = json.value("active", true);
    driver.speedScale = json.value("speedScale", 1.0F);
    driver.processCallback = static_cast<AnimationProcessCallbackUVE>(json.value("processCallback", std::uint8_t{0}));
    driver.animatePosition = json.value("animatePosition", true);
    driver.animateRotation = json.value("animateRotation", true);
    driver.animateScale = json.value("animateScale", true);
    driver.transition = static_cast<AnimationTransitionModeUVE>(json.value("transition", std::uint8_t{0}));
    driver.rootMotion = static_cast<AnimationRootMotionModeUVE>(json.value("rootMotion", std::uint8_t{0}));
    driver.rootMotionBone = json.value("rootMotionBone", std::string{});
    return driver;
}

[[nodiscard]] nlohmann::json ToJsonUVE(const GameplayAttributesComponentUVE& component) {
    nlohmann::json attributes = nlohmann::json::array();
    for (const GameplayAttributeUVE& attribute : component.attributes) {
        attributes.push_back({{"id", attribute.id},
                              {"current", attribute.current},
                              {"maximum", attribute.maximum},
                              {"regenPerSecond", attribute.regenPerSecond}});
    }
    return {{"attributes", std::move(attributes)}};
}

[[nodiscard]] nlohmann::json ToJsonUVE(const GameplayTagComponentUVE& component) {
    return {{"tags", component.tags}};
}

[[nodiscard]] nlohmann::json ToJsonUVE(const StatusEffectsComponentUVE& component) {
    nlohmann::json effects = nlohmann::json::array();
    for (const StatusEffectUVE& effect : component.effects) {
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

[[nodiscard]] nlohmann::json ToJsonUVE(const ControllerComponentUVE& component) {
    // The pawn link is deliberately absent: entity references resolve through file-local ids.
    return {{"kind", static_cast<unsigned int>(component.kind)}};
}

[[nodiscard]] nlohmann::json ToJsonUVE(const PawnComponentUVE& component) {
    // The controller link is deliberately absent: entity references resolve through file-local ids.
    return {{"input",
             {{"move", ToJsonUVE(component.input.move)},
              {"rise", component.input.rise},
              {"jumpPressed", component.input.jumpPressed},
              {"lookPointer", ToJsonUVE(component.input.lookPointer)},
              {"lookStick", ToJsonUVE(component.input.lookStick)},
              {"interactPressed", component.input.interactPressed}}}};
}

[[nodiscard]] nlohmann::json ToJsonUVE(const CameraFollowComponentUVE& component) {
    // The target link is deliberately absent: entity references resolve through file-local ids.
    return {{"offset", ToJsonUVE(component.offset)},
            {"followPossessedPawn", component.followPossessedPawn}};
}

[[nodiscard]] nlohmann::json ToJsonUVE(const TriggerVolumeComponentUVE& component) {
    // Runtime state rides along on purpose: a spent one-shot must stay spent across save/load.
    return {{"policy", static_cast<unsigned int>(component.policy)},
            {"intervalSeconds", component.intervalSeconds},
            {"cooldownSeconds", component.cooldownSeconds},
            {"armed", component.armed},
            {"firedCount", component.firedCount},
            {"cooldownRemaining", component.cooldownRemaining},
            {"intervalRemaining", component.intervalRemaining}};
}

[[nodiscard]] nlohmann::json ToJsonUVE(const PerceptionComponentUVE& component) {
    return {{"watchedTag", component.watchedTag},
            {"sightRangeMetres", component.sightRangeMetres},
            {"sightFieldOfViewDegrees", component.sightFieldOfViewDegrees},
            {"hearingRadiusMetres", component.hearingRadiusMetres},
            {"requiresLineOfSight", component.requiresLineOfSight}};
}

[[nodiscard]] nlohmann::json ToJsonUVE(const NoiseEmitterComponentUVE& component) {
    return {{"loudness", component.loudness}, {"decayPerSecond", component.decayPerSecond}};
}

[[nodiscard]] nlohmann::json ToJsonUVE(const BlackboardComponentUVE& component) {
    nlohmann::json entries = nlohmann::json::array();
    for (const AiBlackboardEntryUVE& entry : component.entries) {
        entries.push_back({{"key", entry.key}, {"value", entry.value}});
    }
    nlohmann::json vectors = nlohmann::json::array();
    for (const AiBlackboardVectorEntryUVE& entry : component.vectors) {
        vectors.push_back({{"key", entry.key}, {"value", ToJsonUVE(entry.value)}});
    }
    return {{"entries", std::move(entries)}, {"vectors", std::move(vectors)}};
}

[[nodiscard]] nlohmann::json ToJsonUVE(const AiBrainComponentUVE& component) {
    nlohmann::json actions = nlohmann::json::array();
    for (const AiActionUVE& action : component.actions) {
        nlohmann::json considerations = nlohmann::json::array();
        for (const AiConsiderationUVE& consideration : action.considerations) {
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

[[nodiscard]] nlohmann::json ToJsonUVE(const CinematicComponentUVE& component) {
    nlohmann::json events = nlohmann::json::array();
    for (const CinematicEventKeyUVE& key : component.events) {
        events.push_back({{"timeSeconds", key.timeSeconds}, {"eventId", key.eventId}});
    }
    nlohmann::json cuts = nlohmann::json::array();
    for (const CinematicCameraCutUVE& cut : component.cuts) {
        cuts.push_back({{"timeSeconds", cut.timeSeconds}});
    }
    nlohmann::json cameraKeys = nlohmann::json::array();
    for (const CinematicCameraKeyUVE& key : component.cameraKeys) {
        cameraKeys.push_back({{"timeSeconds", key.timeSeconds},
                              {"position", ToJsonUVE(key.position)},
                              {"rotation", ToJsonUVE(key.rotation)}});
    }
    nlohmann::json animationKeys = nlohmann::json::array();
    for (const CinematicAnimationKeyUVE& key : component.animationKeys) {
        // The target rides the parallel animTargetLocalIds array (entity reference); the clip is a
        // plain guid, the same {"clip", value} spelling the sequencer codec uses.
        animationKeys.push_back({{"timeSeconds", key.timeSeconds}, {"clip", key.clip.value}});
    }
    nlohmann::json audioKeys = nlohmann::json::array();
    for (const CinematicAudioKeyUVE& key : component.audioKeys) {
        audioKeys.push_back({{"timeSeconds", key.timeSeconds},
                             {"audioAssetPath", key.audioAssetPath},
                             {"volume", key.volume},
                             {"spatial", key.spatial},
                             {"position", ToJsonUVE(key.position)},
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
[[nodiscard]] CinematicComponentUVE CinematicObjectFromJsonUVE(const nlohmann::json& json) {
    CinematicComponentUVE value;
    value.durationSeconds = json.value("durationSeconds", 0.0);
    value.autoplay = json.value("autoplay", false);
    value.speed = json.value("speed", 1.0F);
    value.loopMode = static_cast<CinematicLoopModeUVE>(
        json.value("loopMode", static_cast<std::uint8_t>(CinematicLoopModeUVE::Once)));
    if (const auto events = json.find("events"); events != json.end() && events->is_array()) {
        for (const nlohmann::json& entry : *events) {
            CinematicEventKeyUVE key;
            key.timeSeconds = entry.value("timeSeconds", 0.0);
            key.eventId = entry.value("eventId", std::string{});
            value.events.push_back(std::move(key));
        }
    }
    if (const auto cuts = json.find("cuts"); cuts != json.end() && cuts->is_array()) {
        for (const nlohmann::json& entry : *cuts) {
            CinematicCameraCutUVE cut;
            cut.timeSeconds = entry.value("timeSeconds", 0.0);
            value.cuts.push_back(std::move(cut));
        }
    }
    if (const auto keys = json.find("cameraKeys"); keys != json.end() && keys->is_array()) {
        for (const nlohmann::json& entry : *keys) {
            CinematicCameraKeyUVE key;
            key.timeSeconds = entry.value("timeSeconds", 0.0);
            key.position = Vector3FromJsonUVE(entry.at("position"));
            key.rotation = QuaternionFromJsonUVE(entry.at("rotation"));
            value.cameraKeys.push_back(std::move(key));
        }
    }
    if (const auto animKeys = json.find("animationKeys");
         animKeys != json.end() && animKeys->is_array()) {
        for (const nlohmann::json& entry : *animKeys) {
            CinematicAnimationKeyUVE key;
            key.timeSeconds = entry.value("timeSeconds", 0.0);
            key.clip = Asset::AssetGuidUVE{entry.value("clip", std::uint64_t{0})};
            value.animationKeys.push_back(std::move(key));
        }
    }
    if (const auto soundKeys = json.find("audioKeys");
         soundKeys != json.end() && soundKeys->is_array()) {
        for (const nlohmann::json& entry : *soundKeys) {
            CinematicAudioKeyUVE key;
            key.timeSeconds = entry.value("timeSeconds", 0.0);
            key.audioAssetPath = entry.value("audioAssetPath", std::string{});
            key.volume = entry.value("volume", 1.0F);
            key.spatial = entry.value("spatial", false);
            if (const auto position = entry.find("position"); position != entry.end()) {
                key.position = Vector3FromJsonUVE(*position);
            }
            key.minDistance = entry.value("minDistance", 1.0F);
            key.maxDistance = entry.value("maxDistance", 25.0F);
            value.audioKeys.push_back(std::move(key));
        }
    }
    return value;
}

template <typename KeyUVE>
void ResolveParallelTargetsUVE(std::vector<KeyUVE>& keys, EntityUVE KeyUVE::*member,
                               const nlohmann::json& json, const char* sidecar,
                               const std::unordered_map<std::uint32_t, EntityUVE>& localIdToEntity) {
    const nlohmann::json ids = json.value(sidecar, nlohmann::json::array());
    std::vector<KeyUVE> resolved;
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
[[nodiscard]] CinematicComponentUVE CinematicComponentWithResolvedReferencesUVE(
    const nlohmann::json& json, const std::unordered_map<std::uint32_t, EntityUVE>& localIdToEntity) {
    CinematicComponentUVE value = CinematicObjectFromJsonUVE(json);
    ResolveParallelTargetsUVE(value.cuts, &CinematicCameraCutUVE::camera, json, "cutCameraLocalIds",
                              localIdToEntity);
    ResolveParallelTargetsUVE(value.animationKeys, &CinematicAnimationKeyUVE::target, json,
                              "animTargetLocalIds", localIdToEntity);
    return value;
}

[[nodiscard]] nlohmann::json ToJsonUVE(const AnimationGraphComponentUVE& component) {
    nlohmann::json parameters = nlohmann::json::array();
    for (const AnimationParameterUVE& parameter : component.parameters) {
        parameters.push_back({{"name", parameter.name},
                              {"type", static_cast<std::uint8_t>(parameter.type)},
                              {"value", parameter.value}});
    }
    nlohmann::json nodes = nlohmann::json::array();
    for (const AnimationGraphNodeUVE& object : component.nodes) {
        nlohmann::json statePositions = nlohmann::json::array();
        for (const Math::Vector2UVE& at : object.statePositions) {
            statePositions.push_back({at.x, at.y});
        }
        nlohmann::json transitions = nlohmann::json::array();
        for (const AnimationGraphTransitionUVE& transition : object.transitions) {
            nlohmann::json conditions = nlohmann::json::array();
            for (const AnimationTransitionConditionUVE& test : transition.conditions) {
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
        for (const AnimationBlendPointUVE& point : object.blendPoints) {
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
                         {"anyPosition", {object.anyPosition.x, object.anyPosition.y}}});
    }
    return {{"parameters", std::move(parameters)},
            {"nodes", std::move(nodes)}};
}

/// Reads a tree. A tree saved as the earlier two-clip blend becomes the same thing as a graph:
/// Output fed by a Blend2 of Clip A and Clip B, weighted by a "blend" parameter.
[[nodiscard]] AnimationGraphComponentUVE AnimationGraphFromJsonUVE(const nlohmann::json& json) {
    AnimationGraphComponentUVE tree;
    if (!json.contains("nodes") && (json.contains("clipA") || json.contains("clipB"))) {
        tree.parameters = {AnimationParameterUVE{"blend", AnimationParameterTypeUVE::Float, json.value("blend", 0.0F)}};
        const auto makeObject = [](const std::uint32_t id, const AnimationGraphNodeKindUVE kind, std::string name) {
            AnimationGraphNodeUVE object;
            object.id = id;
            object.kind = kind;
            object.name = std::move(name);
            return object;
        };
        AnimationGraphNodeUVE output = makeObject(1U, AnimationGraphNodeKindUVE::Output, "Output");
        output.inputs = {2U};
        output.position = Math::Vector2UVE{480.0F, 0.0F};
        AnimationGraphNodeUVE blend = makeObject(2U, AnimationGraphNodeKindUVE::Blend2, "Blend");
        blend.inputs = {3U, 4U};
        blend.parameter = "blend";
        blend.position = Math::Vector2UVE{240.0F, 0.0F};
        const float speed = json.value("speed", 1.0F);
        AnimationGraphNodeUVE clipA = makeObject(3U, AnimationGraphNodeKindUVE::Clip, "Clip A");
        clipA.clip = Asset::AssetGuidUVE{json.value("clipA", std::uint64_t{0})};
        clipA.speed = speed;
        AnimationGraphNodeUVE clipB = makeObject(4U, AnimationGraphNodeKindUVE::Clip, "Clip B");
        clipB.clip = Asset::AssetGuidUVE{json.value("clipB", std::uint64_t{0})};
        clipB.speed = speed;
        clipB.position = Math::Vector2UVE{0.0F, 120.0F};
        tree.nodes = {output, blend, clipA, clipB};
        return tree;
    }
    tree.parameters.clear();
    for (const nlohmann::json& item : json.value("parameters", nlohmann::json::array())) {
        tree.parameters.push_back(AnimationParameterUVE{item.at("name").get<std::string>(),
                                                        static_cast<AnimationParameterTypeUVE>(
                                                            item.value("type", std::uint8_t{0})),
                                                        item.value("value", 0.0F)});
    }
    if (json.contains("nodes")) {
        tree.nodes.clear();
        // Older saves placed a blend space's animations on its inputs, positioned by "points" (1D)
        // or "points2D"; they are folded into its own points once every object is read.
        std::vector<std::vector<Math::Vector2UVE>> legacyPositions;
        std::vector<std::uint32_t> fitArea; // ids of spaces saved before they had an area
        for (const nlohmann::json& item : json.at("nodes")) {
            AnimationGraphNodeUVE object;
            object.id = item.at("id").get<std::uint32_t>();
            object.kind = static_cast<AnimationGraphNodeKindUVE>(item.at("kind").get<std::uint8_t>());
            object.name = item.value("name", std::string{});
            const std::vector<float> position = item.value("position", std::vector<float>{0.0F, 0.0F});
            if (position.size() == 2U) {
                object.position = Math::Vector2UVE{position[0], position[1]};
            }
            object.inputs = item.value("inputs", std::vector<std::uint32_t>{});
            object.clip = Asset::AssetGuidUVE{item.value("clip", std::uint64_t{0})};
            object.loop = item.value("loop", true);
            object.speed = item.value("speed", 1.0F);
            object.parameter = item.value("parameter", std::string{});
            object.value = item.value("value", 0.5F);
            object.fadeSeconds = item.value("fadeSeconds", 0.2F);
            object.sync = item.value("sync", false);
            object.blendMode = static_cast<AnimationBlendModeUVE>(item.value("blendMode", std::uint8_t{0}));
            object.smoothingSeconds = item.value("smoothingSeconds", 0.0F);
            object.parameterY = item.value("parameterY", std::string{});
            object.valueY = item.value("valueY", 0.0F);
            object.bones = item.value("bones", std::vector<std::string>{});
            object.restart = item.value("restart", true);
            const auto readPair = [&item](const char* key, Math::Vector2UVE& out) {
                if (const auto found = item.find(key); found != item.end() && found->is_array() && found->size() == 2U) {
                    out = Math::Vector2UVE{(*found)[0].get<float>(), (*found)[1].get<float>()};
                }
            };
            readPair("areaMin", object.areaMin);
            readPair("areaMax", object.areaMax);
            std::vector<Math::Vector2UVE> legacy;
            for (const float x : item.value("points", std::vector<float>{})) {
                legacy.push_back(Math::Vector2UVE{x, 0.0F});
            }
            if (const auto found = item.find("points2D"); found != item.end() && found->is_array()) {
                for (const auto& point : *found) {
                    if (point.is_array() && point.size() == 2U) {
                        legacy.push_back(Math::Vector2UVE{point[0].get<float>(), point[1].get<float>()});
                    }
                }
            }
            legacyPositions.push_back(std::move(legacy));
            if (!item.contains("areaMin")) {
                fitArea.push_back(object.id);
            }
            if (const auto found = item.find("blendPoints"); found != item.end() && found->is_array()) {
                for (const auto& pointJson : *found) {
                    AnimationBlendPointUVE point;
                    const std::vector<float> at = pointJson.value("position", std::vector<float>{0.0F, 0.0F});
                    if (at.size() == 2U) {
                        point.position = Math::Vector2UVE{at[0], at[1]};
                    }
                    point.clip = Asset::AssetGuidUVE{pointJson.value("clip", std::uint64_t{0})};
                    point.speed = pointJson.value("speed", 1.0F);
                    point.loop = pointJson.value("loop", true);
                    object.blendPoints.push_back(point);
                }
            }
            object.entryState = item.value("entryState", std::uint32_t{0});
            for (const nlohmann::json& transitionJson : item.value("transitions", nlohmann::json::array())) {
                AnimationGraphTransitionUVE transition;
                transition.fromState = transitionJson.value("from", kAnyAnimationStateUVE);
                transition.toState = transitionJson.value("to", std::uint32_t{0});
                if (const auto list = transitionJson.find("conditions"); list != transitionJson.end() && list->is_array()) {
                    for (const nlohmann::json& testJson : *list) {
                        AnimationTransitionConditionUVE test;
                        test.condition = static_cast<AnimationConditionUVE>(testJson.value("condition", std::uint8_t{4}));
                        test.parameter = testJson.value("parameter", std::string{});
                        test.threshold = testJson.value("threshold", 0.0F);
                        transition.conditions.push_back(std::move(test));
                    }
                } else {
                    // Saved when a transition had one condition; Always is no condition at all.
                    const auto condition =
                        static_cast<AnimationConditionUVE>(transitionJson.value("condition", std::uint8_t{0}));
                    if (condition != AnimationConditionUVE::Always) {
                        transition.conditions.push_back(AnimationTransitionConditionUVE{
                            condition, transitionJson.value("parameter", std::string{}), transitionJson.value("threshold", 0.0F)});
                    }
                }
                transition.exitPhase = transitionJson.value("exitPhase", -1.0F);
                transition.start = static_cast<AnimationTransitionStartUVE>(transitionJson.value("start", std::uint8_t{0}));
                transition.curve = static_cast<AnimationTransitionCurveUVE>(transitionJson.value("curve", std::uint8_t{0}));
                transition.interruptible = transitionJson.value("interruptible", true);
                transition.enabled = transitionJson.value("enabled", true);
                transition.fadeSeconds = transitionJson.value("fadeSeconds", 0.2F);
                object.transitions.push_back(std::move(transition));
            }
            for (const nlohmann::json& at : item.value("statePositions", nlohmann::json::array())) {
                if (at.is_array() && at.size() == 2U) {
                    object.statePositions.push_back(Math::Vector2UVE{at[0].get<float>(), at[1].get<float>()});
                }
            }
            readPair("entryPosition", object.entryPosition);
            readPair("anyPosition", object.anyPosition);
            tree.nodes.push_back(std::move(object));
        }
        static_cast<void>(MigrateBlendSpaceInputsUVE(tree.nodes, legacyPositions));
        // A space saved before it had an area gets one around its points.
        for (AnimationGraphNodeUVE& object : tree.nodes) {
            if (std::find(fitArea.begin(), fitArea.end(), object.id) == fitArea.end() || object.blendPoints.empty() ||
                (object.kind != AnimationGraphNodeKindUVE::BlendSpace1D && object.kind != AnimationGraphNodeKindUVE::BlendSpace2D)) {
                continue;
            }
            Math::Vector2UVE lo = object.blendPoints.front().position;
            Math::Vector2UVE hi = lo;
            for (const AnimationBlendPointUVE& point : object.blendPoints) {
                lo = Math::Vector2UVE{std::min(lo.x, point.position.x), std::min(lo.y, point.position.y)};
                hi = Math::Vector2UVE{std::max(hi.x, point.position.x), std::max(hi.y, point.position.y)};
            }
            const float marginX = std::max((hi.x - lo.x) * 0.15F, 0.5F);
            const float marginY = std::max((hi.y - lo.y) * 0.15F, 0.5F);
            object.areaMin = Math::Vector2UVE{lo.x - marginX, lo.y - marginY};
            object.areaMax = Math::Vector2UVE{hi.x + marginX, hi.y + marginY};
        }
    }
    return tree;
}









[[nodiscard]] nlohmann::json ToJsonUVE(const CharacterControllerComponentUVE& component) {
    return {{"motionMode", static_cast<std::uint8_t>(component.motionMode)},
            {"upDirection", ToJsonUVE(component.upDirection)},
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
            {"velocity", ToJsonUVE(component.velocity)},
            {"grounded", component.grounded},
            {"isOnCeiling", component.isOnCeiling},
            {"floorNormal", ToJsonUVE(component.floorNormal)},
            {"timeSinceOnFloor", component.timeSinceOnFloor},
            {"jumpBufferRemaining", component.jumpBufferRemaining}};
}



[[nodiscard]] nlohmann::json ToJsonUVE(const UIButtonComponentUVE& component) {
    return {{"positionPixels", ToJsonUVE(component.rect.position)},
            {"sizePixels", ToJsonUVE(component.rect.size)},
            {"normalColor", ToJsonUVE(component.normalColor)},
            {"hoverColor", ToJsonUVE(component.hoverColor)},
            {"pressedColor", ToJsonUVE(component.pressedColor)},
            {"isHovered", component.isHovered},
            {"wasClickedThisFrame", component.wasClickedThisFrame}};
}


[[nodiscard]] nlohmann::json ToJsonUVE(const ScriptComponentUVE& component) {
    nlohmann::json json{{"scriptAssetPath", component.scriptAssetPath}};
    if (!component.exportValues.empty()) {
        json["exportValues"] = component.exportValues;
    }
    return json;
}










// ---- VariantUVE <-> JSON ------------------------------------------------------------------------
//
// {"type": "<name>", "value": <payload>}. The type is written by name (GetVariantTypeNameUVE), never
// by enumerator number, so the Variant type list can grow or reorder without invalidating a single
// saved scene. Vectors are arrays of numbers; Dictionaries are arrays of {key, value} so authored
// order survives, for the same reason metadata entries are.

[[nodiscard]] nlohmann::json VariantToJsonUVE(const Core::VariantUVE& value);

template <typename VectorT>
[[nodiscard]] nlohmann::json ComponentsToJsonUVE(const VectorT& vector) {
    if constexpr (std::is_same_v<VectorT, Math::Vector2UVE>) {
        return nlohmann::json::array({vector.x, vector.y});
    } else if constexpr (std::is_same_v<VectorT, Math::Vector3UVE>) {
        return nlohmann::json::array({vector.x, vector.y, vector.z});
    } else if constexpr (std::is_same_v<VectorT, Core::VariantColorUVE>) {
        return nlohmann::json::array({vector.r, vector.g, vector.b, vector.a});
    } else {
        return nlohmann::json::array({vector.x, vector.y, vector.z, vector.w});
    }
}

[[nodiscard]] nlohmann::json VariantPayloadToJsonUVE(const Core::VariantUVE& value) {
    return std::visit(
        [](const auto& stored) -> nlohmann::json {
            using StoredT = std::decay_t<decltype(stored)>;
            if constexpr (std::is_same_v<StoredT, Math::Vector2UVE> || std::is_same_v<StoredT, Math::Vector3UVE> ||
                          std::is_same_v<StoredT, Core::VariantVector4UVE> ||
                          std::is_same_v<StoredT, Math::QuaternionUVE> ||
                          std::is_same_v<StoredT, Core::VariantColorUVE>) {
                return ComponentsToJsonUVE(stored);
            } else if constexpr (std::is_same_v<StoredT, std::vector<Core::VariantUVE>>) {
                nlohmann::json elements = nlohmann::json::array();
                for (const Core::VariantUVE& element : stored) {
                    elements.push_back(VariantToJsonUVE(element));
                }
                return elements;
            } else if constexpr (std::is_same_v<StoredT, std::vector<Core::VariantDictionaryEntryUVE>>) {
                nlohmann::json entries = nlohmann::json::array();
                for (const Core::VariantDictionaryEntryUVE& entry : stored) {
                    entries.push_back({{"key", entry.key}, {"value", VariantToJsonUVE(entry.value)}});
                }
                return entries;
            } else if constexpr (std::is_same_v<StoredT, std::vector<Math::Vector2UVE>> ||
                                 std::is_same_v<StoredT, std::vector<Math::Vector3UVE>> ||
                                 std::is_same_v<StoredT, std::vector<Core::VariantColorUVE>>) {
                nlohmann::json elements = nlohmann::json::array();
                for (const auto& element : stored) {
                    elements.push_back(ComponentsToJsonUVE(element));
                }
                return elements;
            } else {
                // bool, integers, double, string, and packed arrays of those: nlohmann writes each as
                // its natural JSON form.
                return stored;
            }
        },
        value.GetStorageUVE());
}

[[nodiscard]] nlohmann::json VariantToJsonUVE(const Core::VariantUVE& value) {
    return {{"type", std::string{Core::GetVariantTypeNameUVE(value.GetTypeUVE())}},
            {"value", VariantPayloadToJsonUVE(value)}};
}

[[nodiscard]] float ReadFloatUVE(const nlohmann::json& json) {
    if (!json.is_number()) {
        throw std::runtime_error("Variant component is not a number");
    }
    return json.get<float>();
}

template <typename VectorT>
[[nodiscard]] VectorT ComponentsFromJsonUVE(const nlohmann::json& json) {
    constexpr std::size_t count =
        std::is_same_v<VectorT, Math::Vector2UVE> ? 2U : (std::is_same_v<VectorT, Math::Vector3UVE> ? 3U : 4U);
    if (!json.is_array() || json.size() != count) {
        throw std::runtime_error("Variant vector has the wrong number of components");
    }
    if constexpr (std::is_same_v<VectorT, Math::Vector2UVE>) {
        return {ReadFloatUVE(json[0]), ReadFloatUVE(json[1])};
    } else if constexpr (std::is_same_v<VectorT, Math::Vector3UVE>) {
        return {ReadFloatUVE(json[0]), ReadFloatUVE(json[1]), ReadFloatUVE(json[2])};
    } else if constexpr (std::is_same_v<VectorT, Core::VariantColorUVE>) {
        return {ReadFloatUVE(json[0]), ReadFloatUVE(json[1]), ReadFloatUVE(json[2]), ReadFloatUVE(json[3])};
    } else {
        return {ReadFloatUVE(json[0]), ReadFloatUVE(json[1]), ReadFloatUVE(json[2]), ReadFloatUVE(json[3])};
    }
}

/// Decodes one Variant, throwing on anything malformed - the serializer turns that into a rolled-back
/// load. `depth` stops the recursion at the Variant depth bound, so a hostile file nested thousands
/// deep cannot exhaust the stack before validation ever runs.
[[nodiscard]] Core::VariantUVE VariantFromJsonUVE(const nlohmann::json& json, const std::size_t depth) {
    if (depth > Core::kMaximumVariantDepthUVE) {
        throw std::runtime_error("Variant nesting exceeds the depth limit");
    }
    if (!json.is_object() || !json.contains("type") || !json.contains("value")) {
        throw std::runtime_error("Variant is not a {type, value} object");
    }
    const std::optional<Core::VariantTypeUVE> type = Core::TryParseVariantTypeNameUVE(json.at("type").get<std::string>());
    if (!type.has_value()) {
        throw std::runtime_error("Variant has an unknown type");
    }
    const nlohmann::json& payload = json.at("value");
    Core::VariantUVE value = Core::VariantUVE::MakeDefaultUVE(*type);
    const auto readList = [&payload](auto& list, const auto& readElement) {
        if (!payload.is_array() || payload.size() > Core::kMaximumVariantElementsUVE) {
            throw std::runtime_error("Variant array payload is not a bounded array");
        }
        list.reserve(payload.size());
        for (const nlohmann::json& element : payload) {
            list.push_back(readElement(element));
        }
    };
    value.VisitMutableUVE(
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
            } else if constexpr (std::is_same_v<StoredT, Math::Vector2UVE> ||
                                 std::is_same_v<StoredT, Math::Vector3UVE> ||
                                 std::is_same_v<StoredT, Core::VariantVector4UVE> ||
                                 std::is_same_v<StoredT, Math::QuaternionUVE> ||
                                 std::is_same_v<StoredT, Core::VariantColorUVE>) {
                stored = ComponentsFromJsonUVE<StoredT>(payload);
            } else if constexpr (std::is_same_v<StoredT, std::vector<Core::VariantUVE>>) {
                readList(stored, [depth](const nlohmann::json& element) {
                    return VariantFromJsonUVE(element, depth + 1U);
                });
            } else if constexpr (std::is_same_v<StoredT, std::vector<Core::VariantDictionaryEntryUVE>>) {
                readList(stored, [depth](const nlohmann::json& entry) {
                    return Core::VariantDictionaryEntryUVE{entry.at("key").get<std::string>(),
                                                          VariantFromJsonUVE(entry.at("value"), depth + 1U)};
                });
            } else if constexpr (std::is_same_v<StoredT, std::vector<Math::Vector2UVE>> ||
                                 std::is_same_v<StoredT, std::vector<Math::Vector3UVE>> ||
                                 std::is_same_v<StoredT, std::vector<Core::VariantColorUVE>>) {
                using ElementT = typename StoredT::value_type;
                readList(stored, [](const nlohmann::json& element) { return ComponentsFromJsonUVE<ElementT>(element); });
            } else {
                using ElementT = typename StoredT::value_type;
                readList(stored, [](const nlohmann::json& element) { return element.get<ElementT>(); });
            }
        });
    return value;
}

[[nodiscard]] nlohmann::json ToJsonUVE(const ObjectMetadataComponentUVE& component) {
    // An array of key/value objects rather than one JSON object, so authored order survives a
    // round trip - a JSON object's member order is not something a reader is obliged to keep, and
    // losing it would reorder an inspector's rows and dirty a scene file for no reason.
    nlohmann::json entries = nlohmann::json::array();
    for (const ObjectMetadataEntryUVE& entry : component.entries) {
        entries.push_back({{"key", entry.key}, {"value", VariantToJsonUVE(entry.value)}});
    }
    return {{"entries", std::move(entries)}};
}

[[nodiscard]] nlohmann::json ToJsonUVE(const EditorDescriptionComponentUVE& component) {
    return {{"description", component.description}};
}

[[nodiscard]] nlohmann::json ToJsonUVE(const FolderComponentUVE&) {
    return nlohmann::json::object();
}

[[nodiscard]] nlohmann::json ToJsonUVE(const OutlinerViewport3DComponentUVE& component) {
    // Only a 2D level says so. Writing nothing for 3D keeps every 3D .uvscene byte-for-byte what it
    // was before dimensions were stored, and a file that says nothing is 3D by definition - which is
    // exactly what every scene written before this existed is.
    if (component.dimension == ViewportDimensionUVE::TwoD) {
        return {{"dimension", "2d"}};
    }
    return nlohmann::json::object();
}

[[nodiscard]] nlohmann::json ToJsonUVE(const SceneRootComponentUVE&) {
    return nlohmann::json::object(); // pure marker: no authored state to persist
}

// The type is written by its stable id ("box_mesh_3d"), never the enum's number, so reordering
// the enum can never retype a saved object.
[[nodiscard]] nlohmann::json ToJsonUVE(const SceneObjectTypeComponentUVE& value) {
    return {{"type", std::string{Objects::GetSceneObjectTypeIdUVE(value.kind)}}};
}

[[nodiscard]] nlohmann::json ToJsonUVE(const RayCast3DComponentUVE& value) {
    // `exclusions` is deliberately absent: it holds entity references, and only the entity-aware
    // encoder below knows which file-local id each referenced entity has. Writing the raw handles
    // here is what the old format did, and a raw handle is meaningless in the next session - the
    // pool is free to hand that index to anything.
    return {{"direction", ToJsonUVE(value.direction)},
            {"length", value.length},
            {"collisionMask", value.collisionMask},
            {"enabled", value.enabled}};
}

[[nodiscard]] RayCast3DComponentUVE RayCast3DObjectFromJsonUVE(const nlohmann::json& json) {
    // The registration reader is used everywhere a RayCast3D payload is validated WITHOUT an entity
    // id table - including the scene decoder's validation pass, which only needs to know the
    // payload is well-formed. Entity references are resolved by the entity-aware restore path
    // below, which is the only code that knows which file-local id maps to which restored entity;
    // a legacy `exclusions` array of raw handles is tolerated and dropped, because a raw handle
    // cannot be mapped to anything meaningful.
    RayCast3DComponentUVE value;
    value.direction = Vector3FromJsonUVE(json.at("direction"));
    value.length = json.value("length", 100.0F);
    value.collisionMask = json.value("collisionMask", std::uint32_t{0xFFFFFFFFU});
    value.enabled = json.value("enabled", true);
    return value;
}

[[nodiscard]] ControllerComponentUVE ControllerObjectFromJsonUVE(const nlohmann::json& json) {
    ControllerComponentUVE value;
    value.kind = static_cast<ControllerKindUVE>(json.value("kind", 0U));
    return value;
}

/// The entity-aware half of reading a Controller: its pawn is an entity reference, so only the
/// caller that owns the file's local-id table can resolve it. An unresolvable link drops to
/// invalid - an unpossessed controller loads fine, a guessed one would drive a stranger.
[[nodiscard]] ControllerComponentUVE ControllerComponentWithResolvedPawnUVE(
    const nlohmann::json& json,
    const std::unordered_map<std::uint32_t, EntityUVE>& localIdToEntity) {
    ControllerComponentUVE value = ControllerObjectFromJsonUVE(json);
    const std::int64_t localId =
        json.value("pawnLocalId", static_cast<std::int64_t>(std::numeric_limits<std::uint32_t>::max()));
    if (localId < 0 ||
        static_cast<std::uint64_t>(localId) > std::numeric_limits<std::uint32_t>::max()) {
        throw std::runtime_error("ControllerComponentUVE pawn local ID is outside the uint32 range");
    }
    const auto targetIt = localIdToEntity.find(static_cast<std::uint32_t>(localId));
    value.pawn = targetIt != localIdToEntity.end() ? targetIt->second : kInvalidEntityUVE;
    return value;
}

[[nodiscard]] PawnComponentUVE PawnObjectFromJsonUVE(const nlohmann::json& json) {
    PawnComponentUVE value;
    if (const auto input = json.find("input"); input != json.end() && input->is_object()) {
        value.input.move = Vector3FromJsonUVE(input->at("move"));
        value.input.rise = input->value("rise", 0.0F);
        value.input.jumpPressed = input->value("jumpPressed", false);
        value.input.lookPointer = Vector2FromJsonUVE(input->at("lookPointer"));
        value.input.lookStick = Vector2FromJsonUVE(input->at("lookStick"));
        value.input.interactPressed = input->value("interactPressed", false);
    }
    return value;
}

/// The entity-aware half of reading a Pawn, mirroring the controller's: the link drops to
/// invalid when the file does not contain it.
[[nodiscard]] PawnComponentUVE PawnComponentWithResolvedControllerUVE(
    const nlohmann::json& json,
    const std::unordered_map<std::uint32_t, EntityUVE>& localIdToEntity) {
    PawnComponentUVE value = PawnObjectFromJsonUVE(json);
    const std::int64_t localId = json.value(
        "controllerLocalId", static_cast<std::int64_t>(std::numeric_limits<std::uint32_t>::max()));
    if (localId < 0 ||
        static_cast<std::uint64_t>(localId) > std::numeric_limits<std::uint32_t>::max()) {
        throw std::runtime_error("PawnComponentUVE controller local ID is outside the uint32 range");
    }
    const auto targetIt = localIdToEntity.find(static_cast<std::uint32_t>(localId));
    value.controller = targetIt != localIdToEntity.end() ? targetIt->second : kInvalidEntityUVE;
    return value;
}

[[nodiscard]] CameraFollowComponentUVE CameraFollowObjectFromJsonUVE(const nlohmann::json& json) {
    CameraFollowComponentUVE value;
    value.offset =
        json.contains("offset") ? Vector3FromJsonUVE(json.at("offset")) : Math::Vector3UVE{};
    value.followPossessedPawn = json.value("followPossessedPawn", false);
    return value;
}

/// The entity-aware half of reading a CameraFollow, mirroring the pawn's: the link drops to
/// invalid when the file does not contain it.
[[nodiscard]] CameraFollowComponentUVE CameraFollowComponentWithResolvedTargetUVE(
    const nlohmann::json& json,
    const std::unordered_map<std::uint32_t, EntityUVE>& localIdToEntity) {
    CameraFollowComponentUVE value = CameraFollowObjectFromJsonUVE(json);
    const std::int64_t localId = json.value(
        "targetLocalId", static_cast<std::int64_t>(std::numeric_limits<std::uint32_t>::max()));
    if (localId < 0 ||
        static_cast<std::uint64_t>(localId) > std::numeric_limits<std::uint32_t>::max()) {
        throw std::runtime_error("CameraFollowComponentUVE target local ID is outside the uint32 range");
    }
    const auto targetIt = localIdToEntity.find(static_cast<std::uint32_t>(localId));
    value.target = targetIt != localIdToEntity.end() ? targetIt->second : kInvalidEntityUVE;
    return value;
}

/// The entity-aware half of reading a RayCast3D: its exclusions are entity references, so only the
/// caller that owns the file's local-id table can resolve them. A reference the file does not
/// contain is DROPPED rather than turned back into a raw handle - the same rule the animation
/// target and the visibility parent follow - and the exclusions that did resolve keep their order,
/// so a scene that lost one still loads with the rest instead of losing the whole component.
[[nodiscard]] RayCast3DComponentUVE RayCast3DComponentWithResolvedExclusionsUVE(
    const nlohmann::json& json,
    const std::unordered_map<std::uint32_t, EntityUVE>& localIdToEntity) {
    RayCast3DComponentUVE value = RayCast3DObjectFromJsonUVE(json);
    const nlohmann::json exclusionIds = json.value("exclusionsLocalIds", nlohmann::json::array());
    if (!exclusionIds.is_array() || exclusionIds.size() > kMaximumRayCastExclusionsUVE) {
        throw std::runtime_error("RayCast3DComponentUVE exclusionsLocalIds must be a bounded array");
    }
    std::size_t resolvedCount = 0U;
    for (const auto& exclusionId : exclusionIds) {
        const std::int64_t localId = exclusionId.get<std::int64_t>();
        if (localId < 0 || static_cast<std::uint64_t>(localId) > std::numeric_limits<std::uint32_t>::max()) {
            throw std::runtime_error("RayCast3DComponentUVE exclusion local ID is outside the uint32 range");
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
    if (!IsRayCast3DObjectComponentValidUVE(value)) {
        throw std::runtime_error("Invalid RayCast3DComponentUVE payload");
    }
    return value;
}



[[nodiscard]] nlohmann::json ToJsonUVE(const NavMeshVolume3DComponentUVE& value) {
    // `rebuildRequested` is a request to the running navigation runtime, not a fact about the level:
    // a scene reloaded tomorrow has nothing to rebuild, so it is not written.
    return {{"boundsHalfExtents", ToJsonUVE(value.boundsHalfExtents)},
            {"navigationMeshAssetPath", value.navigationMeshAssetPath},
            {"navigationLayers", value.navigationLayers},
            {"cellSize", value.cellSize},
            {"agentRadius", value.agentRadius},
            {"agentHeight", value.agentHeight},
            {"maximumSlopeDegrees", value.maximumSlopeDegrees},
            {"maximumStepHeight", value.maximumStepHeight},
            {"enabled", value.enabled}};
}

[[nodiscard]] NavMeshVolume3DComponentUVE NavMeshVolume3DObjectFromJsonUVE(const nlohmann::json& json) {
    NavMeshVolume3DComponentUVE value;
    value.boundsHalfExtents = Vector3FromJsonUVE(json.at("boundsHalfExtents"));
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



[[nodiscard]] nlohmann::json ToJsonUVE(const Skeleton3DComponentUVE& value) {
    nlohmann::json bones = nlohmann::json::array();
    for (const SkeletonBoneUVE& bone : value.bones) {
        bones.push_back({{"name", bone.name},
                         {"parentIndex", bone.parentIndex},
                         {"localPosition", ToJsonUVE(bone.localPosition)},
                         {"localRotation", ToJsonUVE(bone.localRotation)},
                         {"localScale", ToJsonUVE(bone.localScale)}});
    }
    return {{"skeletonAssetPath", value.skeletonAssetPath}, {"bones", std::move(bones)}, {"enabled", value.enabled}};
}

[[nodiscard]] Skeleton3DComponentUVE Skeleton3DObjectFromJsonUVE(const nlohmann::json& json) {
    Skeleton3DComponentUVE value;
    value.skeletonAssetPath = json.value("skeletonAssetPath", std::string{});
    const nlohmann::json bones = json.value("bones", nlohmann::json::array());
    if (!bones.is_array() || bones.size() > kMaximumSkeletonBonesUVE) {
        throw std::runtime_error("Skeleton3DComponentUVE bones must be a bounded array");
    }
    value.bones.reserve(bones.size());
    for (const nlohmann::json& boneJson : bones) {
        value.bones.push_back(SkeletonBoneUVE{boneJson.at("name").get<std::string>(),
                                              boneJson.value("parentIndex", -1),
                                              Vector3FromJsonUVE(boneJson.at("localPosition")),
                                              QuaternionFromJsonUVE(boneJson.at("localRotation")),
                                              Vector3FromJsonUVE(boneJson.at("localScale"))});
    }
    value.enabled = json.value("enabled", true);
    return value;
}

/// The `skeletonLocalId` key is deliberately NOT written here: the skeleton is an entity reference,
/// and only the save pass can turn one into a file-local id (see the reference pass in SaveUVE,
/// which writes that key from the component's resolved reference for every saved attachment).
[[nodiscard]] nlohmann::json ToJsonUVE(const BoneAttachment3DComponentUVE& value) {
    return {{"boneIndex", value.boneIndex},
            {"boneName", value.boneName},
            {"localPosition", ToJsonUVE(value.localPosition)},
            {"localRotation", ToJsonUVE(value.localRotation)},
            {"localScale", ToJsonUVE(value.localScale)},
            {"enabled", value.enabled}};
}

[[nodiscard]] BoneAttachment3DComponentUVE BoneAttachment3DObjectFromJsonUVE(const nlohmann::json& json) {
    BoneAttachment3DComponentUVE value;
    // The skeleton reference itself is left unset here: it is a file-local id in the document, and
    // the load pass below is what turns it into an entity. An id this file does not contain leaves
    // the attachment inert rather than binding it to whatever entity shares that id elsewhere.
    value.boneIndex = json.value("boneIndex", kInvalidSkeletonBoneIndexUVE);
    value.boneName = json.value("boneName", std::string{});
    value.localPosition = Vector3FromJsonUVE(json.at("localPosition"));
    value.localRotation = QuaternionFromJsonUVE(json.at("localRotation"));
    value.localScale = Vector3FromJsonUVE(json.at("localScale"));
    value.enabled = json.value("enabled", true);
    return value;
}

/// The load-side half of the reference: the authored file-local id becomes the entity it names.
[[nodiscard]] BoneAttachment3DComponentUVE BoneAttachment3DComponentWithResolvedSkeletonUVE(
    const nlohmann::json& json, const std::unordered_map<std::uint32_t, EntityUVE>& localIdToEntity) {
    BoneAttachment3DComponentUVE value = BoneAttachment3DObjectFromJsonUVE(json);
    const std::uint32_t skeletonLocalId =
        json.value("skeletonLocalId", std::numeric_limits<std::uint32_t>::max());
    const auto skeletonIt = localIdToEntity.find(skeletonLocalId);
    value.skeleton = skeletonIt != localIdToEntity.end() ? skeletonIt->second : kInvalidEntityUVE;
    return value;
}

/// The three `*LocalId` keys are deliberately NOT written here - they are entity references, and
/// only the save pass can turn one into a file-local id (see the reference pass in SaveUVE, which
/// writes them from the component's resolved references for every saved chain).
[[nodiscard]] nlohmann::json ToJsonUVE(const TwoBoneIK3DComponentUVE& value) {
    return {{"rootBoneIndex", value.rootBoneIndex},
            {"rootBoneName", value.rootBoneName},
            {"middleBoneIndex", value.middleBoneIndex},
            {"middleBoneName", value.middleBoneName},
            {"endBoneIndex", value.endBoneIndex},
            {"endBoneName", value.endBoneName},
            {"targetPosition", ToJsonUVE(value.targetPosition)},
            {"poleDirection", ToJsonUVE(value.poleDirection)},
            {"enabled", value.enabled}};
}

[[nodiscard]] TwoBoneIK3DComponentUVE TwoBoneIK3DObjectFromJsonUVE(const nlohmann::json& json) {
    TwoBoneIK3DComponentUVE value;
    // The references themselves are left unset here: they are file-local ids in the document, and
    // the load pass below turns them into entities. An id this file does not contain leaves that
    // reference inert rather than pointing at whatever entity shares the id elsewhere.
    value.rootBoneIndex = json.value("rootBoneIndex", kInvalidSkeletonBoneIndexUVE);
    value.rootBoneName = json.value("rootBoneName", std::string{});
    value.middleBoneIndex = json.value("middleBoneIndex", kInvalidSkeletonBoneIndexUVE);
    value.middleBoneName = json.value("middleBoneName", std::string{});
    value.endBoneIndex = json.value("endBoneIndex", kInvalidSkeletonBoneIndexUVE);
    value.endBoneName = json.value("endBoneName", std::string{});
    value.targetPosition = Vector3FromJsonUVE(json.at("targetPosition"));
    value.poleDirection = Vector3FromJsonUVE(json.at("poleDirection"));
    value.enabled = json.value("enabled", true);
    return value;
}

/// The load-side half of every reference a chain has: an authored file-local id becomes the entity
/// it names, and one that names nothing in this file stays the sentinel - which the solver reads as
/// "the authored target position is what I solve toward".
[[nodiscard]] TwoBoneIK3DComponentUVE TwoBoneIK3DComponentWithResolvedReferencesUVE(
    const nlohmann::json& json, const std::unordered_map<std::uint32_t, EntityUVE>& localIdToEntity) {
    TwoBoneIK3DComponentUVE value = TwoBoneIK3DObjectFromJsonUVE(json);
    const auto resolve = [&json, &localIdToEntity](const char* key) {
        const std::uint32_t localId =
            json.value(key, std::numeric_limits<std::uint32_t>::max());
        const auto found = localIdToEntity.find(localId);
        return found != localIdToEntity.end() ? found->second : kInvalidEntityUVE;
    };
    value.skeleton = resolve("skeletonLocalId");
    value.target = resolve("targetLocalId");
    value.poleTarget = resolve("poleLocalId");
    return value;
}

[[nodiscard]] nlohmann::json ToJsonUVE(const FullBodyIK3DComponentUVE& value) {
    nlohmann::json effectors = nlohmann::json::array();
    for (const FullBodyIKEffectorUVE& effector : value.effectors) {
        effectors.push_back({{"boneIndex", effector.boneIndex},
                             {"boneName", effector.boneName},
                             {"targetPosition", ToJsonUVE(effector.targetPosition)},
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

[[nodiscard]] FullBodyIK3DComponentUVE FullBodyIK3DObjectFromJsonUVE(const nlohmann::json& json) {
    FullBodyIK3DComponentUVE value;
    // The references themselves are left unset here: they are file-local ids in the document, and
    // the load pass below turns them into entities. An id this file does not contain leaves that
    // reference inert rather than pointing at whatever entity shares the id elsewhere.
    value.rootBoneIndex = json.value("rootBoneIndex", kInvalidSkeletonBoneIndexUVE);
    value.rootBoneName = json.value("rootBoneName", std::string{});
    const auto effectors = json.find("effectors");
    if (effectors != json.end() && effectors->is_array()) {
        for (const nlohmann::json& entry : *effectors) {
            FullBodyIKEffectorUVE effector;
            effector.boneIndex = entry.value("boneIndex", kInvalidSkeletonBoneIndexUVE);
            effector.boneName = entry.value("boneName", std::string{});
            effector.targetPosition = Vector3FromJsonUVE(entry.at("targetPosition"));
            effector.positionWeight = entry.value("positionWeight", 1.0F);
            value.effectors.push_back(std::move(effector));
        }
    }
    value.maxIterations = json.value("maxIterations", FullBodyIK3DComponentUVE{}.maxIterations);
    value.toleranceMetres = json.value("toleranceMetres", FullBodyIK3DComponentUVE{}.toleranceMetres);
    value.rootTranslationWeight =
        json.value("rootTranslationWeight", FullBodyIK3DComponentUVE{}.rootTranslationWeight);
    value.maxJointStepRadians =
        json.value("maxJointStepRadians", FullBodyIK3DComponentUVE{}.maxJointStepRadians);
    value.enabled = json.value("enabled", true);
    return value;
}

/// The load-side half of every reference a solve has: an authored file-local id becomes the entity
/// it names, and one that names nothing in this file stays the sentinel - which the solver reads as
/// "the authored target position is what I solve toward". Effector targets resolve inside their own
/// entries so one gone target leaves that limb inert instead of shifting every other one.
[[nodiscard]] FullBodyIK3DComponentUVE FullBodyIK3DComponentWithResolvedReferencesUVE(
    const nlohmann::json& json, const std::unordered_map<std::uint32_t, EntityUVE>& localIdToEntity) {
    FullBodyIK3DComponentUVE value = FullBodyIK3DObjectFromJsonUVE(json);
    const auto resolve = [&json, &localIdToEntity](const char* key) {
        const std::uint32_t localId =
            json.value(key, std::numeric_limits<std::uint32_t>::max());
        const auto found = localIdToEntity.find(localId);
        return found != localIdToEntity.end() ? found->second : kInvalidEntityUVE;
    };
    value.skeleton = resolve("skeletonLocalId");
    const auto effectors = json.find("effectors");
    if (effectors != json.end() && effectors->is_array()) {
        for (std::size_t index = 0U; index < value.effectors.size() && index < effectors->size(); ++index) {
            const std::uint32_t localId =
                (*effectors)[index].value("targetLocalId", std::numeric_limits<std::uint32_t>::max());
            const auto found = localIdToEntity.find(localId);
            value.effectors[index].target =
                found != localIdToEntity.end() ? found->second : kInvalidEntityUVE;
        }
    }
    return value;
}



[[nodiscard]] nlohmann::json ToJsonUVE(const Marker3DComponentUVE& value) {
    return {{"markerName", value.markerName},
            {"localPosition", ToJsonUVE(value.localPosition)},
            {"localRotation", ToJsonUVE(value.localRotation)},
            {"enabled", value.enabled}};
}

[[nodiscard]] Marker3DComponentUVE Marker3DObjectFromJsonUVE(const nlohmann::json& json) {
    return Marker3DComponentUVE{json.value("markerName", std::string{"Marker"}),
                                    Vector3FromJsonUVE(json.at("localPosition")),
                                    QuaternionFromJsonUVE(json.at("localRotation")),
                                    json.value("enabled", true)};
}

[[nodiscard]] nlohmann::json ToJsonUVE(const Hitbox3DComponentUVE& value) {
    return {{"halfExtents", ToJsonUVE(value.halfExtents)},
            {"collisionLayer", value.collisionLayer},
            {"collisionMask", value.collisionMask},
            {"damageChannel", value.damageChannel},
            {"enabled", value.enabled}};
}

[[nodiscard]] Hitbox3DComponentUVE Hitbox3DObjectFromJsonUVE(const nlohmann::json& json) {
    return Hitbox3DComponentUVE{Vector3FromJsonUVE(json.at("halfExtents")),
                                    json.value("collisionLayer", std::uint32_t{1}),
                                    json.value("collisionMask", std::uint32_t{0xFFFFFFFFU}),
                                    json.value("damageChannel", std::string{"default"}),
                                    json.value("enabled", true)};
}

[[nodiscard]] Hitbox3DComponentUVE Hitbox3DComponentWithResolvedIgnoreUVE(
    const nlohmann::json& json, const std::unordered_map<std::uint32_t, EntityUVE>& localIdToEntity) {
    Hitbox3DComponentUVE value = Hitbox3DObjectFromJsonUVE(json);
    const std::int64_t ignoreLocalId = json.value("ignoreLocalId", static_cast<std::int64_t>(-1));
    if (ignoreLocalId >= 0 &&
        static_cast<std::uint64_t>(ignoreLocalId) <= std::numeric_limits<std::uint32_t>::max()) {
        const auto ignoreIt = localIdToEntity.find(static_cast<std::uint32_t>(ignoreLocalId));
        if (ignoreIt != localIdToEntity.end()) {
            value.ignoreEntity = ignoreIt->second;
        }
    }
    if (!IsHitbox3DObjectComponentValidUVE(value)) {
        throw std::runtime_error("Invalid Hitbox3DComponentUVE payload");
    }
    return value;
}

[[nodiscard]] nlohmann::json ToJsonUVE(const Hurtbox3DComponentUVE& value) {
    return {{"halfExtents", ToJsonUVE(value.halfExtents)},
            {"collisionLayer", value.collisionLayer},
            {"collisionMask", value.collisionMask},
            {"damageChannel", value.damageChannel},
            {"enabled", value.enabled}};
}

[[nodiscard]] Hurtbox3DComponentUVE Hurtbox3DObjectFromJsonUVE(const nlohmann::json& json) {
    return Hurtbox3DComponentUVE{Vector3FromJsonUVE(json.at("halfExtents")),
                                     json.value("collisionLayer", std::uint32_t{1}),
                                     json.value("collisionMask", std::uint32_t{0xFFFFFFFFU}),
                                     json.value("damageChannel", std::string{"default"}),
                                     json.value("enabled", true)};
}

[[nodiscard]] Hurtbox3DComponentUVE Hurtbox3DComponentWithResolvedIgnoreUVE(
    const nlohmann::json& json, const std::unordered_map<std::uint32_t, EntityUVE>& localIdToEntity) {
    Hurtbox3DComponentUVE value = Hurtbox3DObjectFromJsonUVE(json);
    const std::int64_t ignoreLocalId = json.value("ignoreLocalId", static_cast<std::int64_t>(-1));
    if (ignoreLocalId >= 0 &&
        static_cast<std::uint64_t>(ignoreLocalId) <= std::numeric_limits<std::uint32_t>::max()) {
        const auto ignoreIt = localIdToEntity.find(static_cast<std::uint32_t>(ignoreLocalId));
        if (ignoreIt != localIdToEntity.end()) {
            value.ignoreEntity = ignoreIt->second;
        }
    }
    if (!IsHurtbox3DObjectComponentValidUVE(value)) {
        throw std::runtime_error("Invalid Hurtbox3DComponentUVE payload");
    }
    return value;
}

[[nodiscard]] nlohmann::json ToJsonUVE(const Projectile3DComponentUVE& value) {
    // `remainingLifetime` and the runtime hit result are state, not authored data: the countdown
    // is re-armed from `maxLifetime` on load, and where this projectile last landed says nothing
    // about the one a freshly loaded scene creates.
    return {{"velocity", ToJsonUVE(value.velocity)},
            {"acceleration", ToJsonUVE(value.acceleration)},
            {"radius", value.radius},
            {"maxLifetime", value.maxLifetime},
            {"collisionMask", value.collisionMask},
            {"active", value.active},
            {"hitPolicy", static_cast<std::uint32_t>(value.hitPolicy)},
            {"restitution", value.restitution},
            {"friction", value.friction}};
}

[[nodiscard]] Projectile3DComponentUVE Projectile3DObjectFromJsonUVE(const nlohmann::json& json) {
    Projectile3DComponentUVE value;
    value.velocity = Vector3FromJsonUVE(json.at("velocity"));
    value.acceleration = Vector3FromJsonUVE(json.at("acceleration"));
    value.radius = json.value("radius", 0.1F);
    value.maxLifetime = json.value("maxLifetime", 10.0F);
    value.remainingLifetime = value.maxLifetime;
    value.collisionMask = json.value("collisionMask", std::uint32_t{0xFFFFFFFFU});
    value.active = json.value("active", true);
    // A file written before these fields existed loads with the authored defaults; an unknown
    // policy is left as it decoded and then refused by the component's validator, never guessed
    // at, so a hand-edited file cannot make a projectile behave like a policy that is not a policy.
    value.hitPolicy = static_cast<Projectile3DHitPolicyUVE>(json.value("hitPolicy", std::uint32_t{0}));
    value.restitution = json.value("restitution", 0.5F);
    value.friction = json.value("friction", 0.2F);
    return value;
}

[[nodiscard]] Projectile3DComponentUVE Projectile3DComponentWithResolvedIgnoreUVE(
    const nlohmann::json& json, const std::unordered_map<std::uint32_t, EntityUVE>& localIdToEntity) {
    Projectile3DComponentUVE value = Projectile3DObjectFromJsonUVE(json);
    const std::int64_t ignoreLocalId = json.value("ignoreLocalId", static_cast<std::int64_t>(-1));
    if (ignoreLocalId >= 0 &&
        static_cast<std::uint64_t>(ignoreLocalId) <= std::numeric_limits<std::uint32_t>::max()) {
        const auto ignoreIt = localIdToEntity.find(static_cast<std::uint32_t>(ignoreLocalId));
        if (ignoreIt != localIdToEntity.end()) {
            value.ignoreEntity = ignoreIt->second;
        }
    }
    if (!IsProjectile3DObjectComponentValidUVE(value)) {
        throw std::runtime_error("Invalid Projectile3DComponentUVE payload");
    }
    return value;
}

[[nodiscard]] nlohmann::json ToJsonUVE(const InteractionArea3DComponentUVE& value) {
    return {{"halfExtents", ToJsonUVE(value.halfExtents)},
            {"collisionLayer", value.collisionLayer},
            {"collisionMask", value.collisionMask},
            {"interactionTag", value.interactionTag},
            {"maximumCandidates", value.maximumCandidates},
            {"enabled", value.enabled}};
}

[[nodiscard]] InteractionArea3DComponentUVE InteractionArea3DObjectFromJsonUVE(const nlohmann::json& json) {
    return InteractionArea3DComponentUVE{Vector3FromJsonUVE(json.at("halfExtents")),
                                             json.value("collisionLayer", std::uint32_t{1}),
                                             json.value("collisionMask", std::uint32_t{0xFFFFFFFFU}),
                                             json.value("interactionTag", std::string{"interactable"}),
                                             json.value("maximumCandidates", std::uint32_t{16}),
                                             json.value("enabled", true)};
}

[[nodiscard]] InteractionArea3DComponentUVE InteractionArea3DComponentWithResolvedIgnoreUVE(
    const nlohmann::json& json, const std::unordered_map<std::uint32_t, EntityUVE>& localIdToEntity) {
    InteractionArea3DComponentUVE value = InteractionArea3DObjectFromJsonUVE(json);
    const std::int64_t ignoreLocalId = json.value("ignoreLocalId", static_cast<std::int64_t>(-1));
    if (ignoreLocalId >= 0 &&
        static_cast<std::uint64_t>(ignoreLocalId) <= std::numeric_limits<std::uint32_t>::max()) {
        const auto ignoreIt = localIdToEntity.find(static_cast<std::uint32_t>(ignoreLocalId));
        if (ignoreIt != localIdToEntity.end()) {
            value.ignoreEntity = ignoreIt->second;
        }
    }
    if (!IsInteractionArea3DObjectComponentValidUVE(value)) {
        throw std::runtime_error("Invalid InteractionArea3DComponentUVE payload");
    }
    return value;
}



[[nodiscard]] nlohmann::json ToJsonUVE(const Sky3DComponentUVE& value) {
    return {{"source", static_cast<std::uint8_t>(value.source)},
            {"skyColor", ToJsonUVE(value.skyColor)},
            {"horizonColor", ToJsonUVE(value.horizonColor)},
            {"groundColor", ToJsonUVE(value.groundColor)},
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
            {"enabled", value.enabled}};
}

[[nodiscard]] Sky3DComponentUVE Sky3DFromJsonUVE(const nlohmann::json& json) {
    Sky3DComponentUVE value;
    value.source = static_cast<SkySourceUVE>(json.value("source", std::uint8_t{0}));
    value.skyColor = Vector3FromJsonUVE(json.at("skyColor"));
    value.horizonColor = Vector3FromJsonUVE(json.at("horizonColor"));
    value.groundColor = Vector3FromJsonUVE(json.at("groundColor"));
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
    value.enabled = json.value("enabled", true);
    return value;
}

[[nodiscard]] nlohmann::json ToJsonUVE(const ReflectionProbe3DComponentUVE& value) {
    return {{"size", ToJsonUVE(value.size)},
            {"visibilityLayers", value.visibilityLayers},
            {"updateMode", static_cast<std::uint8_t>(value.updateMode)},
            {"resolution", static_cast<std::uint8_t>(value.resolution)},
            {"enabled", value.enabled}};
}

[[nodiscard]] ReflectionProbe3DComponentUVE ReflectionProbe3DObjectFromJsonUVE(const nlohmann::json& json) {
    ReflectionProbe3DComponentUVE value;
    value.size = Vector3FromJsonUVE(json.at("size"));
    value.visibilityLayers = json.value("visibilityLayers", std::uint32_t{0xFFFFFFFFU});
    value.updateMode = static_cast<ReflectionProbeUpdateModeUVE>(json.value("updateMode", std::uint8_t{0}));
    value.resolution = static_cast<ReflectionProbeResolutionUVE>(json.value(
        "resolution", static_cast<std::uint8_t>(ReflectionProbeResolutionUVE::Medium)));
    value.enabled = json.value("enabled", true);
    return value;
}











[[nodiscard]] nlohmann::json ToJsonUVE(const LodGroup3DComponentUVE& value) {
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

[[nodiscard]] LodGroup3DComponentUVE LodGroup3DObjectFromJsonUVE(const nlohmann::json& json) {
    LodGroup3DComponentUVE value;
    const nlohmann::json thresholds = json.value("distanceThresholds", nlohmann::json::array());
    if (!thresholds.is_array() || thresholds.empty() || thresholds.size() > kMaximumLodLevelsUVE) {
        throw std::runtime_error("LodGroup3DComponentUVE thresholds must be a bounded non-empty array");
    }
    value.levelCount = static_cast<std::uint8_t>(thresholds.size());
    for (std::size_t index = 0U; index < thresholds.size(); ++index) {
        value.distanceThresholds[index] = thresholds.at(index).get<float>();
    }
    // A level mesh array is optional - a group that overrides no levels writes none - but it is
    // bounded when present, and shorter than levelCount simply leaves the remaining levels on the
    // MeshComponentUVE fallback.
    const nlohmann::json lodMeshes = json.value("lodMeshGuids", nlohmann::json::array());
    if (!lodMeshes.is_array() || lodMeshes.size() > kMaximumLodLevelsUVE) {
        throw std::runtime_error("LodGroup3DComponentUVE lodMeshGuids must be a bounded array");
    }
    for (std::size_t index = 0U; index < lodMeshes.size(); ++index) {
        value.lodMeshGuids[index] = Asset::AssetGuidUVE{lodMeshes.at(index).get<std::uint64_t>()};
    }
    value.hysteresis = json.value("hysteresis", 0.0F);
    value.enabled = json.value("enabled", true);
    return value;
}











[[nodiscard]] nlohmann::json ToJsonUVE(const LevelStreamer3DComponentUVE& value) {
    return {{"levelPath", value.levelPath},
            {"loadDistance", value.loadDistance},
            {"unloadDistance", value.unloadDistance},
            {"enabled", value.enabled}};
}

[[nodiscard]] LevelStreamer3DComponentUVE LevelStreamer3DObjectFromJsonUVE(const nlohmann::json& json) {
    return LevelStreamer3DComponentUVE{json.value("levelPath", std::string{}),
                                           json.value("loadDistance", 250.0F),
                                           json.value("unloadDistance", 300.0F),
                                           json.value("enabled", false), false, false};
}

[[nodiscard]] nlohmann::json ToJsonUVE(const WorldPartition3DComponentUVE& value) {
    return {{"cellSize", value.cellSize},
            {"cellCounts", {value.cellCounts[0], value.cellCounts[1], value.cellCounts[2]}},
            {"maximumLoadedCells", value.maximumLoadedCells},
            {"enabled", value.enabled}};
}

[[nodiscard]] WorldPartition3DComponentUVE WorldPartition3DObjectFromJsonUVE(const nlohmann::json& json) {
    WorldPartition3DComponentUVE value;
    value.cellSize = json.value("cellSize", 128.0F);
    const nlohmann::json counts = json.value("cellCounts", nlohmann::json::array({16U, 1U, 16U}));
    if (!counts.is_array() || counts.size() != 3U) {
        throw std::runtime_error("WorldPartition3DComponentUVE cellCounts must contain three values");
    }
    for (std::size_t index = 0U; index < value.cellCounts.size(); ++index) {
        value.cellCounts[index] = counts.at(index).get<std::uint32_t>();
    }
    value.maximumLoadedCells = json.value("maximumLoadedCells", std::uint32_t{64});
    value.enabled = json.value("enabled", true);
    return value;
}

[[nodiscard]] nlohmann::json ToJsonUVE(const PrefabPropertyOverrideUVE& override) {
    return {{"propertyPath", override.propertyPath}, {"serializedValue", override.serializedValue}};
}

[[nodiscard]] nlohmann::json ToJsonUVE(const PrefabInstanceComponentUVE& component) {
    nlohmann::json overrides = nlohmann::json::array();
    for (const PrefabPropertyOverrideUVE& override : component.overrides) {
        overrides.push_back(ToJsonUVE(override));
    }
    return {{"sourcePrefabGuid", component.sourcePrefabGuid.value},
            {"sourceRevision", component.sourceRevision},
            {"instanceRevision", component.instanceRevision},
            {"overrides", std::move(overrides)}};
}

[[nodiscard]] PrefabInstanceComponentUVE PrefabInstanceFromJsonUVE(const nlohmann::json& json) {
    const nlohmann::json overridesJson = json.value("overrides", nlohmann::json::array());
    if (!overridesJson.is_array()) {
        throw std::runtime_error("PrefabInstanceComponentUVE overrides must be an array");
    }

    std::vector<PrefabPropertyOverrideUVE> overrides;
    overrides.reserve(overridesJson.size());
    for (const nlohmann::json& overrideJson : overridesJson) {
        if (!overrideJson.is_object()) {
            throw std::runtime_error("PrefabInstanceComponentUVE override must be an object");
        }
        overrides.push_back(PrefabPropertyOverrideUVE{
            overrideJson.at("propertyPath").get<std::string>(),
            overrideJson.at("serializedValue").get<std::string>()});
    }

    const PrefabInstanceComponentUVE instance{
        Asset::AssetGuidUVE{json.at("sourcePrefabGuid").get<std::uint64_t>()}, std::move(overrides),
        json.value("sourceRevision", 1ULL), json.value("instanceRevision", 1ULL)};
    if (!IsPrefabInstanceComponentValidUVE(instance)) {
        throw std::runtime_error("Invalid PrefabInstanceComponentUVE payload");
    }
    return instance;
}

/// One entry in the component-serializer table: `isValid` checks the authored component before
/// `toJson` reads it; `fromJson` adds a fresh component (built from `json`) to `entity`.
struct ComponentRegistrationUVE {
    std::type_index typeIndex;
    std::function<nlohmann::json(IEntityManagerUVE&, EntityUVE)> toJson;
    std::function<bool(IEntityManagerUVE&, EntityUVE)> isValid;
    std::function<void(IEntityManagerUVE&, EntityUVE, const nlohmann::json&)> fromJson;
};

// Restored hand-written registrations (Tier 1.6 reseed holdouts - see the ledger in
// GetRegistrationsByNameUVE): these readers reseed runtime state from authored values on
// load, which the property codec cannot express. They live here as one cluster so the
// next migration audit finds them in one place.

[[nodiscard]] nlohmann::json ToJsonUVE(const Decal3DComponentUVE& value) {
    return {{"materialAssetPath", value.materialAssetPath},
            {"size", ToJsonUVE(value.size)},
            {"projection", static_cast<std::uint8_t>(value.projection)},
            {"lifetime", value.lifetime},
            {"enabled", value.enabled},
            {"modulate", ToJsonUVE(value.modulate)},
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

[[nodiscard]] Decal3DComponentUVE Decal3DObjectFromJsonUVE(const nlohmann::json& json) {
    // Every field after `enabled` arrived later; a decal saved before them reads with its defaults.
    const Decal3DComponentUVE defaults{};
    Decal3DComponentUVE value{};
    value.materialAssetPath = json.value("materialAssetPath", std::string{});
    value.size = Vector3FromJsonUVE(json.at("size"));
    value.projection = static_cast<DecalProjectionModeUVE>(json.value("projection", std::uint8_t{0}));
    value.lifetime = json.value("lifetime", 0.0F);
    value.enabled = json.value("enabled", true);
    value.modulate = json.contains("modulate") ? Vector3FromJsonUVE(json.at("modulate")) : defaults.modulate;
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

[[nodiscard]] nlohmann::json ToJsonUVE(const HealthComponentUVE& value) {
    return {{"maxHealth", value.maxHealth}, {"invulnerable", value.invulnerable}};
}

[[nodiscard]] HealthComponentUVE HealthFromJsonUVE(const nlohmann::json& json) {
    HealthComponentUVE value{};
    value.maxHealth = json.value("maxHealth", value.maxHealth);
    value.invulnerable = json.value("invulnerable", value.invulnerable);
    value.health = value.maxHealth;
    return value;
}

[[nodiscard]] nlohmann::json ToJsonUVE(const SpringArm3DComponentUVE& value) {
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
[[nodiscard]] nlohmann::json ToJsonUVE(const MorphTargetComponentUVE& value) {
    nlohmann::json weightsJson = nlohmann::json::array();
    for (const MorphTargetWeightUVE& entry : value.weights) {
        weightsJson.push_back({{"target", entry.target}, {"weight", entry.weight}});
    }
    return nlohmann::json{{"enabled", value.enabled}, {"weights", std::move(weightsJson)}};
}

[[nodiscard]] MorphTargetComponentUVE MorphTargetObjectFromJsonUVE(const nlohmann::json& json) {
    MorphTargetComponentUVE value;
    value.enabled = json.value("enabled", true);
    if (json.contains("weights")) {
        if (!json.at("weights").is_array() || json.at("weights").size() > kMaximumMorphTargetWeightsUVE) {
            throw std::runtime_error("MorphTargetComponentUVE payload has an impossible weights array");
        }
        value.weights.reserve(json.at("weights").size());
        for (const auto& entry : json.at("weights")) {
            if (!entry.is_object() || !entry.contains("target") || !entry.at("target").is_string()) {
                throw std::runtime_error("MorphTargetComponentUVE payload has a weight without a target");
            }
            MorphTargetWeightUVE weight;
            weight.target = entry.at("target").get<std::string>();
            if (entry.contains("weight")) {
                if (!entry.at("weight").is_number()) {
                    throw std::runtime_error("MorphTargetComponentUVE payload has a non-numeric weight");
                }
                weight.weight = entry.at("weight").get<float>();
            }
            value.weights.push_back(std::move(weight));
        }
    }
    if (!IsMorphTargetComponentValidUVE(value)) {
        throw std::runtime_error("Invalid MorphTargetComponentUVE payload");
    }
    return value;
}

[[nodiscard]] SpringArm3DComponentUVE SpringArm3DObjectFromJsonUVE(const nlohmann::json& json) {
    SpringArm3DComponentUVE value;
    value.armLength = json.value("armLength", 4.0F);
    value.margin = json.value("margin", 0.1F);
    value.smoothing = json.value("smoothing", 8.0F);
    value.collisionMask = json.value("collisionMask", std::uint32_t{0xFFFFFFFFU});
    value.currentLength = value.armLength;
    value.enabled = json.value("enabled", true);
    return value;
}

template <typename T, typename FromJsonFunc, typename ValidateFunc>
[[nodiscard]] ComponentRegistrationUVE MakeRegistrationUVE(FromJsonFunc fromJsonFunc, ValidateFunc validateFunc) {
    return ComponentRegistrationUVE{
        std::type_index(typeid(T)),
        [](IEntityManagerUVE& entityManager, EntityUVE entity) -> nlohmann::json {
            return ToJsonUVE(entityManager.GetComponentUVE<T>(entity));
        },
        [validateFunc](IEntityManagerUVE& entityManager, EntityUVE entity) {
            return validateFunc(entityManager.GetComponentUVE<T>(entity));
        },
        [fromJsonFunc](IEntityManagerUVE& entityManager, EntityUVE entity, const nlohmann::json& json) {
            entityManager.AddComponentUVE<T>(entity, fromJsonFunc(json));
        },
    };
}

/// Older names of components, mapped to the name they are registered under now. Reading a saved
/// document goes through this, so a component renamed in code still loads files written before the
/// rename; writing always uses the current name. Same rule as the scene-object registry's legacy
/// type ids: every rename leaves its previous name readable here forever.
[[nodiscard]] std::string_view CanonicalComponentNameUVE(const std::string_view name) noexcept {
    static const std::unordered_map<std::string_view, std::string_view> kLegacyNames{
        // The scene-object pass: the word "Node" left these names, nothing else changed.
        {"SceneNodeTypeComponentUVE", "SceneObjectTypeComponentUVE"},
        {"NodeMetadataComponentUVE", "ObjectMetadataComponentUVE"},
        {"AnimatableBody3DNodeComponentUVE", "Kinematic3DComponentUVE"},
        {"BoneAttachment3DNodeComponentUVE", "BoneAttachment3DComponentUVE"},
        {"Decal3DNodeComponentUVE", "Decal3DComponentUVE"},
        {"FogVolume3DNodeComponentUVE", "FogVolume3DComponentUVE"},
        {"Hitbox3DNodeComponentUVE", "Hitbox3DComponentUVE"},
        {"Hurtbox3DNodeComponentUVE", "Hurtbox3DComponentUVE"},
        {"InteractionArea3DNodeComponentUVE", "InteractionArea3DComponentUVE"},
        {"LevelStreamer3DNodeComponentUVE", "LevelStreamer3DComponentUVE"},
        {"LodGroup3DNodeComponentUVE", "LodGroup3DComponentUVE"},
        {"Marker3DNodeComponentUVE", "Marker3DComponentUVE"},
        {"NavigationAgent3DNodeComponentUVE", "NavSeeker3DComponentUVE"},
        {"NavigationRegion3DNodeComponentUVE", "NavMeshVolume3DComponentUVE"},
        {"Occluder3DNodeComponentUVE", "Occluder3DComponentUVE"},
        {"Projectile3DNodeComponentUVE", "Projectile3DComponentUVE"},
        {"RayCast3DNodeComponentUVE", "RayCast3DComponentUVE"},
        {"ReflectionProbe3DNodeComponentUVE", "ReflectionProbe3DComponentUVE"},
        {"Skeleton3DNodeComponentUVE", "Skeleton3DComponentUVE"},
        {"SpawnPoint3DNodeComponentUVE", "SpawnPoint3DComponentUVE"},
        {"SpringArm3DNodeComponentUVE", "SpringArm3DComponentUVE"},
        {"VisibilityRegion3DNodeComponentUVE", "VisibilityRegion3DComponentUVE"},
        {"WorldEnvironment3DNodeComponentUVE", "WorldEnvironment3DComponentUVE"},
        {"WorldPartition3DNodeComponentUVE", "WorldPartition3DComponentUVE"},
        // The component pass: four components kept the name their scene-object kind had already
        // dropped, so the Add-Object list said "AnimationGraph" while the row it created stored an
        // "AnimationTreeComponentUVE". The kinds' names won. A document written before this rename
        // carries the left-hand name and still loads; writing always uses the right-hand one.
        {"AnimationTreeComponentUVE", "AnimationGraphComponentUVE"},
        {"AnimationPlayerComponentUVE", "AnimationSequencerComponentUVE"},
        {"RigidBodyComponentUVE", "Rigid3DComponentUVE"},
        {"AnimatableBody3DComponentUVE", "Kinematic3DComponentUVE"},
        // The AnimationMixer pass: it never mixed anything, it is the base AnimationSequencer and
        // AnimationGraph share - what they move, which channels, on which clock, how fast. The
        // engine's other base components are named for what they make an object (PhysicsObject3D,
        // RenderInstance3D, LightEmitter3D), so this one follows them instead of borrowing a name.
        {"AnimationMixerComponentUVE", "AnimationDriverComponentUVE"},
        // The navigation pass: those two were the last kinds named after another engine's own
        // navigation classes. A document written before this carries the left-hand name.
        {"NavigationRegion3DComponentUVE", "NavMeshVolume3DComponentUVE"},
        {"NavigationAgent3DComponentUVE", "NavSeeker3DComponentUVE"},
        // The viewport pass: the 2D arrival renamed the level's Viewport to Viewport3D, and its
        // marker component followed. A document written before this carries the left-hand name.
        {"OutlinerViewportComponentUVE", "OutlinerViewport3DComponentUVE"},
    };
    const auto it = kLegacyNames.find(name);
    return it != kLegacyNames.end() ? it->second : name;
}

// --- Metadata-driven (de)serialization ------------------------------------------------------
//
// Components with a generic registration need no hand-written ToJsonUVE/fromJson: the component
// metadata registry already describes every property (name, type id, accessors), and the entry's
// factory builds and attaches the instance. Tier 1.5 proved it on Canvas; Tier 1.6 discovers
// every qualifying entry automatically (see the loop at the end of GetRegistrationsByNameUVE),
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
// {"position", "size"} under its own key, since the legacy pair has room for exactly one. The skip rule is exactly TypeMetadataPropertyUVE::IsSerializedUVE() (no
// runtime state, no editor-only authoring, no unbound properties). A missing key on read keeps
// the factory default - the same leniency as the hand-written json.value(key, default) reads,
// and what lets old files load after a property is added. A property whose type id has no codec
// yet throws naming the property: silent data loss is worse than a loud migration error. (The
// qualification check below means that throw only fires when a hand-written reader is deleted
// for an entry the codec cannot express - a migration bug, caught by the round-trip tests.)

void MetadataPropertyWriteUVE(const Core::TypeMetadataEntryUVE& entry,
                              const Core::TypeMetadataPropertyUVE& property, const void* instance,
                              nlohmann::json& json) {
    if (property.typeId == kPropertyTypeBoolUVE) {
        bool value = false;
        property.getValue(instance, &value);
        json[property.name] = value;
    } else if (property.typeId == kPropertyTypeFloatUVE) {
        float value = 0.0F;
        property.getValue(instance, &value);
        json[property.name] = value;
    } else if (property.typeId == kPropertyTypeInt32UVE) {
        std::int32_t value = 0;
        property.getValue(instance, &value);
        json[property.name] = value;
    } else if (property.typeId == kPropertyTypeUInt32UVE) {
        std::uint32_t value = 0U;
        property.getValue(instance, &value);
        json[property.name] = value;
    } else if (property.typeId == kPropertyTypeUInt8UVE) {
        std::uint8_t value = 0U;
        property.getValue(instance, &value);
        json[property.name] = value;
    } else if (property.typeId == kPropertyTypeBitMask32UVE) {
        std::uint32_t value = 0U;
        property.getValue(instance, &value);
        json[property.name] = value;
    } else if (property.typeId == kPropertyTypeStringUVE) {
        std::string value;
        property.getValue(instance, &value);
        json[property.name] = value;
    } else if (property.typeId == kPropertyTypeVector2UVE) {
        Math::Vector2UVE value{};
        property.getValue(instance, &value);
        json[property.name] = ToJsonUVE(value);
    } else if (property.typeId == kPropertyTypeVector3UVE) {
        Math::Vector3UVE value{};
        property.getValue(instance, &value);
        json[property.name] = ToJsonUVE(value);
    } else if (property.typeId == kPropertyTypeColorUVE || property.typeId == kPropertyTypeLinearColorUVE) {
        // One JSON shape ([x, y, z] linear values) for both colour ids: they differ only in the
        // inspector, and the byte-stability rule serializes linear values raw.
        Math::ColorUVE value{};
        property.getValue(instance, &value);
        json[property.name] = ToJsonUVE(value);
    } else if (property.typeId == kPropertyTypeQuaternionUVE) {
        Math::QuaternionUVE value{};
        property.getValue(instance, &value);
        json[property.name] = ToJsonUVE(value);
    } else if (property.typeId == kPropertyTypeEnumUVE) {
        // Enum accessors speak std::int64_t (see MakeEnumPropertyUVE): the stored shape is a
        // plain number, the enumerator mapping lives in the bound accessors.
        std::int64_t value = 0;
        property.getValue(instance, &value);
        json[property.name] = value;
    } else if (property.typeId == kPropertyTypeStringListUVE) {
        // Written exactly as the component holds it: a save/load pair that changed a value would
        // make every round-trip comparison a special case. The canonical form - no trailing
        // empties - is enforced where the list is edited and by the component's sanitizer, not
        // here, because a codec that rewrites its input is not a codec.
        std::vector<std::string> value;
        property.getValue(instance, &value);
        json[property.name] = value;
    } else if (property.typeId == kPropertyTypeAssetGuidUVE) {
        Asset::AssetGuidUVE value{};
        property.getValue(instance, &value);
        json[property.name] = value.value;
    } else if (property.typeId == kPropertyTypeRectUVE) {
        Math::RectUVE value{};
        property.getValue(instance, &value);
        if (property.name == "rect") {
            // The legacy pair: a Rect writes two keys, not one, keeping the established UI
            // encoding byte-identical (see the Tier 1.6 decision: positionPixels/sizePixels).
            json["positionPixels"] = ToJsonUVE(value.position);
            json["sizePixels"] = ToJsonUVE(value.size);
        } else {
            // A named Rect - a component's second Rect onward - nests under its own key: the
            // legacy pair above has room for exactly one, and sharing it would silently corrupt
            // every Rect but the last.
            json[property.name] = {{"position", ToJsonUVE(value.position)}, {"size", ToJsonUVE(value.size)}};
        }
    } else {
        throw std::runtime_error("Cannot serialize property '" + property.name + "' of '" + entry.displayName +
                                 "': no metadata codec for its type");
    }
}

/// A property counts as present when the document carries its key - or, for a Rect, either half
/// of its legacy pair. Absent keys keep their factory defaults (see the block comment above).
[[nodiscard]] bool MetadataPropertyPresentUVE(const Core::TypeMetadataPropertyUVE& property,
                                              const nlohmann::json& json) {
    if (property.typeId == kPropertyTypeRectUVE && property.name == "rect") {
        return json.contains("positionPixels") || json.contains("sizePixels");
    }
    return json.contains(property.name);
}

void MetadataPropertyReadUVE(const Core::TypeMetadataEntryUVE& entry,
                             const Core::TypeMetadataPropertyUVE& property, const nlohmann::json& json,
                             void* instance) {
    if (property.typeId == kPropertyTypeBoolUVE) {
        const bool value = json.at(property.name).get<bool>();
        property.setValue(instance, &value);
    } else if (property.typeId == kPropertyTypeFloatUVE) {
        const float value = json.at(property.name).get<float>();
        property.setValue(instance, &value);
    } else if (property.typeId == kPropertyTypeInt32UVE) {
        const std::int32_t value = json.at(property.name).get<std::int32_t>();
        property.setValue(instance, &value);
    } else if (property.typeId == kPropertyTypeUInt32UVE) {
        const std::uint32_t value = json.at(property.name).get<std::uint32_t>();
        property.setValue(instance, &value);
    } else if (property.typeId == kPropertyTypeUInt8UVE) {
        const std::uint8_t value = json.at(property.name).get<std::uint8_t>();
        property.setValue(instance, &value);
    } else if (property.typeId == kPropertyTypeBitMask32UVE) {
        const std::uint32_t value = json.at(property.name).get<std::uint32_t>();
        property.setValue(instance, &value);
    } else if (property.typeId == kPropertyTypeStringUVE) {
        const std::string value = json.at(property.name).get<std::string>();
        property.setValue(instance, &value);
    } else if (property.typeId == kPropertyTypeVector2UVE) {
        const Math::Vector2UVE value = Vector2FromJsonUVE(json.at(property.name));
        property.setValue(instance, &value);
    } else if (property.typeId == kPropertyTypeVector3UVE) {
        const Math::Vector3UVE value = Vector3FromJsonUVE(json.at(property.name));
        property.setValue(instance, &value);
    } else if (property.typeId == kPropertyTypeColorUVE || property.typeId == kPropertyTypeLinearColorUVE) {
        const Math::ColorUVE value = ColorFromJsonUVE(json.at(property.name));
        property.setValue(instance, &value);
    } else if (property.typeId == kPropertyTypeQuaternionUVE) {
        const Math::QuaternionUVE value = QuaternionFromJsonUVE(json.at(property.name));
        property.setValue(instance, &value);
    } else if (property.typeId == kPropertyTypeEnumUVE) {
        const std::int64_t value = json.at(property.name).get<std::int64_t>();
        property.setValue(instance, &value);
    } else if (property.typeId == kPropertyTypeStringListUVE) {
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
    } else if (property.typeId == kPropertyTypeAssetGuidUVE) {
        const Asset::AssetGuidUVE value{json.at(property.name).get<std::uint64_t>()};
        property.setValue(instance, &value);
    } else if (property.typeId == kPropertyTypeRectUVE) {
        // Each half keeps its factory value when its key is absent, exactly like the hand-written
        // UI readers this replaces.
        Math::RectUVE value{};
        property.getValue(instance, &value);
        if (property.name == "rect") {
            if (json.contains("positionPixels")) {
                value.position = Vector2FromJsonUVE(json.at("positionPixels"));
            }
            if (json.contains("sizePixels")) {
                value.size = Vector2FromJsonUVE(json.at("sizePixels"));
            }
        } else if (json.contains(property.name)) {
            const nlohmann::json& nested = json.at(property.name);
            if (nested.contains("position")) {
                value.position = Vector2FromJsonUVE(nested.at("position"));
            }
            if (nested.contains("size")) {
                value.size = Vector2FromJsonUVE(nested.at("size"));
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
[[nodiscard]] bool MetadataPropertyHasCodecUVE(const Core::TypeMetadataPropertyUVE& property) {
    const auto typeId = property.typeId;
    return typeId == kPropertyTypeBoolUVE || typeId == kPropertyTypeFloatUVE ||
           typeId == kPropertyTypeInt32UVE || typeId == kPropertyTypeUInt32UVE ||
           typeId == kPropertyTypeUInt8UVE || typeId == kPropertyTypeBitMask32UVE ||
           typeId == kPropertyTypeStringUVE || typeId == kPropertyTypeVector2UVE ||
           typeId == kPropertyTypeVector3UVE || typeId == kPropertyTypeColorUVE ||
           typeId == kPropertyTypeLinearColorUVE || typeId == kPropertyTypeQuaternionUVE ||
           typeId == kPropertyTypeEnumUVE || typeId == kPropertyTypeAssetGuidUVE ||
           typeId == kPropertyTypeRectUVE || typeId == kPropertyTypeStringListUVE;
}

/// An entry earns a generic registration when it is a component with a C++ name, a bound factory
/// plus the archetype-slot trio, at least one serialized property, and serialized properties the
/// codec can all express. Anything else - entity references, lists, a missing factory, a
/// describe-only entry, an entry with nothing to persist (its EditorDescription-style data stays
/// out of the file entirely) - keeps its hand-written registration, or stays unregistered
/// exactly as today.
[[nodiscard]] bool MetadataComponentQualifiesUVE(const Core::TypeMetadataEntryUVE& entry) {
    if (entry.kind != Core::TypeMetadataKindUVE::Component || entry.cppName.empty() ||
        !entry.HasFactoryUVE() || entry.typeIndex == std::type_index(typeid(void)) ||
        entry.instanceSize == 0 || entry.constructDefaultInPlace == nullptr ||
        entry.moveConstructInPlace == nullptr || entry.destroyInPlace == nullptr) {
        return false;
    }
    bool hasSerializedProperty = false;
    for (const Core::TypeMetadataPropertyUVE& property : entry.properties) {
        if (!property.IsSerializedUVE()) {
            continue;
        }
        hasSerializedProperty = true;
        if (!MetadataPropertyHasCodecUVE(property)) {
            return false;
        }
    }
    return hasSerializedProperty;
}

/// Builds a component registration from a metadata entry instead of hand-written JSON: `toJson`
/// walks the entry's serialized properties off the live component, `fromJson` factory-builds a
/// default, overwrites the keys the document carries, validates, and attaches through the
/// type-erased AddComponentUVE + assignInstance pair. `entry` is a shared copy owned by the
/// registration table (the registry snapshot it came from is long gone); an empty `entry` or one
/// without a bound factory throws naming the component.
[[nodiscard]] ComponentRegistrationUVE
MakeMetadataRegistrationUVE(const std::string& componentName,
                            std::shared_ptr<const Core::TypeMetadataEntryUVE> entry) {
    if (!entry || !entry->HasFactoryUVE() || entry->constructDefaultInPlace == nullptr ||
        entry->moveConstructInPlace == nullptr || entry->destroyInPlace == nullptr ||
        entry->typeIndex == std::type_index(typeid(void))) {
        throw std::runtime_error("Cannot build a metadata registration for '" + componentName +
                                 "': its metadata entry is missing or has no factory");
    }
    return ComponentRegistrationUVE{
        entry->typeIndex,
        [entry](IEntityManagerUVE& entityManager, EntityUVE entity) -> nlohmann::json {
            const void* const instance = entityManager.GetComponentPointerUVE(entity, entry->typeIndex);
            nlohmann::json json = nlohmann::json::object();
            for (const Core::TypeMetadataPropertyUVE& property : entry->properties) {
                if (!property.IsSerializedUVE()) {
                    continue;
                }
                MetadataPropertyWriteUVE(*entry, property, instance, json);
            }
            return json;
        },
        [entry](IEntityManagerUVE& entityManager, EntityUVE entity) {
            if (entry->isInstanceValid == nullptr) {
                return true;
            }
            return entry->isInstanceValid(entityManager.GetComponentPointerUVE(entity, entry->typeIndex));
        },
        [entry, componentName](IEntityManagerUVE& entityManager, EntityUVE entity, const nlohmann::json& json) {
            Core::TypeInstanceUVE instance = Core::TypeInstanceUVE::MakeDefaultUVE(*entry);
            for (const Core::TypeMetadataPropertyUVE& property : entry->properties) {
                if (!property.IsSerializedUVE() || !MetadataPropertyPresentUVE(property, json)) {
                    continue;
                }
                MetadataPropertyReadUVE(*entry, property, json, instance.GetMutableUVE());
            }
            if (entry->isInstanceValid != nullptr && !entry->isInstanceValid(instance.GetUVE())) {
                throw std::runtime_error("Invalid " + componentName + " payload");
            }
            const ComponentTypeInfoUVE typeInfo{entry->instanceSize, entry->instanceAlignment,
                                                entry->constructDefaultInPlace, entry->moveConstructInPlace,
                                                entry->destroyInPlace};
            void* const slot = entityManager.AddComponentUVE(entity, entry->typeIndex, typeInfo);
            entry->assignInstance(slot, instance.GetUVE());
        },
    };
}

[[nodiscard]] const std::unordered_map<std::string, ComponentRegistrationUVE>& GetRegistrationsByNameUVE() {
    static const std::unordered_map<std::string, ComponentRegistrationUVE> registrations = [] {
        std::unordered_map<std::string, ComponentRegistrationUVE> table;

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

        table.emplace("TransformComponentUVE", MakeRegistrationUVE<TransformComponentUVE>([](const nlohmann::json& json) {
                          TransformComponentUVE transform{Vector3FromJsonUVE(json.at("localPosition")),
                                                          QuaternionFromJsonUVE(json.at("localRotation")),
                                                          Vector3FromJsonUVE(json.at("localScale"))};
                          // Absent in every scene written before Euler authoring existed. Those
                          // documents get the angles derived from the rotation they DO have, which
                          // is the best available answer and matches what the Inspector used to
                          // show them; from then on the angles are authored and stop drifting.
                          // Absent in scenes written before the flag existed, and false is what
                          // they meant: every transform in them composed from its parent.
                          transform.topLevel = json.value("topLevel", false);
                          if (json.contains("localEulerRadians")) {
                              transform.localEulerRadians = Vector3FromJsonUVE(json.at("localEulerRadians"));
                              transform.eulerOrder = ReadEulerOrderUVE(json);
                              transform.rotationEditMode = ReadRotationEditModeUVE(json);
                          } else {
                              transform.eulerOrder = Math::EulerOrderUVE::XYZ;
                              transform.rotationEditMode = RotationEditModeUVE::Euler;
                              if (!Math::TryToEulerOrderedUVE(transform.localRotation, Math::EulerOrderUVE::XYZ,
                                                              transform.localEulerRadians)) {
                                  transform.localEulerRadians = Math::Vector3UVE{};
                              }
                          }
                          if (!IsTransformComponentValidUVE(transform)) {
                              throw std::runtime_error("Invalid TransformComponentUVE payload");
                          }
                          return transform;
                      }, IsTransformComponentValidUVE));
        table.emplace("AnimationSequencerComponentUVE",
                      MakeRegistrationUVE<AnimationSequencerComponentUVE>([](const nlohmann::json& json) {
                          AnimationSequencerComponentUVE animation;
                          animation.clip = Asset::AssetGuidUVE{json.value("clip", std::uint64_t{0})};
                          animation.libraryRef = Asset::AssetGuidUVE{json.value("libraryRef", std::uint64_t{0})};
                          if (const auto library = json.find("library"); library != json.end() && library->is_array()) {
                              for (const nlohmann::json& guid : *library) {
                                  if (guid.is_number_unsigned() && guid.get<std::uint64_t>() != 0U) {
                                      animation.library.push_back(Asset::AssetGuidUVE{guid.get<std::uint64_t>()});
                                  }
                              }
                          }
                          // Players saved before the object was rebuilt: speed, looping and play-on-awake
                          // carry over; a disabled one no longer autoplays. Their clip was a path no
                          // runtime ever played, and cannot be resolved to an asset here, so it is dropped.
                          if (json.contains("clipAssetPath") && !json.value("clipAssetPath", std::string{}).empty()) {
                              UVE_WARNING("SceneSerializerUVE: AnimationSequencer clip path \"{}\" is no longer "
                                          "supported; pick the clip again in the Inspector",
                                          json.value("clipAssetPath", std::string{}));
                          }
                          const bool legacyEnabled = json.value("enabled", true);
                          animation.autoplay = json.value("autoplay", json.value("playOnAwake", true)) && legacyEnabled;
                          animation.speed = json.value("speed", json.value("playbackSpeed", 1.0F));
                          const bool legacyLooping = json.value("looping", true);
                          animation.loopMode = static_cast<AnimationLoopModeUVE>(json.value(
                              "loopMode", static_cast<std::uint8_t>(legacyLooping ? AnimationLoopModeUVE::Loop
                                                                                  : AnimationLoopModeUVE::Once)));
                          animation.onFinish = static_cast<AnimationFinishActionUVE>(
                              json.value("onFinish", std::uint8_t{0}));
                          animation.startOffsetSeconds = json.value("startOffsetSeconds", 0.0F);
                          animation.blendInSeconds = json.value("blendInSeconds", 0.0F);
                          animation.relative = json.value("relative", false);
                          if (!IsAnimationSequencerComponentValidUVE(animation)) {
                              throw std::runtime_error("Invalid AnimationSequencerComponentUVE payload");
                          }
                          return animation;
                      }, IsAnimationSequencerComponentValidUVE));
        table.emplace("GameplayAttributesComponentUVE",
                      MakeRegistrationUVE<GameplayAttributesComponentUVE>([](const nlohmann::json& json) {
                          GameplayAttributesComponentUVE attributes;
                          if (const auto list = json.find("attributes");
                              list != json.end() && list->is_array()) {
                              for (const nlohmann::json& entry : *list) {
                                  GameplayAttributeUVE attribute;
                                  attribute.id = entry.value("id", std::string{});
                                  attribute.current = entry.value("current", 0.0F);
                                  attribute.maximum = entry.value("maximum", 0.0F);
                                  attribute.regenPerSecond = entry.value("regenPerSecond", 0.0F);
                                  attributes.attributes.push_back(std::move(attribute));
                              }
                          }
                          if (!IsGameplayAttributesComponentValidUVE(attributes)) {
                              throw std::runtime_error("Invalid GameplayAttributesComponentUVE payload");
                          }
                          return attributes;
                      }, IsGameplayAttributesComponentValidUVE));
        table.emplace("GameplayTagComponentUVE",
                      MakeRegistrationUVE<GameplayTagComponentUVE>([](const nlohmann::json& json) {
                          GameplayTagComponentUVE tags;
                          if (const auto list = json.find("tags"); list != json.end() && list->is_array()) {
                              for (const nlohmann::json& entry : *list) {
                                  if (entry.is_string()) {
                                      tags.tags.push_back(entry.get<std::string>());
                                  }
                              }
                          }
                          if (!IsGameplayTagComponentValidUVE(tags)) {
                              throw std::runtime_error("Invalid GameplayTagComponentUVE payload");
                          }
                          return tags;
                      }, IsGameplayTagComponentValidUVE));
        table.emplace("ControllerComponentUVE",
                      MakeRegistrationUVE<ControllerComponentUVE>(
                          [](const nlohmann::json& json) {
                              ControllerComponentUVE value = ControllerObjectFromJsonUVE(json);
                              if (!IsControllerComponentValidUVE(value)) {
                                  throw std::runtime_error("Invalid ControllerComponentUVE payload");
                              }
                              return value;
                          },
                          IsControllerComponentValidUVE));
        table.emplace("PawnComponentUVE",
                      MakeRegistrationUVE<PawnComponentUVE>(
                          [](const nlohmann::json& json) {
                              PawnComponentUVE value = PawnObjectFromJsonUVE(json);
                              if (!IsPawnComponentValidUVE(value)) {
                                  throw std::runtime_error("Invalid PawnComponentUVE payload");
                              }
                              return value;
                          },
                          IsPawnComponentValidUVE));
        table.emplace("CameraFollowComponentUVE",
                      MakeRegistrationUVE<CameraFollowComponentUVE>(
                          [](const nlohmann::json& json) {
                              CameraFollowComponentUVE value = CameraFollowObjectFromJsonUVE(json);
                              if (!IsCameraFollowComponentValidUVE(value)) {
                                  throw std::runtime_error("Invalid CameraFollowComponentUVE payload");
                              }
                              return value;
                          },
                          IsCameraFollowComponentValidUVE));
        table.emplace("TriggerVolumeComponentUVE",
                      MakeRegistrationUVE<TriggerVolumeComponentUVE>([](const nlohmann::json& json) {
                          TriggerVolumeComponentUVE volume;
                          volume.policy =
                              static_cast<TriggerFirePolicyUVE>(json.value("policy", 0U));
                          volume.intervalSeconds = json.value("intervalSeconds", 1.0F);
                          volume.cooldownSeconds = json.value("cooldownSeconds", 0.0F);
                          volume.armed = json.value("armed", true);
                          volume.firedCount = json.value("firedCount", 0U);
                          volume.cooldownRemaining = json.value("cooldownRemaining", 0.0F);
                          volume.intervalRemaining = json.value("intervalRemaining", 0.0F);
                          if (!IsTriggerVolumeComponentValidUVE(volume)) {
                              throw std::runtime_error("Invalid TriggerVolumeComponentUVE payload");
                          }
                          return volume;
                      }, IsTriggerVolumeComponentValidUVE));
        table.emplace("StatusEffectsComponentUVE",
                      MakeRegistrationUVE<StatusEffectsComponentUVE>([](const nlohmann::json& json) {
                          StatusEffectsComponentUVE effects;
                          if (const auto list = json.find("effects");
                              list != json.end() && list->is_array()) {
                              for (const nlohmann::json& entry : *list) {
                                  StatusEffectUVE effect;
                                  effect.effectId = entry.value("effectId", std::string{});
                                  effect.attributeId = entry.value("attributeId", std::string{});
                                  effect.magnitudePerSecond = entry.value("magnitudePerSecond", 0.0F);
                                  effect.remainingSeconds = entry.value("remainingSeconds", 0.0F);
                                  effect.stacking = static_cast<StatusEffectStackingUVE>(
                                      entry.value("stacking", 0U));
                                  effect.stacks = entry.value("stacks", 1U);
                                  effect.maxStacks = entry.value("maxStacks", 1U);
                                  effect.maxDelta = entry.value("maxDelta", 0.0F);
                                  effect.maxApplied = entry.value("maxApplied", false);
                                  effects.effects.push_back(std::move(effect));
                              }
                          }
                          if (!IsStatusEffectsComponentValidUVE(effects)) {
                              throw std::runtime_error("Invalid StatusEffectsComponentUVE payload");
                          }
                          return effects;
                      }, IsStatusEffectsComponentValidUVE));
        table.emplace("CinematicComponentUVE",
                      MakeRegistrationUVE<CinematicComponentUVE>([](const nlohmann::json& json) {
                          // The load pass resolves references against the real id table before this
                          // registration ever sees the document; the empty table here degrades to
                          // authored data minus cameras and animation targets rather than throwing.
                          CinematicComponentUVE cinematic =
                              CinematicComponentWithResolvedReferencesUVE(json, {});
                          if (!IsCinematicComponentValidUVE(cinematic)) {
                              throw std::runtime_error("Invalid CinematicComponentUVE payload");
                          }
                          return cinematic;
                      }, IsCinematicComponentValidUVE));
        table.emplace("BlackboardComponentUVE",
                      MakeRegistrationUVE<BlackboardComponentUVE>([](const nlohmann::json& json) {
                          BlackboardComponentUVE board;
                          if (const auto entries = json.find("entries");
                              entries != json.end() && entries->is_array()) {
                              for (const nlohmann::json& entry : *entries) {
                                  AiBlackboardEntryUVE item;
                                  item.key = entry.value("key", std::string{});
                                  item.value = entry.value("value", 0.0F);
                                  board.entries.push_back(std::move(item));
                              }
                          }
                          if (const auto vectors = json.find("vectors");
                              vectors != json.end() && vectors->is_array()) {
                              for (const nlohmann::json& entry : *vectors) {
                                  AiBlackboardVectorEntryUVE item;
                                  item.key = entry.value("key", std::string{});
                                  item.value = Vector3FromJsonUVE(entry.at("value"));
                                  board.vectors.push_back(std::move(item));
                              }
                          }
                          if (!IsBlackboardComponentValidUVE(board)) {
                              throw std::runtime_error("Invalid BlackboardComponentUVE payload");
                          }
                          return board;
                      }, IsBlackboardComponentValidUVE));
        table.emplace("AiBrainComponentUVE",
                      MakeRegistrationUVE<AiBrainComponentUVE>([](const nlohmann::json& json) {
                          AiBrainComponentUVE brain;
                          brain.hysteresis = json.value("hysteresis", 0.1F);
                          if (const auto actions = json.find("actions");
                              actions != json.end() && actions->is_array()) {
                              for (const nlohmann::json& entry : *actions) {
                                  AiActionUVE action;
                                  action.actionId = entry.value("actionId", std::string{});
                                  action.baseScore = entry.value("baseScore", 1.0F);
                                  if (const auto considerations = entry.find("considerations");
                                      considerations != entry.end() && considerations->is_array()) {
                                      for (const nlohmann::json& item : *considerations) {
                                          AiConsiderationUVE consideration;
                                          consideration.inputId =
                                              item.value("inputId", std::string{});
                                          consideration.curve = static_cast<AiResponseCurveUVE>(
                                              item.value("curve", static_cast<std::uint8_t>(
                                                                     AiResponseCurveUVE::Linear)));
                                          consideration.weight = item.value("weight", 1.0F);
                                          action.considerations.push_back(std::move(consideration));
                                      }
                                  }
                                  brain.actions.push_back(std::move(action));
                              }
                          }
                          if (!IsAiBrainComponentValidUVE(brain)) {
                              throw std::runtime_error("Invalid AiBrainComponentUVE payload");
                          }
                          return brain;
                      }, IsAiBrainComponentValidUVE));
        table.emplace("PerceptionComponentUVE",
                      MakeRegistrationUVE<PerceptionComponentUVE>([](const nlohmann::json& json) {
                          PerceptionComponentUVE sensor;
                          sensor.watchedTag = json.value("watchedTag", std::string{});
                          sensor.sightRangeMetres = json.value("sightRangeMetres", 20.0F);
                          sensor.sightFieldOfViewDegrees =
                              json.value("sightFieldOfViewDegrees", 90.0F);
                          sensor.hearingRadiusMetres = json.value("hearingRadiusMetres", 15.0F);
                          sensor.requiresLineOfSight = json.value("requiresLineOfSight", true);
                          if (!IsPerceptionComponentValidUVE(sensor)) {
                              throw std::runtime_error("Invalid PerceptionComponentUVE payload");
                          }
                          return sensor;
                      }, IsPerceptionComponentValidUVE));
        table.emplace("NoiseEmitterComponentUVE",
                      MakeRegistrationUVE<NoiseEmitterComponentUVE>([](const nlohmann::json& json) {
                          NoiseEmitterComponentUVE emitter;
                          emitter.loudness = json.value("loudness", 1.0F);
                          emitter.decayPerSecond = json.value("decayPerSecond", 0.0F);
                          if (!IsNoiseEmitterComponentValidUVE(emitter)) {
                              throw std::runtime_error("Invalid NoiseEmitterComponentUVE payload");
                          }
                          return emitter;
                      }, IsNoiseEmitterComponentValidUVE));
        table.emplace("AnimationDriverComponentUVE",
                      MakeRegistrationUVE<AnimationDriverComponentUVE>([](const nlohmann::json& json) {
                          const AnimationDriverComponentUVE driver = AnimationDriverFromJsonUVE(json);
                          if (!IsAnimationDriverComponentValidUVE(driver)) {
                              throw std::runtime_error("Invalid AnimationDriverComponentUVE payload");
                          }
                          return driver;
                      }, IsAnimationDriverComponentValidUVE));
        table.emplace("AnimationGraphComponentUVE",
                      MakeRegistrationUVE<AnimationGraphComponentUVE>([](const nlohmann::json& json) {
                          AnimationGraphComponentUVE tree = AnimationGraphFromJsonUVE(json);
                          const std::string problem = DescribeAnimationGraphProblemUVE(tree);
                          if (!problem.empty()) {
                              throw std::runtime_error("Invalid AnimationGraphComponentUVE payload: " + problem);
                          }
                          return tree;
                      }, IsAnimationGraphComponentValidUVE));
        table.emplace("RayCast3DComponentUVE", MakeRegistrationUVE<RayCast3DComponentUVE>(
            [](const nlohmann::json& json) {
                const RayCast3DComponentUVE value = RayCast3DObjectFromJsonUVE(json);
                if (!IsRayCast3DObjectComponentValidUVE(value)) {
                    throw std::runtime_error("Invalid RayCast3DComponentUVE payload");
                }
                return value;
            }, IsRayCast3DObjectComponentValidUVE));
        table.emplace("NavMeshVolume3DComponentUVE", MakeRegistrationUVE<NavMeshVolume3DComponentUVE>(
            [](const nlohmann::json& json) {
                const NavMeshVolume3DComponentUVE value = NavMeshVolume3DObjectFromJsonUVE(json);
                if (!IsNavMeshVolume3DObjectComponentValidUVE(value)) {
                    throw std::runtime_error("Invalid NavMeshVolume3DComponentUVE payload");
                }
                return value;
            }, IsNavMeshVolume3DObjectComponentValidUVE));
        table.emplace("Skeleton3DComponentUVE", MakeRegistrationUVE<Skeleton3DComponentUVE>(
            [](const nlohmann::json& json) {
                const Skeleton3DComponentUVE value = Skeleton3DObjectFromJsonUVE(json);
                if (!IsSkeleton3DObjectComponentValidUVE(value)) {
                    throw std::runtime_error("Invalid Skeleton3DComponentUVE payload");
                }
                return value;
            }, IsSkeleton3DObjectComponentValidUVE));
        table.emplace("BoneAttachment3DComponentUVE", MakeRegistrationUVE<BoneAttachment3DComponentUVE>(
            [](const nlohmann::json& json) {
                const BoneAttachment3DComponentUVE value = BoneAttachment3DObjectFromJsonUVE(json);
                if (!IsBoneAttachment3DObjectComponentValidUVE(value)) {
                    throw std::runtime_error("Invalid BoneAttachment3DComponentUVE payload");
                }
                return value;
            }, IsBoneAttachment3DObjectComponentValidUVE));
        table.emplace("TwoBoneIK3DComponentUVE", MakeRegistrationUVE<TwoBoneIK3DComponentUVE>(
            [](const nlohmann::json& json) {
                const TwoBoneIK3DComponentUVE value = TwoBoneIK3DObjectFromJsonUVE(json);
                if (!IsTwoBoneIK3DObjectComponentValidUVE(value)) {
                    throw std::runtime_error("Invalid TwoBoneIK3DComponentUVE payload");
                }
                return value;
            }, IsTwoBoneIK3DObjectComponentValidUVE));
        table.emplace("FullBodyIK3DComponentUVE", MakeRegistrationUVE<FullBodyIK3DComponentUVE>(
            [](const nlohmann::json& json) {
                const FullBodyIK3DComponentUVE value = FullBodyIK3DObjectFromJsonUVE(json);
                if (!IsFullBodyIK3DObjectComponentValidUVE(value)) {
                    throw std::runtime_error("Invalid FullBodyIK3DComponentUVE payload");
                }
                return value;
            }, IsFullBodyIK3DObjectComponentValidUVE));
        table.emplace("SpringArm3DComponentUVE", MakeRegistrationUVE<SpringArm3DComponentUVE>(
            [](const nlohmann::json& json) {
                const SpringArm3DComponentUVE value = SpringArm3DObjectFromJsonUVE(json);
                if (!IsSpringArm3DObjectComponentValidUVE(value)) {
                    throw std::runtime_error("Invalid SpringArm3DComponentUVE payload");
                }
                return value;
            }, IsSpringArm3DObjectComponentValidUVE));
        table.emplace("Marker3DComponentUVE", MakeRegistrationUVE<Marker3DComponentUVE>(
            [](const nlohmann::json& json) {
                const Marker3DComponentUVE value = Marker3DObjectFromJsonUVE(json);
                if (!IsMarker3DObjectComponentValidUVE(value)) {
                    throw std::runtime_error("Invalid Marker3DComponentUVE payload");
                }
                return value;
            }, IsMarker3DObjectComponentValidUVE));
        // An id this build does not know (a scene from a newer one) adds nothing rather than failing
        // the load: the object is then read from its components, like a scene saved before types were.
        table.emplace("SceneObjectTypeComponentUVE",
                      ComponentRegistrationUVE{
                          std::type_index(typeid(SceneObjectTypeComponentUVE)),
                          [](IEntityManagerUVE& entityManager, EntityUVE entity) -> nlohmann::json {
                              return ToJsonUVE(entityManager.GetComponentUVE<SceneObjectTypeComponentUVE>(entity));
                          },
                          [](IEntityManagerUVE& entityManager, EntityUVE entity) {
                              return IsSceneObjectTypeComponentValidUVE(
                                  entityManager.GetComponentUVE<SceneObjectTypeComponentUVE>(entity));
                          },
                          [](IEntityManagerUVE& entityManager, EntityUVE entity, const nlohmann::json& json) {
                              const auto type = json.find("type");
                              const Objects::SceneObjectDescriptorUVE* const descriptor =
                                  type != json.end() && type->is_string()
                                      ? Objects::FindSceneObjectDescriptorUVE(type->get<std::string>())
                                      : nullptr;
                              if (descriptor != nullptr) {
                                  entityManager.AddComponentUVE<SceneObjectTypeComponentUVE>(
                                      entity, SceneObjectTypeComponentUVE{descriptor->kind});
                              }
                          },
                      });
        table.emplace("FolderComponentUVE", MakeRegistrationUVE<FolderComponentUVE>([](const nlohmann::json&) {
                          return FolderComponentUVE{};
                      }, IsFolderComponentValidUVE));
        table.emplace("OutlinerViewport3DComponentUVE", MakeRegistrationUVE<OutlinerViewport3DComponentUVE>([](const nlohmann::json& json) {
                          // Anything but the one word this writer emits - absent, or a value some
                          // hand-edit left behind - reads as 3D rather than failing the load.
                          const bool is2D = json.contains("dimension") && json["dimension"].is_string() &&
                                            json["dimension"].get<std::string>() == "2d";
                          return OutlinerViewport3DComponentUVE{is2D ? ViewportDimensionUVE::TwoD
                                                                      : ViewportDimensionUVE::ThreeD};
                      }, IsOutlinerViewport3DComponentValidUVE));
        table.emplace("SceneRootComponentUVE",
                    MakeRegistrationUVE<SceneRootComponentUVE>([](const nlohmann::json&) {
                        return SceneRootComponentUVE{};
                    }, IsSceneRootComponentValidUVE));
        table.emplace("SkinComponentUVE",
                      ComponentRegistrationUVE{
                          std::type_index(typeid(SkinComponentUVE)),
                          [](IEntityManagerUVE& entityManager, EntityUVE entity) -> nlohmann::json {
                              const auto& skin = entityManager.GetComponentUVE<SkinComponentUVE>(entity);
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
                          [](IEntityManagerUVE& entityManager, EntityUVE entity) {
                              return IsSkinComponentValidUVE(entityManager.GetComponentUVE<SkinComponentUVE>(entity));
                          },
                          [](IEntityManagerUVE& entityManager, EntityUVE entity, const nlohmann::json& json) {
                              SkinComponentUVE skin{};
                              skin.bindCount = json.value("bindCount", 0U);
                              if (json.contains("boneBinds") && json["boneBinds"].is_array()) {
                                  for (const auto& bindJson : json["boneBinds"]) {
                                      SkinBoneBindUVE bind{};
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
                              entityManager.AddComponentUVE<SkinComponentUVE>(entity, std::move(skin));
                          },
                      });
        table.emplace("MorphTargetComponentUVE",
                      MakeRegistrationUVE<MorphTargetComponentUVE>(MorphTargetObjectFromJsonUVE,
                                                                   IsMorphTargetComponentValidUVE));
        table.emplace("Hitbox3DComponentUVE", MakeRegistrationUVE<Hitbox3DComponentUVE>(
            [](const nlohmann::json& json) {
                const Hitbox3DComponentUVE value = Hitbox3DObjectFromJsonUVE(json);
                if (!IsHitbox3DObjectComponentValidUVE(value)) {
                    throw std::runtime_error("Invalid Hitbox3DComponentUVE payload");
                }
                return value;
            }, IsHitbox3DObjectComponentValidUVE));
        table.emplace("Hurtbox3DComponentUVE", MakeRegistrationUVE<Hurtbox3DComponentUVE>(
            [](const nlohmann::json& json) {
                const Hurtbox3DComponentUVE value = Hurtbox3DObjectFromJsonUVE(json);
                if (!IsHurtbox3DObjectComponentValidUVE(value)) {
                    throw std::runtime_error("Invalid Hurtbox3DComponentUVE payload");
                }
                return value;
            }, IsHurtbox3DObjectComponentValidUVE));
        table.emplace("Projectile3DComponentUVE", MakeRegistrationUVE<Projectile3DComponentUVE>(
            [](const nlohmann::json& json) {
                const Projectile3DComponentUVE value = Projectile3DObjectFromJsonUVE(json);
                if (!IsProjectile3DObjectComponentValidUVE(value)) {
                    throw std::runtime_error("Invalid Projectile3DComponentUVE payload");
                }
                return value;
            }, IsProjectile3DObjectComponentValidUVE));
        table.emplace("InteractionArea3DComponentUVE", MakeRegistrationUVE<InteractionArea3DComponentUVE>(
            [](const nlohmann::json& json) {
                const InteractionArea3DComponentUVE value = InteractionArea3DObjectFromJsonUVE(json);
                if (!IsInteractionArea3DObjectComponentValidUVE(value)) {
                    throw std::runtime_error("Invalid InteractionArea3DComponentUVE payload");
                }
                return value;
            }, IsInteractionArea3DObjectComponentValidUVE));
        table.emplace("Sky3DComponentUVE", MakeRegistrationUVE<Sky3DComponentUVE>(
            [](const nlohmann::json& json) {
                const Sky3DComponentUVE value = Sky3DFromJsonUVE(json);
                if (!IsSky3DObjectComponentValidUVE(value)) {
                    throw std::runtime_error("Invalid Sky3DComponentUVE payload");
                }
                return value;
            }, IsSky3DObjectComponentValidUVE));
        table.emplace("ReflectionProbe3DComponentUVE", MakeRegistrationUVE<ReflectionProbe3DComponentUVE>(
            [](const nlohmann::json& json) {
                const ReflectionProbe3DComponentUVE value = ReflectionProbe3DObjectFromJsonUVE(json);
                if (!IsReflectionProbe3DObjectComponentValidUVE(value)) {
                    throw std::runtime_error("Invalid ReflectionProbe3DComponentUVE payload");
                }
                return value;
            }, IsReflectionProbe3DObjectComponentValidUVE));
        table.emplace("Decal3DComponentUVE", MakeRegistrationUVE<Decal3DComponentUVE>(
            [](const nlohmann::json& json) {
                const Decal3DComponentUVE value = Decal3DObjectFromJsonUVE(json);
                if (!IsDecal3DObjectComponentValidUVE(value)) {
                    throw std::runtime_error("Invalid Decal3DComponentUVE payload");
                }
                return value;
            }, IsDecal3DObjectComponentValidUVE));
        table.emplace("LodGroup3DComponentUVE", MakeRegistrationUVE<LodGroup3DComponentUVE>(
            [](const nlohmann::json& json) {
                const LodGroup3DComponentUVE value = LodGroup3DObjectFromJsonUVE(json);
                if (!IsLodGroup3DObjectComponentValidUVE(value)) {
                    throw std::runtime_error("Invalid LodGroup3DComponentUVE payload");
                }
                return value;
            }, IsLodGroup3DObjectComponentValidUVE));
        table.emplace("HealthComponentUVE", MakeRegistrationUVE<HealthComponentUVE>(
            [](const nlohmann::json& json) {
                const HealthComponentUVE value = HealthFromJsonUVE(json);
                if (!IsHealthComponentValidUVE(value)) {
                    throw std::runtime_error("Invalid HealthComponentUVE payload");
                }
                return value;
            }, IsHealthComponentValidUVE));
        table.emplace("LevelStreamer3DComponentUVE", MakeRegistrationUVE<LevelStreamer3DComponentUVE>(
            [](const nlohmann::json& json) {
                const LevelStreamer3DComponentUVE value = LevelStreamer3DObjectFromJsonUVE(json);
                if (!IsLevelStreamer3DObjectComponentValidUVE(value)) {
                    throw std::runtime_error("Invalid LevelStreamer3DComponentUVE payload");
                }
                return value;
            }, IsLevelStreamer3DObjectComponentValidUVE));
        table.emplace("WorldPartition3DComponentUVE", MakeRegistrationUVE<WorldPartition3DComponentUVE>(
            [](const nlohmann::json& json) {
                const WorldPartition3DComponentUVE value = WorldPartition3DObjectFromJsonUVE(json);
                if (!IsWorldPartition3DObjectComponentValidUVE(value)) {
                    throw std::runtime_error("Invalid WorldPartition3DComponentUVE payload");
                }
                return value;
            }, IsWorldPartition3DObjectComponentValidUVE));
        table.emplace("CharacterControllerComponentUVE",
                      MakeRegistrationUVE<CharacterControllerComponentUVE>([](const nlohmann::json& json) {
                          // Every field falls back to its default, so a file from before a field
                          // existed loads as if it had been left at that default.
                          CharacterControllerComponentUVE c;
                          c.motionMode = static_cast<CharacterMotionModeUVE>(json.value("motionMode", std::uint8_t{0}));
                          c.upDirection = json.contains("upDirection")
                                              ? Vector3FromJsonUVE(json.at("upDirection"))
                                              : Math::Vector3UVE{0.0F, 1.0F, 0.0F};
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
                          c.platformOnLeave = static_cast<CharacterPlatformLeaveModeUVE>(
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
                                           ? Vector3FromJsonUVE(json.at("velocity"))
                                           : Math::Vector3UVE{0.0F, json.value("verticalVelocity", 0.0F), 0.0F};
                          // Three names, one field, in the order they were used: documents have been
                          // written with each. "grounded" is current, "isOnFloor" is what the field
                          // was called when it carried another engine's name, and "isGrounded" is
                          // older still - the serializer already accepted it before this pass.
                          c.grounded = json.value("grounded",
                                                  json.value("isOnFloor", json.value("isGrounded", false)));
                          c.isOnCeiling = json.value("isOnCeiling", false);
                          if (json.contains("floorNormal")) {
                              c.floorNormal = Vector3FromJsonUVE(json.at("floorNormal"));
                          }
                          c.timeSinceOnFloor = json.value("timeSinceOnFloor", 0.0F);
                          c.jumpBufferRemaining = json.value("jumpBufferRemaining", 0.0F);
                          CharacterControllerComponentUVE& characterController = c;
                          if (!IsCharacterControllerComponentValidUVE(characterController)) {
                              throw std::runtime_error("Invalid CharacterControllerComponentUVE payload");
                          }
                          return characterController;
                      }, IsCharacterControllerComponentValidUVE));
        table.emplace("UIButtonComponentUVE",
                      MakeRegistrationUVE<UIButtonComponentUVE>([](const nlohmann::json& json) {
                          UIButtonComponentUVE button;
                          button.rect.position = json.contains("positionPixels")
                              ? Vector2FromJsonUVE(json.at("positionPixels")) : Math::Vector2UVE{};
                          button.rect.size = json.contains("sizePixels") ? Vector2FromJsonUVE(json.at("sizePixels"))
                                                                       : Math::Vector2UVE{120.0F, 32.0F};
                          button.normalColor = json.contains("normalColor")
                              ? Vector3FromJsonUVE(json.at("normalColor")) : Math::Vector3UVE{0.25F, 0.25F, 0.28F};
                          button.hoverColor = json.contains("hoverColor")
                              ? Vector3FromJsonUVE(json.at("hoverColor")) : Math::Vector3UVE{0.35F, 0.35F, 0.40F};
                          button.pressedColor = json.contains("pressedColor")
                              ? Vector3FromJsonUVE(json.at("pressedColor")) : Math::Vector3UVE{0.18F, 0.18F, 0.20F};
                          button.isHovered = json.value("isHovered", false);
                          button.wasClickedThisFrame = json.value("wasClickedThisFrame", false);
                          if (!IsUIButtonComponentValidUVE(button)) {
                              throw std::runtime_error("Invalid UIButtonComponentUVE payload");
                          }
                          return button;
                      }, IsUIButtonComponentValidUVE));
        table.emplace("ScriptComponentUVE", MakeRegistrationUVE<ScriptComponentUVE>([](const nlohmann::json& json) {
                          // exportValues came later; a scene saved before it has none.
                          const ScriptComponentUVE script{
                              json.at("scriptAssetPath").get<std::string>(),
                              json.value("exportValues", std::map<std::string, std::string>{})};
                          if (!IsScriptComponentValidUVE(script)) {
                              throw std::runtime_error("Invalid ScriptComponentUVE payload");
                          }
                          return script;
                      }, IsScriptComponentValidUVE));
        table.emplace("ObjectMetadataComponentUVE",
                      MakeRegistrationUVE<ObjectMetadataComponentUVE>([](const nlohmann::json& json) {
                          ObjectMetadataComponentUVE metadata{};
                          for (const nlohmann::json& entry : json.at("entries")) {
                              const nlohmann::json& value = entry.at("value");
                              // A plain string is the format metadata was saved in before values
                              // were typed; it loads as a String, so no older scene is lost.
                              metadata.entries.push_back(ObjectMetadataEntryUVE{
                                  entry.at("key").get<std::string>(),
                                  value.is_string()
                                      ? Core::VariantUVE::MakeTextUVE(Core::VariantTypeUVE::String,
                                                                      value.get<std::string>())
                                      : VariantFromJsonUVE(value, 0U)});
                          }
                          // The validator rejects a duplicate key, an oversized one and an
                          // over-long list, so a hand-edited or hostile file cannot load an entity
                          // whose metadata lookups would depend on iteration order.
                          if (!IsObjectMetadataComponentValidUVE(metadata)) {
                              throw std::runtime_error("Invalid ObjectMetadataComponentUVE payload");
                          }
                          return metadata;
                      }, IsObjectMetadataComponentValidUVE));
        table.emplace("EditorDescriptionComponentUVE",
                      MakeRegistrationUVE<EditorDescriptionComponentUVE>([](const nlohmann::json& json) {
                          const EditorDescriptionComponentUVE description{
                              json.at("description").get<std::string>()};
                          if (!IsEditorDescriptionComponentValidUVE(description)) {
                              throw std::runtime_error("Invalid EditorDescriptionComponentUVE payload");
                          }
                          return description;
                      }, IsEditorDescriptionComponentValidUVE));
        table.emplace("PrefabInstanceComponentUVE",
                      MakeRegistrationUVE<PrefabInstanceComponentUVE>(
                          [](const nlohmann::json& json) { return PrefabInstanceFromJsonUVE(json); },
                          IsPrefabInstanceComponentValidUVE));

        // --- Metadata auto-discovery (Tier 1.6) -------------------------------------------
        // Adding a component no longer touches this function: every component metadata entry
        // that qualifies gets a generic registration here, unless a hand-written one above
        // already claimed its C++ name (emplace keeps the existing registration, so explicit
        // wins). The snapshot entries are copies; the table takes shared ownership so the
        // registrations' lambdas stay valid for the table's process lifetime.
        const Core::TypeMetadataSnapshotUVE snapshot =
            GetSceneComponentMetadataRegistryUVE().GetSnapshotUVE();
        for (const Core::TypeMetadataEntryUVE& snapshotEntry : snapshot.entries) {
            if (!MetadataComponentQualifiesUVE(snapshotEntry) ||
                table.find(snapshotEntry.cppName) != table.end()) {
                continue;
            }
            auto entryCopy = std::make_shared<Core::TypeMetadataEntryUVE>(snapshotEntry);
            table.emplace(snapshotEntry.cppName,
                          MakeMetadataRegistrationUVE(snapshotEntry.cppName, std::move(entryCopy)));
        }

        return table;
    }();
    return registrations;
}

[[nodiscard]] const std::string* FindNameForTypeIndexUVE(std::type_index typeIndex) {
    static const std::unordered_map<std::type_index, std::string> namesByType = [] {
        std::unordered_map<std::type_index, std::string> map;
        for (const auto& [name, registration] : GetRegistrationsByNameUVE()) {
            map.emplace(registration.typeIndex, name);
        }
        return map;
    }();
    const auto it = namesByType.find(typeIndex);
    return it == namesByType.end() ? nullptr : &it->second;
}

/// Appends `root` and every descendant reachable via HierarchyComponentUVE.parent to
/// `outEntities`, depth-first. The visited set deduplicates overlapping requested roots and makes
/// malformed hierarchy cycles fail closed rather than recursing indefinitely.
[[nodiscard]] bool CollectSubtreeUVE(IEntityManagerUVE& entityManager, const EntityUVE root,
                                     std::unordered_set<EntityUVE>& visited,
                                     std::vector<EntityUVE>& outEntities) {
    if (!entityManager.IsAliveUVE(root)) {
        return false;
    }
    if (!visited.emplace(root).second) {
        return true;
    }

    outEntities.push_back(root);
    // Siblings are written in their order: the file keeps no order number, so loading hands out
    // orders in the sequence the entities appear.
    std::vector<std::pair<std::int64_t, EntityUVE>> ordered;
    entityManager.ForEachUVE<HierarchyComponentUVE>(
        [&ordered, root](const EntityUVE entity, HierarchyComponentUVE& hierarchy) {
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
    std::vector<EntityUVE> children;
    children.reserve(ordered.size());
    for (const auto& [order, child] : ordered) {
        children.push_back(child);
    }
    for (const EntityUVE child : children) {
        if (!CollectSubtreeUVE(entityManager, child, visited, outEntities)) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool IsSceneAssetTypeUVE(const SceneAssetTypeUVE assetType) noexcept {
    return assetType == SceneAssetTypeUVE::Scene || assetType == SceneAssetTypeUVE::Prefab;
}

[[nodiscard]] std::optional<std::vector<std::byte>> EncodeScenePayloadUVE(
    IEntityManagerUVE& entityManager, const std::vector<EntityUVE>& rootEntities,
    const std::string_view sourceDescription) {
    std::vector<EntityUVE> allEntities;
    std::unordered_set<EntityUVE> visited;
    for (const EntityUVE root : rootEntities) {
        if (!CollectSubtreeUVE(entityManager, root, visited, allEntities)) {
            UVE_ERROR("SceneSerializerUVE: \"{}\" includes an invalid root entity", sourceDescription);
            return std::nullopt;
        }
    }
    if (allEntities.size() > std::numeric_limits<std::uint32_t>::max()) {
        UVE_ERROR("SceneSerializerUVE: \"{}\" contains too many entities to serialize", sourceDescription);
        return std::nullopt;
    }

    std::unordered_map<EntityUVE, std::uint32_t> entityToLocalId;
    entityToLocalId.reserve(allEntities.size());
    for (std::uint32_t index = 0; index < allEntities.size(); ++index) {
        entityToLocalId.emplace(allEntities[index], index);
    }

    nlohmann::json entitiesJson = nlohmann::json::array();
    for (const EntityUVE entity : allEntities) {
        nlohmann::json componentsJson = nlohmann::json::object();
        for (const std::type_index type : entityManager.GetComponentTypesUVE(entity)) {
            if (type == std::type_index(typeid(WorldTransformComponentUVE))) {
                continue; // Derived/cached state is rebuilt after restore.
            }
            if (type == std::type_index(typeid(DisabledComponentUVE))) {
                // Presence is the data: an empty object marks the entity disabled on load.
                // Hand-written (not generic) because the registration table skips property-less
                // entries, and there is nothing here to put in the table.
                componentsJson["DisabledComponentUVE"] = nlohmann::json::object();
                continue;
            }
            if (type == std::type_index(typeid(VisibilityComponentUVE))) {
                // Written here rather than through the registration table for the same reason
                // HierarchyComponentUVE is: it holds an entity reference, and only this function
                // knows the file-local id each entity was assigned.
                const VisibilityComponentUVE& visibility =
                    entityManager.GetComponentUVE<VisibilityComponentUVE>(entity);
                std::int64_t visibilityParentLocalId = -1;
                if (visibility.visibilityParent != kInvalidEntityUVE) {
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
                componentsJson["VisibilityComponentUVE"] = {
                    {"visible", visibility.visible},
                    {"visibilityParentLocalId", visibilityParentLocalId}};
                continue;
            }
            if (type == std::type_index(typeid(HierarchyComponentUVE))) {
                const HierarchyComponentUVE& hierarchy = entityManager.GetComponentUVE<HierarchyComponentUVE>(entity);
                std::int64_t parentLocalId = -1;
                if (hierarchy.parent != kInvalidEntityUVE) {
                    const auto parentIt = entityToLocalId.find(hierarchy.parent);
                    if (parentIt != entityToLocalId.end()) {
                        parentLocalId = static_cast<std::int64_t>(parentIt->second);
                    }
                    // A parent outside the captured subtree intentionally becomes a restored root.
                }
                componentsJson["HierarchyComponentUVE"] = {{"parentLocalId", parentLocalId}};
                continue;
            }

            const std::string* const name = FindNameForTypeIndexUVE(type);
            if (name == nullptr) {
                UVE_ERROR("SceneSerializerUVE: no registered serializer for a component type on entity index {} "
                          "while encoding \"{}\"",
                          entity.index, sourceDescription);
                return std::nullopt;
            }
            const ComponentRegistrationUVE& registration = GetRegistrationsByNameUVE().at(*name);
            if (!registration.isValid(entityManager, entity)) {
                UVE_ERROR("SceneSerializerUVE: component type \"{}\" on entity index {} failed authored validation "
                          "while encoding \"{}\"",
                          *name, entity.index, sourceDescription);
                return std::nullopt;
            }
            componentsJson[*name] = registration.toJson(entityManager, entity);
            // Animation targets and bone attachments are entity references: remapped to file-local
            // ids like the visibility parent, and dropped when the target is outside what is being
            // saved.
            const auto writeTarget = [&](const EntityUVE target) {
                std::int64_t targetLocalId = -1;
                if (target != kInvalidEntityUVE) {
                    const auto targetIt = entityToLocalId.find(target);
                    if (targetIt != entityToLocalId.end()) {
                        targetLocalId = static_cast<std::int64_t>(targetIt->second);
                    }
                }
                componentsJson[*name]["targetLocalId"] = targetLocalId;
            };
            if (type == std::type_index(typeid(AnimationDriverComponentUVE))) {
                writeTarget(entityManager.GetComponentUVE<AnimationDriverComponentUVE>(entity).target);
            }
            if (type == std::type_index(typeid(BoneAttachment3DComponentUVE))) {
                // A bone attachment's skeleton is an entity reference too, written under the id key
                // this component has always used so documents saved before it resolved still load.
                const EntityUVE skeleton =
                    entityManager.GetComponentUVE<BoneAttachment3DComponentUVE>(entity).skeleton;
                std::uint32_t skeletonLocalId = std::numeric_limits<std::uint32_t>::max();
                if (skeleton != kInvalidEntityUVE) {
                    const auto skeletonIt = entityToLocalId.find(skeleton);
                    if (skeletonIt != entityToLocalId.end()) {
                        skeletonLocalId = skeletonIt->second;
                    }
                }
                componentsJson[*name]["skeletonLocalId"] = skeletonLocalId;
            }
            if (type == std::type_index(typeid(TwoBoneIK3DComponentUVE))) {
                // A chain's skeleton, target and pole are entity references like an attachment's
                // skeleton, written as file-local ids and resolved back on load.
                const TwoBoneIK3DComponentUVE& chain =
                    entityManager.GetComponentUVE<TwoBoneIK3DComponentUVE>(entity);
                const auto localIdOfUVE = [&entityToLocalId](const EntityUVE referenced) {
                    std::uint32_t localId = std::numeric_limits<std::uint32_t>::max();
                    if (referenced != kInvalidEntityUVE) {
                        const auto found = entityToLocalId.find(referenced);
                        if (found != entityToLocalId.end()) {
                            localId = found->second;
                        }
                    }
                    return localId;
                };
                componentsJson[*name]["skeletonLocalId"] = localIdOfUVE(chain.skeleton);
                componentsJson[*name]["targetLocalId"] = localIdOfUVE(chain.target);
                componentsJson[*name]["poleLocalId"] = localIdOfUVE(chain.poleTarget);
            }
            if (type == std::type_index(typeid(FullBodyIK3DComponentUVE))) {
                // A solve's skeleton and every effector's target are entity references like a
                // chain's, written as file-local ids and resolved back on load. Effector targets
                // ride inside their own entries so one unresolvable id leaves that limb inert
                // instead of shifting every other one.
                const FullBodyIK3DComponentUVE& solve =
                    entityManager.GetComponentUVE<FullBodyIK3DComponentUVE>(entity);
                const auto localIdOfUVE = [&entityToLocalId](const EntityUVE referenced) {
                    std::uint32_t localId = std::numeric_limits<std::uint32_t>::max();
                    if (referenced != kInvalidEntityUVE) {
                        const auto found = entityToLocalId.find(referenced);
                        if (found != entityToLocalId.end()) {
                            localId = found->second;
                        }
                    }
                    return localId;
                };
                componentsJson[*name]["skeletonLocalId"] = localIdOfUVE(solve.skeleton);
                nlohmann::json& effectorsJson = componentsJson[*name]["effectors"];
                if (effectorsJson.is_array() && effectorsJson.size() == solve.effectors.size()) {
                    for (std::size_t index = 0U; index < solve.effectors.size(); ++index) {
                        effectorsJson[index]["targetLocalId"] = localIdOfUVE(solve.effectors[index].target);
                    }
                }
            }
            if (type == std::type_index(typeid(RayCast3DComponentUVE))) {
                // The ray's exclusions are entity references too: written as file-local ids, with
                // targets outside the saved set dropped exactly like an animation target. The
                // registration's own toJson JSON deliberately carries no exclusions key at all, so
                // there is only ever one place a reader can find them.
                const RayCast3DComponentUVE& rayCast =
                    entityManager.GetComponentUVE<RayCast3DComponentUVE>(entity);
                nlohmann::json exclusionLocalIds = nlohmann::json::array();
                const std::size_t exclusionCount = CountRayCast3DExclusionsUVE(rayCast);
                for (std::size_t index = 0U; index < exclusionCount; ++index) {
                    const auto targetIt = entityToLocalId.find(rayCast.exclusions[index]);
                    if (targetIt != entityToLocalId.end()) {
                        exclusionLocalIds.push_back(targetIt->second);
                    }
                }
                componentsJson[*name]["exclusionsLocalIds"] = std::move(exclusionLocalIds);
            }
            if (type == std::type_index(typeid(ControllerComponentUVE))) {
                // A controller's pawn is an entity reference: written as a file-local id, with a
                // target outside the saved set dropped exactly like an animation target.
                const ControllerComponentUVE& controller =
                    entityManager.GetComponentUVE<ControllerComponentUVE>(entity);
                std::uint32_t pawnLocalId = std::numeric_limits<std::uint32_t>::max();
                if (controller.pawn != kInvalidEntityUVE) {
                    const auto found = entityToLocalId.find(controller.pawn);
                    if (found != entityToLocalId.end()) {
                        pawnLocalId = found->second;
                    }
                }
                componentsJson[*name]["pawnLocalId"] = pawnLocalId;
            }
            if (type == std::type_index(typeid(PawnComponentUVE))) {
                // A pawn's controller resolves the same way, from the other side of the link.
                const PawnComponentUVE& pawn =
                    entityManager.GetComponentUVE<PawnComponentUVE>(entity);
                std::uint32_t controllerLocalId = std::numeric_limits<std::uint32_t>::max();
                if (pawn.controller != kInvalidEntityUVE) {
                    const auto found = entityToLocalId.find(pawn.controller);
                    if (found != entityToLocalId.end()) {
                        controllerLocalId = found->second;
                    }
                }
                componentsJson[*name]["controllerLocalId"] = controllerLocalId;
            }
            if (type == std::type_index(typeid(CameraFollowComponentUVE))) {
                // A follow camera's target is an entity reference: written as a file-local id,
                // with a target outside the saved set dropped exactly like a pawn link.
                const CameraFollowComponentUVE& follow =
                    entityManager.GetComponentUVE<CameraFollowComponentUVE>(entity);
                std::uint32_t targetLocalId = std::numeric_limits<std::uint32_t>::max();
                if (follow.target != kInvalidEntityUVE) {
                    const auto found = entityToLocalId.find(follow.target);
                    if (found != entityToLocalId.end()) {
                        targetLocalId = found->second;
                    }
                }
                componentsJson[*name]["targetLocalId"] = targetLocalId;
            }
            if (type == std::type_index(typeid(CinematicComponentUVE))) {
                // Each cut's camera rides in a parallel file-local-id array (the cuts in toJson
                // carry times only): the sentinel marks a camera outside the saved set, and the
                // load pass drops those cuts rather than pointing them at a stranger.
                const CinematicComponentUVE& cinematic =
                    entityManager.GetComponentUVE<CinematicComponentUVE>(entity);
                nlohmann::json cutCameraLocalIds = nlohmann::json::array();
                for (const CinematicCameraCutUVE& cut : cinematic.cuts) {
                    std::uint32_t localId = std::numeric_limits<std::uint32_t>::max();
                    if (cut.camera != kInvalidEntityUVE) {
                        const auto targetIt = entityToLocalId.find(cut.camera);
                        if (targetIt != entityToLocalId.end()) {
                            localId = targetIt->second;
                        }
                    }
                    cutCameraLocalIds.push_back(localId);
                }
                componentsJson[*name]["cutCameraLocalIds"] = std::move(cutCameraLocalIds);
                nlohmann::json animTargetLocalIds = nlohmann::json::array();
                for (const CinematicAnimationKeyUVE& key : cinematic.animationKeys) {
                    std::uint32_t localId = std::numeric_limits<std::uint32_t>::max();
                    if (key.target != kInvalidEntityUVE) {
                        const auto targetIt = entityToLocalId.find(key.target);
                        if (targetIt != entityToLocalId.end()) {
                            localId = targetIt->second;
                        }
                    }
                    animTargetLocalIds.push_back(localId);
                }
                componentsJson[*name]["animTargetLocalIds"] = std::move(animTargetLocalIds);
            }
            if (type == std::type_index(typeid(Hitbox3DComponentUVE))) {
                const Hitbox3DComponentUVE& hitbox = entityManager.GetComponentUVE<Hitbox3DComponentUVE>(entity);
                std::int64_t ignoreLocalId = -1;
                if (hitbox.ignoreEntity != kInvalidEntityUVE) {
                    const auto ignoreIt = entityToLocalId.find(hitbox.ignoreEntity);
                    if (ignoreIt != entityToLocalId.end()) {
                        ignoreLocalId = static_cast<std::int64_t>(ignoreIt->second);
                    }
                }
                componentsJson[*name]["ignoreLocalId"] = ignoreLocalId;
            }
            if (type == std::type_index(typeid(Hurtbox3DComponentUVE))) {
                const Hurtbox3DComponentUVE& hurtbox = entityManager.GetComponentUVE<Hurtbox3DComponentUVE>(entity);
                std::int64_t ignoreLocalId = -1;
                if (hurtbox.ignoreEntity != kInvalidEntityUVE) {
                    const auto ignoreIt = entityToLocalId.find(hurtbox.ignoreEntity);
                    if (ignoreIt != entityToLocalId.end()) {
                        ignoreLocalId = static_cast<std::int64_t>(ignoreIt->second);
                    }
                }
                componentsJson[*name]["ignoreLocalId"] = ignoreLocalId;
            }
            if (type == std::type_index(typeid(InteractionArea3DComponentUVE))) {
                const InteractionArea3DComponentUVE& area =
                    entityManager.GetComponentUVE<InteractionArea3DComponentUVE>(entity);
                std::int64_t ignoreLocalId = -1;
                if (area.ignoreEntity != kInvalidEntityUVE) {
                    const auto ignoreIt = entityToLocalId.find(area.ignoreEntity);
                    if (ignoreIt != entityToLocalId.end()) {
                        ignoreLocalId = static_cast<std::int64_t>(ignoreIt->second);
                    }
                }
                componentsJson[*name]["ignoreLocalId"] = ignoreLocalId;
            }
            if (type == std::type_index(typeid(Projectile3DComponentUVE))) {
                const Projectile3DComponentUVE& projectile =
                    entityManager.GetComponentUVE<Projectile3DComponentUVE>(entity);
                std::int64_t ignoreLocalId = -1;
                if (projectile.ignoreEntity != kInvalidEntityUVE) {
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

void RollbackRestoredEntitiesUVE(IEntityManagerUVE& entityManager, std::vector<EntityUVE>& createdEntities) {
    for (auto entity = createdEntities.rbegin(); entity != createdEntities.rend(); ++entity) {
        if (entityManager.IsAliveUVE(*entity)) {
            entityManager.DestroyEntityUVE(*entity);
        }
    }
}

[[nodiscard]] std::optional<std::vector<EntityUVE>> DecodeScenePayloadUVE(
    IEntityManagerUVE& entityManager, const std::vector<std::byte>& payloadBuffer,
    const std::string_view sourceDescription, const std::optional<std::size_t> expectedRootCount = std::nullopt) {
    const std::string payloadText(reinterpret_cast<const char*>(payloadBuffer.data()), payloadBuffer.size());
    nlohmann::json payload;
    try {
        payload = nlohmann::json::parse(payloadText);
    } catch (const nlohmann::json::parse_error& parseError) {
        UVE_ERROR("SceneSerializerUVE: failed to parse \"{}\": {}", sourceDescription, parseError.what());
        return std::nullopt;
    }

    std::vector<std::pair<std::uint32_t, nlohmann::json>> orderedEntities;
    std::unordered_set<std::uint32_t> localIds;
    try {
        for (const nlohmann::json& entityJson : payload.at("entities")) {
            const std::uint32_t localId = entityJson.at("localId").get<std::uint32_t>();
            const nlohmann::json& components = entityJson.at("components");
            if (!components.is_object() || !localIds.emplace(localId).second) {
                UVE_ERROR("SceneSerializerUVE: malformed entity list in \"{}\"", sourceDescription);
                return std::nullopt;
            }
            for (const auto& [componentName, componentJson] : components.items()) {
                if (componentName == "DisabledComponentUVE") {
                    if (!componentJson.is_object()) {
                        UVE_ERROR("SceneSerializerUVE: malformed disabled-component data in \"{}\"", sourceDescription);
                        return std::nullopt;
                    }
                    continue;
                }
                if (componentName == "VisibilityComponentUVE") {
                    // Validated here and restored by the entity-aware path below, never through
                    // the registration table: it carries an entity reference, and only this
                    // function knows which file-local id maps to which restored entity.
                    if (!componentJson.is_object()) {
                        UVE_ERROR("SceneSerializerUVE: malformed visibility data in \"{}\"", sourceDescription);
                        return std::nullopt;
                    }
                    if (componentJson.contains("visibilityParentLocalId")) {
                        const auto& targetJson = componentJson.at("visibilityParentLocalId");
                        if (!targetJson.is_number_integer()) {
                            UVE_ERROR("SceneSerializerUVE: malformed visibility parent id in \"{}\"",
                                      sourceDescription);
                            return std::nullopt;
                        }
                        const std::int64_t targetLocalId = targetJson.get<std::int64_t>();
                        if (targetLocalId < -1 ||
                            (targetLocalId >= 0 && static_cast<std::uint64_t>(targetLocalId) >
                                                       std::numeric_limits<std::uint32_t>::max())) {
                            UVE_ERROR("SceneSerializerUVE: visibility parent local ID is outside the "
                                      "uint32 range in \"{}\"", sourceDescription);
                            return std::nullopt;
                        }
                    }
                    continue;
                }
                if (componentName == "HierarchyComponentUVE") {
                    if (!componentJson.is_object() || !componentJson.contains("parentLocalId") ||
                        !componentJson.at("parentLocalId").is_number_integer()) {
                        UVE_ERROR("SceneSerializerUVE: malformed hierarchy data in \"{}\"", sourceDescription);
                        return std::nullopt;
                    }
                    const std::int64_t parentLocalId = componentJson.at("parentLocalId").get<std::int64_t>();
                    if (parentLocalId < -1 ||
                        (parentLocalId >= 0 &&
                         static_cast<std::uint64_t>(parentLocalId) > std::numeric_limits<std::uint32_t>::max())) {
                        UVE_ERROR("SceneSerializerUVE: hierarchy parent local ID is outside the uint32 range in \"{}\"",
                                  sourceDescription);
                        return std::nullopt;
                    }
                    continue;
                }
                if (CanonicalComponentNameUVE(componentName) == "RayCast3DComponentUVE") {
                    // Validated here and restored by the entity-aware path below, never through the
                    // registration table: the exclusions are local ids only this function's table
                    // can resolve. The remaining fields are the table reader's business, checked
                    // during restore.
                    if (!componentJson.is_object()) {
                        UVE_ERROR("SceneSerializerUVE: malformed ray cast data in \"{}\"", sourceDescription);
                        return std::nullopt;
                    }
                    const nlohmann::json exclusionIds =
                        componentJson.value("exclusionsLocalIds", nlohmann::json::array());
                    if (!exclusionIds.is_array() || exclusionIds.size() > kMaximumRayCastExclusionsUVE) {
                        UVE_ERROR("SceneSerializerUVE: malformed ray cast exclusions in \"{}\"",
                                  sourceDescription);
                        return std::nullopt;
                    }
                    for (const auto& exclusionId : exclusionIds) {
                        if (!exclusionId.is_number_integer()) {
                            UVE_ERROR("SceneSerializerUVE: malformed ray cast exclusion id in \"{}\"",
                                      sourceDescription);
                            return std::nullopt;
                        }
                        const std::int64_t exclusionLocalId = exclusionId.get<std::int64_t>();
                        if (exclusionLocalId < 0 ||
                            static_cast<std::uint64_t>(exclusionLocalId) >
                                std::numeric_limits<std::uint32_t>::max()) {
                            UVE_ERROR("SceneSerializerUVE: ray cast exclusion id is outside the uint32 "
                                      "range in \"{}\"",
                                      sourceDescription);
                            return std::nullopt;
                        }
                    }
                    continue;
                }
                if (GetRegistrationsByNameUVE().find(std::string{CanonicalComponentNameUVE(componentName)}) ==
                    GetRegistrationsByNameUVE().end()) {
                    UVE_ERROR("SceneSerializerUVE: \"{}\" references unknown component type \"{}\"",
                              sourceDescription, componentName);
                    return std::nullopt;
                }
            }
            orderedEntities.emplace_back(localId, entityJson);
        }
    } catch (const nlohmann::json::exception& jsonError) {
        UVE_ERROR("SceneSerializerUVE: malformed entity list in \"{}\": {}", sourceDescription, jsonError.what());
        return std::nullopt;
    }

    if (expectedRootCount.has_value()) {
        std::size_t rootCount = 0U;
        for (const auto& [unusedLocalId, entityJson] : orderedEntities) {
            static_cast<void>(unusedLocalId);
            const auto& components = entityJson.at("components");
            bool hasKnownParent = false;
            if (components.contains("HierarchyComponentUVE")) {
                const std::int64_t parentLocalId =
                    components.at("HierarchyComponentUVE").at("parentLocalId").get<std::int64_t>();
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
            UVE_ERROR("SceneSerializerUVE: \"{}\" has {} roots but requires {} before entity creation",
                      sourceDescription, rootCount, *expectedRootCount);
            return std::nullopt;
        }
    }

    // Restore bypasses SceneGraphUVE::SetParentUVE(), so reject parent cycles before creating any
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
            if (!components.contains("HierarchyComponentUVE")) {
                break;
            }
            const std::int64_t parentLocalId =
                components.at("HierarchyComponentUVE").at("parentLocalId").get<std::int64_t>();
            if (parentLocalId < 0 ||
                static_cast<std::uint64_t>(parentLocalId) > std::numeric_limits<std::uint32_t>::max()) {
                break;
            }
            currentLocalId = static_cast<std::uint32_t>(parentLocalId);
        }
        if (steps == orderedEntities.size()) {
            UVE_ERROR("SceneSerializerUVE: \"{}\" contains a cyclic hierarchy", sourceDescription);
            return std::nullopt;
        }
    }

    std::unordered_map<std::uint32_t, EntityUVE> localIdToEntity;
    localIdToEntity.reserve(orderedEntities.size());
    std::vector<EntityUVE> createdEntities;
    createdEntities.reserve(orderedEntities.size());
    for (const auto& [localId, unusedEntityJson] : orderedEntities) {
        static_cast<void>(unusedEntityJson);
        const EntityUVE entity = entityManager.CreateEntityUVE();
        localIdToEntity.emplace(localId, entity);
        createdEntities.push_back(entity);
    }

    std::vector<EntityUVE> roots;
    try {
        for (const auto& [localId, entityJson] : orderedEntities) {
            const EntityUVE entity = localIdToEntity.at(localId);
            bool isRoot = true;
            bool hasTransform = false;
            const nlohmann::json& componentsJson = entityJson.at("components");
            // Whether this entity's own document carries a driver, read before the loop so the
            // legacy-migration branch below cannot depend on key order. It used to: "AnimationMixer"
            // (the name AnimationDriverComponentUVE had then) sorted ahead of "AnimationPlayer" and
            // "AnimationTree", so a saved driver was always visited first. Renaming those two to
            // AnimationSequencer and AnimationGraph moved AnimationGraph AHEAD of the driver, and a
            // graph then synthesized a throwaway driver that the real one collided with.
            const bool documentHasMixer = componentsJson.contains("AnimationDriverComponentUVE");
            for (const auto& [componentName, componentJson] : componentsJson.items()) {
                if (componentName == "DisabledComponentUVE") {
                    entityManager.AddComponentUVE<DisabledComponentUVE>(entity);
                    continue;
                }
                if (componentName == "VisibilityComponentUVE") {
                    VisibilityComponentUVE visibility;
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
                    entityManager.AddComponentUVE<VisibilityComponentUVE>(entity, visibility);
                    continue;
                }
                if (CanonicalComponentNameUVE(componentName) == "RayCast3DComponentUVE") {
                    entityManager.AddComponentUVE<RayCast3DComponentUVE>(
                        entity, RayCast3DComponentWithResolvedExclusionsUVE(componentJson, localIdToEntity));
                    continue;
                }
                if (CanonicalComponentNameUVE(componentName) == "ControllerComponentUVE") {
                    entityManager.AddComponentUVE<ControllerComponentUVE>(
                        entity, ControllerComponentWithResolvedPawnUVE(componentJson, localIdToEntity));
                    continue;
                }
                if (CanonicalComponentNameUVE(componentName) == "PawnComponentUVE") {
                    entityManager.AddComponentUVE<PawnComponentUVE>(
                        entity, PawnComponentWithResolvedControllerUVE(componentJson, localIdToEntity));
                    continue;
                }
                if (CanonicalComponentNameUVE(componentName) == "CameraFollowComponentUVE") {
                    entityManager.AddComponentUVE<CameraFollowComponentUVE>(
                        entity, CameraFollowComponentWithResolvedTargetUVE(componentJson, localIdToEntity));
                    continue;
                }
                if (CanonicalComponentNameUVE(componentName) == "Hitbox3DComponentUVE") {
                    entityManager.AddComponentUVE<Hitbox3DComponentUVE>(
                        entity, Hitbox3DComponentWithResolvedIgnoreUVE(componentJson, localIdToEntity));
                    continue;
                }
                if (CanonicalComponentNameUVE(componentName) == "Hurtbox3DComponentUVE") {
                    entityManager.AddComponentUVE<Hurtbox3DComponentUVE>(
                        entity, Hurtbox3DComponentWithResolvedIgnoreUVE(componentJson, localIdToEntity));
                    continue;
                }
                if (CanonicalComponentNameUVE(componentName) == "InteractionArea3DComponentUVE") {
                    entityManager.AddComponentUVE<InteractionArea3DComponentUVE>(
                        entity, InteractionArea3DComponentWithResolvedIgnoreUVE(componentJson, localIdToEntity));
                    continue;
                }
                if (CanonicalComponentNameUVE(componentName) == "Projectile3DComponentUVE") {
                    entityManager.AddComponentUVE<Projectile3DComponentUVE>(
                        entity, Projectile3DComponentWithResolvedIgnoreUVE(componentJson, localIdToEntity));
                    continue;
                }
                if (CanonicalComponentNameUVE(componentName) == "BoneAttachment3DComponentUVE") {
                    entityManager.AddComponentUVE<BoneAttachment3DComponentUVE>(
                        entity, BoneAttachment3DComponentWithResolvedSkeletonUVE(componentJson, localIdToEntity));
                    continue;
                }
                if (CanonicalComponentNameUVE(componentName) == "TwoBoneIK3DComponentUVE") {
                    entityManager.AddComponentUVE<TwoBoneIK3DComponentUVE>(
                        entity, TwoBoneIK3DComponentWithResolvedReferencesUVE(componentJson, localIdToEntity));
                    continue;
                }
                if (CanonicalComponentNameUVE(componentName) == "FullBodyIK3DComponentUVE") {
                    entityManager.AddComponentUVE<FullBodyIK3DComponentUVE>(
                        entity, FullBodyIK3DComponentWithResolvedReferencesUVE(componentJson, localIdToEntity));
                    continue;
                }
                if (CanonicalComponentNameUVE(componentName) == "CinematicComponentUVE") {
                    CinematicComponentUVE cinematic =
                        CinematicComponentWithResolvedReferencesUVE(componentJson, localIdToEntity);
                    if (!IsCinematicComponentValidUVE(cinematic)) {
                        throw std::runtime_error("Invalid CinematicComponentUVE payload");
                    }
                    entityManager.AddComponentUVE<CinematicComponentUVE>(entity, std::move(cinematic));
                    continue;
                }
                if (componentName == "HierarchyComponentUVE") {
                    const std::int64_t parentLocalId = componentJson.at("parentLocalId").get<std::int64_t>();
                    EntityUVE parent = kInvalidEntityUVE;
                    if (parentLocalId >= 0) {
                        const auto parentIt = localIdToEntity.find(static_cast<std::uint32_t>(parentLocalId));
                        if (parentIt != localIdToEntity.end()) {
                            parent = parentIt->second;
                            isRoot = false;
                        }
                    }
                    entityManager.AddComponentUVE<HierarchyComponentUVE>(entity, HierarchyComponentUVE{parent});
                    continue;
                }

                const std::string canonicalName{CanonicalComponentNameUVE(componentName)};
                const auto registrationIt = GetRegistrationsByNameUVE().find(canonicalName);
                if (registrationIt == GetRegistrationsByNameUVE().end()) {
                    throw std::runtime_error("unknown scene component: " + componentName);
                }
                registrationIt->second.fromJson(entityManager, entity, componentJson);
                hasTransform = hasTransform || componentName == "TransformComponentUVE";
                const bool isMixer = componentName == "AnimationDriverComponentUVE";
                const bool isMixerChild =
                    componentName == "AnimationSequencerComponentUVE" || componentName == "AnimationGraphComponentUVE";
                if (isMixer || isMixerChild) {
                    // A target the file does not contain stays unset, which means "the parent".
                    EntityUVE target = kInvalidEntityUVE;
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
                        !entityManager.HasComponentUVE<AnimationDriverComponentUVE>(entity)) {
                        AnimationDriverComponentUVE legacy = AnimationDriverFromJsonUVE(componentJson);
                        if (!IsAnimationDriverComponentValidUVE(legacy)) {
                            legacy = AnimationDriverComponentUVE{};
                        }
                        legacy.target = target;
                        entityManager.AddComponentUVE<AnimationDriverComponentUVE>(entity, legacy);
                    } else if (isMixer && entityManager.HasComponentUVE<AnimationDriverComponentUVE>(entity)) {
                        entityManager.GetComponentUVE<AnimationDriverComponentUVE>(entity).target = target;
                    }
                }
            }

            // AttachTransformUVE creates Transform+WorldTransform+Hierarchy together. Recreate the
            // derived component here so the next SceneGraphUVE update sees restored transforms.
            if (hasTransform && !entityManager.HasComponentUVE<WorldTransformComponentUVE>(entity)) {
                entityManager.AddComponentUVE<WorldTransformComponentUVE>(entity);
            }
            if (isRoot) {
                roots.push_back(entity);
            }
        }
    } catch (const nlohmann::json::exception& jsonError) {
        UVE_ERROR("SceneSerializerUVE: malformed component data in \"{}\": {}", sourceDescription,
                  jsonError.what());
        RollbackRestoredEntitiesUVE(entityManager, createdEntities);
        return std::nullopt;
    } catch (const std::exception& validationError) {
        UVE_ERROR("SceneSerializerUVE: invalid component data in \"{}\": {}", sourceDescription,
                  validationError.what());
        RollbackRestoredEntitiesUVE(entityManager, createdEntities);
        return std::nullopt;
    }

    return roots;
}

[[nodiscard]] bool ValidateSceneAssetTypeUVE(const SceneAssetTypeUVE assetType,
                                             const std::string_view sourceDescription) {
    if (IsSceneAssetTypeUVE(assetType)) {
        return true;
    }
    UVE_ERROR("SceneSerializerUVE: \"{}\" has unexpected asset type {}", sourceDescription,
              static_cast<std::uint32_t>(assetType));
    return false;
}

} // namespace

std::optional<SceneSnapshotUVE> SceneSerializerUVE::CaptureUVE(
    IEntityManagerUVE& entityManager, const std::vector<EntityUVE>& rootEntities,
    const SceneAssetTypeUVE assetType) const {
    if (!ValidateSceneAssetTypeUVE(assetType, "scene snapshot")) {
        return std::nullopt;
    }
    if (assetType == SceneAssetTypeUVE::Prefab && rootEntities.size() != 1U) {
        UVE_ERROR("SceneSerializerUVE: prefab snapshot requires exactly one root entity");
        return std::nullopt;
    }
    const std::optional<std::vector<std::byte>> payload =
        EncodeScenePayloadUVE(entityManager, rootEntities, "scene snapshot");
    if (!payload.has_value()) {
        return std::nullopt;
    }
    return SceneSnapshotUVE{Asset::EncodeUveFileEnvelopeUVE(assetType, *payload), assetType};
}

std::vector<EntityUVE> SceneSerializerUVE::RestoreUVE(IEntityManagerUVE& entityManager,
                                                       const SceneSnapshotUVE& snapshot) const {
    const auto envelope = Asset::DecodeUveFileEnvelopeUVE(snapshot.bytes, "scene snapshot");
    if (!envelope.has_value()) {
        return {};
    }
    const auto& [header, payload] = *envelope;
    if (!ValidateSceneAssetTypeUVE(header.assetType, "scene snapshot") || header.assetType != snapshot.assetType) {
        if (header.assetType != snapshot.assetType) {
            UVE_ERROR("SceneSerializerUVE: scene snapshot asset type metadata does not match its envelope");
        }
        return {};
    }
    const std::optional<std::vector<EntityUVE>> restored = DecodeScenePayloadUVE(
        entityManager, payload, "scene snapshot",
        header.assetType == SceneAssetTypeUVE::Prefab ? std::optional<std::size_t>{1U} : std::nullopt);
    return restored.value_or(std::vector<EntityUVE>{});
}

bool SceneSerializerUVE::SaveUVE(IEntityManagerUVE& entityManager, const std::vector<EntityUVE>& rootEntities,
                                  const std::filesystem::path& path, const SceneAssetTypeUVE assetType) {
    if (!ValidateSceneAssetTypeUVE(assetType, path.string())) {
        return false;
    }
    if (assetType == SceneAssetTypeUVE::Prefab && rootEntities.size() != 1U) {
        UVE_ERROR("SceneSerializerUVE: prefab save requires exactly one root entity for \"{}\"", path.string());
        return false;
    }
    const std::optional<std::vector<std::byte>> payload = EncodeScenePayloadUVE(entityManager, rootEntities, path.string());
    if (!payload.has_value()) {
        return false;
    }

    const std::filesystem::path temporaryPath = path.string() + ".uve_scene_tmp";
    std::error_code errorCode;
    std::filesystem::remove(temporaryPath, errorCode);
    errorCode.clear();
    if (!Asset::WriteUveFileUVE(temporaryPath, assetType, *payload)) {
        std::filesystem::remove(temporaryPath, errorCode);
        return false;
    }
    std::filesystem::rename(temporaryPath, path, errorCode);
    if (errorCode) {
        UVE_ERROR("SceneSerializerUVE: failed to publish temporary scene \"{}\" as \"{}\": {}",
                  temporaryPath.string(), path.string(), errorCode.message());
        std::filesystem::remove(temporaryPath, errorCode);
        return false;
    }
    return true;
}

std::vector<EntityUVE> SceneSerializerUVE::LoadUVE(IEntityManagerUVE& entityManager,
                                                    const std::filesystem::path& path) {
    const auto file = Asset::ReadUveFileUVE(path);
    if (!file.has_value()) {
        return {};
    }
    const auto& [header, payload] = *file;
    if (!ValidateSceneAssetTypeUVE(header.assetType, path.string())) {
        return {};
    }
    const std::optional<std::vector<EntityUVE>> restored = DecodeScenePayloadUVE(
        entityManager, payload, path.string(),
        header.assetType == SceneAssetTypeUVE::Prefab ? std::optional<std::size_t>{1U} : std::nullopt);
    return restored.value_or(std::vector<EntityUVE>{});
}

} // namespace UVE::Scene
