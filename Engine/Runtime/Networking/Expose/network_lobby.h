// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
#pragma once
//
// Scope: matchmaking over direct sessions - a room registry on the server, a typed facade on
// the client, RPC between them. Clients list, create, join, and leave rooms; the server
// notifies survivors on every roster change by calling back into them (RPC is symmetric, so
// push needs no subscription protocol). One room per client at a time (joining moves you),
// empty rooms vanish, names are unique per room. Mutations count into stats, queries don't.
// The lobby rides its own direct sessions, never the relay: broadcast would spam every
// lobby call to every player, so control plane and data plane stay separate until relay
// rooms bind them (a named follow-up, not a missing piece of this one).
//
#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <span>
#include <string>
#include <vector>

#include "network_rpc.h"
#include "network_session.h"

namespace UVE::Network {

/// Identifies one lobby client on the server; 0 is invalid. Monotonic, never reused.
using LobbyClientId = std::uint32_t;
inline constexpr LobbyClientId kInvalidLobbyClientId = 0U;
/// Identifies one room; 0 means no room. Monotonic, never reused.
using LobbyRoomId = std::uint32_t;
inline constexpr LobbyRoomId kInvalidLobbyRoomId = 0U;

inline constexpr const char* kLobbyListMethod = "lobby.list";
inline constexpr const char* kLobbyCreateMethod = "lobby.create";
inline constexpr const char* kLobbyJoinMethod = "lobby.join";
inline constexpr const char* kLobbyLeaveMethod = "lobby.leave";
inline constexpr const char* kLobbyRosterMethod = "lobby.roster";
inline constexpr const char* kLobbyRosterChangedMethod = "lobby.rosterChanged";

/// One room as the list sees it: identity, name, and how full it is.
struct LobbyRoomInfo final {
    LobbyRoomId id = kInvalidLobbyRoomId;
    std::string name;
    std::uint32_t players = 0U;
    std::uint32_t maxPlayers = 0U;

    [[nodiscard]] bool operator==(const LobbyRoomInfo&) const noexcept = default;
};

struct LobbyStats final {
    std::uint64_t roomsCreated = 0U;
    std::uint64_t roomsRemoved = 0U;
    std::uint64_t createsRejected = 0U;
    std::uint64_t joinsAccepted = 0U;
    std::uint64_t joinsRejected = 0U;
    std::uint64_t leavesAccepted = 0U;
    std::uint64_t leavesRejected = 0U;
    std::uint64_t notificationsSent = 0U;
    std::uint64_t notificationsDropped = 0U;

    [[nodiscard]] bool operator==(const LobbyStats&) const noexcept = default;
};

/// Authoritative room registry behind per-client RPC peers. Owns its server sessions (they
/// join by move); Update pumps every peer, and each peer serves the five lobby methods
/// out of the shared registry. Roster changes push to survivors through lobby.rosterChanged
/// calls, fire-and-forget - a client without the handler still joins and leaves fine, it
/// just never hears the news. Move-only (it owns its peers), and moves rebind every
/// peer's handlers to the new home; single-threaded like everything below it.
class NetworkLobby final {
public:
    NetworkLobby() noexcept = default;
    NetworkLobby(const NetworkLobby&) = delete;
    NetworkLobby& operator=(const NetworkLobby&) = delete;
    // Moves rebind every peer's handlers to the new home: the handlers call into the
    // registry, so a stale capture would serve a moved-from husk.
    NetworkLobby(NetworkLobby&& other);
    NetworkLobby& operator=(NetworkLobby&& other);

    /// Adds a server-role session bound to its client endpoint, returning its client id.
    /// Anything else idles: the lobby pumps what it is given, and only a server session
    /// answers a client's Hello. Hands out ids from 1, monotonically, never reused.
    [[nodiscard]] LobbyClientId AddClient(NetworkSession&& session);
    /// Drops a client: leaves its room first (survivors are notified, counts as a leave),
    /// then forgets the session. Returns false when the id is unknown (or already removed -
    /// ids never come back, so a second removal always fails).
    [[nodiscard]] bool RemoveClient(LobbyClientId id) noexcept;
    /// Pumps every client's RPC peer in client-id order. The app polls ClientRpc for dead
    /// sessions and removes them - the lobby never drops a peer by surprise.
    void Update(std::uint64_t nowMs);

    /// The client's RPC peer for state checks and hang-ups, or nullptr when unknown. The
    /// lobby keeps ownership - borrow, never move out.
    [[nodiscard]] NetworkRpc* ClientRpc(LobbyClientId id) noexcept;
    [[nodiscard]] const NetworkRpc* ClientRpc(LobbyClientId id) const noexcept;
    /// The room a client is in, or 0 when the id is unknown or the client roams roomless.
    /// The relay binding reads this to tag peers with their lobby room.
    [[nodiscard]] LobbyRoomId ClientRoom(LobbyClientId id) const noexcept;
    [[nodiscard]] LobbyStats Stats() const noexcept { return m_stats; }
    [[nodiscard]] std::size_t PeerCount() const noexcept { return m_clients.size(); }
    [[nodiscard]] std::size_t RoomCount() const noexcept { return m_rooms.size(); }

private:
    struct LobbyRoom final {
        LobbyRoomId id = kInvalidLobbyRoomId;
        std::string name;
        std::uint32_t maxPlayers = 0U;
        // Member client ids to player names, in member-id order (which is join order: ids
        // are monotonic, so the roster reads oldest member first).
        std::map<LobbyClientId, std::string> members;
    };

