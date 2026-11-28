// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "blueprint_object_host.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <map>
#include <optional>
#include <cctype>
#include <random>
#include <sstream>
#include <string>
#include <type_traits>

#include "i_audio_source_system.h"
#include "i_audio_system.h"
#include "audio_source_component.h"
#include "camera_component.h"
#include "canvas_component.h"
#include "ui_button_component.h"
#include "ui_checkbox_component.h"
#include "ui_dropdown_component.h"
#include "ui_image_component.h"
#include "ui_progress_bar_component.h"
#include "ui_slider_component.h"
#include "ui_text_component.h"
#include "ui_text_input_component.h"
#include "ui_tween_component.h"
#include "ui_view_3d_component.h"
#include "character_controller_component.h"
#include "light_component.h"
#include "name_component.h"
#include "rigid_3d_component.h"
#include "ragdoll_3d.h"
#include "transform_component.h"
#include "visibility_component.h"
#include "world_transform_component.h"
#include "i_entity_manager.h"
#include "gameplay_attributes.h"
#include "gameplay_tags.h"
#include "hierarchy_component.h"
#include "animation_graph_component.h"
#include "animation_sequencer_component.h"
#include "nav_seeker_3d.h"
#include "particle_emitter_component.h"
#include "surface_instance_component.h"
#include "montage_sequencer_component.h"
#include "i_input_system.h"
#include "logging_macros.h"
#include "network_lobby.h"
#include "network_relay.h"
#include "network_rpc.h"
#include "network_session.h"
#include "rigid_3d.h"
#include "blueprint_instance.h"
#include "quaternion.h"
#include "color.h"

