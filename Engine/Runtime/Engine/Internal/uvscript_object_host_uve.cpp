// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/core/uvscript_object_host_uve.h"

#include <cmath>
#include <string>
#include <type_traits>

#include "uve/audio/i_audio_source_system_uve.h"
#include "uve/audio/i_audio_system_uve.h"
#include "uve/component/audio_source_component_uve.h"
#include "uve/component/camera_component_uve.h"
#include "uve/component/character_controller_component_uve.h"
#include "uve/component/light_component_uve.h"
#include "uve/component/name_component_uve.h"
#include "uve/component/rigid_3d_component_uve.h"
#include "uve/component/transform_component_uve.h"
#include "uve/component/visibility_component_uve.h"
#include "uve/component/world_transform_component_uve.h"
#include "uve/entity/i_entity_manager_uve.h"
#include "uve/gameplay/gameplay_attributes_uve.h"
#include "uve/gameplay/gameplay_tags_uve.h"
#include "uve/input/i_input_system_uve.h"
#include "uve/logging/logging_macros_uve.h"
#include "uve/network/network_lobby_uve.h"
#include "uve/network/network_relay_uve.h"
#include "uve/network/network_rpc_uve.h"
#include "uve/network/network_session_uve.h"
#include "uve/objects/3d/rigid_3d_uve.h"
#include "uve/uvscript/uvscript_instance_uve.h"

