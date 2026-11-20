// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <span>
#include <set>
#include <string>
#include <vector>

#include "entity.h"
#include "blueprint_host.h"

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

namespace UVE::Blueprint {
class ScriptInstanceUVE;
} // namespace UVE::Blueprint

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

/// The world a gameplay script reaches beyond its own object: traces, spawning, the clock, the
/// game's state, saves, and messages to other scripts. One set is shared by every script of a game
/// (the engine fills it); each entry may be empty, and a call to an empty one fails closed.
/// Vectors are UVScript's, so this header stays free of engine maths.
struct ScriptServicesUVE final {
    /// What a trace hit: the first collider along the segment.
    struct TraceHitUVE final {
        bool hit = false;
        Blueprint::Vec3ValueUVE point{};
        Blueprint::Vec3ValueUVE normal{};
        double distance = 0.0;
        Scene::EntityUVE entity = Scene::kInvalidEntityUVE;
    };
    /// A line (radius 0) or sphere trace from `from` to `to`, skipping `ignore`.
    std::function<TraceHitUVE(const Blueprint::Vec3ValueUVE& from, const Blueprint::Vec3ValueUVE& to, double radius,
                              Scene::EntityUVE ignore)>
        trace;
    /// Makes an entity or prefab file's objects at `position`, turned by `rotation` (degrees): the root, or none.
    std::function<Scene::EntityUVE(const std::string& path, const Blueprint::Vec3ValueUVE& position,
                                   const Blueprint::Vec3ValueUVE& rotation)>
        spawn;
    /// Removes an object (and what is under it) once every script has run this frame, so a script
    /// may destroy itself; it hears `destroyed` first.
    std::function<void(Scene::EntityUVE)> destroy;
    /// Seconds of game time since play began (stops while paused, follows the time scale).
    std::function<double()> time;
    /// This frame's game seconds.
    std::function<double()> delta;
    /// Puts `child` under `parent` (none: back at the top of the level); false when it cannot be.
    std::function<bool(Scene::EntityUVE child, Scene::EntityUVE parent)> setParent;
    std::function<double()> timeScale;
    std::function<void(double)> setTimeScale;
    std::function<bool()> paused;
    std::function<void(bool)> setPaused;
    /// Ends the game (stops play in the editor).
    std::function<void()> quit;
    /// Loads a level file once this frame is done; false when there is no such file.
    std::function<bool(const std::string& path)> openLevel;
    std::function<std::string()> levelName;
    /// Raises `custom_event(name, payload)` on `target`'s script after the current handlers.
    std::function<void(Scene::EntityUVE target, const std::string& name, const std::string& payload)> emit;
    /// A one-shot sound (an audio file): at `position` when spatial, else heard everywhere.
    std::function<bool(const std::string& path, const Blueprint::Vec3ValueUVE& position, double volume, bool spatial)> playSound;
    /// Particles at once from `emitter`'s ParticleEmitter, on top of its rate.
    std::function<bool(Scene::EntityUVE emitter, std::int64_t count)> burstParticles;
    /// Makes `camera` the view (the others stop being current).
    std::function<bool(Scene::EntityUVE camera)> activateCamera;
    /// Shakes the view: `strength` metres at first, fading out over `seconds`.
    std::function<void(double strength, double seconds)> cameraShake;
    /// Where the view looks (unit), and where it is.
    std::function<Blueprint::Vec3ValueUVE()> cameraForward;
    std::function<Blueprint::Vec3ValueUVE()> cameraLocation;
    /// Plays the AnimationSequencer `player`'s clip named `clip` (its file's name, from its own list;
    /// empty: the one it has) from the start.
    std::function<bool(Scene::EntityUVE player, const std::string& clip)> playAnimation;
    /// Save slots: the game data below, written to and read from a named slot.
    std::function<bool(const std::string& slot)> saveSlot;
    std::function<bool(const std::string& slot)> loadSlot;
    std::function<bool(const std::string& slot)> slotExists;
    std::function<bool(const std::string& slot)> deleteSlot;
    /// The game's own values by key (as text), kept across levels and written by `saveSlot`.
    std::map<std::string, std::string> gameData;
};

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
class BlueprintObjectHostUVE final : public Blueprint::UVScriptHostUVE {
public:
    /// Finds the running script of an entity, for routing cross-node calls at it. Null when the
    /// entity runs no script. Empty on check-only hosts, whose calls then only reach built-ins.
    using InstanceResolverUVE = std::function<Blueprint::ScriptInstanceUVE*(Scene::EntityUVE)>;

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
    BlueprintObjectHostUVE(Scene::IEntityManagerUVE& entityManager, const Input::IInputSystemUVE* input,
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
    ~BlueprintObjectHostUVE() override;

    [[nodiscard]] std::optional<Blueprint::HostPropertyUVE> DescribePropertyUVE(std::string_view name) const override;
    [[nodiscard]] std::optional<Blueprint::HostFunctionUVE> DescribeFunctionUVE(std::string_view name) const override;
    [[nodiscard]] std::optional<std::vector<Blueprint::TypeUVE>> DescribeEventUVE(std::string_view event) const override;

    [[nodiscard]] Blueprint::ValueUVE GetPropertyUVE(std::string_view name) override;
    void SetPropertyUVE(std::string_view name, const Blueprint::ValueUVE& value) override;
    [[nodiscard]] Blueprint::ValueUVE CallFunctionUVE(std::string_view name,
                                                      std::span<const Blueprint::ValueUVE> args) override;
    void CallMethodUVE(Blueprint::ObjectRefUVE target, std::string_view method,
                       std::span<const Blueprint::ValueUVE> args) override;
    void PrintUVE(std::string_view text) override;

    /// Makes a Widget Blueprint's widgets (its `.uvwbp` path) in the world, hidden until
    /// `ui.add_to_viewport` shows them: the root Canvas, or none when the file cannot be made.
    using WidgetFactoryUVE = std::function<Scene::EntityUVE(const std::string& path)>;
    /// Without one, `ui.create` answers no object.
    void SetWidgetFactoryUVE(WidgetFactoryUVE factory) { m_widgetFactory = std::move(factory); }
    /// `ui.show_cursor(show)`: shows and frees the pointer for UI, or hides and captures it for the game.
    using CursorHandlerUVE = std::function<void(bool show)>;
    void SetCursorHandlerUVE(CursorHandlerUVE handler) { m_cursorHandler = std::move(handler); }
    /// The game's services (traces, spawning, the clock, saves, messages). Without them those calls fail closed.
    void SetServicesUVE(ScriptServicesUVE* services) noexcept { m_services = services; }

    /// Moves this script's timers on by `dt` seconds: the ids of those that ran out, in order, for
    /// the caller to raise `timer(id)`. A looping timer starts again; a one-shot one is gone.
    /// A life span (`world.set_life_span`) that ran out destroys the object instead.
    [[nodiscard]] std::vector<std::string> AdvanceTimersUVE(double dt);
    [[nodiscard]] bool HasTimersUVE() const noexcept { return !m_timers.empty(); }

private:
    struct TimerUVE final {
        double remaining = 0.0;
        double period = 0.0;
        bool loop = false;
    };
    [[nodiscard]] Blueprint::ValueUVE TextAndMathCallUVE(std::string_view name, std::span<const Blueprint::ValueUVE> args);
    [[nodiscard]] Blueprint::ValueUVE WorldCallUVE(std::string_view name, std::span<const Blueprint::ValueUVE> args);
    /// The object a script reference names, or none when it names nothing alive.
    [[nodiscard]] Scene::EntityUVE EntityOfUVE(const Blueprint::ValueUVE& value) const;

    /// Widget Blueprints: `ui.*`. The widget named `name` under this object's Canvas (the whole
    /// world when this object is no widget); none when there is no such widget.
    [[nodiscard]] Scene::EntityUVE FindWidgetUVE(std::string_view name) const;
    [[nodiscard]] Blueprint::ValueUVE WidgetCallUVE(std::string_view name, std::span<const Blueprint::ValueUVE> args);

    struct StagedRpcCallUVE final {
        std::string method;
        std::vector<Blueprint::ValueUVE> args;
    };

    [[nodiscard]] Blueprint::ValueUVE GameplayCallUVE(std::string_view name, std::span<const Blueprint::ValueUVE> args);
    [[nodiscard]] Blueprint::ValueUVE RpcCallUVE(std::string_view name,
                                               std::span<const Blueprint::ValueUVE> args);
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

    [[nodiscard]] Blueprint::ValueUVE LobbyCallUVE(std::string_view name,
                                                 std::span<const Blueprint::ValueUVE> args);
    [[nodiscard]] std::uint32_t StartLobbyCallUVE(LobbyCallKindUVE kind);
    [[nodiscard]] Blueprint::ValueUVE FinishLobbyCallUVE(std::uint32_t id);
    [[nodiscard]] std::uint32_t NextLobbyCallIdUVE() noexcept;
    [[nodiscard]] Blueprint::ValueUVE RelayCallUVE(std::string_view name,
                                                 std::span<const Blueprint::ValueUVE> args);

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
    WidgetFactoryUVE m_widgetFactory;
    CursorHandlerUVE m_cursorHandler;
    ScriptServicesUVE* m_services = nullptr;
    std::map<std::string, TimerUVE> m_timers;
    /// The last trace's result, read by `trace.location/normal/object/distance`.
    ScriptServicesUVE::TraceHitUVE m_lastTrace;
};

} // namespace UVE::Core