namespace UVE::Core {
namespace {

using Blueprint::HostFunction;
using Blueprint::HostProperty;
using Blueprint::ObjectRef;
using Blueprint::Type;
using Blueprint::Value;
using Blueprint::Vec3Value;

/// A script's handle to another object packs its entity, plus one: 0 stays reserved for "no
/// object" (entity slot 0, generation 0 is a real entity and must keep its own handle).
[[nodiscard]] constexpr std::uint64_t PackEntity(const Scene::Entity entity) noexcept {
    return ((static_cast<std::uint64_t>(entity.generation) << 32U) | entity.index) + 1U;
}

[[nodiscard]] constexpr Scene::Entity UnpackEntity(const ObjectRef ref) noexcept {
    const std::uint64_t packed = ref.id - 1U;
    return Scene::Entity{static_cast<std::uint32_t>(packed),
                            static_cast<std::uint32_t>(packed >> 32U)};
}

/// The MontageSequencer a script drives: the object's own, else one directly under it (a character's).
[[nodiscard]] Scene::MontageSequencerComponent* FindMontageSequencer(Scene::IEntityManager& entities,
                                                                   const Scene::Entity entity) {
    if (entities.HasComponent<Scene::MontageSequencerComponent>(entity)) {
        return &entities.GetComponent<Scene::MontageSequencerComponent>(entity);
    }
    Scene::MontageSequencerComponent* found = nullptr;
    entities.ForEach<Scene::MontageSequencerComponent>(
        [&](const Scene::Entity candidate, Scene::MontageSequencerComponent& player) {
            if (found == nullptr && entities.HasComponent<Scene::HierarchyComponent>(candidate) &&
                entities.GetComponent<Scene::HierarchyComponent>(candidate).parent == entity) {
                found = &player;
            }
        });
    return found;
}

/// The Animation Graph a script drives: the object's own, else one directly under it (a character's).
[[nodiscard]] Scene::AnimationGraphComponent* FindAnimationGraph(Scene::IEntityManager& entities,
                                                                     const Scene::Entity entity) {
    if (entities.HasComponent<Scene::AnimationGraphComponent>(entity)) {
        return &entities.GetComponent<Scene::AnimationGraphComponent>(entity);
    }
    Scene::AnimationGraphComponent* found = nullptr;
    entities.ForEach<Scene::AnimationGraphComponent>(
        [&](const Scene::Entity candidate, Scene::AnimationGraphComponent& graph) {
            if (found == nullptr && entities.HasComponent<Scene::HierarchyComponent>(candidate) &&
                entities.GetComponent<Scene::HierarchyComponent>(candidate).parent == entity) {
                found = &graph;
            }
        });
    return found;
}

/// The AnimationSequencer a script drives: the object's own, else one directly under it.
[[nodiscard]] Scene::Entity FindSequencer(Scene::IEntityManager& entities, const Scene::Entity entity) {
    if (entities.HasComponent<Scene::AnimationSequencerComponent>(entity)) {
        return entity;
    }
    Scene::Entity found = Scene::kInvalidEntity;
    entities.ForEach<Scene::AnimationSequencerComponent>([&](const Scene::Entity candidate, Scene::AnimationSequencerComponent&) {
        if (found == Scene::kInvalidEntity && entities.HasComponent<Scene::HierarchyComponent>(candidate) &&
            entities.GetComponent<Scene::HierarchyComponent>(candidate).parent == entity) {
            found = candidate;
        }
    });
    return found;
}

[[nodiscard]] Scene::AnimationParameter* FindAnimationParameter(Scene::AnimationGraphComponent& graph,
                                                                     const std::string_view name) {
    for (Scene::AnimationParameter& parameter : graph.parameters) {
        if (parameter.name == name) {
            return &parameter;
        }
    }
    return nullptr;
}

void SetVisible(Scene::IEntityManager& entities, const Scene::Entity entity, const bool shown) {
    if (entities.HasComponent<Scene::VisibilityComponent>(entity)) {
        entities.GetComponent<Scene::VisibilityComponent>(entity).visible = shown;
    } else if (!shown) {
        // Showing adds nothing: an object without the component is already visible.
        Scene::VisibilityComponent visibility;
        visibility.visible = false;
        entities.AddComponent<Scene::VisibilityComponent>(entity, visibility);
    }
}

[[nodiscard]] Vec3Value ToScript(const Math::Vector3& v) noexcept {
    return {static_cast<double>(v.x), static_cast<double>(v.y), static_cast<double>(v.z)};
}

[[nodiscard]] Math::Vector3 ToEngine(const Value& value) {
    const Vec3Value& v = std::get<Vec3Value>(value);
    return {static_cast<float>(v.x), static_cast<float>(v.y), static_cast<float>(v.z)};
}

/// Staged RPC calls waiting on their first `rpc.send`: sixteen scripts' worth of headroom is
/// plenty, and the cap keeps a leaky script from growing the staging map forever.
inline constexpr std::uint32_t kMaxStagedRpcCalls = 16U;

/// The `rpc.result_kind` word for a completed value: "nil", the scalar kind, or the collection
/// kind. Object references never cross the wire, so anything else fails closed as "none".
[[nodiscard]] std::string RpcValueKind(const Value& value) {
    if (std::holds_alternative<std::monostate>(value)) {
        return "nil";
    }
    if (std::holds_alternative<bool>(value)) {
        return "bool";
    }
    if (std::holds_alternative<std::int64_t>(value)) {
        return "int";
    }
    if (std::holds_alternative<double>(value)) {
        return "float";
    }
    if (std::holds_alternative<std::string>(value)) {
        return "str";
    }
    if (std::holds_alternative<Vec3Value>(value)) {
        return "vec3";
    }
    const auto* collection =
        std::get_if<std::shared_ptr<const Blueprint::CollectionValue>>(&value);
    if (collection == nullptr || *collection == nullptr) {
        return "none";
    }
    switch ((*collection)->kind) {
        case Blueprint::CollectionValue::Kind::List:
            return "list";
        case Blueprint::CollectionValue::Kind::Map:
            return "map";
        case Blueprint::CollectionValue::Kind::Tuple:
            return "tuple";
    }
    return "none";
}

/// The world-facing calls every script may make (they fail closed without the game's services):
/// messages, timers, other objects, traces, the game's state and saves.
[[nodiscard]] std::optional<HostFunction> DescribeWorldFunction(const std::string_view name) {
    const Type s = Type::Str();
    const Type f = Type::Float();
    const Type i = Type::Int();
    const Type b = Type::Bool();
    const Type v = Type::Vec3();
    const Type o = Type::Object("Object3D");
    const Type list = Type::List(o);
    static const std::map<std::string, std::pair<std::vector<std::string>, std::string>, std::less<>> kTable = {
        {"events.emit", {{"s", "s"}, "b"}},
        {"events.emit_to", {{"o", "s", "s"}, "b"}},
        {"timers.set", {{"s", "f", "b"}, "b"}},
        {"timers.clear", {{"s"}, "b"}},
        {"timers.active", {{"s"}, "b"}},
        {"timers.remaining", {{"s"}, "f"}},
        {"world.self", {{}, "o"}},
        {"world.spawn", {{"s", "v", "v"}, "o"}},
        {"world.destroy", {{"o"}, "b"}},
        {"world.destroy_self", {{}, "b"}},
        {"world.set_life_span", {{"f"}, "b"}},
        {"world.find_tag", {{"s"}, "o"}},
        {"world.find_all_tag", {{"s"}, "l"}},
        {"world.find_all_name", {{"s"}, "l"}},
        {"world.is_valid", {{"o"}, "b"}},
        {"object.name", {{"o"}, "s"}},
        {"object.get_position", {{"o"}, "v"}},
        {"object.set_position", {{"o", "v"}, "b"}},
        {"object.get_rotation", {{"o"}, "v"}},
        {"object.set_rotation", {{"o", "v"}, "b"}},
        {"object.get_scale", {{"o"}, "v"}},
        {"object.set_scale", {{"o", "v"}, "b"}},
        {"object.set_visible", {{"o", "b"}, "b"}},
        {"object.has_tag", {{"o", "s"}, "b"}},
        {"object.add_tag", {{"o", "s"}, "b"}},
        {"object.remove_tag", {{"o", "s"}, "b"}},
        {"object.distance", {{"o", "o"}, "f"}},
        {"object.attach", {{"o", "o"}, "b"}},
        {"object.detach", {{"o"}, "b"}},
        {"object.parent", {{"o"}, "o"}},
        {"object.children", {{"o"}, "l"}},
        {"trace.line", {{"v", "v"}, "b"}},
        {"trace.sphere", {{"v", "v", "f"}, "b"}},
        {"trace.location", {{}, "v"}},
        {"trace.normal", {{}, "v"}},
        {"trace.object", {{}, "o"}},
        {"trace.distance", {{}, "f"}},
        {"game.time", {{}, "f"}},
        {"game.delta", {{}, "f"}},
        {"game.time_scale", {{}, "f"}},
        {"game.set_time_scale", {{"f"}, "b"}},
        {"game.pause", {{}, "b"}},
        {"game.resume", {{}, "b"}},
        {"game.is_paused", {{}, "b"}},
        {"game.quit", {{}, "b"}},
        {"game.open_level", {{"s"}, "b"}},
        {"game.level_name", {{}, "s"}},
        {"save.set_float", {{"s", "f"}, "b"}},
        {"save.set_int", {{"s", "i"}, "b"}},
        {"save.set_bool", {{"s", "b"}, "b"}},
        {"save.set_str", {{"s", "s"}, "b"}},
        {"save.get_float", {{"s", "f"}, "f"}},
        {"save.get_int", {{"s", "i"}, "i"}},
        {"save.get_bool", {{"s", "b"}, "b"}},
        {"save.get_str", {{"s", "s"}, "s"}},
        {"save.has", {{"s"}, "b"}},
        {"save.remove", {{"s"}, "b"}},
        {"save.write", {{"s"}, "b"}},
        {"save.read", {{"s"}, "b"}},
        {"save.exists", {{"s"}, "b"}},
        {"save.delete", {{"s"}, "b"}},
        {"audio.play_at", {{"s", "v", "f"}, "b"}},
        {"audio.play_2d", {{"s", "f"}, "b"}},
        {"camera.activate", {{"o"}, "b"}},
        {"camera.shake", {{"f", "f"}, "b"}},
        {"camera.forward", {{}, "v"}},
        {"camera.location", {{}, "v"}},
        {"text.to_int", {{"s"}, "i"}},
        {"text.to_float", {{"s"}, "f"}},
        {"text.upper", {{"s"}, "s"}},
        {"text.lower", {{"s"}, "s"}},
        {"text.trim", {{"s"}, "s"}},
        {"text.replace", {{"s", "s", "s"}, "s"}},
        {"text.split", {{"s", "s"}, "L"}},
        {"text.substring", {{"s", "i", "i"}, "s"}},
        {"text.starts_with", {{"s", "s"}, "b"}},
        {"text.format", {{"s", "s", "s"}, "s"}},
        {"math.round", {{"f"}, "i"}},
        {"math.ceil", {{"f"}, "i"}},
        {"math.snap", {{"f", "f"}, "f"}},
        {"math.wrap", {{"f", "f", "f"}, "f"}},
        {"math.sign", {{"f"}, "f"}},
        {"math.rotate_about", {{"v", "v", "f"}, "v"}},
        {"math.random_unit_vector", {{}, "v"}},
        {"math.random_in_box", {{"v", "v"}, "v"}},
        {"array.empty_object", {{}, "l"}},
        {"array.empty_int", {{}, "Li"}},
        {"array.empty_float", {{}, "Lf"}},
        {"array.empty_string", {{}, "L"}},
    };
    const auto found = kTable.find(name);
    if (found == kTable.end()) {
        return std::nullopt;
    }
    const auto type = [&](const std::string& code) {
        return code == "s" ? s : code == "f" ? f : code == "i" ? i : code == "b" ? b : code == "v" ? v : code == "o" ? o
               : code == "L" ? Type::List(s) : code == "Li" ? Type::List(i) : code == "Lf" ? Type::List(f) : list;
    };
    HostFunction out{{}, type(found->second.second)};
    for (const std::string& parameter : found->second.first) {
        out.params.push_back(type(parameter));
    }
    return out;
}

/// Euler degrees (X pitch, Y yaw, Z roll) of a rotation, as the Details panel shows them.
[[nodiscard]] Math::Vector3 DegreesOf(const Math::Quaternion& rotation) {
    Math::Vector3 radians{};
    static_cast<void>(Math::TryToEuler(rotation, radians));
    constexpr float kDegrees = 57.29578F;
    return Math::Vector3{radians.x * kDegrees, radians.y * kDegrees, radians.z * kDegrees};
}

[[nodiscard]] std::optional<Math::Quaternion> RotationOfDegrees(const Math::Vector3& degrees) {
    constexpr float kRadians = 0.017453292F;
    Math::Quaternion rotation{};
    if (!Math::TryMakeEuler(Math::Vector3{degrees.x * kRadians, degrees.y * kRadians, degrees.z * kRadians}, rotation)) {
        return std::nullopt;
    }
    return rotation;
}

/// The timer a life span runs on: no script can name it (`timers.set` refuses it).
inline constexpr std::string_view kLifeSpanTimer = "__life_span";

void MarkMoved(Scene::IEntityManager& entities, const Scene::Entity entity) {
    if (entities.HasComponent<Scene::WorldTransformComponent>(entity)) {
        entities.GetComponent<Scene::WorldTransformComponent>(entity).dirty = true;
    }
}

} // namespace

static_assert(std::is_same_v<Network::RpcCallId, std::uint32_t>,
              "RpcResponseSink spells call ids as uint32_t to keep the host header "
              "forward-declared; keep the peer's id type the same.");
static_assert(std::is_same_v<Network::LobbyRoomId, std::uint32_t>,
              "LobbyRosterSink spells room ids as uint32_t to keep the host header "
              "forward-declared; keep the lobby's room id type the same.");

BlueprintObjectHost::BlueprintObjectHost(Scene::IEntityManager& entityManager, const Input::IInputSystem* const input,
                                         const Scene::Entity entity,
                                         Audio::IAudioSourceSystem* const audioSources,
                                         Audio::IAudioSystem* const audio,
                                         InstanceResolver resolver,
                                         Network::NetworkRpc* const rpc,
                                         RpcResponseSink sink,
                                         Network::NetworkLobbyClient* const lobby,
                                         LobbyResultSink lobbySink,
                                         LobbyRosterSink rosterSink,
                                         Network::NetworkRelay* const relay,
                                         Network::NetworkSession* const relaySession) noexcept
    : m_entityManager(entityManager), m_input(input), m_entity(entity), m_audioSources(audioSources),
      m_audio(audio), m_resolver(std::move(resolver)), m_rpc(rpc), m_sink(std::move(sink)),
      m_lobby(lobby), m_lobbySink(std::move(lobbySink)), m_rosterSink(std::move(rosterSink)),
      m_relay(relay), m_relaySession(relaySession) {}

BlueprintObjectHost::~BlueprintObjectHost() {
    // The peer outlives the host by contract; cancel what it still holds for us so no
    // callback fires into a dead host. Completed calls keep their retained results.
    if (m_rpc != nullptr) {
        for (const std::uint32_t callId : m_pendingRpc) {
            static_cast<void>(m_rpc->Cancel(callId));
        }
    }
    // Same for the lobby peer, bluntly: the facade abandons every in-flight call (a shared
    // peer's C++-initiated calls go too - teardown abandons everything), and the roster
    // push unhooks so no sink fires into a dead host.
    if (m_lobby != nullptr) {
        static_cast<void>(m_lobby->CancelPending());
        m_lobby->SetRosterHandler(Network::LobbyRosterHandler{});
    }
}

std::optional<HostProperty> BlueprintObjectHost::DescribeProperty(const std::string_view name) const {
    if (name == "name") {
        return HostProperty{Type::Str(), false};
    }
    if ((name == "position" || name == "scale") && m_entityManager.HasComponent<Scene::TransformComponent>(m_entity)) {
        return HostProperty{Type::Vec3(), true};
    }
    if (name == "velocity" &&
        (m_entityManager.HasComponent<Scene::CharacterControllerComponent>(m_entity) ||
         m_entityManager.HasComponent<Scene::Rigid3DComponent>(m_entity))) {
        return HostProperty{Type::Vec3(), true};
    }
    if ((name == "volume" || name == "pitch") &&
        m_entityManager.HasComponent<Scene::AudioSourceComponent>(m_entity)) {
        return HostProperty{Type::Float(), true};
    }
    // Ungated: every object is inherently visible, so a script reads true and a write adds the
    // component. The scene graph owns visibleInHierarchy; scripts only ever touch the switch.
    if (name == "visible") {
        return HostProperty{Type::Bool(), true};
    }
    if (name == "intensity" && m_entityManager.HasComponent<Scene::LightComponent>(m_entity)) {
        return HostProperty{Type::Float(), true};
    }
    // Rotation as Euler angles in degrees (X pitch, Y yaw, Z roll), like the Details panel shows it.
    if (name == "rotation" && m_entityManager.HasComponent<Scene::TransformComponent>(m_entity)) {
        return HostProperty{Type::Vec3(), true};
    }
    if ((name == "light_color") && m_entityManager.HasComponent<Scene::LightComponent>(m_entity)) {
        return HostProperty{Type::Vec3(), true};
    }
    if ((name == "max_speed" || name == "jump_height") &&
        m_entityManager.HasComponent<Scene::CharacterControllerComponent>(m_entity)) {
        return HostProperty{Type::Float(), true};
    }
    if (name == "fov" && m_entityManager.HasComponent<Scene::CameraComponent>(m_entity)) {
        return HostProperty{Type::Float(), true};
    }
    if (m_entityManager.HasComponent<Scene::CharacterControllerComponent>(m_entity)) {
        // "grounded", matching the other properties here: name, position, scale, velocity - a plain
        // word for the thing, no "is_" prefix and no snake_case. "is_on_floor" is the name this had
        // before, and a script written against it still reads, because a script out in the world is
        // not something a rename in here is allowed to break. Writing always uses the current name.
        if (name == "grounded" || name == "is_on_floor") {
            return HostProperty{Type::Bool(), false};
        }
    }
    return std::nullopt;
}

namespace {

/// The Ragdoll3D a script on `host` drives: the host's own, or the first one in its subtree (a character's
/// ragdoll sits under its skeleton, not on the character itself).
[[nodiscard]] Scene::Entity FindRagdollFor(Scene::IEntityManager& entityManager, const Scene::Entity host) {
    if (!entityManager.IsAlive(host)) return Scene::kInvalidEntity;
    if (entityManager.HasComponent<Scene::Ragdoll3DComponent>(host)) return host;
    Scene::Entity found = Scene::kInvalidEntity;
    entityManager.ForEach<Scene::Ragdoll3DComponent>([&](const Scene::Entity candidate, Scene::Ragdoll3DComponent&) {
        if (found != Scene::kInvalidEntity) return;
        Scene::Entity cursor = candidate;
        for (std::size_t depth = 0U; depth < 64U && cursor != Scene::kInvalidEntity; ++depth) {
            if (cursor == host) {
                found = candidate;
                return;
            }
            cursor = entityManager.HasComponent<Scene::HierarchyComponent>(cursor)
                         ? entityManager.GetComponent<Scene::HierarchyComponent>(cursor).parent
                         : Scene::kInvalidEntity;
        }
    });
    return found;
}

} // namespace

std::optional<HostFunction> BlueprintObjectHost::DescribeFunction(const std::string_view name) const {
    if (name.starts_with("ragdoll.") && FindRagdollFor(m_entityManager, m_entity) != Scene::kInvalidEntity) {
        if (name == "ragdoll.activate" || name == "ragdoll.activate_powered" || name == "ragdoll.deactivate" ||
            name == "ragdoll.get_up" || name == "ragdoll.is_active" || name == "ragdoll.is_at_rest" ||
            name == "ragdoll.is_lying") {
            return HostFunction{{}, Type::Bool()};
        }
        if (name == "ragdoll.apply_impulse") return HostFunction{{Type::Vec3()}, Type::Bool()};
        if (name == "ragdoll.hit") return HostFunction{{Type::Vec3(), Type::Vec3()}, Type::Bool()};
        if (name == "ragdoll.set_blend" || name == "ragdoll.set_motor_stiffness") {
            return HostFunction{{Type::Float()}, Type::Bool()};
        }
        if (name == "ragdoll.set_auto_recover") return HostFunction{{Type::Bool()}, Type::Bool()};
        if (name == "ragdoll.get_blend") return HostFunction{{}, Type::Float()};
    }
    if (m_entityManager.HasComponent<Scene::Rigid3DComponent>(m_entity) &&
        (name == "physics.apply_force" || name == "physics.apply_impulse" || name == "physics.apply_torque")) {
        return HostFunction{{Type::Vec3()}, Type::Bool()};
    }
    if (m_entityManager.HasComponent<Scene::AudioSourceComponent>(m_entity) &&
        (name == "audio.play" || name == "audio.stop" || name == "audio.is_playing")) {
        return HostFunction{{}, Type::Bool()};
    }
    if (m_entityManager.HasComponent<Scene::GameplayAttributesComponent>(m_entity) &&
        (name == "attributes.has" || name == "attributes.get" || name == "attributes.max")) {
        return HostFunction{{Type::Str()}, name == "attributes.has" ? Type::Bool() : Type::Float()};
    }
    if (m_entityManager.HasComponent<Scene::GameplayAttributesComponent>(m_entity) &&
        (name == "attributes.damage" || name == "attributes.heal")) {
        return HostFunction{{Type::Str(), Type::Float()}, Type::Bool()};
    }
    if (m_entityManager.HasComponent<Scene::GameplayTagComponent>(m_entity) &&
        (name == "tags.has" || name == "tags.add" || name == "tags.remove")) {
        return HostFunction{{Type::Str()}, Type::Bool()};
    }
    if (name == "input.pressed" || name == "input.held" || name == "input.released") {
        return HostFunction{{Type::Str()}, Type::Bool()};
    }
    if (name == "input.axis_value") {
        return HostFunction{{Type::Str()}, Type::Float()};
    }
    if (name == "input.mouse_delta") {
        return HostFunction{{}, Type::Vec3()};
    }
    // A character: steering, jumping, speed.
    if (m_entityManager.HasComponent<Scene::CharacterControllerComponent>(m_entity)) {
        if (name == "character.move" || name == "character.launch") {
            return HostFunction{{Type::Vec3()}, Type::Bool()};
        }
        if (name == "character.jump" || name == "character.falling") {
            return HostFunction{{}, Type::Bool()};
        }
        if (name == "character.speed") {
            return HostFunction{{}, Type::Float()};
        }
    }
    // Its own axes and turning, from its transform.
    if (m_entityManager.HasComponent<Scene::TransformComponent>(m_entity)) {
        if (name == "transform.forward" || name == "transform.right" || name == "transform.up") {
            return HostFunction{{}, Type::Vec3()};
        }
        if (name == "transform.add_yaw" || name == "transform.add_pitch") {
            return HostFunction{{Type::Float()}, Type::Bool()};
        }
        if (name == "transform.look_at") {
            return HostFunction{{Type::Vec3()}, Type::Bool()};
        }
        if (name == "transform.distance_to") {
            return HostFunction{{Type::Vec3()}, Type::Float()};
        }
    }
    // Maths the language has no word for.
    if (name == "math.random_range" || name == "math.pow" || name == "math.atan2" || name == "math.fmod") {
        return HostFunction{{Type::Float(), Type::Float()}, Type::Float()};
    }
    if (name == "math.random_bool") {
        return HostFunction{{}, Type::Bool()};
    }
    if (name == "math.select_float" || name == "math.select_int" || name == "math.select_vector" || name == "math.select_string") {
        const Type value = name == "math.select_float" ? Type::Float()
                              : name == "math.select_int" ? Type::Int()
                              : name == "math.select_vector" ? Type::Vec3()
                                                              : Type::Str();
        return HostFunction{{Type::Bool(), value, value}, value};
    }
    if (name == "math.random_int") {
        return HostFunction{{Type::Int(), Type::Int()}, Type::Int()};
    }
    // Animation Graph parameters: the object's graph, or the one under it, by parameter name.
    if (name.starts_with("anim.") &&
        FindAnimationGraph(const_cast<Scene::IEntityManager&>(m_entityManager), m_entity) != nullptr) {
        if (name == "anim.set_float") {
            return HostFunction{{Type::Str(), Type::Float()}, Type::Bool()};
        }
        if (name == "anim.set_bool") {
            return HostFunction{{Type::Str(), Type::Bool()}, Type::Bool()};
        }
        if (name == "anim.set_int") {
            return HostFunction{{Type::Str(), Type::Int()}, Type::Bool()};
        }
        if (name == "anim.trigger") {
            return HostFunction{{Type::Str()}, Type::Bool()};
        }
        if (name == "anim.set_vector") {
            return HostFunction{{Type::Str(), Type::Float(), Type::Float(), Type::Float()}, Type::Bool()};
        }
        if (name == "anim.get_float") {
            return HostFunction{{Type::Str()}, Type::Float()};
        }
        if (name == "anim.get_bool") {
            return HostFunction{{Type::Str()}, Type::Bool()};
        }
    }
    // Montages: the object's MontageSequencer, or the one under it. Plays by the montage file's name.
    if (name.starts_with("montage.") &&
        FindMontageSequencer(const_cast<Scene::IEntityManager&>(m_entityManager), m_entity) != nullptr) {
        if (name == "montage.play" || name == "montage.jump_to") {
            return HostFunction{{Type::Str()}, Type::Bool()};
        }
        if (name == "montage.play_section") {
            return HostFunction{{Type::Str(), Type::Str()}, Type::Bool()};
        }
        if (name == "montage.stop" || name == "montage.is_playing") {
            return HostFunction{{}, Type::Bool()};
        }
        if (name == "montage.section") {
            return HostFunction{{}, Type::Str()};
        }
        if (name == "montage.curve") {
            return HostFunction{{Type::Str()}, Type::Float()};
        }
    }
    // Its particles, its look, its clips and its walking (the object's own components).
    if (name.starts_with("particles.") && m_entityManager.HasComponent<Scene::ParticleEmitterComponent>(m_entity)) {
        if (name == "particles.set_active") {
            return HostFunction{{Type::Bool()}, Type::Bool()};
        }
        if (name == "particles.burst") {
            return HostFunction{{Type::Int()}, Type::Bool()};
        }
    }
    if (name.starts_with("render.") && m_entityManager.HasComponent<Scene::SurfaceInstanceComponent>(m_entity)) {
        if (name == "render.set_material") {
            return HostFunction{{Type::Str()}, Type::Bool()};
        }
        if (name == "render.set_transparency") {
            return HostFunction{{Type::Float()}, Type::Bool()};
        }
    }
    if (name.starts_with("sequencer.") &&
        FindSequencer(const_cast<Scene::IEntityManager&>(m_entityManager), m_entity) != Scene::kInvalidEntity) {
        if (name == "sequencer.play") {
            return HostFunction{{Type::Str()}, Type::Bool()};
        }
        if (name == "sequencer.stop" || name == "sequencer.is_playing") {
            return HostFunction{{}, Type::Bool()};
        }
        if (name == "sequencer.set_rate") {
            return HostFunction{{Type::Float()}, Type::Bool()};
        }
    }
    if (name.starts_with("ai.") && m_entityManager.HasComponent<Scene::NavSeeker3DComponent>(m_entity)) {
        if (name == "ai.move_to") {
            return HostFunction{{Type::Vec3()}, Type::Bool()};
        }
        if (name == "ai.stop" || name == "ai.reached") {
            return HostFunction{{}, Type::Bool()};
        }
        if (name == "ai.status") {
            return HostFunction{{}, Type::Str()};
        }
    }
    // Widget Blueprints, ungated: a widget's graph reaches its own widgets by name; any graph
    // may create a widget and put it on the screen.
    if (name == "ui.create") {
        return HostFunction{{Type::Str()}, Type::Object("Object3D")};
    }
    if (name == "ui.add_to_viewport") {
        return HostFunction{{Type::Object("Object3D"), Type::Int()}, Type::Bool()};
    }
    if (name == "ui.set_text") {
        return HostFunction{{Type::Str(), Type::Str()}, Type::Bool()};
    }
    if (name == "ui.get_text") {
        return HostFunction{{Type::Str()}, Type::Str()};
    }
    if (name == "ui.set_visible") {
        return HostFunction{{Type::Str(), Type::Bool()}, Type::Bool()};
    }
    if (name == "ui.set_color") {
        return HostFunction{{Type::Str(), Type::Vec3()}, Type::Bool()};
    }
    if (name == "ui.set_progress") {
        return HostFunction{{Type::Str(), Type::Float()}, Type::Bool()};
    }
    if (name == "ui.get_value") {
        return HostFunction{{Type::Str()}, Type::Float()};
    }
    if (name == "ui.is_checked") {
        return HostFunction{{Type::Str()}, Type::Bool()};
    }
    if (name == "ui.remove") {
        return HostFunction{{}, Type::Bool()};
    }
    if (name == "ui.show_cursor") {
        return HostFunction{{Type::Bool()}, Type::Bool()};
    }
    if (name == "ui.show_screen") {
        return HostFunction{{Type::Str()}, Type::Bool()};
    }
    if (name == "ui.set_checked") {
        return HostFunction{{Type::Str(), Type::Bool()}, Type::Bool()};
    }
    if (name == "ui.get_selected_option") {
        return HostFunction{{Type::Str()}, Type::Str()};
    }
    if (name == "ui.get_selected_index") {
        return HostFunction{{Type::Str()}, Type::Int()};
    }
    if (name == "ui.set_selected_index") {
        return HostFunction{{Type::Str(), Type::Int()}, Type::Bool()};
    }
    if (name == "ui.add_option") {
        return HostFunction{{Type::Str(), Type::Str()}, Type::Bool()};
    }
    if (name == "ui.set_position" || name == "ui.set_size") {
        return HostFunction{{Type::Str(), Type::Vec3()}, Type::Bool()};
    }
    if (name == "ui.get_position" || name == "ui.get_size") {
        return HostFunction{{Type::Str()}, Type::Vec3()};
    }
    if (name == "ui.set_opacity" || name == "ui.set_font_size") {
        return HostFunction{{Type::Str(), Type::Float()}, Type::Bool()};
    }
    if (name == "ui.play_tween" || name == "ui.stop_tween" || name == "ui.is_hovered") {
        return HostFunction{{Type::Str()}, Type::Bool()};
    }
    // Ungated: any script may look another object up. The kind is only known when the script runs,
    // so every lookup answers the common root; a missing name answers no object (id 0).
    if (name == "node") {
        return HostFunction{{Type::Str()}, Type::Object("Object3D")};
    }
    if (name == "input.axis") {
        return HostFunction{{Type::Str(), Type::Str()}, Type::Float()};
    }
    // Ungated: every script may attempt RPC. Without a peer every call fails closed (0, false,
    // "", "none"); the game injects its peer, like the input and audio systems.
    if (name == "rpc.begin") {
        return HostFunction{{Type::Str()}, Type::Int()};
    }
    if (name == "rpc.push_bool") {
        return HostFunction{{Type::Int(), Type::Bool()}, Type::Bool()};
    }
    if (name == "rpc.push_int") {
        return HostFunction{{Type::Int(), Type::Int()}, Type::Bool()};
    }
    if (name == "rpc.push_float") {
        return HostFunction{{Type::Int(), Type::Float()}, Type::Bool()};
    }
    if (name == "rpc.push_str") {
        return HostFunction{{Type::Int(), Type::Str()}, Type::Bool()};
    }
    if (name == "rpc.push_vec3") {
        return HostFunction{{Type::Int(), Type::Vec3()}, Type::Bool()};
    }
    if (name == "rpc.send") {
        return HostFunction{{Type::Int()}, Type::Int()};
    }
    if (name == "rpc.abort" || name == "rpc.cancel" || name == "rpc.release") {
        return HostFunction{{Type::Int()}, Type::Bool()};
    }
    if (name == "rpc.result_kind" || name == "rpc.error") {
        return HostFunction{{Type::Int()}, Type::Str()};
    }
    if (name == "rpc.result_bool") {
        return HostFunction{{Type::Int()}, Type::Bool()};
    }
    if (name == "rpc.result_int") {
        return HostFunction{{Type::Int()}, Type::Int()};
    }
    if (name == "rpc.result_float") {
        return HostFunction{{Type::Int()}, Type::Float()};
    }
    if (name == "rpc.result_str") {
        return HostFunction{{Type::Int()}, Type::Str()};
    }
    if (name == "rpc.result_vec3") {
        return HostFunction{{Type::Int()}, Type::Vec3()};
    }
    // Ungated: every script may attempt the lobby. Without a peer every call fails closed
    // (0, false, "", "none"); the game injects its peer, like the RPC one.
    if (name == "lobby.list" || name == "lobby.leave" || name == "lobby.room" ||
        name == "lobby.roster_count") {
        return HostFunction{{}, Type::Int()};
    }
    if (name == "lobby.create") {
        return HostFunction{{Type::Str(), Type::Str(), Type::Int()},
                               Type::Int()};
    }
    if (name == "lobby.join") {
        return HostFunction{{Type::Int(), Type::Str()}, Type::Int()};
    }
    if (name == "lobby.roster") {
        return HostFunction{{Type::Int()}, Type::Int()};
    }
    if (name == "lobby.result_kind" || name == "lobby.error") {
        return HostFunction{{Type::Int()}, Type::Str()};
    }
    if (name == "lobby.result_room_count" || name == "lobby.result_created_room" ||
        name == "lobby.result_roster_count") {
        return HostFunction{{Type::Int()}, Type::Int()};
    }
    if (name == "lobby.result_room_id" || name == "lobby.result_room_players" ||
        name == "lobby.result_room_max") {
        return HostFunction{{Type::Int(), Type::Int()}, Type::Int()};
    }
    if (name == "lobby.result_room_name" || name == "lobby.result_roster_name") {
        return HostFunction{{Type::Int(), Type::Int()}, Type::Str()};
    }
    if (name == "lobby.roster_name") {
        return HostFunction{{Type::Int()}, Type::Str()};
    }
    if (name == "lobby.result_left" || name == "lobby.release") {
        return HostFunction{{Type::Int()}, Type::Bool()};
    }
    if (name == "lobby.cancel_all") {
        return HostFunction{{}, Type::Int()};
    }
    // Ungated: every script may attempt the relay. Without the attachment each call fails
    // closed (0, false, "", "unknown"); the game injects its relay and session, like the
    // lobby peer. All synchronous - nothing to await, no events.
    if (name == "relay.peer_count" || name == "relay.stat_received" ||
        name == "relay.stat_forwarded" || name == "relay.stat_dropped") {
        return HostFunction{{}, Type::Int()};
    }
    if (name == "relay.room") {
        return HostFunction{{Type::Int()}, Type::Int()};
    }
    if (name == "relay.remove") {
        return HostFunction{{Type::Int()}, Type::Bool()};
    }
    if (name == "relay.peer_state") {
        return HostFunction{{Type::Int()}, Type::Str()};
    }
    if (name == "relay.set_room" || name == "relay.interested") {
        return HostFunction{{Type::Int(), Type::Int()}, Type::Bool()};
    }
    if (name == "relay.set_interest") {
        return HostFunction{{Type::Int(), Type::Int(), Type::Bool()},
                               Type::Bool()};
    }
    if (name == "relay.send") {
        return HostFunction{{Type::Str()}, Type::Bool()};
    }
    if (name == "relay.poll") {
        return HostFunction{{}, Type::Str()};
    }
    return DescribeWorldFunction(name);
}

std::optional<std::vector<Type>> BlueprintObjectHost::DescribeEvent(const std::string_view event) const {
    if (event == "ready") {
        return std::vector<Type>{};
    }
    // A widget under this object was used: a button clicked, a slider or check box changed (its
    // name), a text box committed (its name and text).
    if (event == "ui_clicked" || event == "ui_changed") {
        return std::vector<Type>{Type::Str()};
    }
    // The pointer came onto, or left, a button (its name).
    if (event == "ui_hovered" || event == "ui_unhovered") {
        return std::vector<Type>{Type::Str()};
    }
    if (event == "ui_committed") {
        return std::vector<Type>{Type::Str(), Type::Str()};
    }
    if (event == "tick") {
        return std::vector<Type>{Type::Float()};
    }
    // A message another script (or this one) sent with `events.emit`: its name and payload.
    if (event == "custom_event") {
        return std::vector<Type>{Type::Str(), Type::Str()};
    }
    // One of this script's timers ran out (its id).
    if (event == "timer") {
        return std::vector<Type>{Type::Str()};
    }
    // The object is about to be removed (`world.destroy`, a life span, or the end of play).
    if (event == "destroyed") {
        return std::vector<Type>{};
    }
    // An AnimationSequencer's clip passed one of its events: sent to the player's own script and to
    // the script of the object it animates (a character's script hears its footsteps).
    if (event == "animation_event") {
        return std::vector<Type>{Type::Str()};
    }
    // A montage passed a notify, entered or left a notify state, or ended (its name).
    if (event == "montage_notify" || event == "montage_notify_begin" || event == "montage_notify_end" ||
        event == "montage_ended") {
        return std::vector<Type>{Type::Str()};
    }
    // Contact edges: both parties' scripts hear, each with the other object's name ("" when the
    // other has no Name component). Scripts predate neither party - a contact already active when
    // a script starts surfaces no enter, though its exit still fires.
    if (event == "collision_enter" || event == "collision_exit" || event == "overlap_enter" ||
        event == "overlap_exit") {
        return std::vector<Type>{Type::Str()};
    }
    // An RPC call this object's script sent has completed: the call id plus whether it
    // succeeded. Raised by the game from this host's response sink; completed results wait in
    // the peer for the result getters (or release).
    if (event == "rpc_response") {
        return std::vector<Type>{Type::Int(), Type::Bool()};
    }
    // A lobby call this object's script sent has completed: the call id plus whether it
    // succeeded. Raised by the game from this host's result sink; completed results wait in
    // the host for the result getters (or release).
    if (event == "lobby_result") {
        return std::vector<Type>{Type::Int(), Type::Bool()};
    }
    // The lobby pushed a roster for the room this object's script is in: the room id.
    // Raised by the game from this host's roster sink; the roster itself reads live.
    if (event == "lobby_roster") {
        return std::vector<Type>{Type::Int()};
    }
    return std::nullopt;
}

Value BlueprintObjectHost::GetProperty(const std::string_view name) {
    if (name == "name") {
        return m_entityManager.HasComponent<Scene::NameComponent>(m_entity)
                   ? Value{m_entityManager.GetComponent<Scene::NameComponent>(m_entity).name}
                   : Value{std::string{}};
    }
    if (name == "position" || name == "scale") {
        const Scene::TransformComponent& transform = m_entityManager.GetComponent<Scene::TransformComponent>(m_entity);
        return ToScript(name == "position" ? transform.localPosition : transform.localScale);
    }
    if (name == "velocity") {
        // Both bodies on one entity is nonsense, but the character wins rather than crashing.
        if (m_entityManager.HasComponent<Scene::CharacterControllerComponent>(m_entity)) {
            return ToScript(
                m_entityManager.GetComponent<Scene::CharacterControllerComponent>(m_entity).velocity);
        }
        return ToScript(m_entityManager.GetComponent<Scene::Rigid3DComponent>(m_entity).velocity);
    }
    if (name == "volume" || name == "pitch") {
        const Scene::AudioSourceComponent& source =
            m_entityManager.GetComponent<Scene::AudioSourceComponent>(m_entity);
        return static_cast<double>(name == "volume" ? source.volume : source.pitch);
    }
    if (name == "visible") {
        return !m_entityManager.HasComponent<Scene::VisibilityComponent>(m_entity) ||
               m_entityManager.GetComponent<Scene::VisibilityComponent>(m_entity).visible;
    }
    if (name == "intensity") {
        return static_cast<double>(
            m_entityManager.GetComponent<Scene::LightComponent>(m_entity).intensity);
    }
    if (name == "fov") {
        return static_cast<double>(
            m_entityManager.GetComponent<Scene::CameraComponent>(m_entity).fieldOfViewDegrees);
    }
    if (name == "rotation") {
        Math::Vector3 radians{};
        static_cast<void>(Math::TryToEuler(m_entityManager.GetComponent<Scene::TransformComponent>(m_entity).localRotation, radians));
        constexpr float kDegrees = 57.29578F;
        return ToScript(Math::Vector3{radians.x * kDegrees, radians.y * kDegrees, radians.z * kDegrees});
    }
    if (name == "light_color") {
        return ToScript(Math::DisplayFromColor(m_entityManager.GetComponent<Scene::LightComponent>(m_entity).color));
    }
    if (name == "max_speed") {
        return static_cast<double>(m_entityManager.GetComponent<Scene::CharacterControllerComponent>(m_entity).moveSpeed);
    }
    if (name == "jump_height") {
        return static_cast<double>(m_entityManager.GetComponent<Scene::CharacterControllerComponent>(m_entity).jumpHeight);
    }
    return m_entityManager.GetComponent<Scene::CharacterControllerComponent>(m_entity).grounded;
}

void BlueprintObjectHost::SetProperty(const std::string_view name, const Value& value) {
    if (name == "position" || name == "scale") {
        Scene::TransformComponent& transform = m_entityManager.GetComponent<Scene::TransformComponent>(m_entity);
        (name == "position" ? transform.localPosition : transform.localScale) = ToEngine(value);
        // Marked stale so the scene graph recomputes the world transform. Not through
        // SetLocalTransform: that ignores an object that has no world transform yet.
        if (m_entityManager.HasComponent<Scene::WorldTransformComponent>(m_entity)) {
            m_entityManager.GetComponent<Scene::WorldTransformComponent>(m_entity).dirty = true;
        }
    } else if (name == "velocity") {
        if (m_entityManager.HasComponent<Scene::CharacterControllerComponent>(m_entity)) {
            m_entityManager.GetComponent<Scene::CharacterControllerComponent>(m_entity).velocity =
                ToEngine(value);
        } else {
            m_entityManager.GetComponent<Scene::Rigid3DComponent>(m_entity).velocity = ToEngine(value);
        }
    } else if (name == "volume" || name == "pitch") {
        const float updated = static_cast<float>(std::get<double>(value));
        Scene::AudioSourceComponent& source =
            m_entityManager.GetComponent<Scene::AudioSourceComponent>(m_entity);
        // Unlike velocity's blind store, an invalid write is ignored: a NaN volume would make
        // AudioSourceSystem skip the source on every Sync, so the component stays valid.
        const bool accepted = name == "volume" ? (std::isfinite(updated) && updated >= 0.0F)
                                               : (std::isfinite(updated) && updated > 0.0F);
        if (accepted) {
            (name == "volume" ? source.volume : source.pitch) = updated;
        }
    } else if (name == "visible") {
        SetVisible(m_entityManager, m_entity, std::get<bool>(value));
    } else if (name == "intensity") {
        const float updated = static_cast<float>(std::get<double>(value));
        if (std::isfinite(updated) && updated >= 0.0F) {
            m_entityManager.GetComponent<Scene::LightComponent>(m_entity).intensity = updated;
        }
    } else if (name == "rotation") {
        const Math::Vector3 degrees = ToEngine(value);
        constexpr float kRadians = 0.017453292F;
        Math::Quaternion rotation{};
        if (Math::TryMakeEuler(Math::Vector3{degrees.x * kRadians, degrees.y * kRadians, degrees.z * kRadians}, rotation)) {
            m_entityManager.GetComponent<Scene::TransformComponent>(m_entity).localRotation = rotation;
            if (m_entityManager.HasComponent<Scene::WorldTransformComponent>(m_entity)) {
                m_entityManager.GetComponent<Scene::WorldTransformComponent>(m_entity).dirty = true;
            }
        }
    } else if (name == "light_color") {
        const Math::Vector3 colour = ToEngine(value);
        if (std::isfinite(colour.x) && std::isfinite(colour.y) && std::isfinite(colour.z)) {
            // Given as a display colour (0..1, as a colour picker shows it), kept linear.
            m_entityManager.GetComponent<Scene::LightComponent>(m_entity).color = Math::ColorFromDisplay(
                Math::Vector3{std::clamp(colour.x, 0.0F, 1.0F), std::clamp(colour.y, 0.0F, 1.0F), std::clamp(colour.z, 0.0F, 1.0F)});
        }
    } else if (name == "max_speed" || name == "jump_height") {
        const float updated = static_cast<float>(std::get<double>(value));
        if (std::isfinite(updated) && updated >= 0.0F) {
            auto& body = m_entityManager.GetComponent<Scene::CharacterControllerComponent>(m_entity);
            (name == "max_speed" ? body.moveSpeed : body.jumpHeight) = updated;
        }
    } else if (name == "fov") {
        const float updated = static_cast<float>(std::get<double>(value));
        if (std::isfinite(updated) && updated >= Scene::kMinimumCameraFieldOfViewDegrees &&
            updated < 180.0F) {
            m_entityManager.GetComponent<Scene::CameraComponent>(m_entity).fieldOfViewDegrees =
                updated;
        }
    }
}

Value BlueprintObjectHost::CallFunction(const std::string_view name, const std::span<const Value> args) {
    // Ragdoll needs no input system either, so it routes before the null-input early-out below.
    if (name.starts_with("ragdoll.")) {
        const Scene::Entity ragdollEntity = FindRagdollFor(m_entityManager, m_entity);
        if (ragdollEntity == Scene::kInvalidEntity) {
            return Value{false};
        }
        Scene::Ragdoll3DComponent& ragdoll = m_entityManager.GetComponent<Scene::Ragdoll3DComponent>(ragdollEntity);
        if (name == "ragdoll.activate") {
            return Value{Scene::ActivateRagdoll(m_entityManager, ragdollEntity, Scene::RagdollMode::Simulating)};
        }
        if (name == "ragdoll.activate_powered") {
            return Value{Scene::ActivateRagdoll(m_entityManager, ragdollEntity, Scene::RagdollMode::Active)};
        }
        if (name == "ragdoll.deactivate") {
            return Value{Scene::DeactivateRagdoll(m_entityManager, ragdollEntity)};
        }
        if (name == "ragdoll.get_up") {
            return Value{Scene::BeginRagdollRecovery(m_entityManager, ragdollEntity)};
        }
        if (name == "ragdoll.is_active") {
            return Value{Scene::IsRagdollActive(m_entityManager, ragdollEntity)};
        }
        if (name == "ragdoll.is_at_rest") {
            return Value{Scene::IsRagdollActive(m_entityManager, ragdollEntity) && ragdoll.solver.IsSleeping()};
        }
        if (name == "ragdoll.is_lying") {
            Scene::RagdollGetUpState state;
            return Value{Scene::EvaluateRagdollGetUpState(m_entityManager, ragdollEntity, state) &&
                         (state.orientation == Scene::RagdollLyingOrientation::FaceUp ||
                          state.orientation == Scene::RagdollLyingOrientation::FaceDown)};
        }
        if (name == "ragdoll.apply_impulse") {
            // A shove on the whole body: the hit lands on the pelvis, the first body.
            if (ragdoll.solver.GetBodyCount() == 0U) return Value{false};
            return Value{Scene::ApplyRagdollImpact(m_entityManager, ragdollEntity,
                                                   ragdoll.solver.GetBody(0U).position, ToEngine(args[0]), 10.0F)};
        }
        if (name == "ragdoll.hit") {
            return Value{Scene::ApplyRagdollImpact(m_entityManager, ragdollEntity, ToEngine(args[0]),
                                                   ToEngine(args[1]), 0.5F)};
        }
        if (name == "ragdoll.set_blend") {
            const double blend = std::get<double>(args[0]);
            if (!std::isfinite(blend)) return Value{false};
            ragdoll.physicsBlendWeight = static_cast<float>(std::clamp(blend, 0.0, 1.0));
            return Value{true};
        }
        if (name == "ragdoll.get_blend") {
            return Value{static_cast<double>(ragdoll.physicsBlendWeight)};
        }
        if (name == "ragdoll.set_auto_recover") {
            ragdoll.autoRecover = std::get<bool>(args[0]);
            return Value{true};
        }
        if (name == "ragdoll.set_motor_stiffness") {
            const double stiffness = std::get<double>(args[0]);
            if (!std::isfinite(stiffness)) return Value{false};
            ragdoll.motorStiffness = static_cast<float>(std::max(0.0, stiffness));
            return Value{true};
        }
        return Value{false};
    }
    // Physics needs no input system, so it routes before the null-input early-out below.
    if (name == "physics.apply_force") {
        return Scene::Rigid3D::ApplyForce(m_entityManager, m_entity, ToEngine(args[0]));
    }
    if (name == "physics.apply_impulse") {
        return Scene::Rigid3D::ApplyImpulse(m_entityManager, m_entity, ToEngine(args[0]));
    }
    if (name == "physics.apply_torque") {
        return Scene::Rigid3D::ApplyTorque(m_entityManager, m_entity, ToEngine(args[0]));
    }
    // Audio needs no input system, so it routes before the null-input early-out below.
    if (name == "audio.play" || name == "audio.stop" || name == "audio.is_playing") {
        if (m_audioSources == nullptr || m_audio == nullptr) {
            return Value{false};
        }
        if (name == "audio.play") {
            return m_audioSources->PlayEntity(m_entity, m_entityManager, *m_audio);
        }
        if (name == "audio.stop") {
            return m_audioSources->StopEntity(m_entity, *m_audio);
        }
        return m_audioSources->IsEntityPlaying(m_entity, *m_audio);
    }
    // Gameplay pools and tags need no input system, so they route before the null-input
    // early-out below. A missing pool reads false and 0.0; use `attributes.has` to tell a missing
    // pool from a depleted one.
    if (name == "attributes.has" || name == "attributes.get" || name == "attributes.max" ||
        name == "attributes.damage" || name == "attributes.heal") {
        const std::string& id = std::get<std::string>(args[0]);
        Scene::GameplayAttribute* found = nullptr;
        if (m_entityManager.HasComponent<Scene::GameplayAttributesComponent>(m_entity)) {
            found = Scene::FindGameplayAttribute(
                m_entityManager.GetComponent<Scene::GameplayAttributesComponent>(m_entity), id);
        }
        if (name == "attributes.has") {
            return found != nullptr;
        }
        if (name == "attributes.get") {
            return found != nullptr ? static_cast<double>(found->current) : 0.0;
        }
        if (name == "attributes.max") {
            return found != nullptr ? static_cast<double>(found->maximum) : 0.0;
        }
        if (found == nullptr) {
            return Value{false};
        }
        const float amount = static_cast<float>(std::get<double>(args[1]));
        const Scene::GameplayAttributeDamageResult result =
            name == "attributes.damage" ? Scene::DamageGameplayAttribute(*found, amount)
                                        : Scene::HealGameplayAttribute(*found, amount);
        return result.applied;
    }
    if (name.starts_with("anim.")) {
        Scene::AnimationGraphComponent* const graph = FindAnimationGraph(m_entityManager, m_entity);
        Scene::AnimationParameter* const parameter =
            graph != nullptr && !args.empty() ? FindAnimationParameter(*graph, std::get<std::string>(args[0])) : nullptr;
        if (name == "anim.get_float") {
            return Value{parameter != nullptr ? static_cast<double>(parameter->value) : 0.0};
        }
        if (name == "anim.get_bool") {
            return Value{parameter != nullptr && parameter->value != 0.0F};
        }
        if (parameter == nullptr) {
            return Value{false};
        }
        // The engine writes a character parameter (speed, direction, in air, grounded) every frame:
        // a script's value would be gone at once, so it is refused.
        if (parameter->source != Scene::AnimationParameterSource::Script) {
            UVE_WARNING("UVScript: animation parameter '{}' is set by the character, not by scripts", parameter->name);
            return Value{false};
        }
        const auto number = [](const Value& value) {
            if (const auto* d = std::get_if<double>(&value)) {
                return static_cast<float>(*d);
            }
            if (const auto* i = std::get_if<std::int64_t>(&value)) {
                return static_cast<float>(*i);
            }
            if (const auto* b = std::get_if<bool>(&value)) {
                return *b ? 1.0F : 0.0F;
            }
            return 0.0F;
        };
        if (name == "anim.set_float" || name == "anim.set_int" || name == "anim.set_bool") {
            const float value = number(args[1]);
            if (!std::isfinite(value)) {
                return Value{false};
            }
            parameter->value = value;
            return Value{true};
        }
        if (name == "anim.trigger") {
            parameter->value = 1.0F;
            return Value{true};
        }
        if (name == "anim.set_vector") {
            if (parameter->type != Scene::AnimationParameterType::Vector3) {
                return Value{false};
            }
            const Math::Vector3 value{number(args[1]), number(args[2]), number(args[3])};
            if (!Math::IsFinite(value)) {
                return Value{false};
            }
            parameter->vector = value;
            return Value{true};
        }
        return Value{false};
    }
    if (name.starts_with("montage.")) {
        Scene::MontageSequencerComponent* const player = FindMontageSequencer(m_entityManager, m_entity);
        if (player == nullptr) {
            return name == "montage.section" ? Value{std::string{}} : name == "montage.curve" ? Value{0.0} : Value{false};
        }
        // Carried out by the engine's next animation pass, where the montage files are.
        if (name == "montage.play" || name == "montage.play_section") {
            player->request = Scene::MontageRequest::Play;
            player->requestMontage = std::get<std::string>(args[0]);
            player->requestSection = name == "montage.play_section" ? std::get<std::string>(args[1]) : std::string{};
            return Value{true};
        }
        if (name == "montage.jump_to") {
            if (!player->IsPlaying()) {
                return Value{false};
            }
            player->request = Scene::MontageRequest::JumpTo;
            player->requestSection = std::get<std::string>(args[0]);
            return Value{true};
        }
        if (name == "montage.stop") {
            const bool playing = player->IsPlaying();
            player->request = Scene::MontageRequest::Stop;
            return Value{playing};
        }
        if (name == "montage.is_playing") {
            return Value{player->IsPlaying() || player->request == Scene::MontageRequest::Play};
        }
        if (name == "montage.section") {
            return Value{player->playback.section};
        }
        if (name == "montage.curve") {
            const std::string& curve = std::get<std::string>(args[0]);
            for (const auto& [curveName, value] : player->curveValues) {
                if (curveName == curve) {
                    return Value{static_cast<double>(value)};
                }
            }
            return Value{0.0};
        }
    }
    if (name == "tags.has" || name == "tags.add" || name == "tags.remove") {
        if (!m_entityManager.HasComponent<Scene::GameplayTagComponent>(m_entity)) {
            return Value{false};
        }
        Scene::GameplayTagComponent& tags =
            m_entityManager.GetComponent<Scene::GameplayTagComponent>(m_entity);
        const std::string& tag = std::get<std::string>(args[0]);
        if (name == "tags.has") {
            return Scene::HasGameplayTag(tags, tag);
        }
        if (name == "tags.add") {
            return Scene::AddGameplayTag(tags, std::string{tag});
        }
        return Scene::RemoveGameplayTag(tags, tag);
    }
    if (name.starts_with("ui.")) {
        return WidgetCall(name, args);
    }
    // A lookup needs no input system, so it routes before the null-input early-out below.
    if (name == "node") {
        const std::string& wanted = std::get<std::string>(args[0]);
        Scene::Entity found = Scene::kInvalidEntity;
        m_entityManager.ForEach<Scene::NameComponent>(
            [&](const Scene::Entity entity, const Scene::NameComponent& named) {
                if (found == Scene::kInvalidEntity && named.name == wanted) {
                    found = entity;
                }
            });
        if (found == Scene::kInvalidEntity) {
            return Value{ObjectRef{}};
        }
        return Value{ObjectRef{PackEntity(found)}};
    }
    if (name.starts_with("array.empty_")) {
        return Blueprint::MakeListValue({});
    }
    if (name.starts_with("text.") || name == "math.round" || name == "math.ceil" || name == "math.snap" || name == "math.wrap" ||
        name == "math.sign" || name == "math.rotate_about" || name == "math.random_unit_vector" || name == "math.random_in_box") {
        return TextAndMathCall(name, args);
    }
    if (name.starts_with("character.") || name.starts_with("transform.") || name.starts_with("math.")) {
        return GameplayCall(name, args);
    }
    if (name.starts_with("events.") || name.starts_with("timers.") || name.starts_with("world.") || name.starts_with("object.") ||
        name.starts_with("trace.") || name.starts_with("game.") || name.starts_with("save.") || name.starts_with("camera.") ||
        name.starts_with("particles.") || name.starts_with("render.") || name.starts_with("sequencer.") || name.starts_with("ai.") ||
        name == "audio.play_at" || name == "audio.play_2d") {
        return WorldCall(name, args);
    }
    // RPC, the lobby, and the relay need no input system, so they route before the
    // null-input early-out below.
    if (name.starts_with("rpc.")) {
        return RpcCall(name, args);
    }
    if (name.starts_with("lobby.")) {
        return LobbyCall(name, args);
    }
    if (name.starts_with("relay.")) {
        return RelayCall(name, args);
    }
    if (m_input == nullptr) {
        if (name == "input.mouse_delta") {
            return ToScript(Math::Vector3{});
        }
        return name == "input.axis" || name == "input.axis_value" ? Value{0.0} : Value{false};
    }
    if (name == "input.axis_value") {
        return static_cast<double>(m_input->GetAxisValue(std::get<std::string>(args[0])));
    }
    if (name == "input.mouse_delta") {
        const Math::Vector2 delta = m_input->GetMouseDelta();
        return ToScript(Math::Vector3{delta.x, delta.y, 0.0F});
    }
    if (name == "input.axis") {
        const bool negative = m_input->IsActionHeld(std::get<std::string>(args[0]));
        const bool positive = m_input->IsActionHeld(std::get<std::string>(args[1]));
        return (positive ? 1.0 : 0.0) - (negative ? 1.0 : 0.0);
    }
    const std::string& action = std::get<std::string>(args[0]);
    if (name == "input.pressed") {
        return m_input->IsActionTriggered(action);
    }
    if (name == "input.released") {
        return m_input->IsActionReleased(action);
    }
    return m_input->IsActionHeld(action);
}

Value BlueprintObjectHost::GameplayCall(const std::string_view name, const std::span<const Value> args) {
    if (name.starts_with("math.")) {
        static std::mt19937 random{std::random_device{}()};
        if (name.starts_with("math.select_")) {
            return std::get<bool>(args[0]) ? args[1] : args[2];
        }
        if (name == "math.random_bool") {
            return std::uniform_int_distribution<int>(0, 1)(random) == 1;
        }
        if (name == "math.random_int") {
            const std::int64_t a = std::get<std::int64_t>(args[0]);
            const std::int64_t b = std::get<std::int64_t>(args[1]);
            return std::uniform_int_distribution<std::int64_t>(std::min(a, b), std::max(a, b))(random);
        }
        const double a = std::get<double>(args[0]);
        const double b = std::get<double>(args[1]);
        if (name == "math.random_range") {
            return a == b ? a : std::uniform_real_distribution<double>(std::min(a, b), std::max(a, b))(random);
        }
        if (name == "math.pow") {
            return std::pow(a, b);
        }
        if (name == "math.atan2") {
            return std::atan2(a, b) * 57.29577951308232;   // degrees, as rotations are
        }
        return b == 0.0 ? 0.0 : std::fmod(a, b);
    }
    if (name.starts_with("character.")) {
        auto& body = m_entityManager.GetComponent<Scene::CharacterControllerComponent>(m_entity);
        if (name == "character.move") {
            const Math::Vector3 direction = ToEngine(args[0]);
            if (!std::isfinite(direction.x) || !std::isfinite(direction.z)) {
                return false;
            }
            Math::Vector3 move{body.scriptMove.x + direction.x, 0.0F, body.scriptMove.z + direction.z};
            const float length = std::sqrt(move.x * move.x + move.z * move.z);
            body.scriptMove = length > 1.0F ? Math::Vector3{move.x / length, 0.0F, move.z / length} : move;
            return true;
        }
        if (name == "character.jump") {
            body.scriptJump = true;
            return true;
        }
        if (name == "character.launch") {
            const Math::Vector3 push = ToEngine(args[0]);
            if (!std::isfinite(push.x) || !std::isfinite(push.y) || !std::isfinite(push.z)) {
                return false;
            }
            body.velocity = Math::Vector3{body.velocity.x + push.x, body.velocity.y + push.y, body.velocity.z + push.z};
            return true;
        }
        if (name == "character.falling") {
            return !body.grounded && body.velocity.y < 0.0F;
        }
        return static_cast<double>(std::sqrt(body.velocity.x * body.velocity.x + body.velocity.z * body.velocity.z));
    }
    // transform.*
    Scene::TransformComponent& transform = m_entityManager.GetComponent<Scene::TransformComponent>(m_entity);
    const auto touched = [this] {
        if (m_entityManager.HasComponent<Scene::WorldTransformComponent>(m_entity)) {
            m_entityManager.GetComponent<Scene::WorldTransformComponent>(m_entity).dirty = true;
        }
    };
    if (name == "transform.forward" || name == "transform.right" || name == "transform.up") {
        // The engine's forward is -Z, right +X, up +Y, turned by the object's rotation.
        const Math::Vector3 axis = name == "transform.forward" ? Math::Vector3{0.0F, 0.0F, -1.0F}
                                      : name == "transform.right" ? Math::Vector3{1.0F, 0.0F, 0.0F}
                                                                  : Math::Vector3{0.0F, 1.0F, 0.0F};
        return ToScript(Math::RotateVector(transform.localRotation, axis));
    }
    if (name == "transform.distance_to") {
        const Math::Vector3 to = ToEngine(args[0]);
        const Math::Vector3 d{to.x - transform.localPosition.x, to.y - transform.localPosition.y, to.z - transform.localPosition.z};
        return static_cast<double>(std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z));
    }
    if (name == "transform.add_yaw" || name == "transform.add_pitch") {
        const float degrees = static_cast<float>(std::get<double>(args[0]));
        if (!std::isfinite(degrees)) {
            return false;
        }
        Math::Quaternion turn{};
        const Math::Vector3 axis = name == "transform.add_yaw" ? Math::Vector3{0.0F, 1.0F, 0.0F} : Math::Vector3{1.0F, 0.0F, 0.0F};
        if (!Math::TryMakeAxisAngle(axis, degrees * 0.017453292F, turn)) {
            return false;
        }
        // Yaw turns about the world's up; pitch about the object's own right.
        transform.localRotation = name == "transform.add_yaw" ? Math::Multiply(turn, transform.localRotation)
                                                              : Math::Multiply(transform.localRotation, turn);
        touched();
        return true;
    }
    // transform.look_at: turn (about up) so forward points at the target.
    const Math::Vector3 target = ToEngine(args[0]);
    const float dx = target.x - transform.localPosition.x;
    const float dz = target.z - transform.localPosition.z;
    if (!(dx * dx + dz * dz > 1e-8F)) {
        return false;
    }
    Math::Quaternion facing{};
    if (!Math::TryMakeAxisAngle(Math::Vector3{0.0F, 1.0F, 0.0F}, std::atan2(-dx, -dz), facing)) {
        return false;
    }
    transform.localRotation = facing;
    touched();
    return true;
}

