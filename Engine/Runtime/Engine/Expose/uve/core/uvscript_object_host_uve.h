// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "uve/component/entity_uve.h"
#include "uve/uvscript/uvscript_host_uve.h"

namespace UVE::Input {
class IInputSystemUVE;
} // namespace UVE::Input

namespace UVE::Scene {
class IEntityManagerUVE;
} // namespace UVE::Scene

namespace UVE::Audio {
class IAudioSourceSystemUVE;
class IAudioSystemUVE;
} // namespace Audio

namespace UVE::UVScript {
class ScriptInstanceUVE;
} // namespace UVE::UVScript

namespace UVE::Network {
class NetworkRpcUVE;
class NetworkLobbyClientUVE;
class NetworkRelayUVE;
class NetworkSessionUVE;
} // namespace UVE::Network

namespace UVE::Core {

/// Fired from the RPC peer's response callback when one of this host's script-initiated
/// calls completes: the call id plus whether it succeeded. The game installs it to raise
/// `rpc_response` on this host's instance; like the instance resolver, its lifetime is the
/// installer's business. Call ids are the peer's RpcCallIdUVE, spelled here so this header
/// stays forward-declared (asserted equal where the peer is complete).
using RpcResponseSinkUVE = std::function<void(std::uint32_t, bool)>;

/// Fired from this host's lobby facade callbacks when one of its script-initiated calls
/// completes: the call id plus whether it succeeded. The game installs it to raise
/// `lobby_result` on this host's instance; like the instance resolver, its lifetime is the
/// installer's business. Call ids are minted by the host from 1 (0 is invalid), so no peer
/// type needs asserting here.
using LobbyResultSinkUVE = std::function<void(std::uint32_t, bool)>;
/// Fired from the lobby peer's roster handler on every roster push for the current room:
/// the room id, spelled uint32_t so this header stays forward-declared (asserted equal
/// where the peer is complete). The game installs it to raise `lobby_roster`.
using LobbyRosterSinkUVE = std::function<void(std::uint32_t)>;

/// What a `.uvs` script can reach on the object it is attached to. The object's components decide:
/// - every object: `name` (read-only);
/// - an object with a transform: `position`, `scale` (local, metres);
/// - a character body: `velocity`, `grounded` (read-only);
/// - a rigid body: `velocity` (linear, metres/second);
/// - a rigid body: `physics.apply_force/apply_impulse/apply_torque(vec3)` - a persistent force in
///   newtons until changed, an instant mass-scaled velocity kick, a persistent torque - each
///   answering whether it applied;
/// - an object with gameplay attributes: `attributes.has/get/max(id)` reads a pool (a missing id
///   reads false and 0.0), `attributes.damage/heal(id, amount)` moves it, answering whether it
///   applied - health itself stays in the strike pipeline, out of these pools;
/// - an object with gameplay tags: `tags.has/add/remove(tag)`, each answering whether it applied;
/// - with an RPC peer attached: `rpc.begin(method)` stages a call (a staging id, 0 without a
///   peer); `rpc.push_bool/push_int/push_float/push_str/push_vec3(id, v)` appends one flat
///   argument; `rpc.send(id)` fires it (a call id, 0 on failure); `rpc.abort(id)` drops staged
///   args; `rpc.cancel(id)` gives up on a pending call; `rpc.result_kind(id)` reports "none",
///   "pending", "error", or the value kind ("nil", "bool", "int", "float", "str", "vec3",
///   "list", "map", "tuple"); `rpc.result_bool/result_int/result_float/result_str/result_vec3(id)`
///   reads a completed result (each a default when the kind mismatches; `result_str` formats any
///   completed value as text); `rpc.error(id)` reads the failure text ("" unless the call
///   failed); `rpc.release(id)` forgets a completed call; event `rpc_response(id, ok)`;
/// - with a lobby peer attached: `lobby.list()` starts a room listing (a call id, 0 without
///   a peer or when the send fails); `lobby.create(room, player, max)`, `lobby.join(room,
///   player)`, `lobby.leave()`, `lobby.roster(room)` start the matching call;
///   `lobby.result_kind(id)` reports "none", "pending", "error", "rooms", "room", "roster",
///   or "left"; `lobby.error(id)` reads the failure text;
///   `lobby.result_room_count(id)` + `lobby.result_room_id/name/players/max(id, index)` read
///   a listing; `lobby.result_created_room(id)` reads a create;
///   `lobby.result_roster_count(id)` + `lobby.result_roster_name(id, index)` read a
///   join/roster; `lobby.result_left(id)` reads a leave; `lobby.release(id)` forgets a
///   completed call (pending calls refuse - abandon them with `lobby.cancel_all()`, which
///   answers how many it dropped); `lobby.room()`, `lobby.roster_count()`,
///   `lobby.roster_name(index)` read the live room and roster cache; events
///   `lobby_result(id, ok)` and `lobby_roster(room)`;
/// - with a relay attached: `relay.peer_count()` counts peers; `relay.set_room(peer, room)`
///   and `relay.room(peer)` tag and read fan-out rooms; `relay.set_interest(receiver,
///   sender, interested)` and `relay.interested(receiver, sender)` mute and read mutes;
///   `relay.remove(peer)` drops a peer; `relay.stat_received/stat_forwarded/stat_dropped()`
///   read the fan-out counters; `relay.peer_state(peer)` reports "unknown", "disconnected",
///   "connecting", "reconnecting", or "established"; with a relay session attached:
///   `relay.send(text)` shouts and `relay.poll()` reads one arrival ("" when none). All
///   synchronous - nothing to await, no events; without the attachment each call fails
///   closed (0, false, "", "unknown");
/// - always: `input.pressed/held/released(action)` and `input.axis(negative, positive)`, which
///   read the project's input map; events `ready` and `tick(dt)`.
/// - always: `node(name)` finds another object by name, and `ref.method(args)` calls one of its
///   script functions (or the built-in `hide()`/`show()`), fire-and-forget.
class UVScriptObjectHostUVE final : public UVScript::UVScriptHostUVE {
public:
    /// Finds the running script of an entity, for routing cross-node calls at it. Null when the
    /// entity runs no script. Empty on check-only hosts, whose calls then only reach built-ins.
    using InstanceResolverUVE = std::function<UVScript::ScriptInstanceUVE*(Scene::EntityUVE)>;

