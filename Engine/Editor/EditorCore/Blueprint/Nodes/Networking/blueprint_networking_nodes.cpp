// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "blueprint_networking_nodes.h"

namespace UVE::Blueprint::Nodes::Networking {

std::span<const NetworkingNodeSpec> GetNetworkingBlueprintNodes() noexcept {
    using PT = NetworkingPinType;
    using Kind = NetworkingNodeKind;
    using Pin = NetworkingPinSpec;

    static const std::vector<NetworkingNodeSpec> kNodes = {
        // ---- RPC (delegates to Engine/Runtime/Networking/ NetworkRpc)
        {"net_rpc_begin", "RPC Begin", "Networking|RPC", "rpc network call",
         "Stages an RPC call on Engine/Runtime/Networking/ NetworkRpc", Kind::Pure,
         {Pin{"Method", PT::String, "Ping"}}, {Pin{"Call Id", PT::Int, ""}}, "rpc.begin({0})"},
        {"net_rpc_push_int", "RPC Push Int", "Networking|RPC", "rpc arg int",
         "Appends an integer argument to a staged RPC call", Kind::Action,
         {Pin{"Call Id", PT::Int, "0"}, Pin{"Value", PT::Int, "0"}}, {}, "rpc.push_int({1}, {2})"},
        {"net_rpc_push_float", "RPC Push Float", "Networking|RPC", "rpc arg float",
         "Appends a float argument to a staged RPC call", Kind::Action,
         {Pin{"Call Id", PT::Int, "0"}, Pin{"Value", PT::Float, "0.0"}}, {}, "rpc.push_float({1}, {2})"},
        {"net_rpc_push_str", "RPC Push String", "Networking|RPC", "rpc arg string",
         "Appends a string argument to a staged RPC call", Kind::Action,
         {Pin{"Call Id", PT::Int, "0"}, Pin{"Value", PT::String, ""}}, {}, "rpc.push_str({1}, {2})"},
        {"net_rpc_push_bool", "RPC Push Bool", "Networking|RPC", "rpc arg bool",
         "Appends a bool argument to a staged RPC call", Kind::Action,
         {Pin{"Call Id", PT::Int, "0"}, Pin{"Value", PT::Bool, "false"}}, {}, "rpc.push_bool({1}, {2})"},
        {"net_rpc_push_vec3", "RPC Push Vector", "Networking|RPC", "rpc arg vec3",
         "Appends a vector argument to a staged RPC call", Kind::Action,
         {Pin{"Call Id", PT::Int, "0"}, Pin{"Value", PT::Vector, "0,0,0"}}, {}, "rpc.push_vec3({1}, {2})"},
        {"net_rpc_send", "RPC Send", "Networking|RPC", "rpc dispatch network",
         "Sends a staged RPC call through NetworkRpc", Kind::Action,
         {Pin{"Call Id", PT::Int, "0"}}, {}, "rpc.send({1})"},
        {"net_rpc_cancel", "RPC Cancel", "Networking|RPC", "rpc cancel abort",
         "Cancels an in-flight RPC call on NetworkRpc", Kind::Action,
         {Pin{"Call Id", PT::Int, "0"}}, {}, "rpc.cancel({1})"},
        {"net_rpc_result_kind", "RPC Result Kind", "Networking|RPC", "rpc result type",
         "Returns the result value kind of a completed RPC call", Kind::Pure,
         {Pin{"Call Id", PT::Int, "0"}}, {Pin{"Kind", PT::String, ""}}, "rpc.result_kind({0})"},
        {"net_rpc_result_int", "RPC Result Int", "Networking|RPC", "rpc return int",
         "Reads an integer result from a completed RPC call", Kind::Pure,
         {Pin{"Call Id", PT::Int, "0"}}, {Pin{"Value", PT::Int, ""}}, "rpc.result_int({0})"},
        {"net_rpc_result_str", "RPC Result String", "Networking|RPC", "rpc return string",
         "Reads a string result from a completed RPC call", Kind::Pure,
         {Pin{"Call Id", PT::Int, "0"}}, {Pin{"Value", PT::String, ""}}, "rpc.result_str({0})"},

        // ---- Lobby (delegates to Engine/Runtime/Networking/ NetworkLobbyClient)
        {"net_lobby_list", "Lobby List Rooms", "Networking|Lobby", "lobby rooms matchmaking",
         "Requests the active room list from NetworkLobbyClient", Kind::Action,
         {}, {}, "lobby.list()"},
        {"net_lobby_create", "Lobby Create Room", "Networking|Lobby", "lobby host room",
         "Creates a lobby room via NetworkLobbyClient", Kind::Action,
         {Pin{"Room Name", PT::String, "Match"}, Pin{"Map", PT::String, "Default"}, Pin{"Max Players", PT::Int, "8"}},
         {}, "lobby.create({1}, {2}, {3})"},
        {"net_lobby_join", "Lobby Join Room", "Networking|Lobby", "lobby join connect",
         "Joins a lobby room via NetworkLobbyClient", Kind::Action,
         {Pin{"Room Id", PT::Int, "1"}, Pin{"Player Name", PT::String, "Player"}},
         {}, "lobby.join({1}, {2})"},
        {"net_lobby_leave", "Lobby Leave Room", "Networking|Lobby", "lobby leave disconnect",
         "Leaves the current lobby room via NetworkLobbyClient", Kind::Action,
         {}, {}, "lobby.leave()"},
        {"net_lobby_room", "Get Lobby Room", "Networking|Lobby", "lobby room id",
         "Returns the current lobby room id from NetworkLobbyClient", Kind::Pure,
         {}, {Pin{"Room Id", PT::Int, ""}}, "lobby.room()"},
        {"net_lobby_roster_count", "Get Lobby Roster Count", "Networking|Lobby", "lobby players count",
         "Returns the number of players in the current lobby room", Kind::Pure,
         {}, {Pin{"Count", PT::Int, ""}}, "lobby.roster_count()"},
        {"net_lobby_roster_name", "Get Lobby Roster Name", "Networking|Lobby", "lobby player name",
         "Returns a player name from the current lobby roster", Kind::Pure,
         {Pin{"Index", PT::Int, "0"}}, {Pin{"Name", PT::String, ""}}, "lobby.roster_name({0})"},

        // ---- Relay / Session (delegates to Engine/Runtime/Networking/ NetworkRelay)
        {"net_relay_peer_count", "Get Relay Peer Count", "Networking|Relay", "relay peers count",
         "Returns the connected peer count from NetworkRelay", Kind::Pure,
         {}, {Pin{"Count", PT::Int, ""}}, "relay.peer_count()"},
        {"net_relay_peer_room", "Get Relay Peer Room", "Networking|Relay", "relay peer room",
         "Returns the room id assigned to a peer on NetworkRelay", Kind::Pure,
         {Pin{"Peer Id", PT::Int, "0"}}, {Pin{"Room Id", PT::Int, ""}}, "relay.room({0})"},
        {"net_relay_peer_state", "Get Relay Peer State", "Networking|Relay", "relay peer status",
         "Returns the connection state of a peer on NetworkRelay", Kind::Pure,
         {Pin{"Peer Id", PT::Int, "0"}}, {Pin{"State", PT::String, ""}}, "relay.peer_state({0})"},
        {"net_relay_set_room", "Set Relay Peer Room", "Networking|Relay", "relay assign room",
         "Assigns a peer to a relay room on NetworkRelay", Kind::Action,
         {Pin{"Peer Id", PT::Int, "0"}, Pin{"Room Id", PT::Int, "0"}}, {}, "relay.set_room({1}, {2})"},
        {"net_relay_set_interest", "Set Relay Interest", "Networking|Relay", "relay interest relevancy",
         "Sets peer-to-peer interest relevancy on NetworkRelay", Kind::Action,
         {Pin{"Peer A", PT::Int, "0"}, Pin{"Peer B", PT::Int, "0"}, Pin{"Interested", PT::Bool, "true"}},
         {}, "relay.set_interest({1}, {2}, {3})"},
    };
    return kNodes;
}

} // namespace UVE::Blueprint::Nodes::Networking
