// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "network_lobby.h"

#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "blueprint_value.h"

namespace UVE::Network {
namespace {

// Reads an int64 value as a u32 within [low, high]; false on wrong type or out of range.
[[nodiscard]] bool AsUint32(const Blueprint::Value& value, const std::int64_t low,
                               const std::int64_t high, std::uint32_t& out) {
    const std::int64_t* number = std::get_if<std::int64_t>(&value);
    if (number == nullptr || *number < low || *number > high) {
        return false;
    }
    out = static_cast<std::uint32_t>(*number);
    return true;
}

// Reads a collection value's items; false unless the value holds that kind.
[[nodiscard]] bool AsCollectionItems(
    const Blueprint::Value& value, const Blueprint::CollectionValue::Kind kind,
    const std::vector<Blueprint::Value>*& items) {
    const auto* collection =
        std::get_if<std::shared_ptr<const Blueprint::CollectionValue>>(&value);
    if (collection == nullptr || *collection == nullptr || (*collection)->kind != kind) {
        return false;
    }
    items = &(*collection)->items;
    return true;
}

[[nodiscard]] bool ParseRoster(const Blueprint::Value& value,
                                  std::vector<std::string>& roster) {
    const std::vector<Blueprint::Value>* items = nullptr;
    if (!AsCollectionItems(value, Blueprint::CollectionValue::Kind::List, items)) {
        return false;
    }
    std::vector<std::string> parsed;
    for (const Blueprint::Value& item : *items) {
        const std::string* name = std::get_if<std::string>(&item);
        if (name == nullptr) {
            return false;
        }
        parsed.push_back(*name);
    }
    roster = std::move(parsed);
    return true;
}

// One room descriptor: exactly the four known keys, any order, duplicates refused. Strict
// both ways - a skewed descriptor fails loud instead of half-parsing.
[[nodiscard]] bool ParseRoomList(const Blueprint::Value& value,
                                    std::vector<LobbyRoomInfo>& rooms) {
    const std::vector<Blueprint::Value>* items = nullptr;
    if (!AsCollectionItems(value, Blueprint::CollectionValue::Kind::List, items)) {
        return false;
    }
    std::vector<LobbyRoomInfo> parsed;
    for (const Blueprint::Value& entry : *items) {
        const std::vector<Blueprint::Value>* pairs = nullptr;
        if (!AsCollectionItems(entry, Blueprint::CollectionValue::Kind::Map, pairs) ||
            pairs->size() != 8U) {
            return false;
        }
        LobbyRoomInfo info;
        bool hasId = false;
        bool hasName = false;
        bool hasPlayers = false;
        bool hasMax = false;
        for (std::size_t i = 0U; i < pairs->size(); i += 2U) {
            const std::string* key = std::get_if<std::string>(&(*pairs)[i]);
            if (key == nullptr) {
                return false;
            }
            if (*key == "id") {
                if (hasId || !AsUint32((*pairs)[i + 1U], 1LL, 4294967295LL, info.id)) {
                    return false;
                }
                hasId = true;
            } else if (*key == "name") {
                const std::string* name = std::get_if<std::string>(&(*pairs)[i + 1U]);
                if (hasName || name == nullptr) {
                    return false;
                }
                info.name = *name;
                hasName = true;
            } else if (*key == "players") {
                if (hasPlayers ||
                    !AsUint32((*pairs)[i + 1U], 0LL, 4294967295LL, info.players)) {
                    return false;
                }
                hasPlayers = true;
            } else if (*key == "max") {
                if (hasMax || !AsUint32((*pairs)[i + 1U], 1LL, 4294967295LL, info.maxPlayers)) {
                    return false;
                }
                hasMax = true;
            } else {
                return false;
            }
        }
        if (!hasId || !hasName || !hasPlayers || !hasMax) {
            return false;
        }
        parsed.push_back(std::move(info));
    }
    rooms = std::move(parsed);
    return true;
}

[[nodiscard]] Blueprint::Value RoomInfoValue(const LobbyRoomId id, const std::string& name,
                                                 const std::uint32_t players,
                                                 const std::uint32_t maxPlayers) {
    return Blueprint::MakeMapValue(
        {Blueprint::Value{std::string{"id"}}, Blueprint::Value{static_cast<std::int64_t>(id)},
         Blueprint::Value{std::string{"name"}}, Blueprint::Value{name},
         Blueprint::Value{std::string{"players"}},
         Blueprint::Value{static_cast<std::int64_t>(players)},
         Blueprint::Value{std::string{"max"}},
         Blueprint::Value{static_cast<std::int64_t>(maxPlayers)}});
}

[[nodiscard]] Blueprint::Value RosterValue(
    const std::map<LobbyClientId, std::string>& members) {
    std::vector<Blueprint::Value> names;
    for (const auto& member : members) {
        names.push_back(Blueprint::Value{member.second});
    }
    return Blueprint::MakeListValue(std::move(names));
}

} // namespace

LobbyClientId NetworkLobby::AddClient(NetworkSession&& session) {
    const LobbyClientId id = m_nextClientId++;
    if (m_nextClientId == kInvalidLobbyClientId) {
        ++m_nextClientId;
    }
    NetworkRpc rpc(std::move(session));
    RegisterPeerHandlers(rpc, id);
    m_clients.emplace(id, std::move(rpc));
    return id;
}

bool NetworkLobby::RemoveClient(const LobbyClientId id) noexcept {
    const auto peer = m_clients.find(id);
    if (peer == m_clients.end()) {
        return false;
    }
    // Kicked with notice: leaving counts and survivors hear it, like any other leave.
    LeaveRoom(id);
    m_clientRoom.erase(id);
    m_clients.erase(peer);
    return true;
}

NetworkLobby::NetworkLobby(NetworkLobby&& other)
    : m_rooms(std::move(other.m_rooms)),
      m_clientRoom(std::move(other.m_clientRoom)),
      m_nextClientId(other.m_nextClientId),
      m_nextRoomId(other.m_nextRoomId),
      m_stats(other.m_stats),
      m_clients(std::move(other.m_clients)) {
    RebindPeerHandlers();
}

NetworkLobby& NetworkLobby::operator=(NetworkLobby&& other) {
    if (this == &other) {
        return *this;
    }
    m_rooms = std::move(other.m_rooms);
    m_clientRoom = std::move(other.m_clientRoom);
    m_nextClientId = other.m_nextClientId;
    m_nextRoomId = other.m_nextRoomId;
    m_stats = other.m_stats;
    m_clients = std::move(other.m_clients);
    RebindPeerHandlers();
    return *this;
}

void NetworkLobby::RegisterPeerHandlers(NetworkRpc& peer,
                                               const LobbyClientId id) {
    static_cast<void>(peer.UnregisterMethod(kLobbyListMethod));
    static_cast<void>(peer.UnregisterMethod(kLobbyCreateMethod));
    static_cast<void>(peer.UnregisterMethod(kLobbyJoinMethod));
    static_cast<void>(peer.UnregisterMethod(kLobbyLeaveMethod));
    static_cast<void>(peer.UnregisterMethod(kLobbyRosterMethod));
    static_cast<void>(peer.RegisterMethod(kLobbyListMethod,
                                             [this](std::span<const Blueprint::Value> args) {
                                                 return HandleList(args);
                                             }));
    static_cast<void>(peer.RegisterMethod(
        kLobbyCreateMethod, [this, id](std::span<const Blueprint::Value> args) {
            return HandleCreate(id, args);
        }));
    static_cast<void>(peer.RegisterMethod(
        kLobbyJoinMethod, [this, id](std::span<const Blueprint::Value> args) {
            return HandleJoin(id, args);
        }));
    static_cast<void>(peer.RegisterMethod(
        kLobbyLeaveMethod, [this, id](std::span<const Blueprint::Value> args) {
            return HandleLeave(id, args);
        }));
    static_cast<void>(peer.RegisterMethod(kLobbyRosterMethod,
                                             [this](std::span<const Blueprint::Value> args) {
                                                 return HandleRoster(args);
                                             }));
}

void NetworkLobby::RebindPeerHandlers() {
    for (auto& peer : m_clients) {
        RegisterPeerHandlers(peer.second, peer.first);
    }
}

void NetworkLobby::Update(const std::uint64_t nowMs) {
    for (auto& peer : m_clients) {
        peer.second.Update(nowMs);
    }
}

NetworkRpc* NetworkLobby::ClientRpc(const LobbyClientId id) noexcept {
    const auto peer = m_clients.find(id);
    if (peer == m_clients.end()) {
        return nullptr;
    }
    return &peer->second;
}

const NetworkRpc* NetworkLobby::ClientRpc(
    const LobbyClientId id) const noexcept {
    const auto peer = m_clients.find(id);
    if (peer == m_clients.end()) {
        return nullptr;
    }
    return &peer->second;
}

LobbyRoomId NetworkLobby::ClientRoom(const LobbyClientId id) const noexcept {
    const auto current = m_clientRoom.find(id);
    if (current == m_clientRoom.end()) {
        return kInvalidLobbyRoomId;
    }
    return current->second;
}

RpcOutcome NetworkLobby::HandleList(
    const std::span<const Blueprint::Value> args) const {
    if (!args.empty()) {
        return RpcError("lobby.list takes no arguments");
    }
    std::vector<Blueprint::Value> rooms;
    for (const auto& [id, room] : m_rooms) {
        rooms.push_back(RoomInfoValue(id, room.name,
                                         static_cast<std::uint32_t>(room.members.size()),
                                         room.maxPlayers));
    }
    return RpcOk(Blueprint::MakeListValue(std::move(rooms)));
}

RpcOutcome NetworkLobby::HandleCreate(const LobbyClientId caller,
                                               const std::span<const Blueprint::Value> args) {
    if (args.size() != 3U) {
        ++m_stats.createsRejected;
        return RpcError("lobby.create takes (name, player, max)");
    }
    const std::string* name = std::get_if<std::string>(&args[0]);
    const std::string* player = std::get_if<std::string>(&args[1]);
    const std::int64_t* maxArg = std::get_if<std::int64_t>(&args[2]);
    if (name == nullptr || player == nullptr || maxArg == nullptr) {
        ++m_stats.createsRejected;
        return RpcError("lobby.create takes (name, player, max)");
    }
    if (name->empty()) {
        ++m_stats.createsRejected;
        return RpcError("room name is empty");
    }
    if (player->empty()) {
        ++m_stats.createsRejected;
        return RpcError("player name is empty");
    }
    if (*maxArg < 1LL || *maxArg > 4294967295LL) {
        ++m_stats.createsRejected;
        return RpcError("max players must be at least 1");
    }
    // Validated before mutating: a refused create never kicks you from your room.
    LeaveRoom(caller);
    const LobbyRoomId id = m_nextRoomId++;
    if (m_nextRoomId == kInvalidLobbyRoomId) {
        ++m_nextRoomId;
    }
    LobbyRoom room;
    room.id = id;
    room.name = *name;
    room.maxPlayers = static_cast<std::uint32_t>(*maxArg);
    room.members.emplace(caller, *player);
    m_rooms.emplace(id, std::move(room));
    m_clientRoom[caller] = id;
    ++m_stats.roomsCreated;
    return RpcOk(Blueprint::Value{static_cast<std::int64_t>(id)});
}

RpcOutcome NetworkLobby::HandleJoin(const LobbyClientId caller,
                                             const std::span<const Blueprint::Value> args) {
    if (args.size() != 2U) {
        ++m_stats.joinsRejected;
        return RpcError("lobby.join takes (room id, player)");
    }
    const std::int64_t* roomArg = std::get_if<std::int64_t>(&args[0]);
    const std::string* player = std::get_if<std::string>(&args[1]);
    if (roomArg == nullptr || player == nullptr) {
        ++m_stats.joinsRejected;
        return RpcError("lobby.join takes (room id, player)");
    }
    std::uint32_t roomId = kInvalidLobbyRoomId;
    const auto room = AsUint32(args[0], 1LL, 4294967295LL, roomId)
                          ? m_rooms.find(roomId)
                          : m_rooms.end();
    if (room == m_rooms.end()) {
        ++m_stats.joinsRejected;
        return RpcError("unknown room '" + std::to_string(*roomArg) + "'");
    }
    if (player->empty()) {
        ++m_stats.joinsRejected;
        return RpcError("player name is empty");
    }
    // Rejoining your own room just ensures the name: the same name is a quiet success, a new
    // one renames (taken-checked against the others) and notifies.
    const auto current = m_clientRoom.find(caller);
    if (current != m_clientRoom.end() && current->second == roomId) {
        const std::string& mine = room->second.members.find(caller)->second;
        if (mine == *player) {
            ++m_stats.joinsAccepted;
            return RpcOk(RosterValue(room->second.members));
        }
        for (const auto& member : room->second.members) {
            if (member.first != caller && member.second == *player) {
                ++m_stats.joinsRejected;
                return RpcError("name '" + *player + "' is taken in room '" +
                                   std::to_string(roomId) + "'");
            }
        }
        room->second.members[caller] = *player;
        ++m_stats.joinsAccepted;
        NotifyRoom(roomId, caller);
        return RpcOk(RosterValue(room->second.members));
    }
    for (const auto& member : room->second.members) {
        if (member.second == *player) {
            ++m_stats.joinsRejected;
            return RpcError("name '" + *player + "' is taken in room '" +
                               std::to_string(roomId) + "'");
        }
    }
    if (room->second.members.size() >= room->second.maxPlayers) {
        ++m_stats.joinsRejected;
        return RpcError("room '" + std::to_string(roomId) + "' is full");
    }
    LeaveRoom(caller);
    room->second.members.emplace(caller, *player);
    m_clientRoom[caller] = roomId;
    ++m_stats.joinsAccepted;
    NotifyRoom(roomId, caller);
    return RpcOk(RosterValue(room->second.members));
}

RpcOutcome NetworkLobby::HandleLeave(const LobbyClientId caller,
                                              const std::span<const Blueprint::Value> args) {
    if (!args.empty()) {
        ++m_stats.leavesRejected;
        return RpcError("lobby.leave takes no arguments");
    }
    if (!LeaveRoom(caller)) {
        ++m_stats.leavesRejected;
        return RpcError("not in a room");
    }
    return RpcOk(Blueprint::Value{true});
}

RpcOutcome NetworkLobby::HandleRoster(
    const std::span<const Blueprint::Value> args) const {
    if (args.size() != 1U || std::get_if<std::int64_t>(&args[0]) == nullptr) {
        return RpcError("lobby.roster takes (room id)");
    }
    const std::int64_t roomArg = std::get<std::int64_t>(args[0]);
    std::uint32_t roomId = kInvalidLobbyRoomId;
    const auto room = AsUint32(args[0], 1LL, 4294967295LL, roomId) ? m_rooms.find(roomId)
                                                                      : m_rooms.end();
    if (room == m_rooms.end()) {
        return RpcError("unknown room '" + std::to_string(roomArg) + "'");
    }
    return RpcOk(RosterValue(room->second.members));
}

bool NetworkLobby::LeaveRoom(const LobbyClientId caller) {
    const auto current = m_clientRoom.find(caller);
    if (current == m_clientRoom.end()) {
        return false;
    }
    const LobbyRoomId roomId = current->second;
    m_clientRoom.erase(current);
    const auto room = m_rooms.find(roomId);
    if (room == m_rooms.end()) {
        ++m_stats.leavesAccepted;
        return true;
    }
    room->second.members.erase(caller);
    ++m_stats.leavesAccepted;
    NotifyRoom(roomId, caller);
    if (room->second.members.empty()) {
        m_rooms.erase(room);
        ++m_stats.roomsRemoved;
    }
    return true;
}

void NetworkLobby::NotifyRoom(const LobbyRoomId roomId,
                                    const LobbyClientId exceptClient) {
    const auto room = m_rooms.find(roomId);
    if (room == m_rooms.end()) {
        return;
    }
    const Blueprint::Value roster = RosterValue(room->second.members);
    for (const auto& member : room->second.members) {
        if (member.first == exceptClient) {
            continue;
        }
        const auto peer = m_clients.find(member.first);
        if (peer == m_clients.end()) {
            continue;
        }
        // Fire-and-forget: the outcome is ignored (a member without the handler is
        // tolerated, not punished), and retention self-evicts past the cap.
        const RpcCallId call = peer->second.Call(
            kLobbyRosterChangedMethod,
            {Blueprint::Value{static_cast<std::int64_t>(roomId)}, roster},
            [](RpcCallId, const RpcOutcome&) {});
        if (call == kInvalidRpcCallId) {
            ++m_stats.notificationsDropped;
        } else {
            ++m_stats.notificationsSent;
        }
    }
}

NetworkLobbyClient::NetworkLobbyClient(NetworkSession&& session)
    : m_rpc(std::move(session)), m_state(std::make_shared<ClientState>()) {
    static_cast<void>(m_rpc.RegisterMethod(
        kLobbyRosterChangedMethod,
        [state = m_state](std::span<const Blueprint::Value> args) {
            return HandleRosterChanged(*state, args);
        }));
}

void NetworkLobbyClient::ListRooms(LobbyRoomsCallback callback) {
    if (callback == nullptr) {
        return;
    }
    const RpcCallId call = m_rpc.Call(
        kLobbyListMethod, {},
        [state = m_state, callback](const RpcCallId callId,
                                    const RpcOutcome& outcome) {
            state->pending.erase(callId);
            if (!outcome.ok) {
                callback({}, outcome.error);
                return;
            }
            std::vector<LobbyRoomInfo> rooms;
            if (!ParseRoomList(outcome.value, rooms)) {
                callback({}, "bad lobby.list reply");
                return;
            }
            callback(std::move(rooms), {});
        });
    if (call == kInvalidRpcCallId) {
        callback({}, "lobby.list failed to send");
    } else {
        m_state->pending.insert(call);
    }
}

void NetworkLobbyClient::CreateRoom(const std::string& roomName,
                                          const std::string& playerName,
                                          const std::uint32_t maxPlayers,
                                          LobbyRoomCallback callback) {
    if (callback == nullptr) {
        return;
    }
    const RpcCallId call = m_rpc.Call(
        kLobbyCreateMethod,
        {Blueprint::Value{roomName}, Blueprint::Value{playerName},
         Blueprint::Value{static_cast<std::int64_t>(maxPlayers)}},
        [state = m_state, callback, playerName](const RpcCallId callId,
                                                const RpcOutcome& outcome) {
            state->pending.erase(callId);
            if (!outcome.ok) {
                callback(kInvalidLobbyRoomId, outcome.error);
                return;
            }
            LobbyRoomId id = kInvalidLobbyRoomId;
            if (!AsUint32(outcome.value, 1LL, 4294967295LL, id)) {
                callback(kInvalidLobbyRoomId, "bad lobby.create reply");
                return;
            }
            state->currentRoom = id;
            state->cachedRoster = {playerName};
            callback(id, {});
        });
    if (call == kInvalidRpcCallId) {
        callback(kInvalidLobbyRoomId, "lobby.create failed to send");
    } else {
        m_state->pending.insert(call);
    }
}

void NetworkLobbyClient::JoinRoom(const LobbyRoomId roomId,
                                        const std::string& playerName,
                                        LobbyRosterCallback callback) {
    if (callback == nullptr) {
        return;
    }
    const RpcCallId call = m_rpc.Call(
        kLobbyJoinMethod,
        {Blueprint::Value{static_cast<std::int64_t>(roomId)}, Blueprint::Value{playerName}},
        [state = m_state, callback, roomId](const RpcCallId callId,
                                            const RpcOutcome& outcome) {
            state->pending.erase(callId);
            if (!outcome.ok) {
                callback({}, outcome.error);
                return;
            }
            std::vector<std::string> roster;
            if (!ParseRoster(outcome.value, roster)) {
                callback({}, "bad lobby.join reply");
                return;
            }
            state->currentRoom = roomId;
            state->cachedRoster = roster;
            callback(std::move(roster), {});
        });
    if (call == kInvalidRpcCallId) {
        callback({}, "lobby.join failed to send");
    } else {
        m_state->pending.insert(call);
    }
}

void NetworkLobbyClient::LeaveRoom(LobbyLeaveCallback callback) {
    if (callback == nullptr) {
        return;
    }
    const RpcCallId call = m_rpc.Call(
        kLobbyLeaveMethod, {},
        [state = m_state, callback](const RpcCallId callId,
                                    const RpcOutcome& outcome) {
            state->pending.erase(callId);
            if (!outcome.ok) {
                callback(false, outcome.error);
                return;
            }
            const bool* left = std::get_if<bool>(&outcome.value);
            if (left == nullptr || !*left) {
                callback(false, "bad lobby.leave reply");
                return;
            }
            state->currentRoom = kInvalidLobbyRoomId;
            state->cachedRoster.clear();
            callback(true, {});
        });
    if (call == kInvalidRpcCallId) {
        callback(false, "lobby.leave failed to send");
    } else {
        m_state->pending.insert(call);
    }
}

void NetworkLobbyClient::QueryRoster(const LobbyRoomId roomId,
                                            LobbyRosterCallback callback) {
    if (callback == nullptr) {
        return;
    }
    const RpcCallId call = m_rpc.Call(
        kLobbyRosterMethod, {Blueprint::Value{static_cast<std::int64_t>(roomId)}},
        [state = m_state, callback](const RpcCallId callId,
                                    const RpcOutcome& outcome) {
            state->pending.erase(callId);
            if (!outcome.ok) {
                callback({}, outcome.error);
                return;
            }
            std::vector<std::string> roster;
            if (!ParseRoster(outcome.value, roster)) {
                callback({}, "bad lobby.roster reply");
                return;
            }
            callback(std::move(roster), {});
        });
    if (call == kInvalidRpcCallId) {
        callback({}, "lobby.roster failed to send");
    } else {
        m_state->pending.insert(call);
    }
}

std::size_t NetworkLobbyClient::CancelPending() {
    for (const RpcCallId id : m_state->pending) {
        static_cast<void>(m_rpc.Cancel(id));
    }
    const std::size_t abandoned = m_state->pending.size();
    m_state->pending.clear();
    return abandoned;
}

void NetworkLobbyClient::SetRosterHandler(LobbyRosterHandler handler) {
    m_state->handler = std::move(handler);
}

void NetworkLobbyClient::Update(const std::uint64_t nowMs) {
    m_rpc.Update(nowMs);
}

RpcOutcome NetworkLobbyClient::HandleRosterChanged(
    ClientState& state, const std::span<const Blueprint::Value> args) {
    if (args.size() != 2U) {
        return RpcError("lobby.rosterChanged takes (room id, roster)");
    }
    LobbyRoomId roomId = kInvalidLobbyRoomId;
    std::vector<std::string> roster;
    if (!AsUint32(args[0], 1LL, 4294967295LL, roomId) || !ParseRoster(args[1], roster)) {
        return RpcError("lobby.rosterChanged takes (room id, roster)");
    }
    if (roomId != state.currentRoom) {
        return RpcOk(Blueprint::Value{});
    }
    state.cachedRoster = roster;
    if (state.handler != nullptr) {
        state.handler(roomId, roster);
    }
    return RpcOk(Blueprint::Value{});
}

} // namespace UVE::Network