Value BlueprintObjectHost::RpcCall(const std::string_view name,
                                           const std::span<const Value> args) {
    if (m_rpc == nullptr) {
        // Fail closed without a peer: ids read 0, staging and cancel reads answer false,
        // results read their defaults, and kinds read "none".
        if (name == "rpc.result_kind") {
            return Value{std::string{"none"}};
        }
        if (name == "rpc.result_bool") {
            return Value{false};
        }
        if (name == "rpc.result_int") {
            return Value{std::int64_t{0}};
        }
        if (name == "rpc.result_float") {
            return Value{0.0};
        }
        if (name == "rpc.result_str" || name == "rpc.error") {
            return Value{std::string{}};
        }
        if (name == "rpc.result_vec3") {
            return Value{Vec3Value{}};
        }
        if (name == "rpc.begin" || name == "rpc.send") {
            return Value{std::int64_t{0}};
        }
        return Value{false};
    }
    if (name == "rpc.begin") {
        if (m_stagedRpc.size() >= kMaxStagedRpcCalls) {
            return Value{std::int64_t{0}};
        }
        const std::uint32_t stagingId = NextStagingId();
        m_stagedRpc.emplace(stagingId,
                            StagedRpcCall{std::string{std::get<std::string>(args[0])}, {}});
        return Value{static_cast<std::int64_t>(stagingId)};
    }
    const auto idArg = static_cast<std::uint32_t>(std::get<std::int64_t>(args[0]));
    if (name == "rpc.push_bool" || name == "rpc.push_int" || name == "rpc.push_float" ||
        name == "rpc.push_str" || name == "rpc.push_vec3") {
        const auto staged = m_stagedRpc.find(idArg);
        if (staged == m_stagedRpc.end()) {
            return Value{false};
        }
        staged->second.args.push_back(args[1]);
        return Value{true};
    }
    if (name == "rpc.send") {
        const auto staged = m_stagedRpc.find(idArg);
        if (staged == m_stagedRpc.end()) {
            return Value{std::int64_t{0}};
        }
        StagedRpcCall call = std::move(staged->second);
        m_stagedRpc.erase(staged);
        const Network::RpcCallId callId = m_rpc->Call(
            call.method, call.args,
            [this](const Network::RpcCallId id, const Network::RpcOutcome& outcome) {
                m_pendingRpc.erase(id);
                if (m_sink) {
                    m_sink(id, outcome.ok);
                }
            });
        if (callId == Network::kInvalidRpcCallId) {
            return Value{std::int64_t{0}};
        }
        m_pendingRpc.insert(callId);
        return Value{static_cast<std::int64_t>(callId)};
    }
    if (name == "rpc.abort") {
        return Value{m_stagedRpc.erase(idArg) > 0U};
    }
    if (name == "rpc.cancel") {
        // Converge to the peer's truth: a call it no longer tracks is not ours either.
        m_pendingRpc.erase(idArg);
        return Value{m_rpc->Cancel(idArg)};
    }
    if (name == "rpc.release") {
        return Value{m_rpc->ForgetResult(idArg)};
    }
    if (name == "rpc.result_kind") {
        const std::optional<Network::RpcOutcome> outcome = m_rpc->Result(idArg);
        if (!outcome.has_value()) {
            return Value{std::string{m_pendingRpc.contains(idArg) ? "pending" : "none"}};
        }
        if (!outcome->ok) {
            return Value{std::string{"error"}};
        }
        return Value{std::string{RpcValueKind(outcome->value)}};
    }
    if (name == "rpc.error") {
        const std::optional<Network::RpcOutcome> outcome = m_rpc->Result(idArg);
        if (!outcome.has_value() || outcome->ok) {
            return Value{std::string{}};
        }
        return Value{outcome->error};
    }
    // The remaining names are the result getters: a pending or failed call reads its default,
    // a mismatched kind reads its default, and `result_str` formats anything as text.
    const std::optional<Network::RpcOutcome> outcome = m_rpc->Result(idArg);
    const bool failed = !outcome.has_value() || !outcome->ok;
    if (name == "rpc.result_bool") {
        if (failed) {
            return Value{false};
        }
        const bool* boolean = std::get_if<bool>(&outcome->value);
        return Value{boolean != nullptr && *boolean};
    }
    if (name == "rpc.result_int") {
        if (failed) {
            return Value{std::int64_t{0}};
        }
        const auto* integer = std::get_if<std::int64_t>(&outcome->value);
        return Value{integer != nullptr ? *integer : std::int64_t{0}};
    }
    if (name == "rpc.result_float") {
        if (failed) {
            return Value{0.0};
        }
        const auto* number = std::get_if<double>(&outcome->value);
        return Value{number != nullptr ? *number : 0.0};
    }
    if (name == "rpc.result_str") {
        if (failed) {
            return Value{std::string{}};
        }
        return Value{Blueprint::FormatValue(outcome->value)};
    }
    if (name == "rpc.result_vec3") {
        if (failed) {
            return Value{Vec3Value{}};
        }
        const auto* vec = std::get_if<Vec3Value>(&outcome->value);
        return Value{vec != nullptr ? *vec : Vec3Value{}};
    }
    return Value{false};
}