namespace UVE::Core {
namespace {

using UVScript::HostFunctionUVE;
using UVScript::HostPropertyUVE;
using UVScript::ObjectRefUVE;
using UVScript::TypeUVE;
using UVScript::ValueUVE;
using UVScript::Vec3ValueUVE;

/// A script's handle to another object packs its entity, plus one: 0 stays reserved for "no
/// object" (entity slot 0, generation 0 is a real entity and must keep its own handle).
[[nodiscard]] constexpr std::uint64_t PackEntityUVE(const Scene::EntityUVE entity) noexcept {
    return ((static_cast<std::uint64_t>(entity.generation) << 32U) | entity.index) + 1U;
}

[[nodiscard]] constexpr Scene::EntityUVE UnpackEntityUVE(const ObjectRefUVE ref) noexcept {
    const std::uint64_t packed = ref.id - 1U;
    return Scene::EntityUVE{static_cast<std::uint32_t>(packed),
                            static_cast<std::uint32_t>(packed >> 32U)};
}

void SetVisibleUVE(Scene::IEntityManagerUVE& entities, const Scene::EntityUVE entity, const bool shown) {
    if (entities.HasComponentUVE<Scene::VisibilityComponentUVE>(entity)) {
        entities.GetComponentUVE<Scene::VisibilityComponentUVE>(entity).visible = shown;
    } else if (!shown) {
        // Showing adds nothing: an object without the component is already visible.
        Scene::VisibilityComponentUVE visibility;
        visibility.visible = false;
        entities.AddComponentUVE<Scene::VisibilityComponentUVE>(entity, visibility);
    }
}

[[nodiscard]] Vec3ValueUVE ToScriptUVE(const Math::Vector3UVE& v) noexcept {
    return {static_cast<double>(v.x), static_cast<double>(v.y), static_cast<double>(v.z)};
}

[[nodiscard]] Math::Vector3UVE ToEngineUVE(const ValueUVE& value) {
    const Vec3ValueUVE& v = std::get<Vec3ValueUVE>(value);
    return {static_cast<float>(v.x), static_cast<float>(v.y), static_cast<float>(v.z)};
}

/// Staged RPC calls waiting on their first `rpc.send`: sixteen scripts' worth of headroom is
/// plenty, and the cap keeps a leaky script from growing the staging map forever.
inline constexpr std::uint32_t kMaxStagedRpcCallsUVE = 16U;

/// The `rpc.result_kind` word for a completed value: "nil", the scalar kind, or the collection
/// kind. Object references never cross the wire, so anything else fails closed as "none".
[[nodiscard]] std::string RpcValueKindUVE(const ValueUVE& value) {
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
    if (std::holds_alternative<Vec3ValueUVE>(value)) {
        return "vec3";
    }
    const auto* collection =
        std::get_if<std::shared_ptr<const UVScript::CollectionValueUVE>>(&value);
    if (collection == nullptr || *collection == nullptr) {
        return "none";
    }
    switch ((*collection)->kind) {
        case UVScript::CollectionValueUVE::KindUVE::List:
            return "list";
        case UVScript::CollectionValueUVE::KindUVE::Map:
            return "map";
        case UVScript::CollectionValueUVE::KindUVE::Tuple:
            return "tuple";
    }
    return "none";
}

} // namespace

static_assert(std::is_same_v<Network::RpcCallIdUVE, std::uint32_t>,
              "RpcResponseSinkUVE spells call ids as uint32_t to keep the host header "
              "forward-declared; keep the peer's id type the same.");
static_assert(std::is_same_v<Network::LobbyRoomIdUVE, std::uint32_t>,
              "LobbyRosterSinkUVE spells room ids as uint32_t to keep the host header "
              "forward-declared; keep the lobby's room id type the same.");

UVScriptObjectHostUVE::UVScriptObjectHostUVE(Scene::IEntityManagerUVE& entityManager, const Input::IInputSystemUVE* const input,
                                         const Scene::EntityUVE entity,
                                         Audio::IAudioSourceSystemUVE* const audioSources,
                                         Audio::IAudioSystemUVE* const audio,
                                         InstanceResolverUVE resolver,
                                         Network::NetworkRpcUVE* const rpc,
                                         RpcResponseSinkUVE sink,
                                         Network::NetworkLobbyClientUVE* const lobby,
                                         LobbyResultSinkUVE lobbySink,
                                         LobbyRosterSinkUVE rosterSink,
                                         Network::NetworkRelayUVE* const relay,
                                         Network::NetworkSessionUVE* const relaySession) noexcept
    : m_entityManager(entityManager), m_input(input), m_entity(entity), m_audioSources(audioSources),
      m_audio(audio), m_resolver(std::move(resolver)), m_rpc(rpc), m_sink(std::move(sink)),
      m_lobby(lobby), m_lobbySink(std::move(lobbySink)), m_rosterSink(std::move(rosterSink)),
      m_relay(relay), m_relaySession(relaySession) {}

UVScriptObjectHostUVE::~UVScriptObjectHostUVE() {
    // The peer outlives the host by contract; cancel what it still holds for us so no
    // callback fires into a dead host. Completed calls keep their retained results.
    if (m_rpc != nullptr) {
        for (const std::uint32_t callId : m_pendingRpc) {
            static_cast<void>(m_rpc->CancelUVE(callId));
        }
    }
    // Same for the lobby peer, bluntly: the facade abandons every in-flight call (a shared
    // peer's C++-initiated calls go too - teardown abandons everything), and the roster
    // push unhooks so no sink fires into a dead host.
    if (m_lobby != nullptr) {
        static_cast<void>(m_lobby->CancelPendingUVE());
        m_lobby->SetRosterHandlerUVE(Network::LobbyRosterHandlerUVE{});
    }
}

std::optional<HostPropertyUVE> UVScriptObjectHostUVE::DescribePropertyUVE(const std::string_view name) const {
    if (name == "name") {
        return HostPropertyUVE{TypeUVE::StrUVE(), false};
    }
    if ((name == "position" || name == "scale") && m_entityManager.HasComponentUVE<Scene::TransformComponentUVE>(m_entity)) {
        return HostPropertyUVE{TypeUVE::Vec3UVE(), true};
    }
    if (name == "velocity" &&
        (m_entityManager.HasComponentUVE<Scene::CharacterControllerComponentUVE>(m_entity) ||
         m_entityManager.HasComponentUVE<Scene::Rigid3DComponentUVE>(m_entity))) {
        return HostPropertyUVE{TypeUVE::Vec3UVE(), true};
    }
    if ((name == "volume" || name == "pitch") &&
        m_entityManager.HasComponentUVE<Scene::AudioSourceComponentUVE>(m_entity)) {
        return HostPropertyUVE{TypeUVE::FloatUVE(), true};
    }
    // Ungated: every object is inherently visible, so a script reads true and a write adds the
    // component. The scene graph owns visibleInHierarchy; scripts only ever touch the switch.
    if (name == "visible") {
        return HostPropertyUVE{TypeUVE::BoolUVE(), true};
    }
    if (name == "intensity" && m_entityManager.HasComponentUVE<Scene::LightComponentUVE>(m_entity)) {
        return HostPropertyUVE{TypeUVE::FloatUVE(), true};
    }
    if (name == "fov" && m_entityManager.HasComponentUVE<Scene::CameraComponentUVE>(m_entity)) {
        return HostPropertyUVE{TypeUVE::FloatUVE(), true};
    }
    if (m_entityManager.HasComponentUVE<Scene::CharacterControllerComponentUVE>(m_entity)) {
        // "grounded", matching the other properties here: name, position, scale, velocity - a plain
        // word for the thing, no "is_" prefix and no snake_case. "is_on_floor" is the name this had
        // before, and a script written against it still reads, because a script out in the world is
        // not something a rename in here is allowed to break. Writing always uses the current name.
        if (name == "grounded" || name == "is_on_floor") {
            return HostPropertyUVE{TypeUVE::BoolUVE(), false};
        }
    }
    return std::nullopt;
}

std::optional<HostFunctionUVE> UVScriptObjectHostUVE::DescribeFunctionUVE(const std::string_view name) const {
    if (m_entityManager.HasComponentUVE<Scene::Rigid3DComponentUVE>(m_entity) &&
        (name == "physics.apply_force" || name == "physics.apply_impulse" || name == "physics.apply_torque")) {
        return HostFunctionUVE{{TypeUVE::Vec3UVE()}, TypeUVE::BoolUVE()};
    }
    if (m_entityManager.HasComponentUVE<Scene::AudioSourceComponentUVE>(m_entity) &&
        (name == "audio.play" || name == "audio.stop" || name == "audio.is_playing")) {
        return HostFunctionUVE{{}, TypeUVE::BoolUVE()};
    }
    if (m_entityManager.HasComponentUVE<Scene::GameplayAttributesComponentUVE>(m_entity) &&
        (name == "attributes.has" || name == "attributes.get" || name == "attributes.max")) {
        return HostFunctionUVE{{TypeUVE::StrUVE()}, name == "attributes.has" ? TypeUVE::BoolUVE() : TypeUVE::FloatUVE()};
    }
    if (m_entityManager.HasComponentUVE<Scene::GameplayAttributesComponentUVE>(m_entity) &&
        (name == "attributes.damage" || name == "attributes.heal")) {
        return HostFunctionUVE{{TypeUVE::StrUVE(), TypeUVE::FloatUVE()}, TypeUVE::BoolUVE()};
    }
    if (m_entityManager.HasComponentUVE<Scene::GameplayTagComponentUVE>(m_entity) &&
        (name == "tags.has" || name == "tags.add" || name == "tags.remove")) {
        return HostFunctionUVE{{TypeUVE::StrUVE()}, TypeUVE::BoolUVE()};
    }
    if (name == "input.pressed" || name == "input.held" || name == "input.released") {
        return HostFunctionUVE{{TypeUVE::StrUVE()}, TypeUVE::BoolUVE()};
    }
    // Ungated: any script may look another object up. The kind is only known when the script runs,
    // so every lookup answers the common root; a missing name answers no object (id 0).
    if (name == "node") {
        return HostFunctionUVE{{TypeUVE::StrUVE()}, TypeUVE::ObjectUVE("Object3D")};
    }
    if (name == "input.axis") {
        return HostFunctionUVE{{TypeUVE::StrUVE(), TypeUVE::StrUVE()}, TypeUVE::FloatUVE()};
    }
    // Ungated: every script may attempt RPC. Without a peer every call fails closed (0, false,
    // "", "none"); the game injects its peer, like the input and audio systems.
    if (name == "rpc.begin") {
        return HostFunctionUVE{{TypeUVE::StrUVE()}, TypeUVE::IntUVE()};
    }
    if (name == "rpc.push_bool") {
        return HostFunctionUVE{{TypeUVE::IntUVE(), TypeUVE::BoolUVE()}, TypeUVE::BoolUVE()};
    }
    if (name == "rpc.push_int") {
        return HostFunctionUVE{{TypeUVE::IntUVE(), TypeUVE::IntUVE()}, TypeUVE::BoolUVE()};
    }
    if (name == "rpc.push_float") {
        return HostFunctionUVE{{TypeUVE::IntUVE(), TypeUVE::FloatUVE()}, TypeUVE::BoolUVE()};
    }
    if (name == "rpc.push_str") {
        return HostFunctionUVE{{TypeUVE::IntUVE(), TypeUVE::StrUVE()}, TypeUVE::BoolUVE()};
    }
    if (name == "rpc.push_vec3") {
        return HostFunctionUVE{{TypeUVE::IntUVE(), TypeUVE::Vec3UVE()}, TypeUVE::BoolUVE()};
    }
    if (name == "rpc.send") {
        return HostFunctionUVE{{TypeUVE::IntUVE()}, TypeUVE::IntUVE()};
    }
    if (name == "rpc.abort" || name == "rpc.cancel" || name == "rpc.release") {
        return HostFunctionUVE{{TypeUVE::IntUVE()}, TypeUVE::BoolUVE()};
    }
    if (name == "rpc.result_kind" || name == "rpc.error") {
        return HostFunctionUVE{{TypeUVE::IntUVE()}, TypeUVE::StrUVE()};
    }
    if (name == "rpc.result_bool") {
        return HostFunctionUVE{{TypeUVE::IntUVE()}, TypeUVE::BoolUVE()};
    }
    if (name == "rpc.result_int") {
        return HostFunctionUVE{{TypeUVE::IntUVE()}, TypeUVE::IntUVE()};
    }
    if (name == "rpc.result_float") {
        return HostFunctionUVE{{TypeUVE::IntUVE()}, TypeUVE::FloatUVE()};
    }
    if (name == "rpc.result_str") {
        return HostFunctionUVE{{TypeUVE::IntUVE()}, TypeUVE::StrUVE()};
    }
    if (name == "rpc.result_vec3") {
        return HostFunctionUVE{{TypeUVE::IntUVE()}, TypeUVE::Vec3UVE()};
    }
    // Ungated: every script may attempt the lobby. Without a peer every call fails closed
    // (0, false, "", "none"); the game injects its peer, like the RPC one.
    if (name == "lobby.list" || name == "lobby.leave" || name == "lobby.room" ||
        name == "lobby.roster_count") {
        return HostFunctionUVE{{}, TypeUVE::IntUVE()};
    }
    if (name == "lobby.create") {
        return HostFunctionUVE{{TypeUVE::StrUVE(), TypeUVE::StrUVE(), TypeUVE::IntUVE()},
                               TypeUVE::IntUVE()};
    }
    if (name == "lobby.join") {
        return HostFunctionUVE{{TypeUVE::IntUVE(), TypeUVE::StrUVE()}, TypeUVE::IntUVE()};
    }
    if (name == "lobby.roster") {
        return HostFunctionUVE{{TypeUVE::IntUVE()}, TypeUVE::IntUVE()};
    }
    if (name == "lobby.result_kind" || name == "lobby.error") {
        return HostFunctionUVE{{TypeUVE::IntUVE()}, TypeUVE::StrUVE()};
    }
    if (name == "lobby.result_room_count" || name == "lobby.result_created_room" ||
        name == "lobby.result_roster_count") {
        return HostFunctionUVE{{TypeUVE::IntUVE()}, TypeUVE::IntUVE()};
    }
    if (name == "lobby.result_room_id" || name == "lobby.result_room_players" ||
        name == "lobby.result_room_max") {
        return HostFunctionUVE{{TypeUVE::IntUVE(), TypeUVE::IntUVE()}, TypeUVE::IntUVE()};
    }
    if (name == "lobby.result_room_name" || name == "lobby.result_roster_name") {
        return HostFunctionUVE{{TypeUVE::IntUVE(), TypeUVE::IntUVE()}, TypeUVE::StrUVE()};
    }
    if (name == "lobby.roster_name") {
        return HostFunctionUVE{{TypeUVE::IntUVE()}, TypeUVE::StrUVE()};
    }
    if (name == "lobby.result_left" || name == "lobby.release") {
        return HostFunctionUVE{{TypeUVE::IntUVE()}, TypeUVE::BoolUVE()};
    }
    if (name == "lobby.cancel_all") {
        return HostFunctionUVE{{}, TypeUVE::IntUVE()};
    }
    // Ungated: every script may attempt the relay. Without the attachment each call fails
    // closed (0, false, "", "unknown"); the game injects its relay and session, like the
    // lobby peer. All synchronous - nothing to await, no events.
    if (name == "relay.peer_count" || name == "relay.stat_received" ||
        name == "relay.stat_forwarded" || name == "relay.stat_dropped") {
        return HostFunctionUVE{{}, TypeUVE::IntUVE()};
    }
    if (name == "relay.room") {
        return HostFunctionUVE{{TypeUVE::IntUVE()}, TypeUVE::IntUVE()};
    }
    if (name == "relay.remove") {
        return HostFunctionUVE{{TypeUVE::IntUVE()}, TypeUVE::BoolUVE()};
    }
    if (name == "relay.peer_state") {
        return HostFunctionUVE{{TypeUVE::IntUVE()}, TypeUVE::StrUVE()};
    }
    if (name == "relay.set_room" || name == "relay.interested") {
        return HostFunctionUVE{{TypeUVE::IntUVE(), TypeUVE::IntUVE()}, TypeUVE::BoolUVE()};
    }
    if (name == "relay.set_interest") {
        return HostFunctionUVE{{TypeUVE::IntUVE(), TypeUVE::IntUVE(), TypeUVE::BoolUVE()},
                               TypeUVE::BoolUVE()};
    }
    if (name == "relay.send") {
        return HostFunctionUVE{{TypeUVE::StrUVE()}, TypeUVE::BoolUVE()};
    }
    if (name == "relay.poll") {
        return HostFunctionUVE{{}, TypeUVE::StrUVE()};
    }
    return std::nullopt;
}

std::optional<std::vector<TypeUVE>> UVScriptObjectHostUVE::DescribeEventUVE(const std::string_view event) const {
    if (event == "ready") {
        return std::vector<TypeUVE>{};
    }
    if (event == "tick") {
        return std::vector<TypeUVE>{TypeUVE::FloatUVE()};
    }
    // An AnimationSequencer's clip passed one of its events: sent to the player's own script and to
    // the script of the object it animates (a character's script hears its footsteps).
    if (event == "animation_event") {
        return std::vector<TypeUVE>{TypeUVE::StrUVE()};
    }
    // Contact edges: both parties' scripts hear, each with the other object's name ("" when the
    // other has no Name component). Scripts predate neither party - a contact already active when
    // a script starts surfaces no enter, though its exit still fires.
    if (event == "collision_enter" || event == "collision_exit" || event == "overlap_enter" ||
        event == "overlap_exit") {
        return std::vector<TypeUVE>{TypeUVE::StrUVE()};
    }
    // An RPC call this object's script sent has completed: the call id plus whether it
    // succeeded. Raised by the game from this host's response sink; completed results wait in
    // the peer for the result getters (or release).
    if (event == "rpc_response") {
        return std::vector<TypeUVE>{TypeUVE::IntUVE(), TypeUVE::BoolUVE()};
    }
    // A lobby call this object's script sent has completed: the call id plus whether it
    // succeeded. Raised by the game from this host's result sink; completed results wait in
    // the host for the result getters (or release).
    if (event == "lobby_result") {
        return std::vector<TypeUVE>{TypeUVE::IntUVE(), TypeUVE::BoolUVE()};
    }
    // The lobby pushed a roster for the room this object's script is in: the room id.
    // Raised by the game from this host's roster sink; the roster itself reads live.
    if (event == "lobby_roster") {
        return std::vector<TypeUVE>{TypeUVE::IntUVE()};
    }
    return std::nullopt;
}

ValueUVE UVScriptObjectHostUVE::GetPropertyUVE(const std::string_view name) {
    if (name == "name") {
        return m_entityManager.HasComponentUVE<Scene::NameComponentUVE>(m_entity)
                   ? ValueUVE{m_entityManager.GetComponentUVE<Scene::NameComponentUVE>(m_entity).name}
                   : ValueUVE{std::string{}};
    }
    if (name == "position" || name == "scale") {
        const Scene::TransformComponentUVE& transform = m_entityManager.GetComponentUVE<Scene::TransformComponentUVE>(m_entity);
        return ToScriptUVE(name == "position" ? transform.localPosition : transform.localScale);
    }
    if (name == "velocity") {
        // Both bodies on one entity is nonsense, but the character wins rather than crashing.
        if (m_entityManager.HasComponentUVE<Scene::CharacterControllerComponentUVE>(m_entity)) {
            return ToScriptUVE(
                m_entityManager.GetComponentUVE<Scene::CharacterControllerComponentUVE>(m_entity).velocity);
        }
        return ToScriptUVE(m_entityManager.GetComponentUVE<Scene::Rigid3DComponentUVE>(m_entity).velocity);
    }
    if (name == "volume" || name == "pitch") {
        const Scene::AudioSourceComponentUVE& source =
            m_entityManager.GetComponentUVE<Scene::AudioSourceComponentUVE>(m_entity);
        return static_cast<double>(name == "volume" ? source.volume : source.pitch);
    }
    if (name == "visible") {
        return !m_entityManager.HasComponentUVE<Scene::VisibilityComponentUVE>(m_entity) ||
               m_entityManager.GetComponentUVE<Scene::VisibilityComponentUVE>(m_entity).visible;
    }
    if (name == "intensity") {
        return static_cast<double>(
            m_entityManager.GetComponentUVE<Scene::LightComponentUVE>(m_entity).intensity);
    }
    if (name == "fov") {
        return static_cast<double>(
            m_entityManager.GetComponentUVE<Scene::CameraComponentUVE>(m_entity).fieldOfViewDegrees);
    }
    return m_entityManager.GetComponentUVE<Scene::CharacterControllerComponentUVE>(m_entity).grounded;
}

void UVScriptObjectHostUVE::SetPropertyUVE(const std::string_view name, const ValueUVE& value) {
    if (name == "position" || name == "scale") {
        Scene::TransformComponentUVE& transform = m_entityManager.GetComponentUVE<Scene::TransformComponentUVE>(m_entity);
        (name == "position" ? transform.localPosition : transform.localScale) = ToEngineUVE(value);
        // Marked stale so the scene graph recomputes the world transform. Not through
        // SetLocalTransformUVE: that ignores an object that has no world transform yet.
        if (m_entityManager.HasComponentUVE<Scene::WorldTransformComponentUVE>(m_entity)) {
            m_entityManager.GetComponentUVE<Scene::WorldTransformComponentUVE>(m_entity).dirty = true;
        }
    } else if (name == "velocity") {
        if (m_entityManager.HasComponentUVE<Scene::CharacterControllerComponentUVE>(m_entity)) {
            m_entityManager.GetComponentUVE<Scene::CharacterControllerComponentUVE>(m_entity).velocity =
                ToEngineUVE(value);
        } else {
            m_entityManager.GetComponentUVE<Scene::Rigid3DComponentUVE>(m_entity).velocity = ToEngineUVE(value);
        }
    } else if (name == "volume" || name == "pitch") {
        const float updated = static_cast<float>(std::get<double>(value));
        Scene::AudioSourceComponentUVE& source =
            m_entityManager.GetComponentUVE<Scene::AudioSourceComponentUVE>(m_entity);
        // Unlike velocity's blind store, an invalid write is ignored: a NaN volume would make
        // AudioSourceSystemUVE skip the source on every Sync, so the component stays valid.
        const bool accepted = name == "volume" ? (std::isfinite(updated) && updated >= 0.0F)
                                               : (std::isfinite(updated) && updated > 0.0F);
        if (accepted) {
            (name == "volume" ? source.volume : source.pitch) = updated;
        }
    } else if (name == "visible") {
        SetVisibleUVE(m_entityManager, m_entity, std::get<bool>(value));
    } else if (name == "intensity") {
        const float updated = static_cast<float>(std::get<double>(value));
        if (std::isfinite(updated) && updated >= 0.0F) {
            m_entityManager.GetComponentUVE<Scene::LightComponentUVE>(m_entity).intensity = updated;
        }
    } else if (name == "fov") {
        const float updated = static_cast<float>(std::get<double>(value));
        if (std::isfinite(updated) && updated >= Scene::kMinimumCameraFieldOfViewDegreesUVE &&
            updated < 180.0F) {
            m_entityManager.GetComponentUVE<Scene::CameraComponentUVE>(m_entity).fieldOfViewDegrees =
                updated;
        }
    }
}

ValueUVE UVScriptObjectHostUVE::CallFunctionUVE(const std::string_view name, const std::span<const ValueUVE> args) {
    // Physics needs no input system, so it routes before the null-input early-out below.
    if (name == "physics.apply_force") {
        return Scene::Rigid3DUVE::ApplyForceUVE(m_entityManager, m_entity, ToEngineUVE(args[0]));
    }
    if (name == "physics.apply_impulse") {
        return Scene::Rigid3DUVE::ApplyImpulseUVE(m_entityManager, m_entity, ToEngineUVE(args[0]));
    }
    if (name == "physics.apply_torque") {
        return Scene::Rigid3DUVE::ApplyTorqueUVE(m_entityManager, m_entity, ToEngineUVE(args[0]));
    }
    // Audio needs no input system, so it routes before the null-input early-out below.
    if (name == "audio.play" || name == "audio.stop" || name == "audio.is_playing") {
        if (m_audioSources == nullptr || m_audio == nullptr) {
            return ValueUVE{false};
        }
        if (name == "audio.play") {
            return m_audioSources->PlayEntityUVE(m_entity, m_entityManager, *m_audio);
        }
        if (name == "audio.stop") {
            return m_audioSources->StopEntityUVE(m_entity, *m_audio);
        }
        return m_audioSources->IsEntityPlayingUVE(m_entity, *m_audio);
    }
    // Gameplay pools and tags need no input system, so they route before the null-input
    // early-out below. A missing pool reads false and 0.0; use `attributes.has` to tell a missing
    // pool from a depleted one.
    if (name == "attributes.has" || name == "attributes.get" || name == "attributes.max" ||
        name == "attributes.damage" || name == "attributes.heal") {
        const std::string& id = std::get<std::string>(args[0]);
        Scene::GameplayAttributeUVE* found = nullptr;
        if (m_entityManager.HasComponentUVE<Scene::GameplayAttributesComponentUVE>(m_entity)) {
            found = Scene::FindGameplayAttributeUVE(
                m_entityManager.GetComponentUVE<Scene::GameplayAttributesComponentUVE>(m_entity), id);
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
            return ValueUVE{false};
        }
        const float amount = static_cast<float>(std::get<double>(args[1]));
        const Scene::GameplayAttributeDamageResultUVE result =
            name == "attributes.damage" ? Scene::DamageGameplayAttributeUVE(*found, amount)
                                        : Scene::HealGameplayAttributeUVE(*found, amount);
        return result.applied;
    }
    if (name == "tags.has" || name == "tags.add" || name == "tags.remove") {
        if (!m_entityManager.HasComponentUVE<Scene::GameplayTagComponentUVE>(m_entity)) {
            return ValueUVE{false};
        }
        Scene::GameplayTagComponentUVE& tags =
            m_entityManager.GetComponentUVE<Scene::GameplayTagComponentUVE>(m_entity);
        const std::string& tag = std::get<std::string>(args[0]);
        if (name == "tags.has") {
            return Scene::HasGameplayTagUVE(tags, tag);
        }
        if (name == "tags.add") {
            return Scene::AddGameplayTagUVE(tags, std::string{tag});
        }
        return Scene::RemoveGameplayTagUVE(tags, tag);
    }
    // A lookup needs no input system, so it routes before the null-input early-out below.
    if (name == "node") {
        const std::string& wanted = std::get<std::string>(args[0]);
        Scene::EntityUVE found = Scene::kInvalidEntityUVE;
        m_entityManager.ForEachUVE<Scene::NameComponentUVE>(
            [&](const Scene::EntityUVE entity, const Scene::NameComponentUVE& named) {
                if (found == Scene::kInvalidEntityUVE && named.name == wanted) {
                    found = entity;
                }
            });
        if (found == Scene::kInvalidEntityUVE) {
            return ValueUVE{ObjectRefUVE{}};
        }
        return ValueUVE{ObjectRefUVE{PackEntityUVE(found)}};
    }
    // RPC, the lobby, and the relay need no input system, so they route before the
    // null-input early-out below.
    if (name.starts_with("rpc.")) {
        return RpcCallUVE(name, args);
    }
    if (name.starts_with("lobby.")) {
        return LobbyCallUVE(name, args);
    }
    if (name.starts_with("relay.")) {
        return RelayCallUVE(name, args);
    }
    if (m_input == nullptr) {
        return name == "input.axis" ? ValueUVE{0.0} : ValueUVE{false};
    }
    if (name == "input.axis") {
        const bool negative = m_input->IsActionHeldUVE(std::get<std::string>(args[0]));
        const bool positive = m_input->IsActionHeldUVE(std::get<std::string>(args[1]));
        return (positive ? 1.0 : 0.0) - (negative ? 1.0 : 0.0);
    }
    const std::string& action = std::get<std::string>(args[0]);
    if (name == "input.pressed") {
        return m_input->IsActionTriggeredUVE(action);
    }
    if (name == "input.released") {
        return m_input->IsActionReleasedUVE(action);
    }
    return m_input->IsActionHeldUVE(action);
}

ValueUVE UVScriptObjectHostUVE::RpcCallUVE(const std::string_view name,
                                           const std::span<const ValueUVE> args) {
    if (m_rpc == nullptr) {
        // Fail closed without a peer: ids read 0, staging and cancel reads answer false,
        // results read their defaults, and kinds read "none".
        if (name == "rpc.result_kind") {
            return ValueUVE{std::string{"none"}};
        }
        if (name == "rpc.result_bool") {
            return ValueUVE{false};
        }
        if (name == "rpc.result_int") {
            return ValueUVE{std::int64_t{0}};
        }
        if (name == "rpc.result_float") {
            return ValueUVE{0.0};
        }
        if (name == "rpc.result_str" || name == "rpc.error") {
            return ValueUVE{std::string{}};
        }
        if (name == "rpc.result_vec3") {
            return ValueUVE{Vec3ValueUVE{}};
        }
        if (name == "rpc.begin" || name == "rpc.send") {
            return ValueUVE{std::int64_t{0}};
        }
        return ValueUVE{false};
    }
    if (name == "rpc.begin") {
        if (m_stagedRpc.size() >= kMaxStagedRpcCallsUVE) {
            return ValueUVE{std::int64_t{0}};
        }
        const std::uint32_t stagingId = NextStagingIdUVE();
        m_stagedRpc.emplace(stagingId,
                            StagedRpcCallUVE{std::string{std::get<std::string>(args[0])}, {}});
        return ValueUVE{static_cast<std::int64_t>(stagingId)};
    }
    const auto idArg = static_cast<std::uint32_t>(std::get<std::int64_t>(args[0]));
    if (name == "rpc.push_bool" || name == "rpc.push_int" || name == "rpc.push_float" ||
        name == "rpc.push_str" || name == "rpc.push_vec3") {
        const auto staged = m_stagedRpc.find(idArg);
        if (staged == m_stagedRpc.end()) {
            return ValueUVE{false};
        }
        staged->second.args.push_back(args[1]);
        return ValueUVE{true};
    }
    if (name == "rpc.send") {
        const auto staged = m_stagedRpc.find(idArg);
        if (staged == m_stagedRpc.end()) {
            return ValueUVE{std::int64_t{0}};
        }
        StagedRpcCallUVE call = std::move(staged->second);
        m_stagedRpc.erase(staged);
        const Network::RpcCallIdUVE callId = m_rpc->CallUVE(
            call.method, call.args,
            [this](const Network::RpcCallIdUVE id, const Network::RpcOutcomeUVE& outcome) {
                m_pendingRpc.erase(id);
                if (m_sink) {
                    m_sink(id, outcome.ok);
                }
            });
        if (callId == Network::kInvalidRpcCallIdUVE) {
            return ValueUVE{std::int64_t{0}};
        }
        m_pendingRpc.insert(callId);
        return ValueUVE{static_cast<std::int64_t>(callId)};
    }
    if (name == "rpc.abort") {
        return ValueUVE{m_stagedRpc.erase(idArg) > 0U};
    }
    if (name == "rpc.cancel") {
        // Converge to the peer's truth: a call it no longer tracks is not ours either.
        m_pendingRpc.erase(idArg);
        return ValueUVE{m_rpc->CancelUVE(idArg)};
    }
    if (name == "rpc.release") {
        return ValueUVE{m_rpc->ForgetResultUVE(idArg)};
    }
    if (name == "rpc.result_kind") {
        const std::optional<Network::RpcOutcomeUVE> outcome = m_rpc->ResultUVE(idArg);
        if (!outcome.has_value()) {
            return ValueUVE{std::string{m_pendingRpc.contains(idArg) ? "pending" : "none"}};
        }
        if (!outcome->ok) {
            return ValueUVE{std::string{"error"}};
        }
        return ValueUVE{std::string{RpcValueKindUVE(outcome->value)}};
    }
    if (name == "rpc.error") {
        const std::optional<Network::RpcOutcomeUVE> outcome = m_rpc->ResultUVE(idArg);
        if (!outcome.has_value() || outcome->ok) {
            return ValueUVE{std::string{}};
        }
        return ValueUVE{outcome->error};
    }
    // The remaining names are the result getters: a pending or failed call reads its default,
    // a mismatched kind reads its default, and `result_str` formats anything as text.
    const std::optional<Network::RpcOutcomeUVE> outcome = m_rpc->ResultUVE(idArg);
    const bool failed = !outcome.has_value() || !outcome->ok;
    if (name == "rpc.result_bool") {
        if (failed) {
            return ValueUVE{false};
        }
        const bool* boolean = std::get_if<bool>(&outcome->value);
        return ValueUVE{boolean != nullptr && *boolean};
    }
    if (name == "rpc.result_int") {
        if (failed) {
            return ValueUVE{std::int64_t{0}};
        }
        const auto* integer = std::get_if<std::int64_t>(&outcome->value);
        return ValueUVE{integer != nullptr ? *integer : std::int64_t{0}};
    }
    if (name == "rpc.result_float") {
        if (failed) {
            return ValueUVE{0.0};
        }
        const auto* number = std::get_if<double>(&outcome->value);
        return ValueUVE{number != nullptr ? *number : 0.0};
    }
    if (name == "rpc.result_str") {
        if (failed) {
            return ValueUVE{std::string{}};
        }
        return ValueUVE{UVScript::FormatValueUVE(outcome->value)};
    }
    if (name == "rpc.result_vec3") {
        if (failed) {
            return ValueUVE{Vec3ValueUVE{}};
        }
        const auto* vec = std::get_if<Vec3ValueUVE>(&outcome->value);
        return ValueUVE{vec != nullptr ? *vec : Vec3ValueUVE{}};
    }
    return ValueUVE{false};
}

ValueUVE UVScriptObjectHostUVE::LobbyCallUVE(const std::string_view name,
                                             const std::span<const ValueUVE> args) {
    if (m_lobby == nullptr) {
        // Fail closed without a peer: ids read 0, booleans answer false, results read
        // their defaults, and kinds read "none".
        if (name == "lobby.result_kind") {
            return ValueUVE{std::string{"none"}};
        }
        if (name == "lobby.error" || name == "lobby.result_room_name" ||
            name == "lobby.result_roster_name" || name == "lobby.roster_name") {
            return ValueUVE{std::string{}};
        }
        if (name == "lobby.result_left" || name == "lobby.release") {
            return ValueUVE{false};
        }
        return ValueUVE{std::int64_t{0}};
    }
    // The script owns the peer's roster push: hook it on every call (cheap, self-healing),
    // so the live readers below stay true however the room was reached.
    m_lobby->SetRosterHandlerUVE(
        [this](const Network::LobbyRoomIdUVE room, const std::vector<std::string>&) {
            if (m_rosterSink) {
                m_rosterSink(room);
            }
        });
    if (name == "lobby.room") {
        return ValueUVE{static_cast<std::int64_t>(m_lobby->CurrentRoomUVE())};
    }
    if (name == "lobby.roster_count") {
        return ValueUVE{static_cast<std::int64_t>(m_lobby->CachedRosterUVE().size())};
    }
    if (name == "lobby.roster_name") {
        const std::vector<std::string> roster = m_lobby->CachedRosterUVE();
        const std::int64_t index = std::get<std::int64_t>(args[0]);
        if (index < 0 || index >= static_cast<std::int64_t>(roster.size())) {
            return ValueUVE{std::string{}};
        }
        return ValueUVE{roster[static_cast<std::size_t>(index)]};
    }
    if (name == "lobby.list") {
        const std::uint32_t id = StartLobbyCallUVE(LobbyCallKindUVE::List);
        m_lobby->ListRoomsUVE(
            [this, id](std::vector<Network::LobbyRoomInfoUVE> rooms, const std::string& error) {
                auto outcome = m_lobbyCalls.find(id);
                if (outcome == m_lobbyCalls.end()) {
                    return;
                }
                outcome->second.done = true;
                outcome->second.ok = error.empty();
                outcome->second.error = error;
                for (const Network::LobbyRoomInfoUVE& room : rooms) {
                    outcome->second.rooms.push_back(
                        LobbyRoomDescUVE{room.id, room.name, room.players, room.maxPlayers});
                }
                if (outcome->second.settled && m_lobbySink) {
                    m_lobbySink(id, outcome->second.ok);
                }
            });
        return FinishLobbyCallUVE(id);
    }
    if (name == "lobby.create") {
        // Refuse what would wrap; a preserved 0 still goes to the server for its own error.
        const std::int64_t maxArg = std::get<std::int64_t>(args[2]);
        if (maxArg < 0 || maxArg > 4294967295LL) {
            return ValueUVE{std::int64_t{0}};
        }
        const std::uint32_t id = StartLobbyCallUVE(LobbyCallKindUVE::Create);
        m_lobby->CreateRoomUVE(
            std::get<std::string>(args[0]), std::get<std::string>(args[1]),
            static_cast<std::uint32_t>(maxArg),
            [this, id](const Network::LobbyRoomIdUVE room, const std::string& error) {
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
        return FinishLobbyCallUVE(id);
    }
    if (name == "lobby.join") {
        const std::int64_t roomArg = std::get<std::int64_t>(args[0]);
        if (roomArg < 0 || roomArg > 4294967295LL) {
            return ValueUVE{std::int64_t{0}};
        }
        const std::uint32_t id = StartLobbyCallUVE(LobbyCallKindUVE::Join);
        m_lobby->JoinRoomUVE(
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
        return FinishLobbyCallUVE(id);
    }
    if (name == "lobby.leave") {
        const std::uint32_t id = StartLobbyCallUVE(LobbyCallKindUVE::Leave);
        m_lobby->LeaveRoomUVE([this, id](const bool left, const std::string& error) {
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
        return FinishLobbyCallUVE(id);
    }
    if (name == "lobby.roster") {
        const std::int64_t roomArg = std::get<std::int64_t>(args[0]);
        if (roomArg < 0 || roomArg > 4294967295LL) {
            return ValueUVE{std::int64_t{0}};
        }
        const std::uint32_t id = StartLobbyCallUVE(LobbyCallKindUVE::Roster);
        m_lobby->QueryRosterUVE(
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
        return FinishLobbyCallUVE(id);
    }
    if (name == "lobby.cancel_all") {
        const std::size_t abandoned = m_lobby->CancelPendingUVE();
        // Completed outcomes stay readable; only the now-never-completing go.
        for (auto call = m_lobbyCalls.begin(); call != m_lobbyCalls.end();) {
            if (call->second.done) {
                ++call;
            } else {
                call = m_lobbyCalls.erase(call);
            }
        }
        return ValueUVE{static_cast<std::int64_t>(abandoned)};
    }
    const auto idArg = static_cast<std::uint32_t>(std::get<std::int64_t>(args[0]));
    const auto call = m_lobbyCalls.find(idArg);
    if (name == "lobby.release") {
        if (call == m_lobbyCalls.end() || !call->second.done) {
            return ValueUVE{false};
        }
        m_lobbyCalls.erase(call);
        return ValueUVE{true};
    }
    if (name == "lobby.result_kind") {
        if (call == m_lobbyCalls.end()) {
            return ValueUVE{std::string{"none"}};
        }
        if (!call->second.done) {
            return ValueUVE{std::string{"pending"}};
        }
        if (!call->second.ok) {
            return ValueUVE{std::string{"error"}};
        }
        switch (call->second.kind) {
            case LobbyCallKindUVE::List:
                return ValueUVE{std::string{"rooms"}};
            case LobbyCallKindUVE::Create:
                return ValueUVE{std::string{"room"}};
            case LobbyCallKindUVE::Join:
            case LobbyCallKindUVE::Roster:
                return ValueUVE{std::string{"roster"}};
            case LobbyCallKindUVE::Leave:
                return ValueUVE{std::string{"left"}};
        }
        return ValueUVE{std::string{"none"}};
    }
    if (name == "lobby.error") {
        if (call == m_lobbyCalls.end() || !call->second.done || call->second.ok) {
            return ValueUVE{std::string{}};
        }
        return ValueUVE{call->second.error};
    }
    // The remaining names are the result getters: anything but a completed success of the
    // matching kind reads its default.
    const bool failed = call == m_lobbyCalls.end() || !call->second.done || !call->second.ok;
    if (name == "lobby.result_room_count") {
        if (failed || call->second.kind != LobbyCallKindUVE::List) {
            return ValueUVE{std::int64_t{0}};
        }
        return ValueUVE{static_cast<std::int64_t>(call->second.rooms.size())};
    }
    if (name == "lobby.result_room_id" || name == "lobby.result_room_name" ||
        name == "lobby.result_room_players" || name == "lobby.result_room_max") {
        const bool wantName = name == "lobby.result_room_name";
        if (failed || call->second.kind != LobbyCallKindUVE::List) {
            return wantName ? ValueUVE{std::string{}} : ValueUVE{std::int64_t{0}};
        }
        const std::int64_t index = std::get<std::int64_t>(args[1]);
        if (index < 0 || index >= static_cast<std::int64_t>(call->second.rooms.size())) {
            return wantName ? ValueUVE{std::string{}} : ValueUVE{std::int64_t{0}};
        }
        const LobbyRoomDescUVE& room = call->second.rooms[static_cast<std::size_t>(index)];
        if (name == "lobby.result_room_id") {
            return ValueUVE{static_cast<std::int64_t>(room.id)};
        }
        if (wantName) {
            return ValueUVE{room.name};
        }
        if (name == "lobby.result_room_players") {
            return ValueUVE{static_cast<std::int64_t>(room.players)};
        }
        return ValueUVE{static_cast<std::int64_t>(room.maxPlayers)};
    }
    if (name == "lobby.result_created_room") {
        if (failed || call->second.kind != LobbyCallKindUVE::Create) {
            return ValueUVE{std::int64_t{0}};
        }
        return ValueUVE{static_cast<std::int64_t>(call->second.room)};
    }
    if (name == "lobby.result_roster_count") {
        if (failed || (call->second.kind != LobbyCallKindUVE::Join &&
                       call->second.kind != LobbyCallKindUVE::Roster)) {
            return ValueUVE{std::int64_t{0}};
        }
        return ValueUVE{static_cast<std::int64_t>(call->second.roster.size())};
    }
    if (name == "lobby.result_roster_name") {
        if (failed || (call->second.kind != LobbyCallKindUVE::Join &&
                       call->second.kind != LobbyCallKindUVE::Roster)) {
            return ValueUVE{std::string{}};
        }
        const std::int64_t index = std::get<std::int64_t>(args[1]);
        if (index < 0 || index >= static_cast<std::int64_t>(call->second.roster.size())) {
            return ValueUVE{std::string{}};
        }
        return ValueUVE{call->second.roster[static_cast<std::size_t>(index)]};
    }
    if (name == "lobby.result_left") {
        if (failed || call->second.kind != LobbyCallKindUVE::Leave) {
            return ValueUVE{false};
        }
        return ValueUVE{call->second.left};
    }
    return ValueUVE{false};
}

std::uint32_t UVScriptObjectHostUVE::StartLobbyCallUVE(const LobbyCallKindUVE kind) {
    const std::uint32_t id = NextLobbyCallIdUVE();
    LobbyCallStateUVE& call = m_lobbyCalls.emplace(id, LobbyCallStateUVE{}).first->second;
    call.kind = kind;
    return id;
}

ValueUVE UVScriptObjectHostUVE::FinishLobbyCallUVE(const std::uint32_t id) {
    auto call = m_lobbyCalls.find(id);
    if (call == m_lobbyCalls.end()) {
        return ValueUVE{std::int64_t{0}};
    }
    call->second.settled = true;
    if (call->second.done && !call->second.ok) {
        // The send failed synchronously: no sink fired (nothing settled yet), so forget the
        // stillborn call and answer 0 like rpc.send does.
        m_lobbyCalls.erase(call);
        return ValueUVE{std::int64_t{0}};
    }
    return ValueUVE{static_cast<std::int64_t>(id)};
}

std::uint32_t UVScriptObjectHostUVE::NextStagingIdUVE() noexcept {
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

std::uint32_t UVScriptObjectHostUVE::NextLobbyCallIdUVE() noexcept {
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

ValueUVE UVScriptObjectHostUVE::RelayCallUVE(const std::string_view name,
                                             const std::span<const ValueUVE> args) {
    // The client side needs the session, not the relay: send shouts, poll reads one arrival.
    if (name == "relay.send") {
        if (m_relaySession == nullptr) {
            return ValueUVE{false};
        }
        const std::string& text = std::get<std::string>(args[0]);
        return ValueUVE{m_relaySession->SendDataUVE(
            std::vector<std::uint8_t>(text.begin(), text.end()))};
    }
    if (name == "relay.poll") {
        if (m_relaySession == nullptr) {
            return ValueUVE{std::string{}};
        }
        const std::optional<std::vector<std::uint8_t>> datagram =
            m_relaySession->TryReceiveDataUVE();
        if (!datagram.has_value()) {
            return ValueUVE{std::string{}};
        }
        return ValueUVE{std::string{datagram->begin(), datagram->end()}};
    }
    if (m_relay == nullptr) {
        // Fail closed without a relay: counts and rooms read 0, setters answer false, and
        // states read "unknown".
        if (name == "relay.peer_state") {
            return ValueUVE{std::string{"unknown"}};
        }
        if (name == "relay.set_room" || name == "relay.set_interest" ||
            name == "relay.interested" || name == "relay.remove") {
            return ValueUVE{false};
        }
        return ValueUVE{std::int64_t{0}};
    }
    if (name == "relay.peer_count") {
        return ValueUVE{static_cast<std::int64_t>(m_relay->PeerCountUVE())};
    }
    if (name == "relay.stat_received") {
        return ValueUVE{static_cast<std::int64_t>(m_relay->StatsUVE().payloadsReceived)};
    }
    if (name == "relay.stat_forwarded") {
        return ValueUVE{static_cast<std::int64_t>(m_relay->StatsUVE().payloadsForwarded)};
    }
    if (name == "relay.stat_dropped") {
        return ValueUVE{static_cast<std::int64_t>(m_relay->StatsUVE().forwardsDropped)};
    }
    // Refuse what would wrap; in-range but unknown ids follow the core's own defaults
    // (room 0, fail-open interest, unremoved, "unknown").
    const std::int64_t peerArg = std::get<std::int64_t>(args[0]);
    if (peerArg < 0 || peerArg > 4294967295LL) {
        if (name == "relay.peer_state") {
            return ValueUVE{std::string{"unknown"}};
        }
        if (name == "relay.room") {
            return ValueUVE{std::int64_t{0}};
        }
        return ValueUVE{false};
    }
    const auto peer = static_cast<Network::RelayPeerIdUVE>(peerArg);
    if (name == "relay.peer_state") {
        const Network::NetworkSessionUVE* session = m_relay->PeerSessionUVE(peer);
        if (session == nullptr) {
            return ValueUVE{std::string{"unknown"}};
        }
        switch (session->StateUVE()) {
            case Network::SessionStateUVE::Disconnected:
                return ValueUVE{std::string{"disconnected"}};
            case Network::SessionStateUVE::Connecting:
                return ValueUVE{std::string{"connecting"}};
            case Network::SessionStateUVE::Reconnecting:
                return ValueUVE{std::string{"reconnecting"}};
            case Network::SessionStateUVE::Established:
                return ValueUVE{std::string{"established"}};
        }
        return ValueUVE{std::string{"unknown"}};
    }
    if (name == "relay.room") {
        return ValueUVE{static_cast<std::int64_t>(m_relay->PeerRoomUVE(peer))};
    }
    if (name == "relay.remove") {
        return ValueUVE{m_relay->RemovePeerUVE(peer)};
    }
    if (name == "relay.set_room") {
        const std::int64_t roomArg = std::get<std::int64_t>(args[1]);
        if (roomArg < 0 || roomArg > 4294967295LL) {
            return ValueUVE{false};
        }
        return ValueUVE{
            m_relay->SetPeerRoomUVE(peer, static_cast<Network::RelayRoomIdUVE>(roomArg))};
    }
    const std::int64_t senderArg = std::get<std::int64_t>(args[1]);
    if (senderArg < 0 || senderArg > 4294967295LL) {
        return ValueUVE{false};
    }
    const auto sender = static_cast<Network::RelayPeerIdUVE>(senderArg);
    if (name == "relay.interested") {
        return ValueUVE{m_relay->IsPeerInterestedUVE(peer, sender)};
    }
    if (name == "relay.set_interest") {
        return ValueUVE{m_relay->SetPeerInterestUVE(peer, sender, std::get<bool>(args[2]))};
    }
    return ValueUVE{false};
}

void UVScriptObjectHostUVE::CallMethodUVE(const ObjectRefUVE target, const std::string_view method,
                                       const std::span<const ValueUVE> args) {
    // A stale handle (its entity long destroyed) lands on another occupant or nothing at all; the
    // generation check inside IsAliveUVE tells the two apart, and both fail closed as nothing.
    if (target.id == 0U) {
        return;
    }
    const Scene::EntityUVE entity = UnpackEntityUVE(target);
    if (!m_entityManager.IsAliveUVE(entity)) {
        return;
    }
    // The target's own script first: a `fn hide()` there overrides the built-in below.
    if (m_resolver) {
        if (UVScript::ScriptInstanceUVE* const instance = m_resolver(entity);
            instance != nullptr && instance->HasFunctionUVE(method, args.size())) {
            static_cast<void>(instance->CallUVE(method, args));
            return;
        }
    }
    if (method == "hide" || method == "show") {
        SetVisibleUVE(m_entityManager, entity, method == "show");
    }
}

void UVScriptObjectHostUVE::PrintUVE(const std::string_view text) {
    const std::string object = m_entityManager.HasComponentUVE<Scene::NameComponentUVE>(m_entity)
                                 ? m_entityManager.GetComponentUVE<Scene::NameComponentUVE>(m_entity).name
                                 : std::string{"object"};
    UVE_INFO("[{}] {}", object, text);
}

} // namespace UVE::Core