    /// `input` may be null (no input system): input calls then read as not pressed.
    /// `audioSources`/`audio` may be null (no audio): audio calls then fail closed as false. Both
    /// default to null so check-only hosts (editor diagnostics, tests that never touch audio) stay
    /// three-argument constructions.
    /// `rpc` may be null (no RPC peer): rpc calls then fail closed as 0, false, "", or "none".
    /// The peer must outlive the host (pending script-initiated calls cancel in the destructor,
    /// so no callback fires into a dead host). `sink` fires per completed script-initiated call
    /// and may be empty (responses are then only visible through the result getters).
    /// `lobby` may be null (no lobby peer): lobby calls then fail closed the same way. The
    /// peer must outlive the host (in-flight script-initiated calls abandon and the roster
    /// handler clears in the destructor). `lobbySink` fires per completed script-initiated
    /// call and `rosterSink` per roster push; both may be empty.
    /// `relay` may be null (no relay): relay management calls then fail closed as 0, false,
    /// or "unknown". `relaySession` may be null (no relay session): `relay.send` answers
    /// false and `relay.poll` answers "". Both must outlive the host; neither installs
    /// callbacks, so destruction unhooks nothing.
    UVScriptObjectHostUVE(Scene::IEntityManagerUVE& entityManager, const Input::IInputSystemUVE* input,
                          Scene::EntityUVE entity, Audio::IAudioSourceSystemUVE* audioSources = nullptr,
                          Audio::IAudioSystemUVE* audio = nullptr,
                          InstanceResolverUVE resolver = InstanceResolverUVE{},
                          Network::NetworkRpcUVE* rpc = nullptr,
                          RpcResponseSinkUVE sink = RpcResponseSinkUVE{},
                          Network::NetworkLobbyClientUVE* lobby = nullptr,
                          LobbyResultSinkUVE lobbySink = LobbyResultSinkUVE{},
                          LobbyRosterSinkUVE rosterSink = LobbyRosterSinkUVE{},
                          Network::NetworkRelayUVE* relay = nullptr,
                          Network::NetworkSessionUVE* relaySession = nullptr) noexcept;
    ~UVScriptObjectHostUVE() override;