Value BlueprintObjectHost::LobbyCall(const std::string_view name,
                                             const std::span<const Value> args) {
    if (m_lobby == nullptr) {
        // Fail closed without a peer: ids read 0, booleans answer false, results read
        // their defaults, and kinds read "none".
        if (name == "lobby.result_kind") {
            return Value{std::string{"none"}};
        }
        if (name == "lobby.error" || name == "lobby.result_room_name" ||
            name == "lobby.result_roster_name" || name == "lobby.roster_name") {
            return Value{std::string{}};
        }
        if (name == "lobby.result_left" || name == "lobby.release") {
            return Value{false};
        }
        return Value{std::int64_t{0}};
    }
    // The script owns the peer's roster push: hook it on every call (cheap, self-healing),
    // so the live readers below stay true however the room was reached.
    m_lobby->SetRosterHandler(
        [this](const Network::LobbyRoomId room, const std::vector<std::string>&) {
            if (m_rosterSink) {
                m_rosterSink(room);
            }
        });
    if (name == "lobby.room") {
        return Value{static_cast<std::int64_t>(m_lobby->CurrentRoom())};
    }
    if (name == "lobby.roster_count") {
        return Value{static_cast<std::int64_t>(m_lobby->CachedRoster().size())};
    }
    if (name == "lobby.roster_name") {
        const std::vector<std::string> roster = m_lobby->CachedRoster();
        const std::int64_t index = std::get<std::int64_t>(args[0]);
        if (index < 0 || index >= static_cast<std::int64_t>(roster.size())) {
            return Value{std::string{}};
        }
        return Value{roster[static_cast<std::size_t>(index)]};
    }
    if (name == "lobby.list") {
        const std::uint32_t id = StartLobbyCall(LobbyCallKind::List);
        m_lobby->ListRooms(
            [this, id](std::vector<Network::LobbyRoomInfo> rooms, const std::string& error) {
                auto outcome = m_lobbyCalls.find(id);
                if (outcome == m_lobbyCalls.end()) {
                    return;
                }
                outcome->second.done = true;
                outcome->second.ok = error.empty();
                outcome->second.error = error;
                for (const Network::LobbyRoomInfo& room : rooms) {
                    outcome->second.rooms.push_back(
                        LobbyRoomDesc{room.id, room.name, room.players, room.maxPlayers});
                }
                if (outcome->second.settled && m_lobbySink) {
                    m_lobbySink(id, outcome->second.ok);
                }
            });
        return FinishLobbyCall(id);
    }
    if (name == "lobby.create") {
        // Refuse what would wrap; a preserved 0 still goes to the server for its own error.
        const std::int64_t maxArg = std::get<std::int64_t>(args[2]);
        if (maxArg < 0 || maxArg > 4294967295LL) {
            return Value{std::int64_t{0}};
        }
        const std::uint32_t id = StartLobbyCall(LobbyCallKind::Create);
        m_lobby->CreateRoom(
            std::get<std::string>(args[0]), std::get<std::string>(args[1]),
            static_cast<std::uint32_t>(maxArg),
            [this, id](const Network::LobbyRoomId room, const std::string& error) {
                auto outcome = m_lobbyCalls.find(id);
                if (outcome == m_lobbyCalls.end()) {
                    return;
                }
                outcome->second.done = true;
                outcome->second.ok = error.empty();
                outcome->second.error = error;
                outcome->second.room = room;
                if (outcome->second.settled && m_lobbySink) {
                    m_lobbySink(id, outcome->second.ok);
                }
            });
        return FinishLobbyCall(id);
    }
    if (name == "lobby.join") {
        const std::int64_t roomArg = std::get<std::int64_t>(args[0]);
        if (roomArg < 0 || roomArg > 4294967295LL) {
            return Value{std::int64_t{0}};
        }
        const std::uint32_t id = StartLobbyCall(LobbyCallKind::Join);
        m_lobby->JoinRoom(
            static_cast<std::uint32_t>(roomArg), std::get<std::string>(args[1]),
            [this, id](std::vector<std::string> roster, const std::string& error) {
                auto outcome = m_lobbyCalls.find(id);
                if (outcome == m_lobbyCalls.end()) {
                    return;
                }
                outcome->second.done = true;
                outcome->second.ok = error.empty();
                outcome->second.error = error;
                outcome->second.roster = std::move(roster);
                if (outcome->second.settled && m_lobbySink) {
                    m_lobbySink(id, outcome->second.ok);
                }
            });
        return FinishLobbyCall(id);
    }
    if (name == "lobby.leave") {
        const std::uint32_t id = StartLobbyCall(LobbyCallKind::Leave);
        m_lobby->LeaveRoom([this, id](const bool left, const std::string& error) {
            auto outcome = m_lobbyCalls.find(id);
            if (outcome == m_lobbyCalls.end()) {
                return;
            }
            outcome->second.done = true;
            outcome->second.ok = error.empty();
            outcome->second.error = error;
            outcome->second.left = left;
            if (outcome->second.settled && m_lobbySink) {
                m_lobbySink(id, outcome->second.ok);
            }
        });
        return FinishLobbyCall(id);
    }
    if (name == "lobby.roster") {
        const std::int64_t roomArg = std::get<std::int64_t>(args[0]);
        if (roomArg < 0 || roomArg > 4294967295LL) {
            return Value{std::int64_t{0}};
        }
        const std::uint32_t id = StartLobbyCall(LobbyCallKind::Roster);
        m_lobby->QueryRoster(
            static_cast<std::uint32_t>(roomArg),
            [this, id](std::vector<std::string> roster, const std::string& error) {
                auto outcome = m_lobbyCalls.find(id);
                if (outcome == m_lobbyCalls.end()) {
                    return;
                }
                outcome->second.done = true;
                outcome->second.ok = error.empty();
                outcome->second.error = error;
                outcome->second.roster = std::move(roster);
                if (outcome->second.settled && m_lobbySink) {
                    m_lobbySink(id, outcome->second.ok);
                }
            });
        return FinishLobbyCall(id);
    }
    if (name == "lobby.cancel_all") {
        const std::size_t abandoned = m_lobby->CancelPending();
        // Completed outcomes stay readable; only the now-never-completing go.
        for (auto call = m_lobbyCalls.begin(); call != m_lobbyCalls.end();) {
            if (call->second.done) {
                ++call;
            } else {
                call = m_lobbyCalls.erase(call);
            }
        }
        return Value{static_cast<std::int64_t>(abandoned)};
    }
    const auto idArg = static_cast<std::uint32_t>(std::get<std::int64_t>(args[0]));
    const auto call = m_lobbyCalls.find(idArg);
    if (name == "lobby.release") {
        if (call == m_lobbyCalls.end() || !call->second.done) {
            return Value{false};
        }
        m_lobbyCalls.erase(call);
        return Value{true};
    }
    if (name == "lobby.result_kind") {
        if (call == m_lobbyCalls.end()) {
            return Value{std::string{"none"}};
        }
        if (!call->second.done) {
            return Value{std::string{"pending"}};
        }
        if (!call->second.ok) {
            return Value{std::string{"error"}};
        }
        switch (call->second.kind) {
            case LobbyCallKind::List:
                return Value{std::string{"rooms"}};
            case LobbyCallKind::Create:
                return Value{std::string{"room"}};
            case LobbyCallKind::Join:
            case LobbyCallKind::Roster:
                return Value{std::string{"roster"}};
            case LobbyCallKind::Leave:
                return Value{std::string{"left"}};
        }
        return Value{std::string{"none"}};
    }
    if (name == "lobby.error") {
        if (call == m_lobbyCalls.end() || !call->second.done || call->second.ok) {
            return Value{std::string{}};
        }
        return Value{call->second.error};
    }
    // The remaining names are the result getters: anything but a completed success of the
    // matching kind reads its default.
    const bool failed = call == m_lobbyCalls.end() || !call->second.done || !call->second.ok;
    if (name == "lobby.result_room_count") {
        if (failed || call->second.kind != LobbyCallKind::List) {
            return Value{std::int64_t{0}};
        }
        return Value{static_cast<std::int64_t>(call->second.rooms.size())};
    }
    if (name == "lobby.result_room_id" || name == "lobby.result_room_name" ||
        name == "lobby.result_room_players" || name == "lobby.result_room_max") {
        const bool wantName = name == "lobby.result_room_name";
        if (failed || call->second.kind != LobbyCallKind::List) {
            return wantName ? Value{std::string{}} : Value{std::int64_t{0}};
        }
        const std::int64_t index = std::get<std::int64_t>(args[1]);
        if (index < 0 || index >= static_cast<std::int64_t>(call->second.rooms.size())) {
            return wantName ? Value{std::string{}} : Value{std::int64_t{0}};
        }
        const LobbyRoomDesc& room = call->second.rooms[static_cast<std::size_t>(index)];
        if (name == "lobby.result_room_id") {
            return Value{static_cast<std::int64_t>(room.id)};
        }
        if (wantName) {
            return Value{room.name};
        }
        if (name == "lobby.result_room_players") {
            return Value{static_cast<std::int64_t>(room.players)};
        }
        return Value{static_cast<std::int64_t>(room.maxPlayers)};
    }
    if (name == "lobby.result_created_room") {
        if (failed || call->second.kind != LobbyCallKind::Create) {
            return Value{std::int64_t{0}};
        }
        return Value{static_cast<std::int64_t>(call->second.room)};
    }
    if (name == "lobby.result_roster_count") {
        if (failed || (call->second.kind != LobbyCallKind::Join &&
                       call->second.kind != LobbyCallKind::Roster)) {
            return Value{std::int64_t{0}};
        }
        return Value{static_cast<std::int64_t>(call->second.roster.size())};
    }
    if (name == "lobby.result_roster_name") {
        if (failed || (call->second.kind != LobbyCallKind::Join &&
                       call->second.kind != LobbyCallKind::Roster)) {
            return Value{std::string{}};
        }
        const std::int64_t index = std::get<std::int64_t>(args[1]);
        if (index < 0 || index >= static_cast<std::int64_t>(call->second.roster.size())) {
            return Value{std::string{}};
        }
        return Value{call->second.roster[static_cast<std::size_t>(index)]};
    }
    if (name == "lobby.result_left") {
        if (failed || call->second.kind != LobbyCallKind::Leave) {
            return Value{false};
        }
        return Value{call->second.left};
    }
    return Value{false};
}

