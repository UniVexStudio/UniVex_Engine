// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/network/network_lobby_uve.h"

#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "uve/uvscript/uvscript_value_uve.h"

namespace UVE::Network {
namespace {

// Reads an int64 value as a u32 within [low, high]; false on wrong type or out of range.
[[nodiscard]] bool AsUint32UVE(const UVScript::ValueUVE& value, const std::int64_t low,
                               const std::int64_t high, std::uint32_t& out) {
    const std::int64_t* number = std::get_if<std::int64_t>(&value);
    if (number == nullptr || *number < low || *number > high) {
        return false;
    }
    out = static_cast<std::uint32_t>(*number);
    return true;
}

// Reads a collection value's items; false unless the value holds that kind.
[[nodiscard]] bool AsCollectionItemsUVE(
    const UVScript::ValueUVE& value, const UVScript::CollectionValueUVE::KindUVE kind,
    const std::vector<UVScript::ValueUVE>*& items) {
    const auto* collection =
        std::get_if<std::shared_ptr<const UVScript::CollectionValueUVE>>(&value);
    if (collection == nullptr || *collection == nullptr || (*collection)->kind != kind) {
        return false;
    }
    items = &(*collection)->items;
    return true;
}

[[nodiscard]] bool ParseRosterUVE(const UVScript::ValueUVE& value,
                                  std::vector<std::string>& roster) {
    const std::vector<UVScript::ValueUVE>* items = nullptr;
    if (!AsCollectionItemsUVE(value, UVScript::CollectionValueUVE::KindUVE::List, items)) {
        return false;
    }
    std::vector<std::string> parsed;
    for (const UVScript::ValueUVE& item : *items) {
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
[[nodiscard]] bool ParseRoomListUVE(const UVScript::ValueUVE& value,
                                    std::vector<LobbyRoomInfoUVE>& rooms) {
    const std::vector<UVScript::ValueUVE>* items = nullptr;
    if (!AsCollectionItemsUVE(value, UVScript::CollectionValueUVE::KindUVE::List, items)) {
        return false;
    }
    std::vector<LobbyRoomInfoUVE> parsed;
    for (const UVScript::ValueUVE& entry : *items) {
        const std::vector<UVScript::ValueUVE>* pairs = nullptr;
        if (!AsCollectionItemsUVE(entry, UVScript::CollectionValueUVE::KindUVE::Map, pairs) ||
            pairs->size() != 8U) {
            return false;
        }
        LobbyRoomInfoUVE info;
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
                if (hasId || !AsUint32UVE((*pairs)[i + 1U], 1LL, 4294967295LL, info.id)) {
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
                    !AsUint32UVE((*pairs)[i + 1U], 0LL, 4294967295LL, info.players)) {
                    return false;
                }
                hasPlayers = true;
            } else if (*key == "max") {
                if (hasMax || !AsUint32UVE((*pairs)[i + 1U], 1LL, 4294967295LL, info.maxPlayers)) {
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

[[nodiscard]] UVScript::ValueUVE RoomInfoValueUVE(const LobbyRoomIdUVE id, const std::string& name,
                                                 const std::uint32_t players,
                                                 const std::uint32_t maxPlayers) {
    return UVScript::MakeMapValueUVE(
        {UVScript::ValueUVE{std::string{"id"}}, UVScript::ValueUVE{static_cast<std::int64_t>(id)},
         UVScript::ValueUVE{std::string{"name"}}, UVScript::ValueUVE{name},
         UVScript::ValueUVE{std::string{"players"}},
         UVScript::ValueUVE{static_cast<std::int64_t>(players)},
         UVScript::ValueUVE{std::string{"max"}},
         UVScript::ValueUVE{static_cast<std::int64_t>(maxPlayers)}});
}

[[nodiscard]] UVScript::ValueUVE RosterValueUVE(
    const std::map<LobbyClientIdUVE, std::string>& members) {
    std::vector<UVScript::ValueUVE> names;
    for (const auto& member : members) {
        names.push_back(UVScript::ValueUVE{member.second});
    }
    return UVScript::MakeListValueUVE(std::move(names));
}

} // namespace

LobbyClientIdUVE NetworkLobbyUVE::AddClientUVE(NetworkSessionUVE&& session) {
    const LobbyClientIdUVE id = m_nextClientId++;
    if (m_nextClientId == kInvalidLobbyClientIdUVE) {
        ++m_nextClientId;
    }
    NetworkRpcUVE rpc(std::move(session));
    RegisterPeerHandlersUVE(rpc, id);
    m_clients.emplace(id, std::move(rpc));
    return id;
}

bool NetworkLobbyUVE::RemoveClientUVE(const LobbyClientIdUVE id) noexcept {
    const auto peer = m_clients.find(id);
    if (peer == m_clients.end()) {
        return false;
    }
    // Kicked with notice: leaving counts and survivors hear it, like any other leave.
    LeaveRoomUVE(id);
    m_clientRoom.erase(id);
    m_clients.erase(peer);
    return true;
}

NetworkLobbyUVE::NetworkLobbyUVE(NetworkLobbyUVE&& other)
    : m_rooms(std::move(other.m_rooms)),
      m_clientRoom(std::move(other.m_clientRoom)),
      m_nextClientId(other.m_nextClientId),
      m_nextRoomId(other.m_nextRoomId),
      m_stats(other.m_stats),
      m_clients(std::move(other.m_clients)) {
    RebindPeerHandlersUVE();
}

NetworkLobbyUVE& NetworkLobbyUVE::operator=(NetworkLobbyUVE&& other) {
    if (this == &other) {
        return *this;
    }
    m_rooms = std::move(other.m_rooms);
    m_clientRoom = std::move(other.m_clientRoom);
    m_nextClientId = other.m_nextClientId;
    m_nextRoomId = other.m_nextRoomId;
    m_stats = other.m_stats;
    m_clients = std::move(other.m_clients);
    RebindPeerHandlersUVE();
    return *this;
}

void NetworkLobbyUVE::RegisterPeerHandlersUVE(NetworkRpcUVE& peer,
                                               const LobbyClientIdUVE id) {
    static_cast<void>(peer.UnregisterMethodUVE(kLobbyListMethodUVE));
    static_cast<void>(peer.UnregisterMethodUVE(kLobbyCreateMethodUVE));
    static_cast<void>(peer.UnregisterMethodUVE(kLobbyJoinMethodUVE));
    static_cast<void>(peer.UnregisterMethodUVE(kLobbyLeaveMethodUVE));
    static_cast<void>(peer.UnregisterMethodUVE(kLobbyRosterMethodUVE));
    static_cast<void>(peer.RegisterMethodUVE(kLobbyListMethodUVE,
                                             [this](std::span<const UVScript::ValueUVE> args) {
                                                 return HandleListUVE(args);
                                             }));
    static_cast<void>(peer.RegisterMethodUVE(
        kLobbyCreateMethodUVE, [this, id](std::span<const UVScript::ValueUVE> args) {
            return HandleCreateUVE(id, args);
        }));
    static_cast<void>(peer.RegisterMethodUVE(
        kLobbyJoinMethodUVE, [this, id](std::span<const UVScript::ValueUVE> args) {
            return HandleJoinUVE(id, args);
        }));
    static_cast<void>(peer.RegisterMethodUVE(
        kLobbyLeaveMethodUVE, [this, id](std::span<const UVScript::ValueUVE> args) {
            return HandleLeaveUVE(id, args);
        }));
    static_cast<void>(peer.RegisterMethodUVE(kLobbyRosterMethodUVE,
                                             [this](std::span<const UVScript::ValueUVE> args) {
                                                 return HandleRosterUVE(args);
                                             }));
}

void NetworkLobbyUVE::RebindPeerHandlersUVE() {
    for (auto& peer : m_clients) {
        RegisterPeerHandlersUVE(peer.second, peer.first);
    }
}

void NetworkLobbyUVE::UpdateUVE(const std::uint64_t nowMs) {
    for (auto& peer : m_clients) {
        peer.second.UpdateUVE(nowMs);
    }
}

NetworkRpcUVE* NetworkLobbyUVE::ClientRpcUVE(const LobbyClientIdUVE id) noexcept {
    const auto peer = m_clients.find(id);
    if (peer == m_clients.end()) {
        return nullptr;
    }
    return &peer->second;
}

const NetworkRpcUVE* NetworkLobbyUVE::ClientRpcUVE(
    const LobbyClientIdUVE id) const noexcept {
    const auto peer = m_clients.find(id);
    if (peer == m_clients.end()) {
        return nullptr;
    }
    return &peer->second;
}

LobbyRoomIdUVE NetworkLobbyUVE::ClientRoomUVE(const LobbyClientIdUVE id) const noexcept {
    const auto current = m_clientRoom.find(id);
    if (current == m_clientRoom.end()) {
        return kInvalidLobbyRoomIdUVE;
    }
    return current->second;
}

RpcOutcomeUVE NetworkLobbyUVE::HandleListUVE(
    const std::span<const UVScript::ValueUVE> args) const {
    if (!args.empty()) {
        return RpcErrorUVE("lobby.list takes no arguments");
    }
    std::vector<UVScript::ValueUVE> rooms;
    for (const auto& [id, room] : m_rooms) {
        rooms.push_back(RoomInfoValueUVE(id, room.name,
                                         static_cast<std::uint32_t>(room.members.size()),
                                         room.maxPlayers));
    }
    return RpcOkUVE(UVScript::MakeListValueUVE(std::move(rooms)));
}

RpcOutcomeUVE NetworkLobbyUVE::HandleCreateUVE(const LobbyClientIdUVE caller,
                                               const std::span<const UVScript::ValueUVE> args) {
    if (args.size() != 3U) {
        ++m_stats.createsRejected;
        return RpcErrorUVE("lobby.create takes (name, player, max)");
    }
    const std::string* name = std::get_if<std::string>(&args[0]);
    const std::string* player = std::get_if<std::string>(&args[1]);
    const std::int64_t* maxArg = std::get_if<std::int64_t>(&args[2]);
    if (name == nullptr || player == nullptr || maxArg == nullptr) {
        ++m_stats.createsRejected;
        return RpcErrorUVE("lobby.create takes (name, player, max)");
    }
    if (name->empty()) {
        ++m_stats.createsRejected;
        return RpcErrorUVE("room name is empty");
    }
    if (player->empty()) {
        ++m_stats.createsRejected;
        return RpcErrorUVE("player name is empty");
    }
    if (*maxArg < 1LL || *maxArg > 4294967295LL) {
        ++m_stats.createsRejected;
        return RpcErrorUVE("max players must be at least 1");
    }
    // Validated before mutating: a refused create never kicks you from your room.
    LeaveRoomUVE(caller);
    const LobbyRoomIdUVE id = m_nextRoomId++;
    if (m_nextRoomId == kInvalidLobbyRoomIdUVE) {
        ++m_nextRoomId;
    }
    LobbyRoomUVE room;
    room.id = id;
    room.name = *name;
    room.maxPlayers = static_cast<std::uint32_t>(*maxArg);
    room.members.emplace(caller, *player);
    m_rooms.emplace(id, std::move(room));
    m_clientRoom[caller] = id;
    ++m_stats.roomsCreated;
    return RpcOkUVE(UVScript::ValueUVE{static_cast<std::int64_t>(id)});
}

RpcOutcomeUVE NetworkLobbyUVE::HandleJoinUVE(const LobbyClientIdUVE caller,
                                             const std::span<const UVScript::ValueUVE> args) {
    if (args.size() != 2U) {
        ++m_stats.joinsRejected;
        return RpcErrorUVE("lobby.join takes (room id, player)");
    }
    const std::int64_t* roomArg = std::get_if<std::int64_t>(&args[0]);
    const std::string* player = std::get_if<std::string>(&args[1]);
    if (roomArg == nullptr || player == nullptr) {
        ++m_stats.joinsRejected;
        return RpcErrorUVE("lobby.join takes (room id, player)");
    }
    std::uint32_t roomId = kInvalidLobbyRoomIdUVE;
    const auto room = AsUint32UVE(args[0], 1LL, 4294967295LL, roomId)
                          ? m_rooms.find(roomId)
                          : m_rooms.end();
    if (room == m_rooms.end()) {
        ++m_stats.joinsRejected;
        return RpcErrorUVE("unknown room '" + std::to_string(*roomArg) + "'");
    }
    if (player->empty()) {
        ++m_stats.joinsRejected;
        return RpcErrorUVE("player name is empty");
    }
    // Rejoining your own room just ensures the name: the same name is a quiet success, a new
    // one renames (taken-checked against the others) and notifies.
    const auto current = m_clientRoom.find(caller);
    if (current != m_clientRoom.end() && current->second == roomId) {
        const std::string& mine = room->second.members.find(caller)->second;
        if (mine == *player) {
            ++m_stats.joinsAccepted;
            return RpcOkUVE(RosterValueUVE(room->second.members));
        }
        for (const auto& member : room->second.members) {
            if (member.first != caller && member.second == *player) {
                ++m_stats.joinsRejected;
                return RpcErrorUVE("name '" + *player + "' is taken in room '" +
                                   std::to_string(roomId) + "'");
            }
        }
        room->second.members[caller] = *player;
        ++m_stats.joinsAccepted;
        NotifyRoomUVE(roomId, caller);
        return RpcOkUVE(RosterValueUVE(room->second.members));
    }
    for (const auto& member : room->second.members) {
        if (member.second == *player) {
            ++m_stats.joinsRejected;
            return RpcErrorUVE("name '" + *player + "' is taken in room '" +
                               std::to_string(roomId) + "'");
        }
    }
    if (room->second.members.size() >= room->second.maxPlayers) {
        ++m_stats.joinsRejected;
        return RpcErrorUVE("room '" + std::to_string(roomId) + "' is full");
    }
    LeaveRoomUVE(caller);
    room->second.members.emplace(caller, *player);
    m_clientRoom[caller] = roomId;
    ++m_stats.joinsAccepted;
    NotifyRoomUVE(roomId, caller);
    return RpcOkUVE(RosterValueUVE(room->second.members));
}

RpcOutcomeUVE NetworkLobbyUVE::HandleLeaveUVE(const LobbyClientIdUVE caller,
                                              const std::span<const UVScript::ValueUVE> args) {
    if (!args.empty()) {
        ++m_stats.leavesRejected;
        return RpcErrorUVE("lobby.leave takes no arguments");
    }
    if (!LeaveRoomUVE(caller)) {
        ++m_stats.leavesRejected;
        return RpcErrorUVE("not in a room");
    }
    return RpcOkUVE(UVScript::ValueUVE{true});
}

RpcOutcomeUVE NetworkLobbyUVE::HandleRosterUVE(
    const std::span<const UVScript::ValueUVE> args) const {
    if (args.size() != 1U || std::get_if<std::int64_t>(&args[0]) == nullptr) {
        return RpcErrorUVE("lobby.roster takes (room id)");
    }
    const std::int64_t roomArg = std::get<std::int64_t>(args[0]);
    std::uint32_t roomId = kInvalidLobbyRoomIdUVE;
    const auto room = AsUint32UVE(args[0], 1LL, 4294967295LL, roomId) ? m_rooms.find(roomId)
                                                                      : m_rooms.end();
    if (room == m_rooms.end()) {
        return RpcErrorUVE("unknown room '" + std::to_string(roomArg) + "'");
    }
    return RpcOkUVE(RosterValueUVE(room->second.members));
}

bool NetworkLobbyUVE::LeaveRoomUVE(const LobbyClientIdUVE caller) {
    const auto current = m_clientRoom.find(caller);
    if (current == m_clientRoom.end()) {
        return false;
    }
    const LobbyRoomIdUVE roomId = current->second;
    m_clientRoom.erase(current);
    const auto room = m_rooms.find(roomId);
    if (room == m_rooms.end()) {
        ++m_stats.leavesAccepted;
        return true;
    }
    room->second.members.erase(caller);
    ++m_stats.leavesAccepted;
    NotifyRoomUVE(roomId, caller);
    if (room->second.members.empty()) {
        m_rooms.erase(room);
        ++m_stats.roomsRemoved;
    }
    return true;
}

void NetworkLobbyUVE::NotifyRoomUVE(const LobbyRoomIdUVE roomId,
                                    const LobbyClientIdUVE exceptClient) {
    const auto room = m_rooms.find(roomId);
    if (room == m_rooms.end()) {
        return;
    }
    const UVScript::ValueUVE roster = RosterValueUVE(room->second.members);
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
        const RpcCallIdUVE call = peer->second.CallUVE(
            kLobbyRosterChangedMethodUVE,
            {UVScript::ValueUVE{static_cast<std::int64_t>(roomId)}, roster},
            [](RpcCallIdUVE, const RpcOutcomeUVE&) {});
        if (call == kInvalidRpcCallIdUVE) {
            ++m_stats.notificationsDropped;
        } else {
            ++m_stats.notificationsSent;
        }
    }
}

NetworkLobbyClientUVE::NetworkLobbyClientUVE(NetworkSessionUVE&& session)
    : m_rpc(std::move(session)), m_state(std::make_shared<ClientStateUVE>()) {
    static_cast<void>(m_rpc.RegisterMethodUVE(
        kLobbyRosterChangedMethodUVE,
        [state = m_state](std::span<const UVScript::ValueUVE> args) {
            return HandleRosterChangedUVE(*state, args);
        }));
}

void NetworkLobbyClientUVE::ListRoomsUVE(LobbyRoomsCallbackUVE callback) {
    if (callback == nullptr) {
        return;
    }
    const RpcCallIdUVE call = m_rpc.CallUVE(
        kLobbyListMethodUVE, {},
        [state = m_state, callback](const RpcCallIdUVE callId,
                                    const RpcOutcomeUVE& outcome) {
            state->pending.erase(callId);
            if (!outcome.ok) {
                callback({}, outcome.error);
                return;
            }
            std::vector<LobbyRoomInfoUVE> rooms;
            if (!ParseRoomListUVE(outcome.value, rooms)) {
                callback({}, "bad lobby.list reply");
                return;
            }
            callback(std::move(rooms), {});
        });
    if (call == kInvalidRpcCallIdUVE) {
        callback({}, "lobby.list failed to send");
    } else {
        m_state->pending.insert(call);
    }
}

void NetworkLobbyClientUVE::CreateRoomUVE(const std::string& roomName,
                                          const std::string& playerName,
                                          const std::uint32_t maxPlayers,
                                          LobbyRoomCallbackUVE callback) {
    if (callback == nullptr) {
        return;
    }
    const RpcCallIdUVE call = m_rpc.CallUVE(
        kLobbyCreateMethodUVE,
        {UVScript::ValueUVE{roomName}, UVScript::ValueUVE{playerName},
         UVScript::ValueUVE{static_cast<std::int64_t>(maxPlayers)}},
        [state = m_state, callback, playerName](const RpcCallIdUVE callId,
                                                const RpcOutcomeUVE& outcome) {
            state->pending.erase(callId);
            if (!outcome.ok) {
                callback(kInvalidLobbyRoomIdUVE, outcome.error);
                return;
            }
            LobbyRoomIdUVE id = kInvalidLobbyRoomIdUVE;
            if (!AsUint32UVE(outcome.value, 1LL, 4294967295LL, id)) {
                callback(kInvalidLobbyRoomIdUVE, "bad lobby.create reply");
                return;
            }
            state->currentRoom = id;
            state->cachedRoster = {playerName};
            callback(id, {});
        });
    if (call == kInvalidRpcCallIdUVE) {
        callback(kInvalidLobbyRoomIdUVE, "lobby.create failed to send");
    } else {
        m_state->pending.insert(call);
    }
}

void NetworkLobbyClientUVE::JoinRoomUVE(const LobbyRoomIdUVE roomId,
                                        const std::string& playerName,
                                        LobbyRosterCallbackUVE callback) {
    if (callback == nullptr) {
        return;
    }
    const RpcCallIdUVE call = m_rpc.CallUVE(
        kLobbyJoinMethodUVE,
        {UVScript::ValueUVE{static_cast<std::int64_t>(roomId)}, UVScript::ValueUVE{playerName}},
        [state = m_state, callback, roomId](const RpcCallIdUVE callId,
                                            const RpcOutcomeUVE& outcome) {
            state->pending.erase(callId);
            if (!outcome.ok) {
                callback({}, outcome.error);
                return;
            }
            std::vector<std::string> roster;
            if (!ParseRosterUVE(outcome.value, roster)) {
                callback({}, "bad lobby.join reply");
                return;
            }
            state->currentRoom = roomId;
            state->cachedRoster = roster;
            callback(std::move(roster), {});
        });
    if (call == kInvalidRpcCallIdUVE) {
        callback({}, "lobby.join failed to send");
    } else {
        m_state->pending.insert(call);
    }
}

void NetworkLobbyClientUVE::LeaveRoomUVE(LobbyLeaveCallbackUVE callback) {
    if (callback == nullptr) {
        return;
    }
    const RpcCallIdUVE call = m_rpc.CallUVE(
        kLobbyLeaveMethodUVE, {},
        [state = m_state, callback](const RpcCallIdUVE callId,
                                    const RpcOutcomeUVE& outcome) {
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
            state->currentRoom = kInvalidLobbyRoomIdUVE;
            state->cachedRoster.clear();
            callback(true, {});
        });
    if (call == kInvalidRpcCallIdUVE) {
        callback(false, "lobby.leave failed to send");
    } else {
        m_state->pending.insert(call);
    }
}

void NetworkLobbyClientUVE::QueryRosterUVE(const LobbyRoomIdUVE roomId,
                                            LobbyRosterCallbackUVE callback) {
    if (callback == nullptr) {
        return;
    }
    const RpcCallIdUVE call = m_rpc.CallUVE(
        kLobbyRosterMethodUVE, {UVScript::ValueUVE{static_cast<std::int64_t>(roomId)}},
        [state = m_state, callback](const RpcCallIdUVE callId,
                                    const RpcOutcomeUVE& outcome) {
            state->pending.erase(callId);
            if (!outcome.ok) {
                callback({}, outcome.error);
                return;
            }
            std::vector<std::string> roster;
            if (!ParseRosterUVE(outcome.value, roster)) {
                callback({}, "bad lobby.roster reply");
                return;
            }
            callback(std::move(roster), {});
        });
    if (call == kInvalidRpcCallIdUVE) {
        callback({}, "lobby.roster failed to send");
    } else {
        m_state->pending.insert(call);
    }
}

std::size_t NetworkLobbyClientUVE::CancelPendingUVE() {
    for (const RpcCallIdUVE id : m_state->pending) {
        static_cast<void>(m_rpc.CancelUVE(id));
    }
    const std::size_t abandoned = m_state->pending.size();
    m_state->pending.clear();
    return abandoned;
}

void NetworkLobbyClientUVE::SetRosterHandlerUVE(LobbyRosterHandlerUVE handler) {
    m_state->handler = std::move(handler);
}

void NetworkLobbyClientUVE::UpdateUVE(const std::uint64_t nowMs) {
    m_rpc.UpdateUVE(nowMs);
}

RpcOutcomeUVE NetworkLobbyClientUVE::HandleRosterChangedUVE(
    ClientStateUVE& state, const std::span<const UVScript::ValueUVE> args) {
    if (args.size() != 2U) {
        return RpcErrorUVE("lobby.rosterChanged takes (room id, roster)");
    }
    LobbyRoomIdUVE roomId = kInvalidLobbyRoomIdUVE;
    std::vector<std::string> roster;
    if (!AsUint32UVE(args[0], 1LL, 4294967295LL, roomId) || !ParseRosterUVE(args[1], roster)) {
        return RpcErrorUVE("lobby.rosterChanged takes (room id, roster)");
    }
    if (roomId != state.currentRoom) {
        return RpcOkUVE(UVScript::ValueUVE{});
    }
    state.cachedRoster = roster;
    if (state.handler != nullptr) {
        state.handler(roomId, roster);
    }
    return RpcOkUVE(UVScript::ValueUVE{});
}

} // namespace UVE::Network
