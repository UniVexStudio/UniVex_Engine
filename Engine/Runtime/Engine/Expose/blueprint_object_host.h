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
class IInputSystem;
} // namespace UVE::Input

namespace UVE::Scene {
class IEntityManager;
} // namespace UVE::Scene

namespace UVE::Audio {
class IAudioSourceSystem;
class IAudioSystem;
} // namespace Audio

namespace UVE::Blueprint {
class ScriptInstance;
} // namespace UVE::Blueprint

namespace UVE::Network {
class NetworkRpc;
class NetworkLobbyClient;
class NetworkRelay;
class NetworkSession;
} // namespace UVE::Network

namespace UVE::Core {

/// Fired from the RPC peer's response callback when one of this host's script-initiated
/// calls completes: the call id plus whether it succeeded. The game installs it to raise
/// `rpc_response` on this host's instance; like the instance resolver, its lifetime is the
/// installer's business. Call ids are the peer's RpcCallId, spelled here so this header
/// stays forward-declared (asserted equal where the peer is complete).
using RpcResponseSink = std::function<void(std::uint32_t, bool)>;

/// Fired from this host's lobby facade callbacks when one of its script-initiated calls
/// completes: the call id plus whether it succeeded. The game installs it to raise
/// `lobby_result` on this host's instance; like the instance resolver, its lifetime is the
/// installer's business. Call ids are minted by the host from 1 (0 is invalid), so no peer
/// type needs asserting here.
using LobbyResultSink = std::function<void(std::uint32_t, bool)>;
/// Fired from the lobby peer's roster handler on every roster push for the current room:
/// the room id, spelled uint32_t so this header stays forward-declared (asserted equal
/// where the peer is complete). The game installs it to raise `lobby_roster`.
using LobbyRosterSink = std::function<void(std::uint32_t)>;

/// The world a gameplay script reaches beyond its own object: traces, spawning, the clock, the
/// game's state, saves, and messages to other scripts. One set is shared by every script of a game
/// (the engine fills it); each entry may be empty, and a call to an empty one fails closed.
/// Vectors are UVScript's, so this header stays free of engine maths.
struct ScriptServices final {
    /// What a trace hit: the first collider along the segment.
    struct TraceHit final {
        bool hit = false;
        Blueprint::Vec3Value point{};
        Blueprint::Vec3Value normal{};
        double distance = 0.0;
        Scene::Entity entity = Scene::kInvalidEntity;
    };
    /// A line (radius 0) or sphere trace from `from` to `to`, skipping `ignore`.
    std::function<TraceHit(const Blueprint::Vec3Value& from, const Blueprint::Vec3Value& to, double radius,
                              Scene::Entity ignore)>
        trace;
    /// Makes an entity or prefab file's objects at `position`, turned by `rotation` (degrees): the root, or none.
    std::function<Scene::Entity(const std::string& path, const Blueprint::Vec3Value& position,
                                   const Blueprint::Vec3Value& rotation)>
        spawn;
    /// Removes an object (and what is under it) once every script has run this frame, so a script
    /// may destroy itself; it hears `destroyed` first.
    std::function<void(Scene::Entity)> destroy;
    /// Seconds of game time since play began (stops while paused, follows the time scale).
    std::function<double()> time;
    /// This frame's game seconds.
    std::function<double()> delta;
    /// Puts `child` under `parent` (none: back at the top of the level); false when it cannot be.
    std::function<bool(Scene::Entity child, Scene::Entity parent)> setParent;
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
    std::function<void(Scene::Entity target, const std::string& name, const std::string& payload)> emit;
    /// A one-shot sound (an audio file): at `position` when spatial, else heard everywhere.
    std::function<bool(const std::string& path, const Blueprint::Vec3Value& position, double volume, bool spatial)> playSound;
    /// Particles at once from `emitter`'s ParticleEmitter, on top of its rate.
    std::function<bool(Scene::Entity emitter, std::int64_t count)> burstParticles;
    /// Makes `camera` the view (the others stop being current).
    std::function<bool(Scene::Entity camera)> activateCamera;
    /// Shakes the view: `strength` metres at first, fading out over `seconds`.
    std::function<void(double strength, double seconds)> cameraShake;
    /// Where the view looks (unit), and where it is.
    std::function<Blueprint::Vec3Value()> cameraForward;
    std::function<Blueprint::Vec3Value()> cameraLocation;
    /// Plays the AnimationSequencer `player`'s clip named `clip` (its file's name, from its own list;
    /// empty: the one it has) from the start.
    std::function<bool(Scene::Entity player, const std::string& clip)> playAnimation;
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
class BlueprintObjectHost final : public Blueprint::UVScriptHost {
public:
    /// Finds the running script of an entity, for routing cross-node calls at it. Null when the
    /// entity runs no script. Empty on check-only hosts, whose calls then only reach built-ins.
    using InstanceResolver = std::function<Blueprint::ScriptInstance*(Scene::Entity)>;

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
    BlueprintObjectHost(Scene::IEntityManager& entityManager, const Input::IInputSystem* input,
                          Scene::Entity entity, Audio::IAudioSourceSystem* audioSources = nullptr,
                          Audio::IAudioSystem* audio = nullptr,
                          InstanceResolver resolver = InstanceResolver{},
                          Network::NetworkRpc* rpc = nullptr,
                          RpcResponseSink sink = RpcResponseSink{},
                          Network::NetworkLobbyClient* lobby = nullptr,
                          LobbyResultSink lobbySink = LobbyResultSink{},
                          LobbyRosterSink rosterSink = LobbyRosterSink{},
                          Network::NetworkRelay* relay = nullptr,
                          Network::NetworkSession* relaySession = nullptr) noexcept;
    ~BlueprintObjectHost() override;