std::uint32_t BlueprintObjectHost::StartLobbyCall(const LobbyCallKind kind) {
    const std::uint32_t id = NextLobbyCallId();
    LobbyCallState& call = m_lobbyCalls.emplace(id, LobbyCallState{}).first->second;
    call.kind = kind;
    return id;
}

Value BlueprintObjectHost::FinishLobbyCall(const std::uint32_t id) {
    auto call = m_lobbyCalls.find(id);
    if (call == m_lobbyCalls.end()) {
        return Value{std::int64_t{0}};
    }
    call->second.settled = true;
    if (call->second.done && !call->second.ok) {
        // The send failed synchronously: no sink fired (nothing settled yet), so forget the
        // stillborn call and answer 0 like rpc.send does.
        m_lobbyCalls.erase(call);
        return Value{std::int64_t{0}};
    }
    return Value{static_cast<std::int64_t>(id)};
}

std::uint32_t BlueprintObjectHost::NextStagingId() noexcept {
    for (;;) {
        std::uint32_t stagingId = m_nextStagingId++;
        if (stagingId == 0U) {
            stagingId = m_nextStagingId++;
        }
        if (!m_stagedRpc.contains(stagingId)) {
            return stagingId;
        }
    }
}

std::uint32_t BlueprintObjectHost::NextLobbyCallId() noexcept {
    for (;;) {
        std::uint32_t callId = m_nextLobbyCall++;
        if (callId == 0U) {
            callId = m_nextLobbyCall++;
        }
        if (!m_lobbyCalls.contains(callId)) {
            return callId;
        }
    }
}

