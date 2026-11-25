// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "network_inspector.h"
#include "network_shot.h"

#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "binary_buffer.h"
#include "blueprint_value.h"

namespace UVE::Network {
namespace {

// Every decoder below mirrors its feature's own framing rule for rule - strict sizes, exact
// ends, the same rejections - so the inspector shows what the peer would actually read, not
// what the bytes could generously mean. The mirror is deliberate: a dissector duplicates the
// protocols it reads, and the inspector tests run live traffic through every decoder on each
// build, so a codec that drifts breaks the build before it rots the tool.

[[nodiscard]] std::vector<std::byte> ToBytes(const std::vector<std::uint8_t>& in) {
    std::vector<std::byte> out;
    out.reserve(in.size());
    for (const std::uint8_t byte : in) {
        out.push_back(static_cast<std::byte>(byte));
    }
    return out;
}

[[nodiscard]] std::string Count(const std::size_t n, const char* one, const char* many) {
    return std::to_string(n) + (n == 1U ? one : many);
}

// One ownership-control frame: the marker, a request/accept/deny kind, and the entity -
// exactly 7 bytes, mirroring the replicator's strict read.
[[nodiscard]] std::optional<OwnershipTransferView> DecodeOwnership(
    const std::vector<std::uint8_t>& payload) {
    // The marker travels little-endian, like every multi-byte int: FE FF on the wire.
    if (payload.size() != 7U || payload[0] != 0xFEU || payload[1] != 0xFFU) {
        return std::nullopt;
    }
    if (payload[2] != static_cast<std::uint8_t>(OwnershipMessageKind::Request) &&
        payload[2] != static_cast<std::uint8_t>(OwnershipMessageKind::Accept) &&
        payload[2] != static_cast<std::uint8_t>(OwnershipMessageKind::Deny)) {
        return std::nullopt;
    }
    OwnershipTransferView view;
    view.kind = static_cast<OwnershipMessageKind>(payload[2]);
    view.netId = static_cast<NetworkEntityId>(payload[3]) |
                 (static_cast<NetworkEntityId>(payload[4]) << 8) |
                 (static_cast<NetworkEntityId>(payload[5]) << 16) |
                 (static_cast<NetworkEntityId>(payload[6]) << 24);
    return view;
}

// A u16 count plus that many (entity, position) pairs, and nothing else - the snapshot stands
// or falls as one message, like the replicator insists. A leading delta marker means a
// second u16 holds the count; the entries decode identically either way.
[[nodiscard]] std::optional<ReplicationSnapshotView> DecodeSnapshot(
    const std::vector<std::uint8_t>& payload) {
    const std::vector<std::byte> buffer = ToBytes(payload);
    std::size_t offset = 0U;
    std::uint16_t first = 0U;
    if (!Utilities::ReadUint16LeFromBuffer(buffer, offset, first)) {
        return std::nullopt;
    }
    const bool delta = first == kReplicationDeltaMarker;
    std::uint16_t entryCount = first;
    std::size_t headerBytes = sizeof(std::uint16_t);
    if (delta) {
        headerBytes += sizeof(std::uint16_t);
        if (!Utilities::ReadUint16LeFromBuffer(buffer, offset, entryCount)) {
            return std::nullopt;
        }
    }
    if (buffer.size() != headerBytes + static_cast<std::size_t>(entryCount) *
                                           (sizeof(NetworkEntityId) + 3U * sizeof(float))) {
        return std::nullopt;
    }
    ReplicationSnapshotView view;
    view.isDelta = delta;
    view.entryCount = entryCount;
    view.entries.reserve(entryCount);
    for (std::uint16_t i = 0U; i < entryCount; ++i) {
        NetworkEntityId netId = 0U;
        Math::Vector3 position{};
        if (!Utilities::ReadUint32LeFromBuffer(buffer, offset, netId) ||
            !Utilities::ReadFloatLeFromBuffer(buffer, offset, position.x) ||
            !Utilities::ReadFloatLeFromBuffer(buffer, offset, position.y) ||
            !Utilities::ReadFloatLeFromBuffer(buffer, offset, position.z)) {
            return std::nullopt;
        }
        view.entries.emplace_back(netId, position);
    }
    return view;
}

// RPC value tags, mirroring the frame codec: the inspector reads arguments and results with
// the same tags, the same depth cap, and the same all-or-nothing rule.
enum class InspectorValueTag : std::uint8_t {
    Nil = 0x00,
    Bool = 0x01,
    Int = 0x02,
    Float = 0x03,
    Str = 0x04,
    Vec3 = 0x05,
    List = 0x06,
    Map = 0x07,
    Tuple = 0x08
};

[[nodiscard]] std::optional<std::string> ReadInspectorString(const std::vector<std::byte>& in,
                                                                std::size_t& offset) {
    std::uint16_t length = 0U;
    if (!Utilities::ReadUint16LeFromBuffer(in, offset, length) || length > in.size() - offset) {
        return std::nullopt;
    }
    std::string text;
    if (length > 0U) {
        text.assign(reinterpret_cast<const char*>(&in[offset]), length);
        offset += length;
    }
    return text;
}

[[nodiscard]] std::optional<Blueprint::Value> ReadInspectorValue(
    const std::vector<std::byte>& in, std::size_t& offset, const std::size_t depth) {
    if (offset >= in.size()) {
        return std::nullopt;
    }
    const auto tag =
        static_cast<InspectorValueTag>(std::to_integer<std::uint8_t>(in[offset++]));
    switch (tag) {
        case InspectorValueTag::Nil:
            return Blueprint::Value{};
        case InspectorValueTag::Bool: {
            if (offset >= in.size()) {
                return std::nullopt;
            }
            const bool value = in[offset++] != static_cast<std::byte>(0x00);
            return Blueprint::Value{value};
        }
        case InspectorValueTag::Int: {
            std::uint64_t bits = 0U;
            if (!Utilities::ReadUint64LeFromBuffer(in, offset, bits)) {
                return std::nullopt;
            }
            return Blueprint::Value{static_cast<std::int64_t>(bits)};
        }
        case InspectorValueTag::Float: {
            std::uint64_t bits = 0U;
            if (!Utilities::ReadUint64LeFromBuffer(in, offset, bits)) {
                return std::nullopt;
            }
            return Blueprint::Value{std::bit_cast<double>(bits)};
        }
        case InspectorValueTag::Str: {
            std::optional<std::string> text = ReadInspectorString(in, offset);
            if (!text.has_value()) {
                return std::nullopt;
            }
            return Blueprint::Value{std::move(*text)};
        }
        case InspectorValueTag::Vec3: {
            std::uint64_t x = 0U, y = 0U, z = 0U;
            if (!Utilities::ReadUint64LeFromBuffer(in, offset, x) ||
                !Utilities::ReadUint64LeFromBuffer(in, offset, y) ||
                !Utilities::ReadUint64LeFromBuffer(in, offset, z)) {
                return std::nullopt;
            }
            return Blueprint::Value{Blueprint::Vec3Value{
                std::bit_cast<double>(x), std::bit_cast<double>(y), std::bit_cast<double>(z)}};
        }
        case InspectorValueTag::List:
        case InspectorValueTag::Tuple: {
            if (depth == kRpcMaximumValueDepth) {
                return std::nullopt;
            }
            std::uint16_t count = 0U;
            if (!Utilities::ReadUint16LeFromBuffer(in, offset, count)) {
                return std::nullopt;
            }
            std::vector<Blueprint::Value> items;
            for (std::uint16_t i = 0U; i < count; ++i) {
                std::optional<Blueprint::Value> item =
                    ReadInspectorValue(in, offset, depth + 1U);
                if (!item.has_value()) {
                    return std::nullopt;
                }
                items.push_back(std::move(*item));
            }
            if (tag == InspectorValueTag::List) {
                return Blueprint::MakeListValue(std::move(items));
            }
            return Blueprint::MakeTupleValue(std::move(items));
        }
        case InspectorValueTag::Map: {
            if (depth == kRpcMaximumValueDepth) {
                return std::nullopt;
            }
            std::uint16_t pairs = 0U;
            if (!Utilities::ReadUint16LeFromBuffer(in, offset, pairs)) {
                return std::nullopt;
            }
            std::vector<Blueprint::Value> items;
            for (std::uint16_t i = 0U; i < pairs; ++i) {
                std::optional<Blueprint::Value> key =
                    ReadInspectorValue(in, offset, depth + 1U);
                std::optional<Blueprint::Value> item =
                    ReadInspectorValue(in, offset, depth + 1U);
                if (!key.has_value() || !item.has_value()) {
                    return std::nullopt;
                }
                items.push_back(std::move(*key));
                items.push_back(std::move(*item));
            }
            return Blueprint::MakeMapValue(std::move(items));
        }
    }
    return std::nullopt;
}

// One property frame: the marker, a u16 count, then netId, property id, and a tagged value
// per entry - the inspector's own mirror reads the values, never the shared codec. The frame
// must end exactly where the last value does; a count of zero decodes (the sender never
// emits it, but the shape is strict either way).
[[nodiscard]] std::optional<ReplicationPropertiesView> DecodeProperties(
    const std::vector<std::uint8_t>& payload) {
    constexpr std::uint8_t kPropertyMarker0 = 0xFDU;
    constexpr std::uint8_t kPropertyMarker1 = 0xFFU;
    const std::vector<std::byte> in = ToBytes(payload);
    if (in.size() < 4U || std::to_integer<std::uint8_t>(in[0]) != kPropertyMarker0 ||
        std::to_integer<std::uint8_t>(in[1]) != kPropertyMarker1) {
        return std::nullopt;
    }
    std::size_t offset = 2U;
    std::uint16_t entryCount = 0U;
    if (!Utilities::ReadUint16LeFromBuffer(in, offset, entryCount)) {
        return std::nullopt;
    }
    ReplicationPropertiesView view;
    view.propertyCount = entryCount;
    for (std::uint16_t i = 0U; i < entryCount; ++i) {
        NetworkEntityId netId = 0U;
        if (!Utilities::ReadUint32LeFromBuffer(in, offset, netId) || offset >= in.size()) {
            return std::nullopt;
        }
        const ReplicationPropertyId propId =
            std::to_integer<ReplicationPropertyId>(in[offset++]);
        std::optional<Blueprint::Value> value = ReadInspectorValue(in, offset, 0U);
        if (!value.has_value()) {
            return std::nullopt;
        }
        view.entries.push_back(ReplicationPropertyEntry{netId, propId, std::move(*value)});
    }
    if (offset != in.size()) {
        return std::nullopt;
    }
    return view;
}

struct DecodedRpc final {
    bool isCall = true;
    RpcCallView call;
    RpcResponseView response;
};

// Magic, version, kind, id, then exactly the declared body - trailing bytes fail the frame,
// and an error payload that is not a string fails it too, like the RPC peer demands.
[[nodiscard]] std::optional<DecodedRpc> DecodeRpc(const std::vector<std::uint8_t>& payload) {
    constexpr std::uint8_t kMagic0 = static_cast<std::uint8_t>('R');
    constexpr std::uint8_t kMagic1 = static_cast<std::uint8_t>('C');
    constexpr std::uint8_t kCallKind = 0x01U;
    constexpr std::uint8_t kResponseKind = 0x02U;
    constexpr std::uint8_t kOkStatus = 0x00U;
    constexpr std::uint8_t kErrorStatus = 0x01U;
    const std::vector<std::byte> in = ToBytes(payload);
    if (in.size() < 8U || std::to_integer<std::uint8_t>(in[0]) != kMagic0 ||
        std::to_integer<std::uint8_t>(in[1]) != kMagic1) {
        return std::nullopt;
    }
    std::size_t offset = 2U;
    const std::uint8_t version = std::to_integer<std::uint8_t>(in[offset++]);
    const std::uint8_t kind = std::to_integer<std::uint8_t>(in[offset++]);
    RpcCallId callId = kInvalidRpcCallId;
    if (version != kRpcFrameVersion || (kind != kCallKind && kind != kResponseKind) ||
        !Utilities::ReadUint32LeFromBuffer(in, offset, callId)) {
        return std::nullopt;
    }
    DecodedRpc frame;
    if (kind == kCallKind) {
        std::optional<std::string> method = ReadInspectorString(in, offset);
        std::uint16_t argCount = 0U;
        if (!method.has_value() || !Utilities::ReadUint16LeFromBuffer(in, offset, argCount)) {
            return std::nullopt;
        }
        frame.call.callId = callId;
        frame.call.method = std::move(*method);
        for (std::uint16_t i = 0U; i < argCount; ++i) {
            std::optional<Blueprint::Value> arg = ReadInspectorValue(in, offset, 0U);
            if (!arg.has_value()) {
                return std::nullopt;
            }
            frame.call.args.push_back(std::move(*arg));
        }
    } else {
        if (offset >= in.size()) {
            return std::nullopt;
        }
        const std::uint8_t status = std::to_integer<std::uint8_t>(in[offset++]);
        if (status != kOkStatus && status != kErrorStatus) {
            return std::nullopt;
        }
        std::optional<Blueprint::Value> value = ReadInspectorValue(in, offset, 0U);
        if (!value.has_value()) {
            return std::nullopt;
        }
        frame.isCall = false;
        frame.response.callId = callId;
        frame.response.ok = status == kOkStatus;
        frame.response.value = std::move(*value);
        if (!frame.response.ok) {
            if (!std::holds_alternative<std::string>(frame.response.value)) {
                return std::nullopt;
            }
            frame.response.error = std::get<std::string>(frame.response.value);
        }
    }
    if (offset != in.size()) {
        return std::nullopt;
    }
    return frame;
}

// Prediction input entries: a tick that names a real tick, a pawn, five finite floats, and a
// jump flag that is 0 or 1 - the same gate the server applies before an input enters its
// buffer, dt within the second included.
[[nodiscard]] std::optional<PredictionInputsView> DecodePredictionInputs(
    const std::vector<std::uint8_t>& payload) {
    constexpr std::uint8_t kInputKind = 0x01U;
    constexpr std::size_t kEntryBytes = 30U;
    const std::vector<std::byte> buffer = ToBytes(payload);
    if (buffer.size() < 2U || std::to_integer<std::uint8_t>(buffer[0]) != kInputKind) {
        return std::nullopt;
    }
    const std::size_t count = static_cast<std::size_t>(std::to_integer<std::uint8_t>(buffer[1]));
    if (count == 0U || count > kPredictionMaximumInputsPerMessage ||
        buffer.size() != 2U + count * kEntryBytes) {
        return std::nullopt;
    }
    PredictionInputsView view;
    view.window.reserve(count);
    std::size_t offset = 2U;
    for (std::size_t i = 0U; i < count; ++i) {
        PredictionInputEntry entry;
        if (!Utilities::ReadUint64LeFromBuffer(buffer, offset, entry.tick) ||
            offset >= buffer.size()) {
            return std::nullopt;
        }
        entry.pawn = std::to_integer<std::uint8_t>(buffer[offset++]);
        if (!Utilities::ReadFloatLeFromBuffer(buffer, offset, entry.dtSeconds) ||
            !Utilities::ReadFloatLeFromBuffer(buffer, offset, entry.input.move.x) ||
            !Utilities::ReadFloatLeFromBuffer(buffer, offset, entry.input.move.y) ||
            !Utilities::ReadFloatLeFromBuffer(buffer, offset, entry.input.move.z) ||
            !Utilities::ReadFloatLeFromBuffer(buffer, offset, entry.input.rise) ||
            offset >= buffer.size()) {
            return std::nullopt;
        }
        const std::uint8_t jump = std::to_integer<std::uint8_t>(buffer[offset++]);
        if (entry.tick == kInvalidPredictionTick || jump > 0x01U ||
            !std::isfinite(entry.dtSeconds) || entry.dtSeconds <= 0.0F ||
            entry.dtSeconds > kPredictionMaximumTickDtSeconds ||
            !std::isfinite(entry.input.move.x) || !std::isfinite(entry.input.move.y) ||
            !std::isfinite(entry.input.move.z) || !std::isfinite(entry.input.rise)) {
            return std::nullopt;
        }
        entry.input.jumpPressed = jump == 0x01U;
        view.window.push_back(entry);
    }
    return view;
}

// Prediction states: exactly 39 bytes, a real tick, a pawn, six finite floats, and a
// grounded flag that is 0 or 1 - byte for byte what the client accepts before it reconciles.
[[nodiscard]] std::optional<PredictionStateView> DecodePredictionState(
    const std::vector<std::uint8_t>& payload) {
    constexpr std::uint8_t kStateKind = 0x02U;
    constexpr std::size_t kStateBytes = 39U;
    const std::vector<std::byte> buffer = ToBytes(payload);
    if (buffer.size() != kStateBytes ||
        std::to_integer<std::uint8_t>(buffer[0]) != kStateKind) {
        return std::nullopt;
    }
    std::size_t offset = 1U;
    PredictionStateView view;
    std::uint8_t grounded = 0U;
    if (!Utilities::ReadUint64LeFromBuffer(buffer, offset, view.tick) ||
        offset >= buffer.size()) {
        return std::nullopt;
    }
    view.pawn = std::to_integer<std::uint8_t>(buffer[offset++]);
    if (!Utilities::ReadFloatLeFromBuffer(buffer, offset, view.state.position.x) ||
        !Utilities::ReadFloatLeFromBuffer(buffer, offset, view.state.position.y) ||
        !Utilities::ReadFloatLeFromBuffer(buffer, offset, view.state.position.z) ||
        !Utilities::ReadFloatLeFromBuffer(buffer, offset, view.state.velocity.x) ||
        !Utilities::ReadFloatLeFromBuffer(buffer, offset, view.state.velocity.y) ||
        !Utilities::ReadFloatLeFromBuffer(buffer, offset, view.state.velocity.z) ||
        offset + 2U > buffer.size()) {
        return std::nullopt;
    }
    grounded = std::to_integer<std::uint8_t>(buffer[offset++]);
    if (!Utilities::ReadUint32LeFromBuffer(buffer, offset, view.state.appState)) {
        return std::nullopt;
    }
    if (view.tick == kInvalidPredictionTick || grounded > 0x01U || offset != buffer.size() ||
        !std::isfinite(view.state.position.x) || !std::isfinite(view.state.position.y) ||
        !std::isfinite(view.state.position.z) || !std::isfinite(view.state.velocity.x) ||
        !std::isfinite(view.state.velocity.y) || !std::isfinite(view.state.velocity.z)) {
        return std::nullopt;
    }
    view.state.grounded = grounded == 0x01U;
    return view;
}

[[nodiscard]] std::string HexByte(const std::uint8_t byte) {
    char text[8]{};
    std::snprintf(text, sizeof(text), "0x%02x", byte);
    return text;
}

// Fractions print trimmed - "+0.5", not "+0.500000" - so summaries stay readable.
[[nodiscard]] std::string TrimFloat(const float value) {
    std::string text = std::to_string(value);
    while (text.size() > 1U && text.back() == '0') {
        text.pop_back();
    }
    if (!text.empty() && text.back() == '.') {
        text.pop_back();
    }
    return text;
}

[[nodiscard]] std::string HexToken(const std::uint64_t token) {
    char text[24]{};
    std::snprintf(text, sizeof(text), "0x%016llx", static_cast<unsigned long long>(token));
    return text;
}

// Positions print the way script values print - "(1, 2, 3)" - so a capture reads like the
// values the game itself logs.
[[nodiscard]] std::string FormatPosition(const Math::Vector3& position) {
    return Blueprint::FormatValue(Blueprint::Vec3Value{
        static_cast<double>(position.x), static_cast<double>(position.y),
        static_cast<double>(position.z)});
}

[[nodiscard]] const char* ChannelName(const InspectedChannel channel) noexcept {
    switch (channel) {
        case InspectedChannel::Replication:
            return "replication";
        case InspectedChannel::Rpc:
            return "rpc";
        case InspectedChannel::Prediction:
            return "prediction";
        case InspectedChannel::Shot:
            return "shot";
        case InspectedChannel::Unknown:
            return "wire";
    }
    return "wire";
}

[[nodiscard]] std::string TrailingNote(const std::size_t trailing) {
    return " (+" + Count(trailing, " trailing byte", " trailing bytes") + ")";
}

} // namespace

InspectedPacket InspectPacket(const std::vector<std::uint8_t>& datagram,
                                    const PacketDirection direction,
                                    const std::uint64_t timestampMs,
                                    const InspectedChannel channel,
                                    const bool multiplexed) {
    InspectedPacket packet;
    packet.timestampMs = timestampMs;
    packet.direction = direction;
    packet.channel = channel;
    packet.raw = datagram;
    if (datagram.empty()) {
        packet.sessionKind = "empty";
        packet.summary = "empty datagram";
        packet.malformed = true;
        return packet;
    }
    switch (static_cast<SessionMessageTag>(datagram[0])) {
        case SessionMessageTag::Hello: {
            packet.sessionKind = "hello";
            if (datagram.size() < 3U) {
                packet.summary = "hello (truncated)";
                packet.malformed = true;
                return packet;
            }
            const std::uint16_t version = static_cast<std::uint16_t>(
                static_cast<std::uint16_t>(datagram[1]) |
                (static_cast<std::uint16_t>(datagram[2]) << 8));
            packet.helloVersion = version;
            packet.summary = "hello v" + std::to_string(version);
            if (datagram.size() < kSessionHelloBytes) {
                packet.summary += " (truncated)";
            } else {
                std::uint64_t token = 0U;
                for (std::size_t i = 0U; i < 8U; ++i) {
                    token |= static_cast<std::uint64_t>(datagram[3U + i]) << (8U * i);
                }
                packet.helloToken = token;
                packet.summary +=
                    token == kAnonymousSessionToken ? " anonymous" : " token " + HexToken(token);
                if (datagram.size() > kSessionHelloBytes) {
                    packet.trailingBytes = datagram.size() - kSessionHelloBytes;
                    packet.summary += TrailingNote(packet.trailingBytes);
                }
            }
            if (version != kSessionProtocolVersion) {
                packet.summary +=
                    " (protocol wants v" + std::to_string(kSessionProtocolVersion) + ")";
            }
            return packet;
        }
        case SessionMessageTag::Welcome:
        case SessionMessageTag::Bye: {
            const bool bye = datagram[0] == static_cast<std::uint8_t>(SessionMessageTag::Bye);
            packet.sessionKind = bye ? "bye" : "welcome";
            packet.summary = packet.sessionKind;
            if (datagram.size() > 1U) {
                packet.trailingBytes = datagram.size() - 1U;
                packet.summary += TrailingNote(packet.trailingBytes);
            }
            return packet;
        }
        case SessionMessageTag::Data:
            break;
    }
    // Any tag outside the session's four never reaches a feature - foreign mail, reported raw.
    if (datagram[0] != static_cast<std::uint8_t>(SessionMessageTag::Data)) {
        packet.sessionKind = "foreign";
        packet.summary = "foreign tag " + HexByte(datagram[0]);
        packet.malformed = true;
        return packet;
    }
    packet.sessionKind = "data";
    // Multiplexed data carries its channel past the tag: reported, then stripped, so the
    // feature decoders below meet exactly what the peer's demuxer hands them. A tag with no
    // channel byte never reaches a feature - malformed, like the session reads it.
    std::vector<std::uint8_t>::const_iterator payloadBegin = datagram.begin() + 1;
    if (multiplexed) {
        if (datagram.size() < 2U) {
            packet.dataKind = "malformed";
            packet.summary = "malformed data (no channel byte)";
            packet.malformed = true;
            return packet;
        }
        packet.channelId = datagram[1];
        payloadBegin = datagram.begin() + 2;
    }
    const std::vector<std::uint8_t> payload(payloadBegin, datagram.end());
    if (channel == InspectedChannel::Unknown) {
        packet.dataKind = "opaque";
        packet.summary = Count(payload.size(), " byte (opaque)", " bytes (opaque)");
        return packet;
    }
    switch (channel) {
        case InspectedChannel::Replication: {
            if (payload.size() >= 2U && payload[0] == 0xFEU && payload[1] == 0xFFU) {
                std::optional<OwnershipTransferView> control = DecodeOwnership(payload);
                if (!control.has_value()) {
                    break;
                }
                const char* verb = control->kind == OwnershipMessageKind::Request ? "request"
                                   : control->kind == OwnershipMessageKind::Accept ? "accept"
                                                                                      : "deny";
                packet.dataKind = "ownership";
                packet.summary =
                    "ownership " + std::string(verb) + " " + std::to_string(control->netId);
                packet.data = std::move(*control);
                return packet;
            }
            if (payload.size() >= 2U && payload[0] == 0xFDU && payload[1] == 0xFFU) {
                std::optional<ReplicationPropertiesView> props =
                    DecodeProperties(payload);
                if (!props.has_value()) {
                    break;
                }
                packet.dataKind = "properties";
                packet.summary =
                    "properties " + Count(props->propertyCount, " entry", " entries");
                packet.data = std::move(*props);
                return packet;
            }
            std::optional<ReplicationSnapshotView> view = DecodeSnapshot(payload);
            if (!view.has_value()) {
                break;
            }
            packet.dataKind = view->isDelta ? "delta" : "snapshot";
            packet.summary = (view->isDelta ? "delta " : "snapshot ") +
                             Count(view->entryCount, " entry", " entries");
            packet.data = std::move(*view);
            return packet;
        }
        case InspectedChannel::Rpc: {
            std::optional<DecodedRpc> frame = DecodeRpc(payload);
            if (!frame.has_value()) {
                break;
            }
            if (frame->isCall) {
                packet.dataKind = "call";
                packet.summary = "call #" + std::to_string(frame->call.callId) + " \"" +
                                 frame->call.method + "\" (";
                for (std::size_t i = 0U; i < frame->call.args.size(); ++i) {
                    packet.summary +=
                        (i == 0U ? "" : ", ") + Blueprint::FormatValue(frame->call.args[i]);
                }
                packet.summary += ")";
                packet.data = std::move(frame->call);
            } else {
                packet.dataKind = "response";
                packet.summary = "response #" + std::to_string(frame->response.callId);
                if (frame->response.ok) {
                    packet.summary += " ok " + Blueprint::FormatValue(frame->response.value);
                } else {
                    packet.summary += " error \"" + frame->response.error + "\"";
                }
                packet.data = std::move(frame->response);
            }
            return packet;
        }
        case InspectedChannel::Prediction: {
            if (!payload.empty() && payload[0] == 0x01U) {
                std::optional<PredictionInputsView> view = DecodePredictionInputs(payload);
                if (!view.has_value()) {
                    break;
                }
                packet.dataKind = "inputs";
                packet.summary = "inputs " + Count(view->window.size(), " tick ", " ticks ") +
                                 std::to_string(view->window.front().tick);
                if (view->window.size() > 1U) {
                    packet.summary += "-" + std::to_string(view->window.back().tick);
                }
                // A mixed window names its pawns (first-seen order): ticks only order within
                // a pawn, so the range alone would mislead once siblings share the message.
                std::string seen = "+";
                std::string pawnList;
                for (const PredictionInputEntry& entry : view->window) {
                    const std::string token = std::to_string(entry.pawn);
                    if (seen.find("+" + token + "+") == std::string::npos) {
                        seen += token + "+";
                        pawnList += (pawnList.empty() ? "" : "+") + token;
                    }
                }
                if (pawnList.find('+') != std::string::npos) {
                    packet.summary += " pawns " + pawnList;
                }
                packet.data = std::move(*view);
                return packet;
            }
            if (!payload.empty() && payload[0] == 0x02U) {
                std::optional<PredictionStateView> view = DecodePredictionState(payload);
                if (!view.has_value()) {
                    break;
                }
                packet.dataKind = "state";
                packet.summary = "state ";
                if (view->pawn != kDefaultPredictionPawn) {
                    packet.summary += "pawn " + std::to_string(view->pawn) + " ";
                }
                packet.summary += "tick " + std::to_string(view->tick) + " " +
                                  FormatPosition(view->state.position);
                packet.data = std::move(*view);
                return packet;
            }
            break;
        }
        case InspectedChannel::Shot: {
            const std::optional<ShotIntent> intent = DecodeShotIntent(payload);
            if (!intent.has_value()) {
                break;
            }
            packet.dataKind = "shot";
            packet.summary = "fire pawn " + std::to_string(intent->pawn) + " view " +
                             std::to_string(intent->viewTick) + " +" +
                             TrimFloat(intent->fraction) + " seq " +
                             std::to_string(intent->seq);
            packet.data = ShotIntentView{intent->pawn, intent->seq, intent->viewTick,
                                            intent->fraction};
            return packet;
        }
        case InspectedChannel::Unknown:
            break;
    }
    packet.dataKind = "malformed";
    packet.summary = "malformed " + std::string(ChannelName(channel)) + " payload (" +
                     Count(payload.size(), " byte", " bytes") + ")";
    packet.malformed = true;
    return packet;
}

std::string DescribePacket(const InspectedPacket& packet) {
    const char* arrow = packet.direction == PacketDirection::Outbound ? ">" : "<";
    std::string line = std::string(arrow) + " [" + ChannelName(packet.channel);
    if (packet.channelId.has_value()) {
        line += " ch" + std::to_string(*packet.channelId);
    }
    return line + "] " + packet.summary;
}

} // namespace UVE::Network