    [[nodiscard]] std::optional<Blueprint::HostProperty> DescribeProperty(std::string_view name) const override;
    [[nodiscard]] std::optional<Blueprint::HostFunction> DescribeFunction(std::string_view name) const override;
    [[nodiscard]] std::optional<std::vector<Blueprint::Type>> DescribeEvent(std::string_view event) const override;

    [[nodiscard]] Blueprint::Value GetProperty(std::string_view name) override;
    void SetProperty(std::string_view name, const Blueprint::Value& value) override;
    [[nodiscard]] Blueprint::Value CallFunction(std::string_view name,
                                                      std::span<const Blueprint::Value> args) override;
    void CallMethod(Blueprint::ObjectRef target, std::string_view method,
                       std::span<const Blueprint::Value> args) override;
    void Print(std::string_view text) override;

    /// Makes a Widget Blueprint's widgets (its `.uvwbp` path) in the world, hidden until
    /// `ui.add_to_viewport` shows them: the root Canvas, or none when the file cannot be made.
    using WidgetFactory = std::function<Scene::Entity(const std::string& path)>;
    /// Without one, `ui.create` answers no object.
    void SetWidgetFactory(WidgetFactory factory) { m_widgetFactory = std::move(factory); }
    /// `ui.show_cursor(show)`: shows and frees the pointer for UI, or hides and captures it for the game.
    using CursorHandler = std::function<void(bool show)>;
    void SetCursorHandler(CursorHandler handler) { m_cursorHandler = std::move(handler); }
    /// The game's services (traces, spawning, the clock, saves, messages). Without them those calls fail closed.
    void SetServices(ScriptServices* services) noexcept { m_services = services; }