    [[nodiscard]] std::optional<UVScript::HostPropertyUVE> DescribePropertyUVE(std::string_view name) const override;
    [[nodiscard]] std::optional<UVScript::HostFunctionUVE> DescribeFunctionUVE(std::string_view name) const override;
    [[nodiscard]] std::optional<std::vector<UVScript::TypeUVE>> DescribeEventUVE(std::string_view event) const override;

    [[nodiscard]] UVScript::ValueUVE GetPropertyUVE(std::string_view name) override;
    void SetPropertyUVE(std::string_view name, const UVScript::ValueUVE& value) override;
    [[nodiscard]] UVScript::ValueUVE CallFunctionUVE(std::string_view name,
                                                      std::span<const UVScript::ValueUVE> args) override;
    void CallMethodUVE(UVScript::ObjectRefUVE target, std::string_view method,
                       std::span<const UVScript::ValueUVE> args) override;
    void PrintUVE(std::string_view text) override;

private:
    struct StagedRpcCallUVE final {
        std::string method;
        std::vector<UVScript::ValueUVE> args;
    };

    [[nodiscard]] UVScript::ValueUVE RpcCallUVE(std::string_view name,
                                               std::span<const UVScript::ValueUVE> args);
    [[nodiscard]] std::uint32_t NextStagingIdUVE() noexcept;

    enum class LobbyCallKindUVE : std::uint8_t { List, Create, Join, Leave, Roster };

    struct LobbyRoomDescUVE final {
        std::uint32_t id = 0U;
        std::string name;
        std::uint32_t players = 0U;
        std::uint32_t maxPlayers = 0U;
    };

    struct LobbyCallStateUVE final {
        LobbyCallKindUVE kind = LobbyCallKindUVE::List;
        bool done = false;
        bool ok = false;
        bool settled = false;
        std::string error;
        std::vector<LobbyRoomDescUVE> rooms;
        std::uint32_t room = 0U;
        std::vector<std::string> roster;
        bool left = false;
    };

    [[nodiscard]] UVScript::ValueUVE LobbyCallUVE(std::string_view name,
                                                 std::span<const UVScript::ValueUVE> args);
    [[nodiscard]] std::uint32_t StartLobbyCallUVE(LobbyCallKindUVE kind);
    [[nodiscard]] UVScript::ValueUVE FinishLobbyCallUVE(std::uint32_t id);
    [[nodiscard]] std::uint32_t NextLobbyCallIdUVE() noexcept;
    [[nodiscard]] UVScript::ValueUVE RelayCallUVE(std::string_view name,
                                                 std::span<const UVScript::ValueUVE> args);

    Scene::IEntityManagerUVE& m_entityManager;
    const Input::IInputSystemUVE* m_input = nullptr;
    Scene::EntityUVE m_entity = Scene::kInvalidEntityUVE;
    Audio::IAudioSourceSystemUVE* m_audioSources = nullptr;
    Audio::IAudioSystemUVE* m_audio = nullptr;
    InstanceResolverUVE m_resolver;
    Network::NetworkRpcUVE* m_rpc = nullptr;
    RpcResponseSinkUVE m_sink;
    std::map<std::uint32_t, StagedRpcCallUVE> m_stagedRpc;
    std::set<std::uint32_t> m_pendingRpc;
    std::uint32_t m_nextStagingId = 1U;
    Network::NetworkLobbyClientUVE* m_lobby = nullptr;
    LobbyResultSinkUVE m_lobbySink;
    LobbyRosterSinkUVE m_rosterSink;
    std::map<std::uint32_t, LobbyCallStateUVE> m_lobbyCalls;
    std::uint32_t m_nextLobbyCall = 1U;
    Network::NetworkRelayUVE* m_relay = nullptr;
    Network::NetworkSessionUVE* m_relaySession = nullptr;
};

} // namespace UVE::Core