Value BlueprintObjectHost::RelayCall(const std::string_view name,
                                             const std::span<const Value> args) {
    // The client side needs the session, not the relay: send shouts, poll reads one arrival.
    if (name == "relay.send") {
        if (m_relaySession == nullptr) {
            return Value{false};
        }
        const std::string& text = std::get<std::string>(args[0]);
        return Value{m_relaySession->SendData(
            std::vector<std::uint8_t>(text.begin(), text.end()))};
    }
    if (name == "relay.poll") {
        if (m_relaySession == nullptr) {
            return Value{std::string{}};
        }
        const std::optional<std::vector<std::uint8_t>> datagram =
            m_relaySession->TryReceiveData();
        if (!datagram.has_value()) {
            return Value{std::string{}};
        }
        return Value{std::string{datagram->begin(), datagram->end()}};
    }
    if (m_relay == nullptr) {
        // Fail closed without a relay: counts and rooms read 0, setters answer false, and
        // states read "unknown".
        if (name == "relay.peer_state") {
            return Value{std::string{"unknown"}};
        }
        if (name == "relay.set_room" || name == "relay.set_interest" ||
            name == "relay.interested" || name == "relay.remove") {
            return Value{false};
        }
        return Value{std::int64_t{0}};
    }
    if (name == "relay.peer_count") {
        return Value{static_cast<std::int64_t>(m_relay->PeerCount())};
    }
    if (name == "relay.stat_received") {
        return Value{static_cast<std::int64_t>(m_relay->Stats().payloadsReceived)};
    }
    if (name == "relay.stat_forwarded") {
        return Value{static_cast<std::int64_t>(m_relay->Stats().payloadsForwarded)};
    }
    if (name == "relay.stat_dropped") {
        return Value{static_cast<std::int64_t>(m_relay->Stats().forwardsDropped)};
    }
    // Refuse what would wrap; in-range but unknown ids follow the core's own defaults
    // (room 0, fail-open interest, unremoved, "unknown").
    const std::int64_t peerArg = std::get<std::int64_t>(args[0]);
    if (peerArg < 0 || peerArg > 4294967295LL) {
        if (name == "relay.peer_state") {
            return Value{std::string{"unknown"}};
        }
        if (name == "relay.room") {
            return Value{std::int64_t{0}};
        }
        return Value{false};
    }
    const auto peer = static_cast<Network::RelayPeerId>(peerArg);
    if (name == "relay.peer_state") {
        const Network::NetworkSession* session = m_relay->PeerSession(peer);
        if (session == nullptr) {
            return Value{std::string{"unknown"}};
        }
        switch (session->State()) {
            case Network::SessionState::Disconnected:
                return Value{std::string{"disconnected"}};
            case Network::SessionState::Connecting:
                return Value{std::string{"connecting"}};
            case Network::SessionState::Reconnecting:
                return Value{std::string{"reconnecting"}};
            case Network::SessionState::Established:
                return Value{std::string{"established"}};
        }
        return Value{std::string{"unknown"}};
    }
    if (name == "relay.room") {
        return Value{static_cast<std::int64_t>(m_relay->PeerRoom(peer))};
    }
    if (name == "relay.remove") {
        return Value{m_relay->RemovePeer(peer)};
    }
    if (name == "relay.set_room") {
        const std::int64_t roomArg = std::get<std::int64_t>(args[1]);
        if (roomArg < 0 || roomArg > 4294967295LL) {
            return Value{false};
        }
        return Value{
            m_relay->SetPeerRoom(peer, static_cast<Network::RelayRoomId>(roomArg))};
    }
    const std::int64_t senderArg = std::get<std::int64_t>(args[1]);
    if (senderArg < 0 || senderArg > 4294967295LL) {
        return Value{false};
    }
    const auto sender = static_cast<Network::RelayPeerId>(senderArg);
    if (name == "relay.interested") {
        return Value{m_relay->IsPeerInterested(peer, sender)};
    }
    if (name == "relay.set_interest") {
        return Value{m_relay->SetPeerInterest(peer, sender, std::get<bool>(args[2]))};
    }
    return Value{false};
}

void BlueprintObjectHost::CallMethod(const ObjectRef target, const std::string_view method,
                                       const std::span<const Value> args) {
    // A stale handle (its entity long destroyed) lands on another occupant or nothing at all; the
    // generation check inside IsAlive tells the two apart, and both fail closed as nothing.
    if (target.id == 0U) {
        return;
    }
    const Scene::Entity entity = UnpackEntity(target);
    if (!m_entityManager.IsAlive(entity)) {
        return;
    }
    // The target's own script first: a `fn hide()` there overrides the built-in below.
    if (m_resolver) {
        if (Blueprint::ScriptInstance* const instance = m_resolver(entity);
            instance != nullptr && instance->HasFunction(method, args.size())) {
            static_cast<void>(instance->Call(method, args));
            return;
        }
    }
    if (method == "hide" || method == "show") {
        SetVisible(m_entityManager, entity, method == "show");
    }
}

void BlueprintObjectHost::Print([[maybe_unused]] const std::string_view text) {
    // `print` is a diagnostic: a shipping profile strips the statement below at compile time, and
    // the profile's runtime level would have dropped it anyway. Both the parameter and the object
    // name exist only for that message, so they are marked rather than left to warn as unused.
    [[maybe_unused]] const std::string object = m_entityManager.HasComponent<Scene::NameComponent>(m_entity)
                                 ? m_entityManager.GetComponent<Scene::NameComponent>(m_entity).name
                                 : std::string{"object"};
    UVE_INFO("[{}] {}", object, text);
}

Scene::Entity BlueprintObjectHost::FindWidget(const std::string_view name) const {
    auto& entities = const_cast<Scene::IEntityManager&>(m_entityManager);
    const bool scoped = entities.IsAlive(m_entity) && entities.HasComponent<Scene::CanvasComponent>(m_entity);
    const auto under = [&entities, this](Scene::Entity entity) {
        for (int depth = 0; depth < 256 && entities.IsAlive(entity); ++depth) {
            if (entity == m_entity) {
                return true;
            }
            if (!entities.HasComponent<Scene::HierarchyComponent>(entity)) {
                return false;
            }
            entity = entities.GetComponent<Scene::HierarchyComponent>(entity).parent;
        }
        return false;
    };
    Scene::Entity found = Scene::kInvalidEntity;
    entities.ForEach<Scene::NameComponent>([&](const Scene::Entity entity, const Scene::NameComponent& named) {
        if (found == Scene::kInvalidEntity && named.name == name && (!scoped || under(entity))) {
            found = entity;
        }
    });
    return found;
}

Value BlueprintObjectHost::WidgetCall(const std::string_view name, const std::span<const Value> args) {
    auto& entities = m_entityManager;
    if (name == "ui.create") {
        if (!m_widgetFactory) {
            return Value{ObjectRef{}};
        }
        const Scene::Entity root = m_widgetFactory(std::get<std::string>(args[0]));
        return Value{root == Scene::kInvalidEntity ? ObjectRef{} : ObjectRef{PackEntity(root)}};
    }
    if (name == "ui.add_to_viewport") {
        const ObjectRef ref = std::get<ObjectRef>(args[0]);
        const Scene::Entity root = ref.id == 0U ? Scene::kInvalidEntity : UnpackEntity(ref);
        if (root == Scene::kInvalidEntity || !entities.IsAlive(root) ||
            !entities.HasComponent<Scene::CanvasComponent>(root)) {
            return false;
        }
        Scene::CanvasComponent& canvas = entities.GetComponent<Scene::CanvasComponent>(root);
        canvas.visible = true;
        canvas.sortOrder = Scene::ClampCanvasSortOrder(static_cast<std::int32_t>(std::get<std::int64_t>(args[1])));
        if (entities.HasComponent<Scene::VisibilityComponent>(root)) {
            entities.GetComponent<Scene::VisibilityComponent>(root).visible = true;
        }
        return true;
    }
    if (name == "ui.show_screen") {
        // A widget's screens are the Canvases right under its root Canvas: one shows, the rest hide.
        Scene::Entity root = Scene::kInvalidEntity;
        for (Scene::Entity cursor = m_entity; entities.IsAlive(cursor);) {
            if (entities.HasComponent<Scene::CanvasComponent>(cursor)) {
                root = cursor;
            }
            if (!entities.HasComponent<Scene::HierarchyComponent>(cursor)) {
                break;
            }
            const Scene::Entity up = entities.GetComponent<Scene::HierarchyComponent>(cursor).parent;
            if (up == cursor) {
                break;
            }
            cursor = up;
        }
        if (root == Scene::kInvalidEntity) {
            return false;
        }
        const std::string& wanted = std::get<std::string>(args[0]);
        bool found = false;
        entities.ForEach<Scene::HierarchyComponent>([&](const Scene::Entity child, const Scene::HierarchyComponent& h) {
            if (h.parent != root || !entities.HasComponent<Scene::CanvasComponent>(child)) {
                return;
            }
            const bool match = entities.HasComponent<Scene::NameComponent>(child) &&
                               entities.GetComponent<Scene::NameComponent>(child).name == wanted;
            entities.GetComponent<Scene::CanvasComponent>(child).visible = match;
            found = found || match;
        });
        return found;
    }
    if (name == "ui.show_cursor") {
        if (!m_cursorHandler) {
            return false;
        }
        m_cursorHandler(std::get<bool>(args[0]));
        return true;
    }
    if (name == "ui.remove") {
        if (!entities.IsAlive(m_entity) || !entities.HasComponent<Scene::CanvasComponent>(m_entity)) {
            return false;
        }
        entities.GetComponent<Scene::CanvasComponent>(m_entity).visible = false;
        return true;
    }
    const Scene::Entity widget = FindWidget(std::get<std::string>(args[0]));
    const bool alive = widget != Scene::kInvalidEntity && entities.IsAlive(widget);
    // The widget's box, whichever kind of widget it is (none for a Text, which has a point and a box size).
    const auto rectOf = [&entities, widget, alive]() -> Math::Rect* {
        if (!alive) {
            return nullptr;
        }
        if (entities.HasComponent<Scene::UIButtonComponent>(widget)) { return &entities.GetComponent<Scene::UIButtonComponent>(widget).rect; }
        if (entities.HasComponent<Scene::UIImageComponent>(widget)) { return &entities.GetComponent<Scene::UIImageComponent>(widget).rect; }
        if (entities.HasComponent<Scene::UISliderComponent>(widget)) { return &entities.GetComponent<Scene::UISliderComponent>(widget).rect; }
        if (entities.HasComponent<Scene::UICheckboxComponent>(widget)) { return &entities.GetComponent<Scene::UICheckboxComponent>(widget).rect; }
        if (entities.HasComponent<Scene::UIProgressBarComponent>(widget)) { return &entities.GetComponent<Scene::UIProgressBarComponent>(widget).rect; }
        if (entities.HasComponent<Scene::UITextInputComponent>(widget)) { return &entities.GetComponent<Scene::UITextInputComponent>(widget).rect; }
        if (entities.HasComponent<Scene::UIDropdownComponent>(widget)) { return &entities.GetComponent<Scene::UIDropdownComponent>(widget).rect; }
        if (entities.HasComponent<Scene::UIView3DComponent>(widget)) { return &entities.GetComponent<Scene::UIView3DComponent>(widget).rect; }
        return nullptr;
    };
    if (name == "ui.set_position" || name == "ui.set_size" || name == "ui.get_position" || name == "ui.get_size") {
        const bool position = name == "ui.set_position" || name == "ui.get_position";
        const bool write = name.starts_with("ui.set_");
        const Math::Vector3 value = write ? ToEngine(args[1]) : Math::Vector3{};
        if (write && (!std::isfinite(value.x) || !std::isfinite(value.y) || (!position && (value.x < 0.0F || value.y < 0.0F)))) {
            return false;
        }
        if (Math::Rect* const rect = rectOf()) {
            Math::Vector2& field = position ? rect->position : rect->size;
            if (write) {
                field = Math::Vector2{value.x, value.y};
                return true;
            }
            return ToScript(Math::Vector3{field.x, field.y, 0.0F});
        }
        if (alive && entities.HasComponent<Scene::UITextComponent>(widget)) {
            Scene::UITextComponent& label = entities.GetComponent<Scene::UITextComponent>(widget);
            Math::Vector2& field = position ? label.positionPixels : label.boxSize;
            if (write) {
                field = Math::Vector2{value.x, value.y};
                return true;
            }
            return ToScript(Math::Vector3{field.x, field.y, 0.0F});
        }
        return write ? Value{false} : ToScript(Math::Vector3{});
    }
    if (name == "ui.set_opacity") {
        const float alpha = static_cast<float>(std::get<double>(args[1]));
        if (!std::isfinite(alpha)) {
            return false;
        }
        const float clamped = std::clamp(alpha, 0.0F, 1.0F);
        if (alive && entities.HasComponent<Scene::UITextComponent>(widget)) {
            entities.GetComponent<Scene::UITextComponent>(widget).alpha = clamped;
            return true;
        }
        if (alive && entities.HasComponent<Scene::UIImageComponent>(widget)) {
            entities.GetComponent<Scene::UIImageComponent>(widget).alpha = clamped;
            return true;
        }
        return false;
    }
    if (name == "ui.set_font_size") {
        const float size = static_cast<float>(std::get<double>(args[1]));
        if (!alive || !entities.HasComponent<Scene::UITextComponent>(widget) || !std::isfinite(size)) {
            return false;
        }
        entities.GetComponent<Scene::UITextComponent>(widget).fontSize = Scene::ClampUIFontSize(size);
        return true;
    }
    if (name == "ui.play_tween" || name == "ui.stop_tween") {
        if (!alive || !entities.HasComponent<Scene::UITweenComponent>(widget)) {
            return false;
        }
        Scene::UITweenComponent& tween = entities.GetComponent<Scene::UITweenComponent>(widget);
        if (name == "ui.play_tween") {
            tween.elapsed = 0.0F;
        }
        tween.playing = name == "ui.play_tween";
        return true;
    }
    if (name == "ui.is_hovered") {
        return alive && entities.HasComponent<Scene::UIButtonComponent>(widget) &&
               entities.GetComponent<Scene::UIButtonComponent>(widget).isHovered;
    }
    if (name == "ui.set_text") {
        const std::string& text = std::get<std::string>(args[1]);
        if (alive && entities.HasComponent<Scene::UITextComponent>(widget)) {
            entities.GetComponent<Scene::UITextComponent>(widget).text = text;
            return true;
        }
        if (alive && entities.HasComponent<Scene::UITextInputComponent>(widget)) {
            entities.GetComponent<Scene::UITextInputComponent>(widget).text = text;
            return true;
        }
        return false;
    }
    if (name == "ui.get_text") {
        if (alive && entities.HasComponent<Scene::UITextComponent>(widget)) {
            return entities.GetComponent<Scene::UITextComponent>(widget).text;
        }
        if (alive && entities.HasComponent<Scene::UITextInputComponent>(widget)) {
            return entities.GetComponent<Scene::UITextInputComponent>(widget).text;
        }
        return std::string{};
    }
    if (name == "ui.set_visible") {
        if (!alive) {
            return false;
        }
        const bool visible = std::get<bool>(args[1]);
        if (entities.HasComponent<Scene::CanvasComponent>(widget)) {
            entities.GetComponent<Scene::CanvasComponent>(widget).visible = visible;
        }
        if (!entities.HasComponent<Scene::VisibilityComponent>(widget)) {
            entities.AddComponent<Scene::VisibilityComponent>(widget);
        }
        Scene::VisibilityComponent& visibility = entities.GetComponent<Scene::VisibilityComponent>(widget);
        visibility.visible = visible;
        visibility.visibleInHierarchy = visible;
        return true;
    }
    if (name == "ui.set_color") {
        const Math::Vector3 color = ToEngine(args[1]);
        if (alive && entities.HasComponent<Scene::UITextComponent>(widget)) {
            entities.GetComponent<Scene::UITextComponent>(widget).color = color;
            return true;
        }
        if (alive && entities.HasComponent<Scene::UIButtonComponent>(widget)) {
            entities.GetComponent<Scene::UIButtonComponent>(widget).normalColor = color;
            return true;
        }
        if (alive && entities.HasComponent<Scene::UIImageComponent>(widget)) {
            entities.GetComponent<Scene::UIImageComponent>(widget).tintColor = color;
            return true;
        }
        return false;
    }
    if (name == "ui.set_progress") {
        const float value = static_cast<float>(std::get<double>(args[1]));
        if (alive && entities.HasComponent<Scene::UIProgressBarComponent>(widget)) {
            Scene::UIProgressBarComponent& bar = entities.GetComponent<Scene::UIProgressBarComponent>(widget);
            bar.value = std::clamp(value, bar.minValue, bar.maxValue);
            return true;
        }
        if (alive && entities.HasComponent<Scene::UISliderComponent>(widget)) {
            Scene::UISliderComponent& slider = entities.GetComponent<Scene::UISliderComponent>(widget);
            slider.value = std::clamp(value, slider.minValue, slider.maxValue);
            return true;
        }
        return false;
    }
    if (name == "ui.get_value") {
        if (alive && entities.HasComponent<Scene::UISliderComponent>(widget)) {
            return static_cast<double>(entities.GetComponent<Scene::UISliderComponent>(widget).value);
        }
        if (alive && entities.HasComponent<Scene::UIProgressBarComponent>(widget)) {
            return static_cast<double>(entities.GetComponent<Scene::UIProgressBarComponent>(widget).value);
        }
        return 0.0;
    }
    if (name == "ui.set_checked") {
        if (!alive || !entities.HasComponent<Scene::UICheckboxComponent>(widget)) {
            return false;
        }
        entities.GetComponent<Scene::UICheckboxComponent>(widget).checked = std::get<bool>(args[1]);
        return true;
    }
    // A Combo Box: its options are one per line.
    if (name.starts_with("ui.get_selected") || name == "ui.set_selected_index" || name == "ui.add_option") {
        const bool combo = alive && entities.HasComponent<Scene::UIDropdownComponent>(widget);
        if (name == "ui.get_selected_index") {
            return combo ? static_cast<std::int64_t>(entities.GetComponent<Scene::UIDropdownComponent>(widget).selectedIndex)
                         : std::int64_t{-1};
        }
        if (!combo) {
            return name == "ui.get_selected_option" ? Value{std::string{}} : Value{false};
        }
        Scene::UIDropdownComponent& box = entities.GetComponent<Scene::UIDropdownComponent>(widget);
        std::vector<std::string> options;
        for (std::size_t begin = 0U; begin <= box.options.size() && !box.options.empty();) {
            const std::size_t end = box.options.find('\n', begin);
            options.push_back(box.options.substr(begin, end == std::string::npos ? std::string::npos : end - begin));
            if (end == std::string::npos) {
                break;
            }
            begin = end + 1U;
        }
        if (name == "ui.get_selected_option") {
            return box.selectedIndex >= 0 && static_cast<std::size_t>(box.selectedIndex) < options.size()
                       ? options[static_cast<std::size_t>(box.selectedIndex)]
                       : std::string{};
        }
        if (name == "ui.add_option") {
            box.options += (box.options.empty() ? "" : "\n") + std::get<std::string>(args[1]);
            return true;
        }
        const std::int64_t index = std::get<std::int64_t>(args[1]);
        if (index < -1 || index >= static_cast<std::int64_t>(options.size())) {
            return false;
        }
        box.selectedIndex = static_cast<std::int32_t>(index);
        return true;
    }
    if (name == "ui.is_checked") {
        return alive && entities.HasComponent<Scene::UICheckboxComponent>(widget) &&
               entities.GetComponent<Scene::UICheckboxComponent>(widget).checked;
    }
    return false;
}

