// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "vector3.h"
#include "network_packet_direction.h"
#include "network_prediction.h"
#include "network_replication.h"
#include "network_rpc.h"
#include "network_session.h"
#include "blueprint_value.h"

namespace UVE::Network {

// The feature channel a captured datagram belongs to. One session carries one feature, so the
// watcher always knows this out of band - a replication snapshot has no magic bytes, and guessing
// the channel from the payload would misread one feature's bytes as another's. Unknown skips the
// payload decode and reports the datagram's session layer plus its byte count.
enum class InspectedChannel : std::uint8_t { Unknown, Replication, Rpc, Prediction, Shot };


// One replication snapshot: the declared entry count plus every (entity, position) pair.
// Deltas decode into the same view - their entries are absolute positions too - with
// `isDelta` set so the reader knows the sender's quiet entities were simply omitted.
struct ReplicationSnapshotView final {
    bool isDelta = false;
    std::uint16_t entryCount = 0U;
    std::vector<std::pair<NetworkEntityId, Math::Vector3>> entries;
};

// One ownership-control frame: a handoff request, accept, or deny for one entity.
enum class OwnershipMessageKind : std::uint8_t { Request = 0x01, Accept = 0x02, Deny = 0x03 };

struct OwnershipTransferView final {
    OwnershipMessageKind kind = OwnershipMessageKind::Request;
    NetworkEntityId netId = 0U;
};

// One property entry: which entity, which property, and the decoded value.
struct ReplicationPropertyEntry final {
    NetworkEntityId netId = 0U;
    ReplicationPropertyId propId = 0U;
    Blueprint::Value value;
};

// One property frame: the declared entry count plus every (entity, property, value) triple.
struct ReplicationPropertiesView final {
    std::uint16_t propertyCount = 0U;
    std::vector<ReplicationPropertyEntry> entries;
};

// One RPC call frame: which call, which method, and the decoded argument values.
struct RpcCallView final {
    RpcCallId callId = kInvalidRpcCallId;
    std::string method;
    std::vector<Blueprint::Value> args;
};

// One RPC response frame: which call it answers, whether it succeeded, and the result value -
// or the error text when it failed. Error payloads are always strings on the wire, like the
// RPC peer itself demands.
struct RpcResponseView final {
    RpcCallId callId = kInvalidRpcCallId;
    bool ok = false;
    Blueprint::Value value;
    std::string error;
};

// One prediction input entry: whose tick, which pawn, the dt it stepped with, and the intent.
struct PredictionInputEntry final {
    PredictionTick tick = kInvalidPredictionTick;
    PredictionPawn pawn = kDefaultPredictionPawn;
    float dtSeconds = 0.0F;
    PredictedInput input;
};

// One prediction input message: the entry window the client sent, oldest first.
struct PredictionInputsView final {
    std::vector<PredictionInputEntry> window;
};

// One prediction state message: the tick, the pawn, and the simulated state for it.
struct PredictionStateView final {
    PredictionTick tick = kInvalidPredictionTick;
    PredictionPawn pawn = kDefaultPredictionPawn;
    PredictedState state;
};

// One shot intent: who fired, which flight, and the view tick plus fire fraction the
// server resolves. Mirrors ShotIntent field for field (the decode reuses the real
// codec, so the mirror cannot drift from the protocol).
struct ShotIntentView final {
    PredictionPawn pawn = kDefaultPredictionPawn;
    std::uint32_t seq = 0U;
    PredictionTick viewTick = kInvalidPredictionTick;
    float fraction = 0.0F;
};

// The decoded payload of a data datagram. Empty unless the session kind is data and the
// selected channel's decoder accepted the payload byte for byte.
using InspectedData =
    std::variant<std::monostate, ReplicationSnapshotView, OwnershipTransferView,
                 ReplicationPropertiesView, RpcCallView, RpcResponseView,
                 PredictionInputsView, PredictionStateView,
                 ShotIntentView>;

// One captured session datagram, decoded. The session layer always decodes - every datagram
// starts with its tag - and the data layer decodes when the channel is known. `malformed` is
// set when no decoder accepted the bytes: an empty datagram, a foreign tag, a hello too
// short to hold its version, or a data payload the selected channel rejects. A hello that
// carries its version but not its token still decodes - the summary calls it truncated - and
// an opaque payload (unknown channel) is never malformed: skipping a decode is honest,
// guessing one would not be.
struct InspectedPacket final {
    std::uint64_t timestampMs = 0U;
    PacketDirection direction = PacketDirection::Outbound;
    InspectedChannel channel = InspectedChannel::Unknown;
    // "hello", "welcome", "bye", "data", "empty", or "foreign".
    std::string sessionKind = "empty";
    // The hello's version (bytes 1-2) when at least those survived, and its token (bytes 3-10)
    // when the full eleven did. Empty for every other session kind.
    std::optional<std::uint16_t> helloVersion;
    std::optional<std::uint64_t> helloToken;
    // Bytes past the hello/welcome/bye the session ignores. Always zero for data - the payload
    // past the tag is the whole point there.
    std::size_t trailingBytes = 0U;
    // The multiplexed channel id (the byte past the data tag) when the watcher said the
    // session is multiplexed and the datagram carried one. Empty for every other datagram -
    // the handshake never carries channels.
    std::optional<std::uint8_t> channelId;
    // "" for non-data datagrams; otherwise "snapshot", "delta", "ownership", "properties",
    // "call", "response", "state", "opaque" (unknown channel), or "malformed" (the channel's
    // decoder refused it).
    std::string dataKind;
    InspectedData data;
    // The one-line human view: "snapshot 3 entries", "properties 2 entries",
    // "call #7 \"add\" (20, 22)",
    // "hello v2 token 0x000000000000007b". Always set, even when malformed.
    std::string summary;
    bool malformed = false;
    // The datagram as captured, tag byte included, for hexdumps and replays.
    std::vector<std::uint8_t> raw;
};

// Decodes one captured session datagram the way the peer would read it: the tag byte first,
// then - for data - the selected channel's framing, mirrored rule for rule (strict sizes, exact
// ends, the same rejections). Pure: the same bytes, direction, timestamp, channel, and
// multiplexing flag always give the same record. `multiplexed` must match the watched
// session: when set, the byte past the data tag is the channel id (reported, then stripped
// before the feature decode), and a tag with no channel byte is malformed, like the session
// reads it.
[[nodiscard]] InspectedPacket InspectPacket(const std::vector<std::uint8_t>& datagram,
                                                 PacketDirection direction,
                                                 std::uint64_t timestampMs,
                                                 InspectedChannel channel = InspectedChannel::Unknown,
                                                 bool multiplexed = false);

// Renders one log line for an inspected packet: "> [rpc] call #7 \"add\" (20, 22)". ">" marks
// outbound, "<" inbound; the bracket names the channel ("wire" when unknown).
// A multiplexed datagram names its id too: "> [replication ch2] snapshot 1 entry".
[[nodiscard]] std::string DescribePacket(const InspectedPacket& packet);

} // namespace UVE::Network
