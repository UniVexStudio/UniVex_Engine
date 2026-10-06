// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#include "uve/scene/scene_serializer_uve.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <limits>
#include <optional>
#include <stdexcept>
#include <map>
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
#include "uve/component/animation_driver_component_uve.h"
#include "uve/component/animation_sequencer_component_uve.h"
#include "uve/component/animation_graph_component_uve.h"
#include "uve/component/area_component_uve.h"
#include "uve/component/audio_source_component_uve.h"
#include "uve/component/bone_modifier_component_uve.h"
#include "uve/component/camera_component_uve.h"
#include "uve/component/canvas_component_uve.h"
#include "uve/component/character_controller_component_uve.h"
#include "uve/component/collider_component_uve.h"
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
#include "uve/component/visibility_component_uve.h"
#include "uve/component/ui_button_component_uve.h"
#include "uve/component/ui_image_component_uve.h"
#include "uve/component/ui_text_component_uve.h"
#include "uve/component/world_transform_component_uve.h"

namespace UVE::Scene {

namespace {

// --- Math JSON helpers ---------------------------------------------------------------------

[[nodiscard]] nlohmann::json ToJsonUVE(const Math::Vector3UVE& vector) {
    return nlohmann::json::array({vector.x, vector.y, vector.z});
}

[[nodiscard]] Math::Vector3UVE Vector3FromJsonUVE(const nlohmann::json& json) {
    return Math::Vector3UVE{json.at(0).get<float>(), json.at(1).get<float>(), json.at(2).get<float>()};
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

[[nodiscard]] nlohmann::json ToJsonUVE(const MeshComponentUVE& component) {
    return {{"meshGuid", component.meshGuid.value},
            {"materialGuid", component.materialGuid.value},
            {"visibilityLayers", component.visibilityLayers}};
}

[[nodiscard]] nlohmann::json ToJsonUVE(const PrimitiveMeshComponentUVE& component) {
    return {{"kind", static_cast<std::uint8_t>(component.kind)}, {"baseColor", ToJsonUVE(component.baseColor)}};
}

[[nodiscard]] nlohmann::json ToJsonUVE(const LightComponentUVE& component) {
    return {{"color", ToJsonUVE(component.color)},
            {"intensity", component.intensity},
            {"type", static_cast<std::uint8_t>(component.type)},
            {"range", component.range},
            {"spotAngleDegrees", component.spotAngleDegrees}};
}

[[nodiscard]] nlohmann::json ToJsonUVE(const CameraComponentUVE& component) {
    return {
        {"fieldOfViewDegrees", component.fieldOfViewDegrees},
        {"nearPlane", component.nearPlane},
        {"farPlane", component.farPlane},
        {"projection", static_cast<std::uint8_t>(component.projection)},
        {"orthographicSize", component.orthographicSize},
        {"current", component.current},
    };
}

[[nodiscard]] nlohmann::json ToJsonUVE(const NameComponentUVE& component) {
    return {{"name", component.name}};
}

[[nodiscard]] nlohmann::json ToJsonUVE(const ColliderComponentUVE& component) {
    return {{"halfExtents", ToJsonUVE(component.halfExtents)},
            {"collisionLayer", component.collisionLayer},
            {"collisionMask", component.collisionMask},
            {"friction", component.friction},
            {"restitution", component.restitution},
            {"density", component.density},
            {"shapeType", static_cast<std::uint8_t>(component.shapeType)},
            {"radius", component.radius},
            {"height", component.height},
            {"disabled", component.disabled}};
}

[[nodiscard]] nlohmann::json ToJsonUVE(const AreaComponentUVE& component) {
    return {{"halfExtents", ToJsonUVE(component.halfExtents)},
            {"collisionLayer", component.collisionLayer},
            {"collisionMask", component.collisionMask},
            {"monitoring", component.monitoring},
            {"monitorable", component.monitorable},
            {"gravityOverride", static_cast<std::uint8_t>(component.gravityOverride)},
            {"gravityDirection", ToJsonUVE(component.gravityDirection)},
            {"gravityMagnitude", component.gravityMagnitude},
            {"gravityPoint", component.gravityPoint},
            {"gravityPointOffset", ToJsonUVE(component.gravityPointOffset)},
            {"gravityPointUnitDistance", component.gravityPointUnitDistance},
            {"linearDampOverride", static_cast<std::uint8_t>(component.linearDampOverride)},
            {"linearDamp", component.linearDamp},
            {"angularDampOverride", static_cast<std::uint8_t>(component.angularDampOverride)},
            {"angularDamp", component.angularDamp},
            {"priority", component.priority}};
}

[[nodiscard]] nlohmann::json ToJsonUVE(const Rigid3DComponentUVE& component) {
    return {{"mass", component.mass},
            {"isKinematic", component.isKinematic},
            {"velocity", ToJsonUVE(component.velocity)},
            {"angularVelocity", ToJsonUVE(component.angularVelocity)},
            {"torque", ToJsonUVE(component.torque)},
            {"inverseInertia", ToJsonUVE(component.inverseInertia)},
            {"drag", component.drag},
            {"gravityScale", component.gravityScale}};
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

[[nodiscard]] nlohmann::json ToJsonUVE(const CanvasComponentUVE& component) {
    return {{"visible", component.visible}, {"sortOrder", component.sortOrder}};
}

[[nodiscard]] nlohmann::json ToJsonUVE(const UITextComponentUVE& component) {
    return {{"text", component.text},
            {"positionPixels", ToJsonUVE(component.positionPixels)},
            {"fontSize", component.fontSize},
            {"color", ToJsonUVE(component.color)},
            {"alpha", component.alpha}};
}

[[nodiscard]] nlohmann::json ToJsonUVE(const UIImageComponentUVE& component) {
    return {{"textureAssetGuid", component.textureAssetGuid.value},
            {"positionPixels", ToJsonUVE(component.positionPixels)},
            {"sizePixels", ToJsonUVE(component.sizePixels)},
            {"tintColor", ToJsonUVE(component.tintColor)},
            {"alpha", component.alpha}};
}

[[nodiscard]] nlohmann::json ToJsonUVE(const UIButtonComponentUVE& component) {
    return {{"positionPixels", ToJsonUVE(component.positionPixels)},
            {"sizePixels", ToJsonUVE(component.sizePixels)},
            {"normalColor", ToJsonUVE(component.normalColor)},
            {"hoverColor", ToJsonUVE(component.hoverColor)},
            {"pressedColor", ToJsonUVE(component.pressedColor)},
            {"isHovered", component.isHovered},
            {"wasClickedThisFrame", component.wasClickedThisFrame}};
}

[[nodiscard]] nlohmann::json ToJsonUVE(const AudioSourceComponentUVE& component) {
    return {{"audioAssetPath", component.audioAssetPath},
            {"mixerGroup", component.mixerGroup},
            {"volume", component.volume},
            {"looping", component.looping},
            {"pitch", component.pitch},
            {"spatial", component.spatial},
            {"minDistance", component.minDistance},
            {"maxDistance", component.maxDistance},
            {"attenuationCurve", static_cast<std::uint8_t>(component.attenuationCurve)},
            {"playOnAwake", component.playOnAwake}};
}

[[nodiscard]] nlohmann::json ToJsonUVE(const ScriptComponentUVE& component) {
    nlohmann::json json{{"scriptAssetPath", component.scriptAssetPath}};
    if (!component.exportValues.empty()) {
        json["exportValues"] = component.exportValues;
    }
    return json;
}

[[nodiscard]] nlohmann::json ToJsonUVE(const ParticleEmitterComponentUVE& component) {
    return {{"maxParticles", component.maxParticles},
            {"emitting", component.emitting},
            {"emissionRate", component.emissionRate},
            {"lifetimeSeconds", component.lifetimeSeconds}};
}

[[nodiscard]] nlohmann::json ToJsonUVE(const PhysicsInterpolationComponentUVE& component) {
    // Only `mode` is the authored switch (the component's own doc comment). The pose fields are
    // runtime-computed by SceneGraphUVE::UpdateUVE every frame and must never be persisted - a
    // saved pose from one session would be stale the instant it loaded into another.
    return {{"mode", std::to_underlying(component.mode)}};
}

[[nodiscard]] nlohmann::json ToJsonUVE(const ProcessComponentUVE& component) {
    // resolvedModeInHierarchy is deliberately absent, for the same reason the interpolation
    // component's pose fields are: SceneGraphUVE::UpdateUVE recomputes it from the hierarchy on
    // every update, so persisting it would restore an answer that is already being replaced.
    return {{"mode", std::to_underlying(component.mode)},
            {"priority", component.priority},
            {"physicsPriority", component.physicsPriority}};
}

[[nodiscard]] nlohmann::json ToJsonUVE(const ThreadGroupComponentUVE& component) {
    return {{"mode", std::to_underlying(component.mode)},
            {"order", component.order}};
}

[[nodiscard]] nlohmann::json ToJsonUVE(const BoneModifierComponentUVE& component) {
    return {{"active", component.active}, {"influence", component.influence}};
}

[[nodiscard]] nlohmann::json ToJsonUVE(const PhysicsObjectComponentUVE& component) {
    return {{"disableMode", std::to_underlying(component.disableMode)},
            {"collisionPriority", component.collisionPriority},
            {"inputRayPickable", component.inputRayPickable},
            {"inputCaptureOnDrag", component.inputCaptureOnDrag}};
}

[[nodiscard]] nlohmann::json ToJsonUVE(const SolidBodyComponentUVE& component) {
    return {{"lockMotionX", component.lockMotionX},
            {"lockMotionY", component.lockMotionY},
            {"lockMotionZ", component.lockMotionZ}};
}

[[nodiscard]] nlohmann::json ToJsonUVE(const RenderInstanceComponentUVE& component) {
    return {{"renderLayers", component.renderLayers},
            {"sortingOffset", component.sortingOffset},
            {"sortingUseAabbCenter", component.sortingUseAabbCenter}};
}

[[nodiscard]] nlohmann::json ToJsonUVE(const AutoTranslateComponentUVE& component) {
    return {{"mode", std::to_underlying(component.mode)}};
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

[[nodiscard]] nlohmann::json ToJsonUVE(const OutlinerViewportComponentUVE&) {
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

[[nodiscard]] nlohmann::json ToJsonUVE(const Kinematic3DComponentUVE& value) {
    return {{"targetVelocity", ToJsonUVE(value.targetVelocity)},
            {"interpolation", value.interpolation},
            {"active", value.active}};
}

[[nodiscard]] Kinematic3DComponentUVE Kinematic3DObjectFromJsonUVE(const nlohmann::json& json) {
    return Kinematic3DComponentUVE{Vector3FromJsonUVE(json.at("targetVelocity")),
                                            json.value("interpolation", 1.0F), json.value("active", true)};
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

[[nodiscard]] nlohmann::json ToJsonUVE(const NavSeeker3DComponentUVE& value) {
    // The route and its state - nextPathPosition, desiredVelocity, pathStatus, pathChanged,
    // targetReached - are what the last step computed, not what an author set, so they are not
    // written: a loaded agent finds its own route from `targetPosition` on the first step.
    return {{"targetPosition", ToJsonUVE(value.targetPosition)},
            {"radius", value.radius},
            {"height", value.height},
            {"maxSpeed", value.maxSpeed},
            {"acceleration", value.acceleration},
            {"pathUpdateInterval", value.pathUpdateInterval},
            {"waypointRadius", value.waypointRadius},
            {"targetTolerance", value.targetTolerance},
            {"slowDownRadius", value.slowDownRadius},
            {"avoidanceRadius", value.avoidanceRadius},
            {"navigationLayers", value.navigationLayers},
            {"avoidanceEnabled", value.avoidanceEnabled},
            {"enabled", value.enabled}};
}

[[nodiscard]] NavSeeker3DComponentUVE NavSeeker3DObjectFromJsonUVE(const nlohmann::json& json) {
    NavSeeker3DComponentUVE value;
    value.targetPosition = Vector3FromJsonUVE(json.at("targetPosition"));
    value.radius = json.value("radius", 0.5F);
    value.height = json.value("height", 1.8F);
    value.maxSpeed = json.value("maxSpeed", 4.0F);
    value.acceleration = json.value("acceleration", 20.0F);
    value.pathUpdateInterval = json.value("pathUpdateInterval", 0.1F);
    value.waypointRadius = json.value("waypointRadius", 0.4F);
    value.targetTolerance = json.value("targetTolerance", 1.0F);
    value.slowDownRadius = json.value("slowDownRadius", 1.5F);
    value.avoidanceRadius = json.value("avoidanceRadius", 2.0F);
    value.navigationLayers = json.value("navigationLayers", std::uint32_t{1});
    value.avoidanceEnabled = json.value("avoidanceEnabled", true);
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

[[nodiscard]] nlohmann::json ToJsonUVE(const SpringArm3DComponentUVE& value) {
    return {{"armLength", value.armLength},
            {"margin", value.margin},
            {"smoothing", value.smoothing},
            {"collisionMask", value.collisionMask},
            {"enabled", value.enabled}};
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

[[nodiscard]] nlohmann::json ToJsonUVE(const WorldEnvironment3DComponentUVE& value) {
    return {{"skyAssetPath", value.skyAssetPath},
            {"ambientColor", ToJsonUVE(value.ambientColor)},
            {"fogColor", ToJsonUVE(value.fogColor)},
            {"ambientEnergy", value.ambientEnergy},
            {"exposure", value.exposure},
            {"fogDensity", value.fogDensity},
            {"fogEnabled", value.fogEnabled},
            {"postProcessingEnabled", value.postProcessingEnabled},
            {"skyColor", ToJsonUVE(value.skyColor)},
            {"horizonColor", ToJsonUVE(value.horizonColor)},
            {"groundColor", ToJsonUVE(value.groundColor)},
            {"skyCurve", value.skyCurve},
            {"groundCurve", value.groundCurve},
            {"fogSkyAffect", value.fogSkyAffect},
            {"bloomEnabled", value.bloomEnabled},
            {"bloomIntensity", value.bloomIntensity},
            {"bloomThreshold", value.bloomThreshold},
            {"ssaoEnabled", value.ssaoEnabled},
            {"ssaoIntensity", value.ssaoIntensity},
            {"ssaoRadius", value.ssaoRadius},
            {"brightness", value.brightness},
            {"contrast", value.contrast},
            {"saturation", value.saturation},
            {"colorFilter", ToJsonUVE(value.colorFilter)},
            {"fogHeight", value.fogHeight},
            {"fogHeightFalloff", value.fogHeightFalloff},
            {"fogSunScatter", value.fogSunScatter}};
}

[[nodiscard]] WorldEnvironment3DComponentUVE WorldEnvironment3DObjectFromJsonUVE(const nlohmann::json& json) {
    WorldEnvironment3DComponentUVE value;
    value.skyAssetPath = json.value("skyAssetPath", std::string{});
    value.ambientColor = Vector3FromJsonUVE(json.at("ambientColor"));
    value.fogColor = Vector3FromJsonUVE(json.at("fogColor"));
    value.ambientEnergy = json.value("ambientEnergy", 1.0F);
    value.exposure = json.value("exposure", 1.0F);
    value.fogDensity = json.value("fogDensity", 0.0F);
    value.fogEnabled = json.value("fogEnabled", false);
    value.postProcessingEnabled = json.value("postProcessingEnabled", true);
    if (json.contains("skyColor")) {
        value.skyColor = Vector3FromJsonUVE(json.at("skyColor"));
    }
    if (json.contains("horizonColor")) {
        value.horizonColor = Vector3FromJsonUVE(json.at("horizonColor"));
    }
    if (json.contains("groundColor")) {
        value.groundColor = Vector3FromJsonUVE(json.at("groundColor"));
    }
    value.skyCurve = json.value("skyCurve", value.skyCurve);
    value.groundCurve = json.value("groundCurve", value.groundCurve);
    value.fogSkyAffect = json.value("fogSkyAffect", value.fogSkyAffect);
    value.bloomEnabled = json.value("bloomEnabled", value.bloomEnabled);
    value.bloomIntensity = json.value("bloomIntensity", value.bloomIntensity);
    value.bloomThreshold = json.value("bloomThreshold", value.bloomThreshold);
    value.ssaoEnabled = json.value("ssaoEnabled", value.ssaoEnabled);
    value.ssaoIntensity = json.value("ssaoIntensity", value.ssaoIntensity);
    value.ssaoRadius = json.value("ssaoRadius", value.ssaoRadius);
    value.brightness = json.value("brightness", value.brightness);
    value.contrast = json.value("contrast", value.contrast);
    value.saturation = json.value("saturation", value.saturation);
    if (json.contains("colorFilter")) {
        value.colorFilter = Vector3FromJsonUVE(json.at("colorFilter"));
    }
    value.fogHeight = json.value("fogHeight", value.fogHeight);
    value.fogHeightFalloff = json.value("fogHeightFalloff", value.fogHeightFalloff);
    value.fogSunScatter = json.value("fogSunScatter", value.fogSunScatter);
    return value;
}

[[nodiscard]] nlohmann::json ToJsonUVE(const ReflectionProbe3DComponentUVE& value) {
    return {{"size", ToJsonUVE(value.size)},
            {"visibilityLayers", value.visibilityLayers},
            {"updateMode", static_cast<std::uint8_t>(value.updateMode)},
            {"enabled", value.enabled}};
}

[[nodiscard]] ReflectionProbe3DComponentUVE ReflectionProbe3DObjectFromJsonUVE(const nlohmann::json& json) {
    ReflectionProbe3DComponentUVE value;
    value.size = Vector3FromJsonUVE(json.at("size"));
    value.visibilityLayers = json.value("visibilityLayers", std::uint32_t{0xFFFFFFFFU});
    value.updateMode = static_cast<ReflectionProbeUpdateModeUVE>(json.value("updateMode", std::uint8_t{0}));
    value.enabled = json.value("enabled", true);
    return value;
}

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

[[nodiscard]] nlohmann::json ToJsonUVE(const DirectionalLight3DComponentUVE& value) {
    return {{"shadowMaxDistance", value.shadowMaxDistance}, {"shadowSplitBlend", value.shadowSplitBlend}};
}

[[nodiscard]] DirectionalLight3DComponentUVE DirectionalLight3DFromJsonUVE(const nlohmann::json& json) {
    const DirectionalLight3DComponentUVE defaults{};
    DirectionalLight3DComponentUVE value{};
    value.shadowMaxDistance = json.value("shadowMaxDistance", defaults.shadowMaxDistance);
    value.shadowSplitBlend = json.value("shadowSplitBlend", defaults.shadowSplitBlend);
    return value;
}

[[nodiscard]] nlohmann::json ToJsonUVE(const FogVolume3DComponentUVE& value) {
    return {{"shape", static_cast<std::uint8_t>(value.shape)},
            {"size", ToJsonUVE(value.size)},
            {"density", value.density},
            {"albedo", ToJsonUVE(value.albedo)},
            {"emission", ToJsonUVE(value.emission)},
            {"heightFalloff", value.heightFalloff},
            {"edgeFade", value.edgeFade},
            {"materialAssetPath", value.materialAssetPath}};
}

[[nodiscard]] FogVolume3DComponentUVE FogVolume3DObjectFromJsonUVE(const nlohmann::json& json) {
    FogVolume3DComponentUVE value{};
    value.shape = static_cast<FogVolumeShapeUVE>(json.at("shape").get<std::uint8_t>());
    value.size = Vector3FromJsonUVE(json.at("size"));
    value.density = ReadFloatUVE(json.at("density"));
    value.albedo = Vector3FromJsonUVE(json.at("albedo"));
    value.emission = Vector3FromJsonUVE(json.at("emission"));
    value.heightFalloff = ReadFloatUVE(json.at("heightFalloff"));
    value.edgeFade = ReadFloatUVE(json.at("edgeFade"));
    value.materialAssetPath = json.at("materialAssetPath").get<std::string>();
    return value;
}

[[nodiscard]] nlohmann::json ToJsonUVE(const SurfaceInstanceComponentUVE& value) {
    return {{"materialOverridePath", value.materialOverridePath},
            {"materialOverlayPath", value.materialOverlayPath},
            {"transparency", value.transparency},
            {"castShadow", static_cast<std::uint8_t>(value.castShadow)},
            {"extraCullMargin", value.extraCullMargin},
            {"lodBias", value.lodBias},
            {"ignoreOcclusionCulling", value.ignoreOcclusionCulling},
            {"lightingMode", static_cast<std::uint8_t>(value.lightingMode)},
            {"visibilityRangeBegin", value.visibilityRangeBegin},
            {"visibilityRangeBeginMargin", value.visibilityRangeBeginMargin},
            {"visibilityRangeEnd", value.visibilityRangeEnd},
            {"visibilityRangeEndMargin", value.visibilityRangeEndMargin},
            {"visibilityRangeFadeMode", static_cast<std::uint8_t>(value.visibilityRangeFadeMode)}};
}

[[nodiscard]] SurfaceInstanceComponentUVE SurfaceInstanceFromJsonUVE(const nlohmann::json& json) {
    SurfaceInstanceComponentUVE value{};
    value.materialOverridePath = json.at("materialOverridePath").get<std::string>();
    value.materialOverlayPath = json.at("materialOverlayPath").get<std::string>();
    value.transparency = ReadFloatUVE(json.at("transparency"));
    value.castShadow = static_cast<SurfaceShadowModeUVE>(json.at("castShadow").get<std::uint8_t>());
    value.extraCullMargin = ReadFloatUVE(json.at("extraCullMargin"));
    value.lodBias = ReadFloatUVE(json.at("lodBias"));
    value.ignoreOcclusionCulling = json.at("ignoreOcclusionCulling").get<bool>();
    value.lightingMode = static_cast<SurfaceLightingModeUVE>(json.at("lightingMode").get<std::uint8_t>());
    value.visibilityRangeBegin = ReadFloatUVE(json.at("visibilityRangeBegin"));
    value.visibilityRangeBeginMargin = ReadFloatUVE(json.at("visibilityRangeBeginMargin"));
    value.visibilityRangeEnd = ReadFloatUVE(json.at("visibilityRangeEnd"));
    value.visibilityRangeEndMargin = ReadFloatUVE(json.at("visibilityRangeEndMargin"));
    value.visibilityRangeFadeMode =
        static_cast<SurfaceFadeModeUVE>(json.at("visibilityRangeFadeMode").get<std::uint8_t>());
    return value;
}

[[nodiscard]] nlohmann::json ToJsonUVE(const LightEmitterComponentUVE& value) {
    return {{"color", ToJsonUVE(value.color)},
            {"energy", value.energy},
            {"indirectEnergy", value.indirectEnergy},
            {"volumetricFogEnergy", value.volumetricFogEnergy},
            {"specular", value.specular},
            {"negative", value.negative},
            {"bakeMode", static_cast<std::uint8_t>(value.bakeMode)},
            {"cullMask", value.cullMask},
            {"shadowEnabled", value.shadowEnabled},
            {"shadowBias", value.shadowBias},
            {"shadowNormalBias", value.shadowNormalBias},
            {"shadowOpacity", value.shadowOpacity},
            {"shadowBlur", value.shadowBlur},
            {"distanceFadeEnabled", value.distanceFadeEnabled},
            {"distanceFadeBegin", value.distanceFadeBegin},
            {"distanceFadeShadow", value.distanceFadeShadow},
            {"distanceFadeLength", value.distanceFadeLength}};
}

[[nodiscard]] LightEmitterComponentUVE LightEmitterFromJsonUVE(const nlohmann::json& json) {
    LightEmitterComponentUVE value{};
    value.color = Vector3FromJsonUVE(json.at("color"));
    value.energy = ReadFloatUVE(json.at("energy"));
    value.indirectEnergy = ReadFloatUVE(json.at("indirectEnergy"));
    value.volumetricFogEnergy = ReadFloatUVE(json.at("volumetricFogEnergy"));
    value.specular = ReadFloatUVE(json.at("specular"));
    value.negative = json.at("negative").get<bool>();
    value.bakeMode = static_cast<LightBakeModeUVE>(json.at("bakeMode").get<std::uint8_t>());
    value.cullMask = json.at("cullMask").get<std::uint32_t>();
    value.shadowEnabled = json.at("shadowEnabled").get<bool>();
    value.shadowBias = ReadFloatUVE(json.at("shadowBias"));
    value.shadowNormalBias = ReadFloatUVE(json.at("shadowNormalBias"));
    value.shadowOpacity = ReadFloatUVE(json.at("shadowOpacity"));
    value.shadowBlur = ReadFloatUVE(json.at("shadowBlur"));
    value.distanceFadeEnabled = json.at("distanceFadeEnabled").get<bool>();
    value.distanceFadeBegin = ReadFloatUVE(json.at("distanceFadeBegin"));
    value.distanceFadeShadow = ReadFloatUVE(json.at("distanceFadeShadow"));
    value.distanceFadeLength = ReadFloatUVE(json.at("distanceFadeLength"));
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

[[nodiscard]] nlohmann::json ToJsonUVE(const Occluder3DComponentUVE& value) {
    return {{"halfExtents", ToJsonUVE(value.halfExtents)},
            {"mode", static_cast<std::uint8_t>(value.mode)},
            {"enabled", value.enabled}};
}

[[nodiscard]] Occluder3DComponentUVE Occluder3DObjectFromJsonUVE(const nlohmann::json& json) {
    return Occluder3DComponentUVE{Vector3FromJsonUVE(json.at("halfExtents")),
                                      static_cast<Occluder3DObjectModeUVE>(json.value("mode", std::uint8_t{0})),
                                      json.value("enabled", true)};
}

[[nodiscard]] nlohmann::json ToJsonUVE(const VisibilityRegion3DComponentUVE& value) {
    return {{"halfExtents", ToJsonUVE(value.halfExtents)},
            {"visibilityLayers", value.visibilityLayers},
            {"enabled", value.enabled}};
}

[[nodiscard]] VisibilityRegion3DComponentUVE VisibilityRegion3DObjectFromJsonUVE(const nlohmann::json& json) {
    return VisibilityRegion3DComponentUVE{Vector3FromJsonUVE(json.at("halfExtents")),
                                              json.value("visibilityLayers", std::uint32_t{0xFFFFFFFFU}),
                                              json.value("enabled", true), true};
}

[[nodiscard]] nlohmann::json ToJsonUVE(const SpawnPoint3DComponentUVE& value) {
    return {{"spawnTag", value.spawnTag},
            {"localPosition", ToJsonUVE(value.localPosition)},
            {"localRotation", ToJsonUVE(value.localRotation)},
            {"enabled", value.enabled},
            {"oneShot", value.oneShot}};
}

[[nodiscard]] SpawnPoint3DComponentUVE SpawnPoint3DObjectFromJsonUVE(const nlohmann::json& json) {
    return SpawnPoint3DComponentUVE{json.value("spawnTag", std::string{"spawn"}),
                                        Vector3FromJsonUVE(json.at("localPosition")),
                                        QuaternionFromJsonUVE(json.at("localRotation")),
                                        json.value("enabled", true),
                                        json.value("oneShot", false)};
}

[[nodiscard]] nlohmann::json ToJsonUVE(const PlayerComponentUVE& value) {
    return {{"possessOnPlay", value.possessOnPlay},
            {"lookEnabled", value.lookEnabled},
            {"lookSensitivity", value.lookSensitivity},
            {"lookStickSpeedDegrees", value.lookStickSpeedDegrees},
            {"minPitchDegrees", value.minPitchDegrees},
            {"maxPitchDegrees", value.maxPitchDegrees}};
}

[[nodiscard]] PlayerComponentUVE Player3DObjectFromJsonUVE(const nlohmann::json& json) {
    PlayerComponentUVE value{};
    value.possessOnPlay = json.value("possessOnPlay", value.possessOnPlay);
    value.lookEnabled = json.value("lookEnabled", value.lookEnabled);
    value.lookSensitivity = json.value("lookSensitivity", value.lookSensitivity);
    value.lookStickSpeedDegrees = json.value("lookStickSpeedDegrees", value.lookStickSpeedDegrees);
    value.minPitchDegrees = json.value("minPitchDegrees", value.minPitchDegrees);
    value.maxPitchDegrees = json.value("maxPitchDegrees", value.maxPitchDegrees);
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
    };
    const auto it = kLegacyNames.find(name);
    return it != kLegacyNames.end() ? it->second : name;
}

[[nodiscard]] const std::unordered_map<std::string, ComponentRegistrationUVE>& GetRegistrationsByNameUVE() {
    static const std::unordered_map<std::string, ComponentRegistrationUVE> registrations = [] {
        std::unordered_map<std::string, ComponentRegistrationUVE> table;

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
        table.emplace("MeshComponentUVE", MakeRegistrationUVE<MeshComponentUVE>([](const nlohmann::json& json) {
                          // visibilityLayers defaults through json.value on purpose: scenes saved
                          // before the field existed load unchanged, while meshGuid/materialGuid
                          // stay REQUIRED so the malformed-payload rollback tests keep their teeth.
                          MeshComponentUVE mesh{Asset::AssetGuidUVE{json.at("meshGuid").get<std::uint64_t>()},
                                                 Asset::AssetGuidUVE{json.at("materialGuid").get<std::uint64_t>()}};
                          mesh.visibilityLayers = json.value("visibilityLayers", std::uint32_t{0x00000001U});
                          if (!IsMeshComponentValidUVE(mesh)) {
                              throw std::runtime_error("Invalid MeshComponentUVE payload");
                          }
                          return mesh;
                      }, IsMeshComponentValidUVE));
        table.emplace("PrimitiveMeshComponentUVE",
                      MakeRegistrationUVE<PrimitiveMeshComponentUVE>([](const nlohmann::json& json) {
                          PrimitiveMeshComponentUVE primitive;
                          primitive.kind = static_cast<PrimitiveMeshKindUVE>(json.at("kind").get<std::uint8_t>());
                          primitive.baseColor = Vector3FromJsonUVE(json.at("baseColor"));
                          if (!IsPrimitiveMeshComponentValidUVE(primitive)) {
                              throw std::runtime_error("Invalid PrimitiveMeshComponentUVE payload");
                          }
                          return primitive;
                      }, IsPrimitiveMeshComponentValidUVE));
        table.emplace("LightComponentUVE", MakeRegistrationUVE<LightComponentUVE>([](const nlohmann::json& json) {
                          LightComponentUVE light;
                          light.color = Vector3FromJsonUVE(json.at("color"));
                          light.intensity = json.at("intensity").get<float>();
                          light.type = static_cast<LightTypeUVE>(
                              json.value("type", static_cast<std::uint8_t>(LightTypeUVE::Directional)));
                          light.range = json.value("range", 10.0F);
                          light.spotAngleDegrees = json.value("spotAngleDegrees", 45.0F);
                          if (!IsLightComponentValidUVE(light)) {
                              throw std::runtime_error("Invalid LightComponentUVE payload");
                          }
                          return light;
                      }, IsLightComponentValidUVE));
        table.emplace("CameraComponentUVE", MakeRegistrationUVE<CameraComponentUVE>([](const nlohmann::json& json) {
                          CameraComponentUVE camera;
                          camera.fieldOfViewDegrees = json.at("fieldOfViewDegrees").get<float>();
                          camera.nearPlane = json.at("nearPlane").get<float>();
                          camera.farPlane = json.at("farPlane").get<float>();
                          camera.projection = static_cast<CameraProjectionModeUVE>(json.value(
                              "projection", static_cast<std::uint8_t>(CameraProjectionModeUVE::Perspective)));
                          camera.orthographicSize = json.value("orthographicSize", 5.0F);
                          camera.current = json.value("current", false);
                          if (!IsCameraComponentValidUVE(camera)) {
                              throw std::runtime_error("Invalid CameraComponentUVE payload");
                          }
                          return camera;
                      }, IsCameraComponentValidUVE));
        table.emplace("NameComponentUVE", MakeRegistrationUVE<NameComponentUVE>([](const nlohmann::json& json) {
                          const NameComponentUVE component{json.at("name").get<std::string>()};
                          if (!IsNameComponentValidUVE(component)) {
                              throw std::runtime_error("Invalid NameComponentUVE payload");
                          }
                          return component;
                      }, IsNameComponentValidUVE));
        table.emplace("ColliderComponentUVE", MakeRegistrationUVE<ColliderComponentUVE>([](const nlohmann::json& json) {
                          ColliderComponentUVE collider;
                          collider.halfExtents = Vector3FromJsonUVE(json.at("halfExtents"));
                          collider.collisionLayer = json.value("collisionLayer", std::uint32_t{1});
                          collider.collisionMask = json.value("collisionMask", std::uint32_t{0xFFFFFFFFU});
                          collider.friction = json.value("friction", 0.0F);
                          collider.restitution = json.value("restitution", 0.0F);
                          collider.density = json.value("density", 1.0F);
                          collider.shapeType = static_cast<ColliderShapeTypeUVE>(
                              json.value("shapeType", std::uint8_t{0}));
                          collider.radius = json.value("radius", 0.5F);
                          collider.height = json.value("height", 1.0F);
                          collider.disabled = json.value("disabled", false);
                          if (!IsColliderComponentValidUVE(collider)) {
                              throw std::runtime_error("Invalid ColliderComponentUVE payload");
                          }
                          return collider;
                      }, IsColliderComponentValidUVE));
        table.emplace("AreaComponentUVE", MakeRegistrationUVE<AreaComponentUVE>([](const nlohmann::json& json) {
                          AreaComponentUVE area;
                          area.halfExtents = Vector3FromJsonUVE(json.at("halfExtents"));
                          area.collisionLayer = json.value("collisionLayer", std::uint32_t{1});
                          area.collisionMask = json.value("collisionMask", std::uint32_t{0xFFFFFFFFU});
                          area.monitoring = json.value("monitoring", true);
                          area.monitorable = json.value("monitorable", true);
                          area.gravityOverride = static_cast<AreaSpaceOverrideModeUVE>(
                              json.value("gravityOverride", static_cast<std::uint8_t>(0)));
                          if (json.contains("gravityDirection")) {
                              area.gravityDirection = Vector3FromJsonUVE(json.at("gravityDirection"));
                          }
                          area.gravityMagnitude =
                              json.value("gravityMagnitude", kDefaultAreaGravityMagnitudeUVE);
                          area.gravityPoint = json.value("gravityPoint", false);
                          if (json.contains("gravityPointOffset")) {
                              area.gravityPointOffset = Vector3FromJsonUVE(json.at("gravityPointOffset"));
                          }
                          area.gravityPointUnitDistance = json.value("gravityPointUnitDistance", 0.0F);
                          area.linearDampOverride = static_cast<AreaSpaceOverrideModeUVE>(
                              json.value("linearDampOverride", static_cast<std::uint8_t>(0)));
                          area.linearDamp = json.value("linearDamp", 0.1F);
                          area.angularDampOverride = static_cast<AreaSpaceOverrideModeUVE>(
                              json.value("angularDampOverride", static_cast<std::uint8_t>(0)));
                          area.angularDamp = json.value("angularDamp", 0.1F);
                          area.priority = json.value("priority", 0);
                          if (!IsAreaComponentValidUVE(area)) {
                              throw std::runtime_error("Invalid AreaComponentUVE payload");
                          }
                          return area;
                      }, IsAreaComponentValidUVE));
        table.emplace("RayCast3DComponentUVE", MakeRegistrationUVE<RayCast3DComponentUVE>(
            [](const nlohmann::json& json) {
                const RayCast3DComponentUVE value = RayCast3DObjectFromJsonUVE(json);
                if (!IsRayCast3DObjectComponentValidUVE(value)) {
                    throw std::runtime_error("Invalid RayCast3DComponentUVE payload");
                }
                return value;
            }, IsRayCast3DObjectComponentValidUVE));
        table.emplace("Kinematic3DComponentUVE", MakeRegistrationUVE<Kinematic3DComponentUVE>(
            [](const nlohmann::json& json) {
                const Kinematic3DComponentUVE value = Kinematic3DObjectFromJsonUVE(json);
                if (!IsKinematic3DObjectComponentValidUVE(value)) {
                    throw std::runtime_error("Invalid Kinematic3DComponentUVE payload");
                }
                return value;
            }, IsKinematic3DObjectComponentValidUVE));
        table.emplace("NavMeshVolume3DComponentUVE", MakeRegistrationUVE<NavMeshVolume3DComponentUVE>(
            [](const nlohmann::json& json) {
                const NavMeshVolume3DComponentUVE value = NavMeshVolume3DObjectFromJsonUVE(json);
                if (!IsNavMeshVolume3DObjectComponentValidUVE(value)) {
                    throw std::runtime_error("Invalid NavMeshVolume3DComponentUVE payload");
                }
                return value;
            }, IsNavMeshVolume3DObjectComponentValidUVE));
        table.emplace("NavSeeker3DComponentUVE", MakeRegistrationUVE<NavSeeker3DComponentUVE>(
            [](const nlohmann::json& json) {
                const NavSeeker3DComponentUVE value = NavSeeker3DObjectFromJsonUVE(json);
                if (!IsNavSeeker3DObjectComponentValidUVE(value)) {
                    throw std::runtime_error("Invalid NavSeeker3DComponentUVE payload");
                }
                return value;
            }, IsNavSeeker3DObjectComponentValidUVE));
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
        table.emplace("OutlinerViewportComponentUVE", MakeRegistrationUVE<OutlinerViewportComponentUVE>([](const nlohmann::json&) {
                          return OutlinerViewportComponentUVE{};
                      }, IsOutlinerViewportComponentValidUVE));
        table.emplace("SceneRootComponentUVE",
                    MakeRegistrationUVE<SceneRootComponentUVE>([](const nlohmann::json&) {
                        return SceneRootComponentUVE{};
                    }, IsSceneRootComponentValidUVE));
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
        table.emplace("WorldEnvironment3DComponentUVE", MakeRegistrationUVE<WorldEnvironment3DComponentUVE>(
            [](const nlohmann::json& json) {
                const WorldEnvironment3DComponentUVE value = WorldEnvironment3DObjectFromJsonUVE(json);
                if (!IsWorldEnvironment3DObjectComponentValidUVE(value)) {
                    throw std::runtime_error("Invalid WorldEnvironment3DComponentUVE payload");
                }
                return value;
            }, IsWorldEnvironment3DObjectComponentValidUVE));
        table.emplace("ReflectionProbe3DComponentUVE", MakeRegistrationUVE<ReflectionProbe3DComponentUVE>(
            [](const nlohmann::json& json) {
                const ReflectionProbe3DComponentUVE value = ReflectionProbe3DObjectFromJsonUVE(json);
                if (!IsReflectionProbe3DObjectComponentValidUVE(value)) {
                    throw std::runtime_error("Invalid ReflectionProbe3DComponentUVE payload");
                }
                return value;
            }, IsReflectionProbe3DObjectComponentValidUVE));
        table.emplace("FogVolume3DComponentUVE", MakeRegistrationUVE<FogVolume3DComponentUVE>(
            [](const nlohmann::json& json) {
                const FogVolume3DComponentUVE value = FogVolume3DObjectFromJsonUVE(json);
                if (!IsFogVolume3DObjectComponentValidUVE(value)) {
                    throw std::runtime_error("Invalid FogVolume3DComponentUVE payload");
                }
                return value;
            }, IsFogVolume3DObjectComponentValidUVE));
        table.emplace("SurfaceInstanceComponentUVE", MakeRegistrationUVE<SurfaceInstanceComponentUVE>(
            [](const nlohmann::json& json) {
                const SurfaceInstanceComponentUVE value = SurfaceInstanceFromJsonUVE(json);
                if (!IsSurfaceInstanceComponentValidUVE(value)) {
                    throw std::runtime_error("Invalid SurfaceInstanceComponentUVE payload");
                }
                return value;
            }, IsSurfaceInstanceComponentValidUVE));
        table.emplace("LightEmitterComponentUVE", MakeRegistrationUVE<LightEmitterComponentUVE>(
            [](const nlohmann::json& json) {
                const LightEmitterComponentUVE value = LightEmitterFromJsonUVE(json);
                if (!IsLightEmitterComponentValidUVE(value)) {
                    throw std::runtime_error("Invalid LightEmitterComponentUVE payload");
                }
                return value;
            }, IsLightEmitterComponentValidUVE));
        table.emplace("Decal3DComponentUVE", MakeRegistrationUVE<Decal3DComponentUVE>(
            [](const nlohmann::json& json) {
                const Decal3DComponentUVE value = Decal3DObjectFromJsonUVE(json);
                if (!IsDecal3DObjectComponentValidUVE(value)) {
                    throw std::runtime_error("Invalid Decal3DComponentUVE payload");
                }
                return value;
            }, IsDecal3DObjectComponentValidUVE));
        table.emplace("DirectionalLight3DComponentUVE", MakeRegistrationUVE<DirectionalLight3DComponentUVE>(
            [](const nlohmann::json& json) {
                const DirectionalLight3DComponentUVE value = DirectionalLight3DFromJsonUVE(json);
                if (!IsDirectionalLight3DComponentValidUVE(value)) {
                    throw std::runtime_error("Invalid DirectionalLight3DComponentUVE payload");
                }
                return value;
            }, IsDirectionalLight3DComponentValidUVE));
        table.emplace("LodGroup3DComponentUVE", MakeRegistrationUVE<LodGroup3DComponentUVE>(
            [](const nlohmann::json& json) {
                const LodGroup3DComponentUVE value = LodGroup3DObjectFromJsonUVE(json);
                if (!IsLodGroup3DObjectComponentValidUVE(value)) {
                    throw std::runtime_error("Invalid LodGroup3DComponentUVE payload");
                }
                return value;
            }, IsLodGroup3DObjectComponentValidUVE));
        table.emplace("Occluder3DComponentUVE", MakeRegistrationUVE<Occluder3DComponentUVE>(
            [](const nlohmann::json& json) {
                const Occluder3DComponentUVE value = Occluder3DObjectFromJsonUVE(json);
                if (!IsOccluder3DObjectComponentValidUVE(value)) {
                    throw std::runtime_error("Invalid Occluder3DComponentUVE payload");
                }
                return value;
            }, IsOccluder3DObjectComponentValidUVE));
        table.emplace("VisibilityRegion3DComponentUVE", MakeRegistrationUVE<VisibilityRegion3DComponentUVE>(
            [](const nlohmann::json& json) {
                const VisibilityRegion3DComponentUVE value = VisibilityRegion3DObjectFromJsonUVE(json);
                if (!IsVisibilityRegion3DObjectComponentValidUVE(value)) {
                    throw std::runtime_error("Invalid VisibilityRegion3DComponentUVE payload");
                }
                return value;
            }, IsVisibilityRegion3DObjectComponentValidUVE));
        table.emplace("SpawnPoint3DComponentUVE", MakeRegistrationUVE<SpawnPoint3DComponentUVE>(
            [](const nlohmann::json& json) {
                const SpawnPoint3DComponentUVE value = SpawnPoint3DObjectFromJsonUVE(json);
                if (!IsSpawnPoint3DObjectComponentValidUVE(value)) {
                    throw std::runtime_error("Invalid SpawnPoint3DComponentUVE payload");
                }
                return value;
            }, IsSpawnPoint3DObjectComponentValidUVE));
        table.emplace("PlayerComponentUVE", MakeRegistrationUVE<PlayerComponentUVE>(
            [](const nlohmann::json& json) {
                const PlayerComponentUVE value = Player3DObjectFromJsonUVE(json);
                if (!IsPlayer3DObjectComponentValidUVE(value)) {
                    throw std::runtime_error("Invalid PlayerComponentUVE payload");
                }
                return value;
            }, IsPlayer3DObjectComponentValidUVE));
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
        table.emplace("Rigid3DComponentUVE", MakeRegistrationUVE<Rigid3DComponentUVE>([](const nlohmann::json& json) {
                          Rigid3DComponentUVE rigidBody;
                          rigidBody.mass = json.at("mass").get<float>();
                          rigidBody.isKinematic = json.at("isKinematic").get<bool>();
                          rigidBody.velocity =
                              json.contains("velocity") ? Vector3FromJsonUVE(json.at("velocity")) : Math::Vector3UVE{};
                          rigidBody.angularVelocity = json.contains("angularVelocity")
                              ? Vector3FromJsonUVE(json.at("angularVelocity")) : Math::Vector3UVE{};
                          rigidBody.torque = json.contains("torque")
                              ? Vector3FromJsonUVE(json.at("torque")) : Math::Vector3UVE{};
                          rigidBody.inverseInertia = json.contains("inverseInertia")
                              ? Vector3FromJsonUVE(json.at("inverseInertia")) : Math::Vector3UVE{};
                          rigidBody.drag = json.value("drag", 0.0F);
                          rigidBody.gravityScale = json.value("gravityScale", 1.0F);
                          if (!IsRigid3DComponentValidUVE(rigidBody)) {
                              throw std::runtime_error("Invalid Rigid3DComponentUVE payload");
                          }
                          return rigidBody;
                      }, IsRigid3DComponentValidUVE));
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
        table.emplace("CanvasComponentUVE", MakeRegistrationUVE<CanvasComponentUVE>([](const nlohmann::json& json) {
                          CanvasComponentUVE canvas;
                          canvas.visible = json.value("visible", true);
                          canvas.sortOrder = json.value("sortOrder", 0);
                          if (!IsCanvasComponentValidUVE(canvas)) {
                              throw std::runtime_error("Invalid CanvasComponentUVE payload");
                          }
                          return canvas;
                      }, IsCanvasComponentValidUVE));
        table.emplace("UITextComponentUVE", MakeRegistrationUVE<UITextComponentUVE>([](const nlohmann::json& json) {
                          UITextComponentUVE text;
                          text.text = json.value("text", std::string{});
                          text.positionPixels = json.contains("positionPixels")
                              ? Vector2FromJsonUVE(json.at("positionPixels")) : Math::Vector2UVE{};
                          text.fontSize = json.value("fontSize", 16.0F);
                          text.color = json.contains("color") ? Vector3FromJsonUVE(json.at("color"))
                                                               : Math::Vector3UVE{1.0F, 1.0F, 1.0F};
                          text.alpha = json.value("alpha", 1.0F);
                          if (!IsUITextComponentValidUVE(text)) {
                              throw std::runtime_error("Invalid UITextComponentUVE payload");
                          }
                          return text;
                      }, IsUITextComponentValidUVE));
        table.emplace("UIImageComponentUVE", MakeRegistrationUVE<UIImageComponentUVE>([](const nlohmann::json& json) {
                          UIImageComponentUVE image;
                          image.textureAssetGuid = Asset::AssetGuidUVE{json.value("textureAssetGuid", std::uint64_t{0})};
                          image.positionPixels = json.contains("positionPixels")
                              ? Vector2FromJsonUVE(json.at("positionPixels")) : Math::Vector2UVE{};
                          image.sizePixels = json.contains("sizePixels") ? Vector2FromJsonUVE(json.at("sizePixels"))
                                                                          : Math::Vector2UVE{64.0F, 64.0F};
                          image.tintColor = json.contains("tintColor") ? Vector3FromJsonUVE(json.at("tintColor"))
                                                                        : Math::Vector3UVE{1.0F, 1.0F, 1.0F};
                          image.alpha = json.value("alpha", 1.0F);
                          if (!IsUIImageComponentValidUVE(image)) {
                              throw std::runtime_error("Invalid UIImageComponentUVE payload");
                          }
                          return image;
                      }, IsUIImageComponentValidUVE));
        table.emplace("UIButtonComponentUVE",
                      MakeRegistrationUVE<UIButtonComponentUVE>([](const nlohmann::json& json) {
                          UIButtonComponentUVE button;
                          button.positionPixels = json.contains("positionPixels")
                              ? Vector2FromJsonUVE(json.at("positionPixels")) : Math::Vector2UVE{};
                          button.sizePixels = json.contains("sizePixels") ? Vector2FromJsonUVE(json.at("sizePixels"))
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
        table.emplace("AudioSourceComponentUVE",
                      MakeRegistrationUVE<AudioSourceComponentUVE>([](const nlohmann::json& json) {
                          AudioSourceComponentUVE source;
                          source.audioAssetPath = json.at("audioAssetPath").get<std::string>();
                          source.mixerGroup = json.value("mixerGroup", std::string{});
                          source.volume = json.at("volume").get<float>();
                          source.looping = json.value("looping", false);
                          source.pitch = json.value("pitch", 1.0F);
                          source.spatial = json.value("spatial", true);
                          source.minDistance = json.value("minDistance", 1.0F);
                          source.maxDistance = json.value("maxDistance", 25.0F);
                          source.attenuationCurve = static_cast<AudioAttenuationCurveUVE>(
                              json.value("attenuationCurve", std::uint8_t{0}));
                          source.playOnAwake = json.value("playOnAwake", true);
                          if (!IsAudioSourceComponentValidUVE(source)) {
                              throw std::runtime_error("Invalid AudioSourceComponentUVE payload");
                          }
                          return source;
                      }, IsAudioSourceComponentValidUVE));
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
        table.emplace("ParticleEmitterComponentUVE",
                      MakeRegistrationUVE<ParticleEmitterComponentUVE>([](const nlohmann::json& json) {
                          const ParticleEmitterComponentUVE defaults{};
                          ParticleEmitterComponentUVE emitter{};
                          emitter.maxParticles = json.at("maxParticles").get<std::uint32_t>();
                          emitter.emitting = json.value("emitting", defaults.emitting);
                          emitter.emissionRate = json.value("emissionRate", defaults.emissionRate);
                          emitter.lifetimeSeconds = json.value("lifetimeSeconds", defaults.lifetimeSeconds);
                          if (!IsParticleEmitterComponentValidUVE(emitter)) {
                              throw std::runtime_error("Invalid ParticleEmitterComponentUVE payload");
                          }
                          return emitter;
                      }, IsParticleEmitterComponentValidUVE));
        table.emplace(
            "PhysicsInterpolationComponentUVE",
            MakeRegistrationUVE<PhysicsInterpolationComponentUVE>(
                [](const nlohmann::json& json) {
                    // Only `mode` round-trips; the pose fields default-construct (false
                    // hasPreviousPose), matching a freshly spawned entity - never the possibly
                    // stale pose from whatever session wrote the file.
                    PhysicsInterpolationComponentUVE interpolation{};
                    interpolation.mode = static_cast<PoseSmoothingUVE>(
                        json.at("mode").get<std::underlying_type_t<PoseSmoothingUVE>>());
                    if (!IsPhysicsInterpolationComponentValidUVE(interpolation)) {
                        throw std::runtime_error("Invalid PhysicsInterpolationComponentUVE payload");
                    }
                    return interpolation;
                },
                IsPhysicsInterpolationComponentValidUVE));
        table.emplace("ProcessComponentUVE",
                      MakeRegistrationUVE<ProcessComponentUVE>([](const nlohmann::json& json) {
                          ProcessComponentUVE process{};
                          process.mode = static_cast<TickModeUVE>(
                              json.at("mode").get<std::underlying_type_t<TickModeUVE>>());
                          process.priority = json.at("priority").get<std::int32_t>();
                          process.physicsPriority = json.at("physicsPriority").get<std::int32_t>();
                          if (!IsProcessComponentValidUVE(process)) {
                              throw std::runtime_error("Invalid ProcessComponentUVE payload");
                          }
                          return process;
                      }, IsProcessComponentValidUVE));
        table.emplace("ThreadGroupComponentUVE",
                      MakeRegistrationUVE<ThreadGroupComponentUVE>([](const nlohmann::json& json) {
                          ThreadGroupComponentUVE threadGroup{};
                          threadGroup.mode = static_cast<ThreadGroupModeUVE>(
                              json.at("mode").get<std::underlying_type_t<ThreadGroupModeUVE>>());
                          threadGroup.order = json.at("order").get<std::int32_t>();
                          if (!IsThreadGroupComponentValidUVE(threadGroup)) {
                              throw std::runtime_error("Invalid ThreadGroupComponentUVE payload");
                          }
                          return threadGroup;
                      }, IsThreadGroupComponentValidUVE));
        table.emplace("BoneModifierComponentUVE",
                      MakeRegistrationUVE<BoneModifierComponentUVE>([](const nlohmann::json& json) {
                          BoneModifierComponentUVE modifier{};
                          modifier.active = json.at("active").get<bool>();
                          modifier.influence = ReadFloatUVE(json.at("influence"));
                          if (!IsBoneModifierComponentValidUVE(modifier)) {
                              throw std::runtime_error("Invalid BoneModifierComponentUVE payload");
                          }
                          return modifier;
                      }, IsBoneModifierComponentValidUVE));
        table.emplace("PhysicsObjectComponentUVE",
                      MakeRegistrationUVE<PhysicsObjectComponentUVE>([](const nlohmann::json& json) {
                          PhysicsObjectComponentUVE object{};
                          object.disableMode = static_cast<PhysicsObjectDisableModeUVE>(
                              json.at("disableMode").get<std::underlying_type_t<PhysicsObjectDisableModeUVE>>());
                          // collisionLayer/collisionMask in older files are ignored: the collider
                          // owns them now.
                          object.collisionPriority = ReadFloatUVE(json.at("collisionPriority"));
                          object.inputRayPickable = json.at("inputRayPickable").get<bool>();
                          object.inputCaptureOnDrag = json.at("inputCaptureOnDrag").get<bool>();
                          if (!IsPhysicsObjectComponentValidUVE(object)) {
                              throw std::runtime_error("Invalid PhysicsObjectComponentUVE payload");
                          }
                          return object;
                      }, IsPhysicsObjectComponentValidUVE));
        table.emplace("SolidBodyComponentUVE", MakeRegistrationUVE<SolidBodyComponentUVE>([](const nlohmann::json& json) {
                          SolidBodyComponentUVE body{};
                          body.lockMotionX = json.value("lockMotionX", false);
                          body.lockMotionY = json.value("lockMotionY", false);
                          body.lockMotionZ = json.value("lockMotionZ", false);
                          return body;
                      }, [](const SolidBodyComponentUVE&) noexcept { return true; }));
        table.emplace("RenderInstanceComponentUVE",
                      MakeRegistrationUVE<RenderInstanceComponentUVE>([](const nlohmann::json& json) {
                          RenderInstanceComponentUVE instance{};
                          instance.renderLayers = json.at("renderLayers").get<std::uint32_t>();
                          instance.sortingOffset = ReadFloatUVE(json.at("sortingOffset"));
                          instance.sortingUseAabbCenter = json.at("sortingUseAabbCenter").get<bool>();
                          if (!IsRenderInstanceComponentValidUVE(instance)) {
                              throw std::runtime_error("Invalid RenderInstanceComponentUVE payload");
                          }
                          return instance;
                      }, IsRenderInstanceComponentValidUVE));
        table.emplace("AutoTranslateComponentUVE",
                      MakeRegistrationUVE<AutoTranslateComponentUVE>([](const nlohmann::json& json) {
                          AutoTranslateComponentUVE autoTranslate{};
                          autoTranslate.mode = static_cast<LocalizeModeUVE>(
                              json.at("mode").get<std::underlying_type_t<LocalizeModeUVE>>());
                          if (!IsAutoTranslateComponentValidUVE(autoTranslate)) {
                              throw std::runtime_error("Invalid AutoTranslateComponentUVE payload");
                          }
                          return autoTranslate;
                      }, IsAutoTranslateComponentValidUVE));
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