Scene::Entity BlueprintObjectHost::EntityOf(const Value& value) const {
    const auto* ref = std::get_if<ObjectRef>(&value);
    if (ref == nullptr || ref->id == 0U) {
        return Scene::kInvalidEntity;
    }
    const Scene::Entity entity = UnpackEntity(*ref);
    return m_entityManager.IsAlive(entity) ? entity : Scene::kInvalidEntity;
}

std::vector<std::string> BlueprintObjectHost::AdvanceTimers(const double dt) {
    std::vector<std::string> fired;
    if (!(dt > 0.0) || !std::isfinite(dt)) {
        return fired;
    }
    for (auto it = m_timers.begin(); it != m_timers.end();) {
        Timer& timer = it->second;
        timer.remaining -= dt;
        if (timer.remaining > 0.0) {
            ++it;
            continue;
        }
        if (it->first == kLifeSpanTimer) {
            // A life span: the object goes, through the deferred removal every script uses.
            if (m_services != nullptr && m_services->destroy) {
                m_services->destroy(m_entity);
            }
            it = m_timers.erase(it);
            continue;
        }
        fired.push_back(it->first);
        if (timer.loop && timer.period > 0.0) {
            // Once per period, however long the frame: a hitch never fires a burst.
            timer.remaining = std::max(timer.remaining + timer.period, 0.0);
            ++it;
        } else {
            it = m_timers.erase(it);
        }
    }
    return fired;
}

Value BlueprintObjectHost::TextAndMathCall(const std::string_view name, const std::span<const Value> args) {
    const auto text = [&args](const std::size_t i) -> const std::string& { return std::get<std::string>(args[i]); };
    const auto number = [&args](const std::size_t i) { return std::get<double>(args[i]); };
    const auto whole = [](const double value) {
        // Saturating: a NaN or a value past the range never becomes undefined behaviour.
        if (!std::isfinite(value)) {
            return std::int64_t{0};
        }
        return static_cast<std::int64_t>(std::clamp(value, -9.0e18, 9.0e18));
    };
    if (name == "text.to_int" || name == "text.to_float") {
        // A text that is not a number reads 0.
        try {
            std::size_t used = 0U;
            if (name == "text.to_int") {
                const long long value = std::stoll(text(0U), &used);
                return Value{static_cast<std::int64_t>(value)};
            }
            const double value = std::stod(text(0U), &used);
            return Value{std::isfinite(value) ? value : 0.0};
        } catch (...) {
            return name == "text.to_int" ? Value{std::int64_t{0}} : Value{0.0};
        }
    }
    if (name == "text.upper" || name == "text.lower") {
        std::string out = text(0U);
        for (char& c : out) {
            c = static_cast<char>(name == "text.upper" ? std::toupper(static_cast<unsigned char>(c)) : std::tolower(static_cast<unsigned char>(c)));
        }
        return Value{std::move(out)};
    }
    if (name == "text.trim") {
        const std::string& in = text(0U);
        const std::size_t first = in.find_first_not_of(" \t\r\n");
        if (first == std::string::npos) {
            return Value{std::string{}};
        }
        return Value{in.substr(first, in.find_last_not_of(" \t\r\n") - first + 1U)};
    }
    if (name == "text.replace") {
        std::string out = text(0U);
        const std::string& from = text(1U);
        if (from.empty()) {
            return Value{std::move(out)};
        }
        for (std::size_t at = out.find(from); at != std::string::npos; at = out.find(from, at + text(2U).size())) {
            out.replace(at, from.size(), text(2U));
        }
        return Value{std::move(out)};
    }
    if (name == "text.split") {
        std::vector<Value> parts;
        const std::string& in = text(0U);
        const std::string& separator = text(1U);
        if (separator.empty()) {
            parts.emplace_back(in);
        } else {
            std::size_t start = 0U;
            for (std::size_t at = in.find(separator); at != std::string::npos; at = in.find(separator, start)) {
                parts.emplace_back(in.substr(start, at - start));
                start = at + separator.size();
            }
            parts.emplace_back(in.substr(start));
        }
        return Blueprint::MakeListValue(std::move(parts));
    }
    if (name == "text.substring") {
        const std::string& in = text(0U);
        const std::int64_t start = std::clamp<std::int64_t>(std::get<std::int64_t>(args[1]), 0, static_cast<std::int64_t>(in.size()));
        const std::int64_t count = std::max<std::int64_t>(std::get<std::int64_t>(args[2]), 0);
        return Value{in.substr(static_cast<std::size_t>(start), static_cast<std::size_t>(count))};
    }
    if (name == "text.starts_with") {
        return Value{text(0U).starts_with(text(1U))};
    }
    if (name == "text.format") {
        std::string out;
        const std::string& format = text(0U);
        for (std::size_t i = 0U; i < format.size(); ++i) {
            if (format[i] == '{' && i + 2U < format.size() && format[i + 2U] == '}' && (format[i + 1U] == '0' || format[i + 1U] == '1')) {
                out += text(format[i + 1U] == '0' ? 1U : 2U);
                i += 2U;
                continue;
            }
            out.push_back(format[i]);
        }
        return Value{std::move(out)};
    }
    if (name == "math.round") {
        return Value{whole(std::round(number(0U)))};
    }
    if (name == "math.ceil") {
        return Value{whole(std::ceil(number(0U)))};
    }
    if (name == "math.snap") {
        const double grid = number(1U);
        return Value{grid > 0.0 && std::isfinite(grid) ? std::round(number(0U) / grid) * grid : number(0U)};
    }
    if (name == "math.wrap") {
        const double low = number(1U);
        const double span = number(2U) - low;
        if (!(span > 0.0) || !std::isfinite(span) || !std::isfinite(number(0U))) {
            return Value{low};
        }
        return Value{low + (number(0U) - low - std::floor((number(0U) - low) / span) * span)};
    }
    if (name == "math.sign") {
        const double value = number(0U);
        return Value{value > 0.0 ? 1.0 : value < 0.0 ? -1.0 : 0.0};
    }
    if (name == "math.rotate_about") {
        const Math::Vector3 axis = ToEngine(args[1]);
        const float length = std::sqrt(axis.x * axis.x + axis.y * axis.y + axis.z * axis.z);
        const float degrees = static_cast<float>(number(2U));
        if (!(length > 1e-6F) || !std::isfinite(degrees)) {
            return args[0];
        }
        Math::Quaternion turn{};
        if (!Math::TryMakeAxisAngle(Math::Vector3{axis.x / length, axis.y / length, axis.z / length}, degrees * 0.017453292F, turn)) {
            return args[0];
        }
        return ToScript(Math::RotateVector(turn, ToEngine(args[0])));
    }
    static std::mt19937 random{std::random_device{}()};
    if (name == "math.random_unit_vector") {
        std::normal_distribution<double> gauss(0.0, 1.0);
        for (int attempt = 0; attempt < 8; ++attempt) {
            const double x = gauss(random);
            const double y = gauss(random);
            const double z = gauss(random);
            const double length = std::sqrt(x * x + y * y + z * z);
            if (length > 1e-9) {
                return Value{Vec3Value{x / length, y / length, z / length}};
            }
        }
        return Value{Vec3Value{0.0, 1.0, 0.0}};
    }
    // math.random_in_box
    const Vec3Value& center = std::get<Vec3Value>(args[0]);
    const Vec3Value& half = std::get<Vec3Value>(args[1]);
    std::uniform_real_distribution<double> unit(-1.0, 1.0);
    return Value{Vec3Value{center.x + unit(random) * std::abs(half.x), center.y + unit(random) * std::abs(half.y),
                                 center.z + unit(random) * std::abs(half.z)}};
}

