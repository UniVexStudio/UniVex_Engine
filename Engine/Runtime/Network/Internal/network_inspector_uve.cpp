// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/network/network_inspector_uve.h"
#include "uve/network/network_shot_uve.h"

#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "uve/utilities/binary_buffer_uve.h"
#include "uve/uvscript/uvscript_value_uve.h"

namespace UVE::Network {
namespace {

// Every decoder below mirrors its feature's own framing rule for rule - strict sizes, exact
// ends, the same rejections - so the inspector shows what the peer would actually read, not
// what the bytes could generously mean. The mirror is deliberate: a dissector duplicates the
// protocols it reads, and the inspector tests run live traffic through every decoder on each
// build, so a codec that drifts breaks the build before it rots the tool.

[[nodiscard]] std::vector<std::byte> ToBytesUVE(const std::vector<std::uint8_t>& in) {
    std::vector<std::byte> out;
    out.reserve(in.size());
    for (const std::uint8_t byte : in) {
        out.push_back(static_cast<std::byte>(byte));
    }
    return out;
}

[[nodiscard]] std::string CountUVE(const std::size_t n, const char* one, const char* many) {
    return std::to_string(n) + (n == 1U ? one : many);
}

// One ownership-control frame: the marker, a request/accept/deny kind, and the entity -
// exactly 7 bytes, mirroring the replicator's strict read.
[[nodiscard]] std::optional<OwnershipTransferViewUVE> DecodeOwnershipUVE(
    const std::vector<std::uint8_t>& payload) {
    // The marker travels little-endian, like every multi-byte int: FE FF on the wire.
    if (payload.size() != 7U || payload[0] != 0xFEU || payload[1] != 0xFFU) {
        return std::nullopt;
    }
    if (payload[2] != static_cast<std::uint8_t>(OwnershipMessageKindUVE::Request) &&
        payload[2] != static_cast<std::uint8_t>(OwnershipMessageKindUVE::Accept) &&
        payload[2] != static_cast<std::uint8_t>(OwnershipMessageKindUVE::Deny)) {
        return std::nullopt;
    }
    OwnershipTransferViewUVE view;
    view.kind = static_cast<OwnershipMessageKindUVE>(payload[2]);
    view.netId = static_cast<NetworkEntityIdUVE>(payload[3]) |
                 (static_cast<NetworkEntityIdUVE>(payload[4]) << 8) |
                 (static_cast<NetworkEntityIdUVE>(payload[5]) << 16) |
                 (static_cast<NetworkEntityIdUVE>(payload[6]) << 24);
    return view;
}

// A u16 count plus that many (entity, position) pairs, and nothing else - the snapshot stands
// or falls as one message, like the replicator insists. A leading delta marker means a
// second u16 holds the count; the entries decode identically either way.
[[nodiscard]] std::optional<ReplicationSnapshotViewUVE> DecodeSnapshotUVE(
    const std::vector<std::uint8_t>& payload) {
    const std::vector<std::byte> buffer = ToBytesUVE(payload);
    std::size_t offset = 0U;
    std::uint16_t first = 0U;
    if (!Utilities::ReadUint16LeFromBufferUVE(buffer, offset, first)) {
        return std::nullopt;
    }
    const bool delta = first == kReplicationDeltaMarkerUVE;
    std::uint16_t entryCount = first;
    std::size_t headerBytes = sizeof(std::uint16_t);
    if (delta) {
        headerBytes += sizeof(std::uint16_t);
        if (!Utilities::ReadUint16LeFromBufferUVE(buffer, offset, entryCount)) {
            return std::nullopt;
        }
    }
    if (buffer.size() != headerBytes + static_cast<std::size_t>(entryCount) *
                                           (sizeof(NetworkEntityIdUVE) + 3U * sizeof(float))) {
        return std::nullopt;
    }
    ReplicationSnapshotViewUVE view;
    view.isDelta = delta;
    view.entryCount = entryCount;
    view.entries.reserve(entryCount);
    for (std::uint16_t i = 0U; i < entryCount; ++i) {
        NetworkEntityIdUVE netId = 0U;
        Math::Vector3UVE position{};
        if (!Utilities::ReadUint32LeFromBufferUVE(buffer, offset, netId) ||
            !Utilities::ReadFloatLeFromBufferUVE(buffer, offset, position.x) ||
            !Utilities::ReadFloatLeFromBufferUVE(buffer, offset, position.y) ||
            !Utilities::ReadFloatLeFromBufferUVE(buffer, offset, position.z)) {
            return std::nullopt;
        }
        view.entries.emplace_back(netId, position);
    }
    return view;
}

// RPC value tags, mirroring the frame codec: the inspector reads arguments and results with
// the same tags, the same depth cap, and the same all-or-nothing rule.
enum class InspectorValueTagUVE : std::uint8_t {
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

[[nodiscard]] std::optional<std::string> ReadInspectorStringUVE(const std::vector<std::byte>& in,
                                                                std::size_t& offset) {
    std::uint16_t length = 0U;
    if (!Utilities::ReadUint16LeFromBufferUVE(in, offset, length) || length > in.size() - offset) {
        return std::nullopt;
    }
    std::string text;
    if (length > 0U) {
        text.assign(reinterpret_cast<const char*>(&in[offset]), length);
        offset += length;
    }
    return text;
}

[[nodiscard]] std::optional<UVScript::ValueUVE> ReadInspectorValueUVE(
    const std::vector<std::byte>& in, std::size_t& offset, const std::size_t depth) {
    if (offset >= in.size()) {
        return std::nullopt;
    }
    const auto tag =
        static_cast<InspectorValueTagUVE>(std::to_integer<std::uint8_t>(in[offset++]));
    switch (tag) {
        case InspectorValueTagUVE::Nil:
            return UVScript::ValueUVE{};
        case InspectorValueTagUVE::Bool: {
            if (offset >= in.size()) {
                return std::nullopt;
            }
            const bool value = in[offset++] != static_cast<std::byte>(0x00);
            return UVScript::ValueUVE{value};
        }
        case InspectorValueTagUVE::Int: {
            std::uint64_t bits = 0U;
            if (!Utilities::ReadUint64LeFromBufferUVE(in, offset, bits)) {
                return std::nullopt;
            }
            return UVScript::ValueUVE{static_cast<std::int64_t>(bits)};
        }
        case InspectorValueTagUVE::Float: {
            std::uint64_t bits = 0U;
            if (!Utilities::ReadUint64LeFromBufferUVE(in, offset, bits)) {
                return std::nullopt;
            }
            return UVScript::ValueUVE{std::bit_cast<double>(bits)};
        }
        case InspectorValueTagUVE::Str: {
            std::optional<std::string> text = ReadInspectorStringUVE(in, offset);
            if (!text.has_value()) {
                return std::nullopt;
            }
            return UVScript::ValueUVE{std::move(*text)};
        }
        case InspectorValueTagUVE::Vec3: {
            std::uint64_t x = 0U, y = 0U, z = 0U;
            if (!Utilities::ReadUint64LeFromBufferUVE(in, offset, x) ||
                !Utilities::ReadUint64LeFromBufferUVE(in, offset, y) ||
                !Utilities::ReadUint64LeFromBufferUVE(in, offset, z)) {
                return std::nullopt;
            }
            return UVScript::ValueUVE{UVScript::Vec3ValueUVE{
                std::bit_cast<double>(x), std::bit_cast<double>(y), std::bit_cast<double>(z)}};
        }
        case InspectorValueTagUVE::List:
        case InspectorValueTagUVE::Tuple: {
            if (depth == kRpcMaximumValueDepthUVE) {
                return std::nullopt;
            }
            std::uint16_t count = 0U;
            if (!Utilities::ReadUint16LeFromBufferUVE(in, offset, count)) {
                return std::nullopt;
            }
            std::vector<UVScript::ValueUVE> items;
            for (std::uint16_t i = 0U; i < count; ++i) {
                std::optional<UVScript::ValueUVE> item =
                    ReadInspectorValueUVE(in, offset, depth + 1U);
                if (!item.has_value()) {
                    return std::nullopt;
                }
                items.push_back(std::move(*item));
            }
            if (tag == InspectorValueTagUVE::List) {
                return UVScript::MakeListValueUVE(std::move(items));
            }
            return UVScript::MakeTupleValueUVE(std::move(items));
        }
        case InspectorValueTagUVE::Map: {
            if (depth == kRpcMaximumValueDepthUVE) {
                return std::nullopt;
            }
            std::uint16_t pairs = 0U;
            if (!Utilities::ReadUint16LeFromBufferUVE(in, offset, pairs)) {
                return std::nullopt;
            }
            std::vector<UVScript::ValueUVE> items;
            for (std::uint16_t i = 0U; i < pairs; ++i) {
                std::optional<UVScript::ValueUVE> key =
                    ReadInspectorValueUVE(in, offset, depth + 1U);
                std::optional<UVScript::ValueUVE> item =
                    ReadInspectorValueUVE(in, offset, depth + 1U);
                if (!key.has_value() || !item.has_value()) {
                    return std::nullopt;
                }
                items.push_back(std::move(*key));
                items.push_back(std::move(*item));
            }
            return UVScript::MakeMapValueUVE(std::move(items));
        }
    }
    return std::nullopt;
}

// One property frame: the marker, a u16 count, then netId, property id, and a tagged value
// per entry - the inspector's own mirror reads the values, never the shared codec. The frame
// must end exactly where the last value does; a count of zero decodes (the sender never
// emits it, but the shape is strict either way).
[[nodiscard]] std::optional<ReplicationPropertiesViewUVE> DecodePropertiesUVE(
    const std::vector<std::uint8_t>& payload) {
    constexpr std::uint8_t kPropertyMarker0 = 0xFDU;
    constexpr std::uint8_t kPropertyMarker1 = 0xFFU;
    const std::vector<std::byte> in = ToBytesUVE(payload);
    if (in.size() < 4U || std::to_integer<std::uint8_t>(in[0]) != kPropertyMarker0 ||
        std::to_integer<std::uint8_t>(in[1]) != kPropertyMarker1) {
        return std::nullopt;
    }
    std::size_t offset = 2U;
    std::uint16_t entryCount = 0U;
    if (!Utilities::ReadUint16LeFromBufferUVE(in, offset, entryCount)) {
        return std::nullopt;
    }
    ReplicationPropertiesViewUVE view;
    view.propertyCount = entryCount;
    for (std::uint16_t i = 0U; i < entryCount; ++i) {
        NetworkEntityIdUVE netId = 0U;
        if (!Utilities::ReadUint32LeFromBufferUVE(in, offset, netId) || offset >= in.size()) {
            return std::nullopt;
        }
        const ReplicationPropertyIdUVE propId =
            std::to_integer<ReplicationPropertyIdUVE>(in[offset++]);
        std::optional<UVScript::ValueUVE> value = ReadInspectorValueUVE(in, offset, 0U);
        if (!value.has_value()) {
            return std::nullopt;
        }
        view.entries.push_back(ReplicationPropertyEntryUVE{netId, propId, std::move(*value)});
    }
    if (offset != in.size()) {
        return std::nullopt;
    }
    return view;
}

struct DecodedRpcUVE final {
    bool isCall = true;
    RpcCallViewUVE call;
    RpcResponseViewUVE response;
};

// Magic, version, kind, id, then exactly the declared body - trailing bytes fail the frame,
// and an error payload that is not a string fails it too, like the RPC peer demands.
[[nodiscard]] std::optional<DecodedRpcUVE> DecodeRpcUVE(const std::vector<std::uint8_t>& payload) {
    constexpr std::uint8_t kMagic0 = static_cast<std::uint8_t>('R');
    constexpr std::uint8_t kMagic1 = static_cast<std::uint8_t>('C');
    constexpr std::uint8_t kCallKind = 0x01U;
    constexpr std::uint8_t kResponseKind = 0x02U;
    constexpr std::uint8_t kOkStatus = 0x00U;
    constexpr std::uint8_t kErrorStatus = 0x01U;
    const std::vector<std::byte> in = ToBytesUVE(payload);
    if (in.size() < 8U || std::to_integer<std::uint8_t>(in[0]) != kMagic0 ||
        std::to_integer<std::uint8_t>(in[1]) != kMagic1) {
        return std::nullopt;
    }
    std::size_t offset = 2U;
    const std::uint8_t version = std::to_integer<std::uint8_t>(in[offset++]);
    const std::uint8_t kind = std::to_integer<std::uint8_t>(in[offset++]);
    RpcCallIdUVE callId = kInvalidRpcCallIdUVE;
    if (version != kRpcFrameVersionUVE || (kind != kCallKind && kind != kResponseKind) ||
        !Utilities::ReadUint32LeFromBufferUVE(in, offset, callId)) {
        return std::nullopt;
    }
    DecodedRpcUVE frame;
    if (kind == kCallKind) {
        std::optional<std::string> method = ReadInspectorStringUVE(in, offset);
        std::uint16_t argCount = 0U;
        if (!method.has_value() || !Utilities::ReadUint16LeFromBufferUVE(in, offset, argCount)) {
            return std::nullopt;
        }
        frame.call.callId = callId;
        frame.call.method = std::move(*method);
        for (std::uint16_t i = 0U; i < argCount; ++i) {
            std::optional<UVScript::ValueUVE> arg = ReadInspectorValueUVE(in, offset, 0U);
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
        std::optional<UVScript::ValueUVE> value = ReadInspectorValueUVE(in, offset, 0U);
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
[[nodiscard]] std::optional<PredictionInputsViewUVE> DecodePredictionInputsUVE(
    const std::vector<std::uint8_t>& payload) {
    constexpr std::uint8_t kInputKind = 0x01U;
    constexpr std::size_t kEntryBytes = 30U;
    const std::vector<std::byte> buffer = ToBytesUVE(payload);
    if (buffer.size() < 2U || std::to_integer<std::uint8_t>(buffer[0]) != kInputKind) {
        return std::nullopt;
    }
    const std::size_t count = static_cast<std::size_t>(std::to_integer<std::uint8_t>(buffer[1]));
    if (count == 0U || count > kPredictionMaximumInputsPerMessageUVE ||
        buffer.size() != 2U + count * kEntryBytes) {
        return std::nullopt;
    }
    PredictionInputsViewUVE view;
    view.window.reserve(count);
    std::size_t offset = 2U;
    for (std::size_t i = 0U; i < count; ++i) {
        PredictionInputEntryUVE entry;
        if (!Utilities::ReadUint64LeFromBufferUVE(buffer, offset, entry.tick) ||
            offset >= buffer.size()) {
            return std::nullopt;
        }
        entry.pawn = std::to_integer<std::uint8_t>(buffer[offset++]);
        if (!Utilities::ReadFloatLeFromBufferUVE(buffer, offset, entry.dtSeconds) ||
            !Utilities::ReadFloatLeFromBufferUVE(buffer, offset, entry.input.move.x) ||
            !Utilities::ReadFloatLeFromBufferUVE(buffer, offset, entry.input.move.y) ||
            !Utilities::ReadFloatLeFromBufferUVE(buffer, offset, entry.input.move.z) ||
            !Utilities::ReadFloatLeFromBufferUVE(buffer, offset, entry.input.rise) ||
            offset >= buffer.size()) {
            return std::nullopt;
        }
        const std::uint8_t jump = std::to_integer<std::uint8_t>(buffer[offset++]);
        if (entry.tick == kInvalidPredictionTickUVE || jump > 0x01U ||
            !std::isfinite(entry.dtSeconds) || entry.dtSeconds <= 0.0F ||
            entry.dtSeconds > kPredictionMaximumTickDtSecondsUVE ||
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
[[nodiscard]] std::optional<PredictionStateViewUVE> DecodePredictionStateUVE(
    const std::vector<std::uint8_t>& payload) {
    constexpr std::uint8_t kStateKind = 0x02U;
    constexpr std::size_t kStateBytes = 39U;
    const std::vector<std::byte> buffer = ToBytesUVE(payload);
    if (buffer.size() != kStateBytes ||
        std::to_integer<std::uint8_t>(buffer[0]) != kStateKind) {
        return std::nullopt;
    }
    std::size_t offset = 1U;
    PredictionStateViewUVE view;
    std::uint8_t grounded = 0U;
    if (!Utilities::ReadUint64LeFromBufferUVE(buffer, offset, view.tick) ||
        offset >= buffer.size()) {
        return std::nullopt;
    }
    view.pawn = std::to_integer<std::uint8_t>(buffer[offset++]);
    if (!Utilities::ReadFloatLeFromBufferUVE(buffer, offset, view.state.position.x) ||
        !Utilities::ReadFloatLeFromBufferUVE(buffer, offset, view.state.position.y) ||
        !Utilities::ReadFloatLeFromBufferUVE(buffer, offset, view.state.position.z) ||
        !Utilities::ReadFloatLeFromBufferUVE(buffer, offset, view.state.velocity.x) ||
        !Utilities::ReadFloatLeFromBufferUVE(buffer, offset, view.state.velocity.y) ||
        !Utilities::ReadFloatLeFromBufferUVE(buffer, offset, view.state.velocity.z) ||
        offset + 2U > buffer.size()) {
        return std::nullopt;
    }
    grounded = std::to_integer<std::uint8_t>(buffer[offset++]);
    if (!Utilities::ReadUint32LeFromBufferUVE(buffer, offset, view.state.appState)) {
        return std::nullopt;
    }
    if (view.tick == kInvalidPredictionTickUVE || grounded > 0x01U || offset != buffer.size() ||
        !std::isfinite(view.state.position.x) || !std::isfinite(view.state.position.y) ||
        !std::isfinite(view.state.position.z) || !std::isfinite(view.state.velocity.x) ||
        !std::isfinite(view.state.velocity.y) || !std::isfinite(view.state.velocity.z)) {
        return std::nullopt;
    }
    view.state.grounded = grounded == 0x01U;
    return view;
}

[[nodiscard]] std::string HexByteUVE(const std::uint8_t byte) {
    char text[8]{};
    std::snprintf(text, sizeof(text), "0x%02x", byte);
    return text;
}

// Fractions print trimmed - "+0.5", not "+0.500000" - so summaries stay readable.
[[nodiscard]] std::string TrimFloatUVE(const float value) {
    std::string text = std::to_string(value);
    while (text.size() > 1U && text.back() == '0') {
        text.pop_back();
    }
    if (!text.empty() && text.back() == '.') {
        text.pop_back();
    }
    return text;
}

[[nodiscard]] std::string HexTokenUVE(const std::uint64_t token) {
    char text[24]{};
    std::snprintf(text, sizeof(text), "0x%016llx", static_cast<unsigned long long>(token));
    return text;
}

// Positions print the way script values print - "(1, 2, 3)" - so a capture reads like the
// values the game itself logs.
[[nodiscard]] std::string FormatPositionUVE(const Math::Vector3UVE& position) {
    return UVScript::FormatValueUVE(UVScript::Vec3ValueUVE{
        static_cast<double>(position.x), static_cast<double>(position.y),
        static_cast<double>(position.z)});
}

[[nodiscard]] const char* ChannelNameUVE(const InspectedChannelUVE channel) noexcept {
    switch (channel) {
        case InspectedChannelUVE::Replication:
            return "replication";
        case InspectedChannelUVE::Rpc:
            return "rpc";
        case InspectedChannelUVE::Prediction:
            return "prediction";
        case InspectedChannelUVE::Shot:
            return "shot";
        case InspectedChannelUVE::Unknown:
            return "wire";
    }
    return "wire";
}

[[nodiscard]] std::string TrailingNoteUVE(const std::size_t trailing) {
    return " (+" + CountUVE(trailing, " trailing byte", " trailing bytes") + ")";
}

} // namespace

InspectedPacketUVE InspectPacketUVE(const std::vector<std::uint8_t>& datagram,
                                    const PacketDirectionUVE direction,
                                    const std::uint64_t timestampMs,
                                    const InspectedChannelUVE channel,
                                    const bool multiplexed) {
    InspectedPacketUVE packet;
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
    switch (static_cast<SessionMessageTagUVE>(datagram[0])) {
        case SessionMessageTagUVE::Hello: {
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
            if (datagram.size() < kSessionHelloBytesUVE) {
                packet.summary += " (truncated)";
            } else {
                std::uint64_t token = 0U;
                for (std::size_t i = 0U; i < 8U; ++i) {
                    token |= static_cast<std::uint64_t>(datagram[3U + i]) << (8U * i);
                }
                packet.helloToken = token;
                packet.summary +=
                    token == kAnonymousSessionTokenUVE ? " anonymous" : " token " + HexTokenUVE(token);
                if (datagram.size() > kSessionHelloBytesUVE) {
                    packet.trailingBytes = datagram.size() - kSessionHelloBytesUVE;
                    packet.summary += TrailingNoteUVE(packet.trailingBytes);
                }
            }
            if (version != kSessionProtocolVersionUVE) {
                packet.summary +=
                    " (protocol wants v" + std::to_string(kSessionProtocolVersionUVE) + ")";
            }
            return packet;
        }
        case SessionMessageTagUVE::Welcome:
        case SessionMessageTagUVE::Bye: {
            const bool bye = datagram[0] == static_cast<std::uint8_t>(SessionMessageTagUVE::Bye);
            packet.sessionKind = bye ? "bye" : "welcome";
            packet.summary = packet.sessionKind;
            if (datagram.size() > 1U) {
                packet.trailingBytes = datagram.size() - 1U;
                packet.summary += TrailingNoteUVE(packet.trailingBytes);
            }
            return packet;
        }
        case SessionMessageTagUVE::Data:
            break;
    }
    // Any tag outside the session's four never reaches a feature - foreign mail, reported raw.
    if (datagram[0] != static_cast<std::uint8_t>(SessionMessageTagUVE::Data)) {
        packet.sessionKind = "foreign";
        packet.summary = "foreign tag " + HexByteUVE(datagram[0]);
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
    if (channel == InspectedChannelUVE::Unknown) {
        packet.dataKind = "opaque";
        packet.summary = CountUVE(payload.size(), " byte (opaque)", " bytes (opaque)");
        return packet;
    }
    switch (channel) {
        case InspectedChannelUVE::Replication: {
            if (payload.size() >= 2U && payload[0] == 0xFEU && payload[1] == 0xFFU) {
                std::optional<OwnershipTransferViewUVE> control = DecodeOwnershipUVE(payload);
                if (!control.has_value()) {
                    break;
                }
                const char* verb = control->kind == OwnershipMessageKindUVE::Request ? "request"
                                   : control->kind == OwnershipMessageKindUVE::Accept ? "accept"
                                                                                      : "deny";
                packet.dataKind = "ownership";
                packet.summary =
                    "ownership " + std::string(verb) + " " + std::to_string(control->netId);
                packet.data = std::move(*control);
                return packet;
            }
            if (payload.size() >= 2U && payload[0] == 0xFDU && payload[1] == 0xFFU) {
                std::optional<ReplicationPropertiesViewUVE> props =
                    DecodePropertiesUVE(payload);
                if (!props.has_value()) {
                    break;
                }
                packet.dataKind = "properties";
                packet.summary =
                    "properties " + CountUVE(props->propertyCount, " entry", " entries");
                packet.data = std::move(*props);
                return packet;
            }
            std::optional<ReplicationSnapshotViewUVE> view = DecodeSnapshotUVE(payload);
            if (!view.has_value()) {
                break;
            }
            packet.dataKind = view->isDelta ? "delta" : "snapshot";
            packet.summary = (view->isDelta ? "delta " : "snapshot ") +
                             CountUVE(view->entryCount, " entry", " entries");
            packet.data = std::move(*view);
            return packet;
        }
        case InspectedChannelUVE::Rpc: {
            std::optional<DecodedRpcUVE> frame = DecodeRpcUVE(payload);
            if (!frame.has_value()) {
                break;
            }
            if (frame->isCall) {
                packet.dataKind = "call";
                packet.summary = "call #" + std::to_string(frame->call.callId) + " \"" +
                                 frame->call.method + "\" (";
                for (std::size_t i = 0U; i < frame->call.args.size(); ++i) {
                    packet.summary +=
                        (i == 0U ? "" : ", ") + UVScript::FormatValueUVE(frame->call.args[i]);
                }
                packet.summary += ")";
                packet.data = std::move(frame->call);
            } else {
                packet.dataKind = "response";
                packet.summary = "response #" + std::to_string(frame->response.callId);
                if (frame->response.ok) {
                    packet.summary += " ok " + UVScript::FormatValueUVE(frame->response.value);
                } else {
                    packet.summary += " error \"" + frame->response.error + "\"";
                }
                packet.data = std::move(frame->response);
            }
            return packet;
        }
        case InspectedChannelUVE::Prediction: {
            if (!payload.empty() && payload[0] == 0x01U) {
                std::optional<PredictionInputsViewUVE> view = DecodePredictionInputsUVE(payload);
                if (!view.has_value()) {
                    break;
                }
                packet.dataKind = "inputs";
                packet.summary = "inputs " + CountUVE(view->window.size(), " tick ", " ticks ") +
                                 std::to_string(view->window.front().tick);
                if (view->window.size() > 1U) {
                    packet.summary += "-" + std::to_string(view->window.back().tick);
                }
                // A mixed window names its pawns (first-seen order): ticks only order within
                // a pawn, so the range alone would mislead once siblings share the message.
                std::string seen = "+";
                std::string pawnList;
                for (const PredictionInputEntryUVE& entry : view->window) {
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
                std::optional<PredictionStateViewUVE> view = DecodePredictionStateUVE(payload);
                if (!view.has_value()) {
                    break;
                }
                packet.dataKind = "state";
                packet.summary = "state ";
                if (view->pawn != kDefaultPredictionPawnUVE) {
                    packet.summary += "pawn " + std::to_string(view->pawn) + " ";
                }
                packet.summary += "tick " + std::to_string(view->tick) + " " +
                                  FormatPositionUVE(view->state.position);
                packet.data = std::move(*view);
                return packet;
            }
            break;
        }
        case InspectedChannelUVE::Shot: {
            const std::optional<ShotIntentUVE> intent = DecodeShotIntentUVE(payload);
            if (!intent.has_value()) {
                break;
            }
            packet.dataKind = "shot";
            packet.summary = "fire pawn " + std::to_string(intent->pawn) + " view " +
                             std::to_string(intent->viewTick) + " +" +
                             TrimFloatUVE(intent->fraction) + " seq " +
                             std::to_string(intent->seq);
            packet.data = ShotIntentViewUVE{intent->pawn, intent->seq, intent->viewTick,
                                            intent->fraction};
            return packet;
        }
        case InspectedChannelUVE::Unknown:
            break;
    }
    packet.dataKind = "malformed";
    packet.summary = "malformed " + std::string(ChannelNameUVE(channel)) + " payload (" +
                     CountUVE(payload.size(), " byte", " bytes") + ")";
    packet.malformed = true;
    return packet;
}

std::string DescribePacketUVE(const InspectedPacketUVE& packet) {
    const char* arrow = packet.direction == PacketDirectionUVE::Outbound ? ">" : "<";
    std::string line = std::string(arrow) + " [" + ChannelNameUVE(packet.channel);
    if (packet.channelId.has_value()) {
        line += " ch" + std::to_string(*packet.channelId);
    }
    return line + "] " + packet.summary;
}

} // namespace UVE::Network