    /// Moves this script's timers on by `dt` seconds: the ids of those that ran out, in order, for
    /// the caller to raise `timer(id)`. A looping timer starts again; a one-shot one is gone.
    /// A life span (`world.set_life_span`) that ran out destroys the object instead.
    [[nodiscard]] std::vector<std::string> AdvanceTimers(double dt);
    [[nodiscard]] bool HasTimers() const noexcept { return !m_timers.empty(); }

private:
    struct Timer final {
        double remaining = 0.0;
        double period = 0.0;
        bool loop = false;
    };
    [[nodiscard]] Blueprint::Value TextAndMathCall(std::string_view name, std::span<const Blueprint::Value> args);
    [[nodiscard]] Blueprint::Value WorldCall(std::string_view name, std::span<const Blueprint::Value> args);
    /// The object a script reference names, or none when it names nothing alive.
    [[nodiscard]] Scene::Entity EntityOf(const Blueprint::Value& value) const;

    /// Widget Blueprints: `ui.*`. The widget named `name` under this object's Canvas (the whole
    /// world when this object is no widget); none when there is no such widget.
    [[nodiscard]] Scene::Entity FindWidget(std::string_view name) const;
    [[nodiscard]] Blueprint::Value WidgetCall(std::string_view name, std::span<const Blueprint::Value> args);

    struct StagedRpcCall final {
        std::string method;
        std::vector<Blueprint::Value> args;
    };

    [[nodiscard]] Blueprint::Value GameplayCall(std::string_view name, std::span<const Blueprint::Value> args);
    [[nodiscard]] Blueprint::Value RpcCall(std::string_view name,
                                               std::span<const Blueprint::Value> args);
    [[nodiscard]] std::uint32_t NextStagingId() noexcept;

    enum class LobbyCallKind : std::uint8_t { List, Create, Join, Leave, Roster };

    struct LobbyRoomDesc final {
        std::uint32_t id = 0U;
        std::string name;
        std::uint32_t players = 0U;
        std::uint32_t maxPlayers = 0U;
    };

    struct LobbyCallState final {
        LobbyCallKind kind = LobbyCallKind::List;
        bool done = false;
        bool ok = false;
        bool settled = false;
        std::string error;
        std::vector<LobbyRoomDesc> rooms;
        std::uint32_t room = 0U;
        std::vector<std::string> roster;
        bool left = false;
    };

    [[nodiscard]] Blueprint::Value LobbyCall(std::string_view name,
                                                 std::span<const Blueprint::Value> args);
    [[nodiscard]] std::uint32_t StartLobbyCall(LobbyCallKind kind);
    [[nodiscard]] Blueprint::Value FinishLobbyCall(std::uint32_t id);
    [[nodiscard]] std::uint32_t NextLobbyCallId() noexcept;
    [[nodiscard]] Blueprint::Value RelayCall(std::string_view name,
                                                 std::span<const Blueprint::Value> args);

    Scene::IEntityManager& m_entityManager;
    const Input::IInputSystem* m_input = nullptr;
    Scene::Entity m_entity = Scene::kInvalidEntity;
    Audio::IAudioSourceSystem* m_audioSources = nullptr;
    Audio::IAudioSystem* m_audio = nullptr;
    InstanceResolver m_resolver;
    Network::NetworkRpc* m_rpc = nullptr;
    RpcResponseSink m_sink;
    std::map<std::uint32_t, StagedRpcCall> m_stagedRpc;
    std::set<std::uint32_t> m_pendingRpc;
    std::uint32_t m_nextStagingId = 1U;
    Network::NetworkLobbyClient* m_lobby = nullptr;
    LobbyResultSink m_lobbySink;
    LobbyRosterSink m_rosterSink;
    std::map<std::uint32_t, LobbyCallState> m_lobbyCalls;
    std::uint32_t m_nextLobbyCall = 1U;
    Network::NetworkRelay* m_relay = nullptr;
    Network::NetworkSession* m_relaySession = nullptr;
    WidgetFactory m_widgetFactory;
    CursorHandler m_cursorHandler;
    ScriptServices* m_services = nullptr;
    std::map<std::string, Timer> m_timers;
    /// The last trace's result, read by `trace.location/normal/object/distance`.
    ScriptServices::TraceHit m_lastTrace;
};

} // namespace UVE::Core