Value BlueprintObjectHost::WorldCall(const std::string_view name, const std::span<const Value> args) {
    auto& entities = m_entityManager;
    ScriptServices* const services = m_services;
    const auto text = [&args](const std::size_t i) -> const std::string& { return std::get<std::string>(args[i]); };
    const auto number = [&args](const std::size_t i) { return std::get<double>(args[i]); };
    const auto object = [](const Scene::Entity entity) {
        return entity == Scene::kInvalidEntity ? Value{ObjectRef{}} : Value{ObjectRef{PackEntity(entity)}};
    };
    const auto objects = [&object](const std::vector<Scene::Entity>& found) {
        std::vector<Value> items;
        items.reserve(found.size());
        for (const Scene::Entity entity : found) {
            items.push_back(object(entity));
        }
        return Blueprint::MakeListValue(std::move(items));
    };

    // ---- messages
    if (name == "events.emit" || name == "events.emit_to") {
        const bool to = name == "events.emit_to";
        const Scene::Entity target = to ? EntityOf(args[0]) : m_entity;
        if (services == nullptr || !services->emit || target == Scene::kInvalidEntity) {
            return Value{false};
        }
        services->emit(target, text(to ? 1U : 0U), text(to ? 2U : 1U));
        return Value{true};
    }

    // ---- timers (this script's own)
    if (name == "timers.set") {
        const double seconds = number(1U);
        if (!std::isfinite(seconds) || seconds < 0.0 || text(0U).empty() || text(0U) == kLifeSpanTimer) {
            return Value{false};
        }
        m_timers[text(0U)] = Timer{seconds, seconds, std::get<bool>(args[2])};
        return Value{true};
    }
    if (name == "timers.clear") {
        return Value{m_timers.erase(text(0U)) > 0U};
    }
    if (name == "timers.active") {
        return Value{m_timers.contains(text(0U))};
    }
    if (name == "timers.remaining") {
        const auto found = m_timers.find(text(0U));
        return Value{found != m_timers.end() ? std::max(found->second.remaining, 0.0) : 0.0};
    }

    // ---- the world
    if (name == "world.self") {
        return object(m_entity);
    }
    if (name == "world.spawn") {
        if (services == nullptr || !services->spawn) {
            return object(Scene::kInvalidEntity);
        }
        return object(services->spawn(text(0U), std::get<Vec3Value>(args[1]), std::get<Vec3Value>(args[2])));
    }
    if (name == "world.destroy" || name == "world.destroy_self") {
        const Scene::Entity target = name == "world.destroy" ? EntityOf(args[0]) : m_entity;
        if (services == nullptr || !services->destroy || target == Scene::kInvalidEntity) {
            return Value{false};
        }
        services->destroy(target);
        return Value{true};
    }
    if (name == "world.set_life_span") {
        const double seconds = number(0U);
        if (!std::isfinite(seconds)) {
            return Value{false};
        }
        if (seconds <= 0.0) {
            return Value{m_timers.erase(std::string{kLifeSpanTimer}) > 0U};
        }
        m_timers[std::string{kLifeSpanTimer}] = Timer{seconds, seconds, false};
        return Value{true};
    }
    if (name == "world.find_tag" || name == "world.find_all_tag") {
        std::vector<Scene::Entity> found;
        entities.ForEach<Scene::GameplayTagComponent>([&](const Scene::Entity entity, const Scene::GameplayTagComponent& tags) {
            if (Scene::HasGameplayTag(tags, text(0U))) {
                found.push_back(entity);
            }
        });
        if (name == "world.find_tag") {
            return object(found.empty() ? Scene::kInvalidEntity : found.front());
        }
        return objects(found);
    }
    if (name == "world.find_all_name") {
        // By name, or a name's start ("Enemy" finds Enemy, Enemy2, Enemy_Boss).
        std::vector<Scene::Entity> found;
        entities.ForEach<Scene::NameComponent>([&](const Scene::Entity entity, const Scene::NameComponent& named) {
            if (named.name.starts_with(text(0U))) {
                found.push_back(entity);
            }
        });
        return objects(found);
    }
    if (name == "world.is_valid") {
        return Value{EntityOf(args[0]) != Scene::kInvalidEntity};
    }

    // ---- another object
    if (name.starts_with("object.")) {
        const Scene::Entity target = EntityOf(args[0]);
        const bool alive = target != Scene::kInvalidEntity;
        const bool placed = alive && entities.HasComponent<Scene::TransformComponent>(target);
        if (name == "object.name") {
            return Value{alive && entities.HasComponent<Scene::NameComponent>(target)
                                ? entities.GetComponent<Scene::NameComponent>(target).name
                                : std::string{}};
        }
        if (name == "object.get_position" || name == "object.get_rotation" || name == "object.get_scale") {
            if (!placed) {
                return ToScript(name == "object.get_scale" ? Math::Vector3{1.0F, 1.0F, 1.0F} : Math::Vector3{});
            }
            const Scene::TransformComponent& transform = entities.GetComponent<Scene::TransformComponent>(target);
            return ToScript(name == "object.get_position" ? transform.localPosition
                               : name == "object.get_scale"  ? transform.localScale
                                                             : DegreesOf(transform.localRotation));
        }
        if (name == "object.set_position" || name == "object.set_rotation" || name == "object.set_scale") {
            const Math::Vector3 value = ToEngine(args[1]);
            if (!placed || !std::isfinite(value.x) || !std::isfinite(value.y) || !std::isfinite(value.z)) {
                return Value{false};
            }
            Scene::TransformComponent& transform = entities.GetComponent<Scene::TransformComponent>(target);
            if (name == "object.set_rotation") {
                const std::optional<Math::Quaternion> rotation = RotationOfDegrees(value);
                if (!rotation.has_value()) {
                    return Value{false};
                }
                transform.localRotation = *rotation;
            } else {
                (name == "object.set_position" ? transform.localPosition : transform.localScale) = value;
            }
            MarkMoved(entities, target);
            return Value{true};
        }
        if (name == "object.set_visible") {
            if (!alive) {
                return Value{false};
            }
            SetVisible(entities, target, std::get<bool>(args[1]));
            return Value{true};
        }
        if (name == "object.has_tag" || name == "object.add_tag" || name == "object.remove_tag") {
            if (!alive) {
                return Value{false};
            }
            if (!entities.HasComponent<Scene::GameplayTagComponent>(target)) {
                if (name != "object.add_tag") {
                    return Value{false};
                }
                entities.AddComponent<Scene::GameplayTagComponent>(target, Scene::GameplayTagComponent{});
            }
            Scene::GameplayTagComponent& tags = entities.GetComponent<Scene::GameplayTagComponent>(target);
            if (name == "object.has_tag") {
                return Value{Scene::HasGameplayTag(tags, text(1U))};
            }
            if (name == "object.add_tag") {
                return Value{Scene::AddGameplayTag(tags, text(1U))};
            }
            return Value{Scene::RemoveGameplayTag(tags, text(1U))};
        }
        if (name == "object.distance") {
            const Scene::Entity other = EntityOf(args[1]);
            const auto where = [&entities](const Scene::Entity entity) {
                if (entities.HasComponent<Scene::WorldTransformComponent>(entity)) {
                    return entities.GetComponent<Scene::WorldTransformComponent>(entity).worldPosition;
                }
                return entities.GetComponent<Scene::TransformComponent>(entity).localPosition;
            };
            if (!placed || other == Scene::kInvalidEntity || !entities.HasComponent<Scene::TransformComponent>(other)) {
                return Value{0.0};
            }
            const Math::Vector3 a = where(target);
            const Math::Vector3 b = where(other);
            const double dx = a.x - b.x;
            const double dy = a.y - b.y;
            const double dz = a.z - b.z;
            return Value{std::sqrt(dx * dx + dy * dy + dz * dz)};
        }
        if (name == "object.attach" || name == "object.detach") {
            const Scene::Entity parent = name == "object.attach" ? EntityOf(args[1]) : Scene::kInvalidEntity;
            if (!alive || services == nullptr || !services->setParent || (name == "object.attach" && parent == Scene::kInvalidEntity)) {
                return Value{false};
            }
            return Value{services->setParent(target, parent)};
        }
        if (name == "object.parent") {
            if (!alive || !entities.HasComponent<Scene::HierarchyComponent>(target)) {
                return object(Scene::kInvalidEntity);
            }
            const Scene::Entity parent = entities.GetComponent<Scene::HierarchyComponent>(target).parent;
            return object(entities.IsAlive(parent) ? parent : Scene::kInvalidEntity);
        }
        if (name == "object.children") {
            std::vector<std::pair<std::int64_t, Scene::Entity>> found;
            if (alive) {
                entities.ForEach<Scene::HierarchyComponent>([&](const Scene::Entity entity, const Scene::HierarchyComponent& link) {
                    if (link.parent == target && entity != target) {
                        found.emplace_back(link.siblingOrder, entity);
                    }
                });
            }
            std::ranges::sort(found, [](const auto& a, const auto& b) { return a.first < b.first; });
            std::vector<Scene::Entity> children;
            for (const auto& [order, entity] : found) {
                children.push_back(entity);
            }
            return objects(children);
        }
        return Value{false};
    }

    // ---- traces: the result stays for the trace.* readers
    if (name == "trace.line" || name == "trace.sphere") {
        m_lastTrace = ScriptServices::TraceHit{};
        if (services != nullptr && services->trace) {
            const double radius = name == "trace.sphere" ? std::max(number(2U), 0.0) : 0.0;
            m_lastTrace = services->trace(std::get<Vec3Value>(args[0]), std::get<Vec3Value>(args[1]), radius, m_entity);
        }
        return Value{m_lastTrace.hit};
    }
    if (name == "trace.location") {
        return Value{m_lastTrace.point};
    }
    if (name == "trace.normal") {
        return Value{m_lastTrace.normal};
    }
    if (name == "trace.object") {
        return object(m_lastTrace.hit && entities.IsAlive(m_lastTrace.entity) ? m_lastTrace.entity : Scene::kInvalidEntity);
    }
    if (name == "trace.distance") {
        return Value{m_lastTrace.distance};
    }

    // ---- the game
    if (name.starts_with("game.")) {
        if (name == "game.time") {
            return Value{services != nullptr && services->time ? services->time() : 0.0};
        }
        if (name == "game.delta") {
            return Value{services != nullptr && services->delta ? services->delta() : 0.0};
        }
        if (name == "game.time_scale") {
            return Value{services != nullptr && services->timeScale ? services->timeScale() : 1.0};
        }
        if (name == "game.is_paused") {
            return Value{services != nullptr && services->paused && services->paused()};
        }
        if (name == "game.level_name") {
            return Value{services != nullptr && services->levelName ? services->levelName() : std::string{}};
        }
        if (name == "game.set_time_scale") {
            const double scale = number(0U);
            if (services == nullptr || !services->setTimeScale || !std::isfinite(scale) || scale < 0.0) {
                return Value{false};
            }
            services->setTimeScale(scale);
            return Value{true};
        }
        if (name == "game.pause" || name == "game.resume") {
            if (services == nullptr || !services->setPaused) {
                return Value{false};
            }
            services->setPaused(name == "game.pause");
            return Value{true};
        }
        if (name == "game.quit") {
            if (services == nullptr || !services->quit) {
                return Value{false};
            }
            services->quit();
            return Value{true};
        }
        if (name == "game.open_level") {
            return Value{services != nullptr && services->openLevel && services->openLevel(text(0U))};
        }
        return Value{false};
    }

    // ---- sounds, particles, the view, the look, clips, walking
    if (name == "audio.play_at" || name == "audio.play_2d") {
        const bool at = name == "audio.play_at";
        const double volume = number(at ? 2U : 1U);
        if (services == nullptr || !services->playSound || text(0U).empty() || !std::isfinite(volume) || volume < 0.0) {
            return Value{false};
        }
        return Value{services->playSound(text(0U), at ? std::get<Vec3Value>(args[1]) : Vec3Value{}, volume, at)};
    }
    if (name == "particles.set_active") {
        entities.GetComponent<Scene::ParticleEmitterComponent>(m_entity).emitting = std::get<bool>(args[0]);
        return Value{true};
    }
    if (name == "particles.burst") {
        const std::int64_t count = std::get<std::int64_t>(args[0]);
        return Value{services != nullptr && services->burstParticles && count > 0 && services->burstParticles(m_entity, count)};
    }
    if (name.starts_with("camera.")) {
        if (name == "camera.activate") {
            const Scene::Entity camera = EntityOf(args[0]);
            return Value{services != nullptr && services->activateCamera && camera != Scene::kInvalidEntity &&
                            services->activateCamera(camera)};
        }
        if (name == "camera.shake") {
            const double strength = number(0U);
            const double seconds = number(1U);
            if (services == nullptr || !services->cameraShake || !std::isfinite(strength) || !std::isfinite(seconds) || strength < 0.0 ||
                seconds <= 0.0) {
                return Value{false};
            }
            services->cameraShake(std::min(strength, 10.0), std::min(seconds, 60.0));
            return Value{true};
        }
        if (name == "camera.forward") {
            return Value{services != nullptr && services->cameraForward ? services->cameraForward() : Vec3Value{0.0, 0.0, -1.0}};
        }
        return Value{services != nullptr && services->cameraLocation ? services->cameraLocation() : Vec3Value{}};
    }
    if (name.starts_with("render.")) {
        Scene::SurfaceInstanceComponent& surface = entities.GetComponent<Scene::SurfaceInstanceComponent>(m_entity);
        if (name == "render.set_material") {
            surface.materialOverridePath = text(0U);
            return Value{true};
        }
        const double transparency = number(0U);
        if (!std::isfinite(transparency)) {
            return Value{false};
        }
        surface.transparency = static_cast<float>(std::clamp(transparency, 0.0, 1.0));
        return Value{true};
    }
    if (name.starts_with("sequencer.")) {
        const Scene::Entity holder = FindSequencer(entities, m_entity);
        if (holder == Scene::kInvalidEntity) {
            return Value{false};
        }
        Scene::AnimationSequencerComponent& player = entities.GetComponent<Scene::AnimationSequencerComponent>(holder);
        if (name == "sequencer.play") {
            if (services != nullptr && services->playAnimation) {
                return Value{services->playAnimation(holder, text(0U))};
            }
            if (!text(0U).empty()) {
                return Value{false};   // finding a clip by its name needs the game's files
            }
            player.playRequested = true;
            return Value{true};
        }
        if (name == "sequencer.stop") {
            const bool playing = player.isPlaying || player.playRequested;
            player.isPlaying = false;
            player.playRequested = false;
            return Value{playing};
        }
        if (name == "sequencer.is_playing") {
            return Value{player.isPlaying || player.playRequested};
        }
        const double rate = number(0U);
        if (!std::isfinite(rate) || std::abs(rate) > 100.0) {
            return Value{false};
        }
        player.speed = static_cast<float>(rate);
        return Value{true};
    }
    if (name.starts_with("ai.")) {
        Scene::NavSeeker3DComponent& seeker = entities.GetComponent<Scene::NavSeeker3DComponent>(m_entity);
        if (name == "ai.move_to") {
            seeker.enabled = true;
            return Value{Scene::NavSeeker3D::TrySetTargetPosition(seeker, ToEngine(args[0]))};
        }
        if (name == "ai.stop") {
            const bool moving = seeker.enabled && seeker.pathStatus == Scene::NavigationAgentPathStatus::Following;
            seeker.enabled = false;
            seeker.desiredVelocity = Math::Vector3{};
            return Value{moving};
        }
        if (name == "ai.reached") {
            return Value{seeker.targetReached || seeker.pathStatus == Scene::NavigationAgentPathStatus::Finished};
        }
        switch (seeker.pathStatus) {
            case Scene::NavigationAgentPathStatus::Following: return Value{std::string("Following")};
            case Scene::NavigationAgentPathStatus::Finished: return Value{std::string("Finished")};
            case Scene::NavigationAgentPathStatus::Failed: return Value{std::string("Failed")};
            case Scene::NavigationAgentPathStatus::Idle: break;
        }
        return Value{std::string("Idle")};
    }

    // ---- saved game data
    if (name.starts_with("save.")) {
        if (services == nullptr) {
            if (name == "save.get_float" || name == "save.get_int" || name == "save.get_bool" || name == "save.get_str") {
                return args[1];
            }
            return Value{false};
        }
        std::map<std::string, std::string>& data = services->gameData;
        if (name == "save.set_float" || name == "save.set_int" || name == "save.set_bool" || name == "save.set_str") {
            if (text(0U).empty()) {
                return Value{false};
            }
            std::string stored;
            if (name == "save.set_float") {
                const double value = number(1U);
                if (!std::isfinite(value)) {
                    return Value{false};
                }
                std::ostringstream out;
                out.precision(17);
                out << value;
                stored = out.str();
            } else if (name == "save.set_int") {
                stored = std::to_string(std::get<std::int64_t>(args[1]));
            } else if (name == "save.set_bool") {
                stored = std::get<bool>(args[1]) ? "true" : "false";
            } else {
                stored = text(1U);
            }
            data[text(0U)] = std::move(stored);
            return Value{true};
        }
        if (name == "save.get_float" || name == "save.get_int" || name == "save.get_bool" || name == "save.get_str") {
            const auto found = data.find(text(0U));
            if (found == data.end()) {
                return args[1];
            }
            const std::string& stored = found->second;
            try {
                if (name == "save.get_float") {
                    return Value{std::stod(stored)};
                }
                if (name == "save.get_int") {
                    return Value{static_cast<std::int64_t>(std::stoll(stored))};
                }
            } catch (...) {
                return args[1];
            }
            if (name == "save.get_bool") {
                return Value{stored == "true" || stored == "1"};
            }
            return Value{stored};
        }
        if (name == "save.has") {
            return Value{data.contains(text(0U))};
        }
        if (name == "save.remove") {
            return Value{data.erase(text(0U)) > 0U};
        }
        const auto slot = [&](const std::function<bool(const std::string&)>& call) {
            return Value{call && !text(0U).empty() && call(text(0U))};
        };
        if (name == "save.write") {
            return slot(services->saveSlot);
        }
        if (name == "save.read") {
            return slot(services->loadSlot);
        }
        if (name == "save.exists") {
            return slot(services->slotExists);
        }
        if (name == "save.delete") {
            return slot(services->deleteSlot);
        }
    }
    return Value{false};
}

} // namespace UVE::Core