    [[nodiscard]] RpcOutcome HandleList(
        std::span<const Blueprint::Value> args) const;
    [[nodiscard]] RpcOutcome HandleCreate(LobbyClientId caller,
                                                std::span<const Blueprint::Value> args);
    [[nodiscard]] RpcOutcome HandleJoin(LobbyClientId caller,
                                              std::span<const Blueprint::Value> args);
    [[nodiscard]] RpcOutcome HandleLeave(LobbyClientId caller,
                                               std::span<const Blueprint::Value> args);
    [[nodiscard]] RpcOutcome HandleRoster(
        std::span<const Blueprint::Value> args) const;
    /// Leaves the caller's room, notifying survivors and removing the room when emptied.
    /// False when the caller was in no room.
    bool LeaveRoom(LobbyClientId caller);
    /// Calls lobby.rosterChanged on every member of `roomId` except `exceptClient`.
    void NotifyRoom(LobbyRoomId roomId, LobbyClientId exceptClient);
    /// (Re)binds the five lobby handlers on `peer`, unregistering first so moves rebind.
    void RegisterPeerHandlers(NetworkRpc& peer, LobbyClientId id);
    /// Rebinds every peer's handlers after a move.
    void RebindPeerHandlers();

    // Registry first, peers last: peers die first, so no handler outlives the registry.
    std::map<LobbyRoomId, LobbyRoom> m_rooms;
    std::map<LobbyClientId, LobbyRoomId> m_clientRoom;
    LobbyClientId m_nextClientId = kInvalidLobbyClientId + 1U;
    LobbyRoomId m_nextRoomId = kInvalidLobbyRoomId + 1U;
    LobbyStats m_stats;
    std::map<LobbyClientId, NetworkRpc> m_clients;
};

/// Typed lobby call completions: the value plus an error string, empty on success. Server
/// errors pass through verbatim; transport failures and malformed replies read as errors
/// too, so a callback always fires exactly once - the facade never strands one.
using LobbyRoomsCallback =
    std::function<void(std::vector<LobbyRoomInfo>, const std::string&)>;
using LobbyRoomCallback = std::function<void(LobbyRoomId, const std::string&)>;
using LobbyRosterCallback = std::function<void(std::vector<std::string>, const std::string&)>;
using LobbyLeaveCallback = std::function<void(bool, const std::string&)>;
/// Roster pushes: the room plus its member names in member-id order.
using LobbyRosterHandler =
    std::function<void(LobbyRoomId, const std::vector<std::string>&)>;

/// The client's typed view of the lobby: list, create, join, leave, and roster queries as
/// plain calls, with the current room and roster cached locally. Owns its session (lobby
/// rides direct sessions, so multiplexing buys nothing here); Update pumps the RPC peer.
/// Move-only (it owns its peer), and safe to move: the handler state lives behind a
/// stable address that moves with the facade. Single-threaded like everything below it.
class NetworkLobbyClient final {
public:
    explicit NetworkLobbyClient(NetworkSession&& session);

    /// Lists every room. Empty callbacks are dropped - there is nowhere to deliver to.
    void ListRooms(LobbyRoomsCallback callback);
    /// Creates `roomName` (up to `maxPlayers`) and joins it as `playerName`, leaving any
    /// current room first. Answers the new room id.
    void CreateRoom(const std::string& roomName, const std::string& playerName,
                       std::uint32_t maxPlayers, LobbyRoomCallback callback);
    /// Joins `roomId` as `playerName`, leaving any current room first. Answers the room's
    /// roster, newcomer included.
    void JoinRoom(LobbyRoomId roomId, const std::string& playerName,
                     LobbyRosterCallback callback);
    /// Leaves the current room. Answers true; errors when in no room.
    void LeaveRoom(LobbyLeaveCallback callback);
    /// Queries any room's roster without joining it.
    void QueryRoster(LobbyRoomId roomId, LobbyRosterCallback callback);
    /// Gives up on every in-flight call: their callbacks never fire (a late response lands
    /// stale instead), and the roster handler is untouched. Answers how many were
    /// abandoned. Teardown semantics: scene changes call this rather than strand calls on a
    /// dying listener.
    [[nodiscard]] std::size_t CancelPending();
    /// Sets the roster-push handler; empty clears it. Fires on every rosterChanged for the
    /// current room - pushes for other rooms are impossible from an honest server (it only
    /// notifies members), and ignored when they arrive anyway.
    void SetRosterHandler(LobbyRosterHandler handler);
    /// Pumps the RPC peer (and the session under it).
    void Update(std::uint64_t nowMs);

    /// The owned session: handshake, disconnect, and state live here, not duplicated.
    [[nodiscard]] NetworkSession& Session() noexcept { return m_rpc.Session(); }
    [[nodiscard]] const NetworkSession& Session() const noexcept {
        return m_rpc.Session();
    }
    [[nodiscard]] RpcStats Stats() const noexcept { return m_rpc.Stats(); }
    /// The room the last create/join landed in, or 0 when in none.
    [[nodiscard]] LobbyRoomId CurrentRoom() const noexcept {
        return m_state->currentRoom;
    }
    [[nodiscard]] std::vector<std::string> CachedRoster() const {
        return m_state->cachedRoster;
    }

private:
    // Handler state behind a stable address: both the rosterChanged handler and in-flight
    // response callbacks hold this, so moves (a vector growing under live facades) never
    // strand an update on a moved-from husk. Never actually shared - one state per facade,
    // moved along with it.
    struct ClientState final {
        LobbyRosterHandler handler;
        LobbyRoomId currentRoom = kInvalidLobbyRoomId;
        std::vector<std::string> cachedRoster;
        std::set<RpcCallId> pending;
    };

    [[nodiscard]] static RpcOutcome
    HandleRosterChanged(ClientState& state, std::span<const Blueprint::Value> args);

    NetworkRpc m_rpc;
    std::shared_ptr<ClientState> m_state;
};

} // namespace UVE::Network
