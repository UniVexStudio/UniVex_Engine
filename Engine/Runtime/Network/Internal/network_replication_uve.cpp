// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/network/network_replication_uve.h"
#include "uve/network/network_value_codec_uve.h"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <set>
#include <tuple>
#include <utility>
#include <vector>

#include "uve/utilities/binary_buffer_uve.h"

namespace UVE::Network {
namespace {

// Per-component stillness: every axis within the epsilon means the entity never moved. A NaN
// sample compares unequal to everything (including the epsilon), so corrupt samplers ship
// every tick - loud, like before, instead of silently going quiet.
[[nodiscard]] bool UnchangedUVE(const Math::Vector3UVE& sampled, const Math::Vector3UVE& baseline,
                                const float epsilon) noexcept {
    return std::fabs(sampled.x - baseline.x) <= epsilon &&
           std::fabs(sampled.y - baseline.y) <= epsilon &&
           std::fabs(sampled.z - baseline.z) <= epsilon;
}

// Ownership-control kinds: the third byte of a control frame, after the marker.
inline constexpr std::uint8_t kTransferRequestKindUVE = 0x01U;
inline constexpr std::uint8_t kTransferAcceptKindUVE = 0x02U;
inline constexpr std::uint8_t kTransferDenyKindUVE = 0x03U;
inline constexpr std::size_t kTransferControlBytesUVE = 7U;

} // namespace

NetworkReplicatorUVE::NetworkReplicatorUVE(NetworkSessionUVE&& session,
                                           const std::uint64_t sendIntervalMs) noexcept
    : m_session(std::move(session)), m_sendIntervalMs(sendIntervalMs) {}

NetworkReplicatorUVE::NetworkReplicatorUVE(NetworkSessionUVE& sharedSession,
                                           const NetworkChannelIdUVE channel,
                                           const std::uint64_t sendIntervalMs) noexcept
    : m_session(&sharedSession), m_channel(channel), m_sendIntervalMs(sendIntervalMs) {}

bool NetworkReplicatorUVE::SendPayloadUVE(const std::vector<std::uint8_t>& payload) {
    if (std::holds_alternative<NetworkSessionUVE*>(m_session)) {
        return SessionUVE().SendChannelDataUVE(m_channel, payload);
    }
    return SessionUVE().SendDataUVE(payload);
}

std::optional<std::vector<std::uint8_t>> NetworkReplicatorUVE::ReceivePayloadUVE() {
    if (std::holds_alternative<NetworkSessionUVE*>(m_session)) {
        return SessionUVE().TryReceiveChannelDataUVE(m_channel);
    }
    return SessionUVE().TryReceiveDataUVE();
}

bool NetworkReplicatorUVE::RegisterEntityUVE(const NetworkEntityIdUVE netId, PositionSamplerUVE sampler,
                                             PositionApplierUVE applier,
                                             const ReplicationAuthorityUVE authority,
                                             RelevancyCheckUVE relevancy) {
    if (sampler == nullptr || applier == nullptr || m_entries.contains(netId)) {
        return false;
    }
    ReplicationEntryUVE entry;
    entry.sampler = std::move(sampler);
    entry.applier = std::move(applier);
    entry.authority = authority;
    entry.relevancy = std::move(relevancy);
    m_entries.emplace(netId, std::move(entry));
    return true;
}

bool NetworkReplicatorUVE::UnregisterEntityUVE(const NetworkEntityIdUVE netId) noexcept {
    m_baselines.erase(netId);
    for (auto prop = m_properties.begin(); prop != m_properties.end();) {
        if (prop->first.first == netId) {
            prop = m_properties.erase(prop);
        } else {
            ++prop;
        }
    }
    m_openRequests.erase(netId);
    m_awaitingDecision.erase(netId);
    return m_entries.erase(netId) > 0U;
}

bool NetworkReplicatorUVE::SetAuthorityUVE(const NetworkEntityIdUVE netId,
                                           const ReplicationAuthorityUVE authority) noexcept {
    const auto entry = m_entries.find(netId);
    if (entry == m_entries.end()) {
        return false;
    }
    // Manual rebinding overrides the handshake: whatever was in flight for this entity is
    // moot, silently - the app took the wheel.
    m_openRequests.erase(netId);
    m_awaitingDecision.erase(netId);
    entry->second.authority = authority;
    return true;
}

bool NetworkReplicatorUVE::SetRelevancyUVE(const NetworkEntityIdUVE netId,
                                           RelevancyCheckUVE relevancy) noexcept {
    const auto entry = m_entries.find(netId);
    if (entry == m_entries.end()) {
        return false;
    }
    entry->second.relevancy = std::move(relevancy);
    return true;
}

bool NetworkReplicatorUVE::RegisterPropertyUVE(const NetworkEntityIdUVE netId,
                                               const ReplicationPropertyIdUVE propId,
                                               PropertyApplierUVE applier) {
    if (m_entries.find(netId) == m_entries.end() || applier == nullptr) {
        return false;
    }
    PropertyStateUVE& prop = m_properties[{netId, propId}];
    if (prop.applier != nullptr) {
        return false;
    }
    prop.applier = std::move(applier);
    return true;
}

bool NetworkReplicatorUVE::UnregisterPropertyUVE(const NetworkEntityIdUVE netId,
                                                 const ReplicationPropertyIdUVE propId) noexcept {
    return m_properties.erase({netId, propId}) > 0U;
}

bool NetworkReplicatorUVE::SetPropertyUVE(const NetworkEntityIdUVE netId,
                                          const ReplicationPropertyIdUVE propId,
                                          const UVScript::ValueUVE& value) {
    if (m_entries.find(netId) == m_entries.end()) {
        return false;
    }
    // Encode-at-Set: the wire bytes are validated and cached here, so the send tick never
    // encodes (never fails) and the dirty check is an exact byte compare - two values with
    // identical bytes are the same value on the wire, whatever == says.
    std::vector<std::byte> encoded;
    if (!AppendWireValueUVE(encoded, value, 0U) ||
        encoded.size() > kReplicationMaximumPropertyBytesUVE) {
        ++m_stats.propertiesSkippedOversized;
        return false;
    }
    PropertyStateUVE& prop = m_properties[{netId, propId}];
    if (prop.hasValue && prop.encoded == encoded) {
        return true;
    }
    prop.encoded = std::move(encoded);
    prop.hasValue = true;
    prop.dirty = true;
    return true;
}

bool NetworkReplicatorUVE::SetFullSnapshotPeriodUVE(const std::uint32_t period) noexcept {
    if (period == 0U) {
        return false;
    }
    m_fullPeriod = period;
    return true;
}

bool NetworkReplicatorUVE::SetPositionEpsilonUVE(const float epsilon) noexcept {
    if (!std::isfinite(epsilon) || epsilon < 0.0F) {
        return false;
    }
    m_positionEpsilon = epsilon;
    return true;
}

bool NetworkReplicatorUVE::RequestOwnershipUVE(const NetworkEntityIdUVE netId,
                                              const std::uint64_t nowMs) {
    const auto entry = m_entries.find(netId);
    if (entry == m_entries.end() || entry->second.authority != ReplicationAuthorityUVE::Proxy ||
        m_openRequests.find(netId) != m_openRequests.end() ||
        !SendControlUVE(kTransferRequestKindUVE, netId)) {
        return false;
    }
    m_openRequests.emplace(netId, nowMs);
    ++m_stats.transfersRequested;
    return true;
}

bool NetworkReplicatorUVE::AcceptTransferUVE(const NetworkEntityIdUVE netId) {
    const auto entry = m_entries.find(netId);
    if (entry == m_entries.end() || m_awaitingDecision.find(netId) == m_awaitingDecision.end() ||
        !SendControlUVE(kTransferAcceptKindUVE, netId)) {
        return false;
    }
    m_awaitingDecision.erase(netId);
    entry->second.authority = ReplicationAuthorityUVE::Proxy;
    ++m_stats.transfersCompleted;
    NotifyUVE(netId, OwnershipTransferEventUVE::Completed);
    return true;
}

bool NetworkReplicatorUVE::DenyTransferUVE(const NetworkEntityIdUVE netId) {
    if (m_entries.find(netId) == m_entries.end() ||
        m_awaitingDecision.find(netId) == m_awaitingDecision.end() ||
        !SendControlUVE(kTransferDenyKindUVE, netId)) {
        return false;
    }
    m_awaitingDecision.erase(netId);
    ++m_stats.transfersDenied;
    return true;
}

void NetworkReplicatorUVE::SetOwnershipTransferHandlerUVE(OwnershipTransferHandlerUVE handler) {
    m_transferHandler = std::move(handler);
}

bool NetworkReplicatorUVE::SetOwnershipTimeoutUVE(const std::uint64_t timeoutMs) noexcept {
    if (timeoutMs == 0U) {
        return false;
    }
    m_ownershipTimeoutMs = timeoutMs;
    return true;
}

void NetworkReplicatorUVE::UpdateUVE(const std::uint64_t nowMs) {
    SessionUVE().UpdateUVE(nowMs);
    for (;;) {
        const std::optional<std::vector<std::uint8_t>> payload = ReceivePayloadUVE();
        if (!payload.has_value()) {
            break;
        }
        HandleSnapshotUVE(*payload);
    }
    // Answers land before deadlines: anything the pump just completed is already gone from
    // the open set, so only the truly unanswered expire. Runs unconditionally - a dropped
    // session must not freeze a request past its time.
    ExpireRequestsUVE(nowMs);
    if (SessionUVE().StateUVE() != SessionStateUVE::Established ||
        nowMs - m_lastSendMs < m_sendIntervalMs) {
        return;
    }
    // Fulls carry every sending entity and re-baseline the world; deltas carry only what has
    // no baseline or moved past the epsilon. The first send is always full, and a lost delta
    // heals at worst by the next full - the full is the backstop for loss, truncation, and
    // anything an authority flip hid.
    const bool full = m_sendsCompleted % m_fullPeriod == 0U;
    std::vector<std::pair<NetworkEntityIdUVE, Math::Vector3UVE>> included;
    // Relevancy runs once per entity per tick: the property frame reuses these verdicts
    // instead of re-running app predicates.
    std::set<NetworkEntityIdUVE> relevantSenders;
    for (const auto& [netId, entry] : m_entries) {
        if (entry.authority == ReplicationAuthorityUVE::Proxy) {
            continue;
        }
        if (entry.relevancy != nullptr && !entry.relevancy()) {
            ++m_stats.entriesSkippedIrrelevant;
            continue;
        }
        relevantSenders.insert(netId);
        const Math::Vector3UVE position = entry.sampler();
        if (!full) {
            const auto baseline = m_baselines.find(netId);
            if (baseline != m_baselines.end() &&
                UnchangedUVE(position, baseline->second, m_positionEpsilon)) {
                ++m_stats.entriesSkippedUnchanged;
                continue;
            }
        }
        if (included.size() >= kReplicationMaximumEntriesPerSnapshotUVE) {
            ++m_stats.entriesTruncated;
            continue;
        }
        included.emplace_back(netId, position);
    }
    std::vector<std::byte> framed;
    if (!full) {
        Utilities::AppendUint16LeUVE(framed, kReplicationDeltaMarkerUVE);
    }
    Utilities::AppendUint16LeUVE(framed, static_cast<std::uint16_t>(included.size()));
    for (const auto& [netId, position] : included) {
        Utilities::AppendUint32LeUVE(framed, netId);
        Utilities::AppendFloatLeUVE(framed, position.x);
        Utilities::AppendFloatLeUVE(framed, position.y);
        Utilities::AppendFloatLeUVE(framed, position.z);
    }
    std::vector<std::uint8_t> datagram;
    datagram.reserve(framed.size());
    for (const std::byte byte : framed) {
        datagram.push_back(static_cast<std::uint8_t>(byte));
    }
    if (SendPayloadUVE(datagram)) {
        for (const auto& [netId, position] : included) {
            m_baselines[netId] = position;
        }
        ++m_stats.snapshotsSent;
        if (!full) {
            ++m_stats.deltaSnapshotsSent;
        }
        ++m_sendsCompleted;
        m_lastSendMs = nowMs;
    }
    // Property frames ride the snapshot tick - same cadence, same full schedule - but send
    // independently: a failed snapshot still leaves the properties their own attempt, and a
    // quiet tick (nothing dirty, no full due) sends no frame at all.
    SendPropertiesUVE(full, relevantSenders);
}

void NetworkReplicatorUVE::SendPropertiesUVE(
    const bool full, const std::set<NetworkEntityIdUVE>& relevantSenders) {
    std::vector<std::pair<NetworkEntityIdUVE, ReplicationPropertyIdUVE>> included;
    std::vector<std::byte> entries;
    for (const auto& [key, prop] : m_properties) {
        const auto entry = m_entries.find(key.first);
        if (entry == m_entries.end() ||
            entry->second.authority == ReplicationAuthorityUVE::Proxy ||
            relevantSenders.find(key.first) == relevantSenders.end() || !prop.hasValue ||
            (!full && !prop.dirty)) {
            continue;
        }
        constexpr std::size_t kPropertyHeaderBytesUVE = 2U * sizeof(std::uint16_t);
        const std::size_t entryBytes = sizeof(NetworkEntityIdUVE) +
                                       sizeof(ReplicationPropertyIdUVE) + prop.encoded.size();
        if (kPropertyHeaderBytesUVE + entries.size() + entryBytes >
            kSessionMaximumPayloadBytesUVE) {
            ++m_stats.propertiesTruncated;
            continue;
        }
        Utilities::AppendUint32LeUVE(entries, key.first);
        entries.push_back(static_cast<std::byte>(key.second));
        entries.insert(entries.end(), prop.encoded.begin(), prop.encoded.end());
        included.push_back(key);
    }
    if (included.empty()) {
        return;
    }
    std::vector<std::byte> framed;
    Utilities::AppendUint16LeUVE(framed, kReplicationPropertyMarkerUVE);
    Utilities::AppendUint16LeUVE(framed, static_cast<std::uint16_t>(included.size()));
    framed.insert(framed.end(), entries.begin(), entries.end());
    std::vector<std::uint8_t> datagram;
    datagram.reserve(framed.size());
    for (const std::byte byte : framed) {
        datagram.push_back(static_cast<std::uint8_t>(byte));
    }
    if (!SendPayloadUVE(datagram)) {
        return;
    }
    for (const auto& key : included) {
        m_properties[key].dirty = false;
    }
    ++m_stats.propertyFramesSent;
}

void NetworkReplicatorUVE::HandleSnapshotUVE(const std::vector<std::uint8_t>& payload) {
    // Strict framing: the byte count must match the declared entry count exactly, or the whole
    // message goes out unapplied - no partial snapshots, no trailing bytes, no exceptions.
    // Delta entries are absolute positions, identical in shape to full ones, so both kinds
    // parse and apply through the same path once the marker is read. Control frames are the
    // third citizen: exactly 7 bytes, a valid kind, or malformed like everything else.
    std::vector<std::byte> buffer;
    buffer.reserve(payload.size());
    for (const std::uint8_t byte : payload) {
        buffer.push_back(static_cast<std::byte>(byte));
    }
    std::size_t offset = 0U;
    std::uint16_t first = 0U;
    if (!Utilities::ReadUint16LeFromBufferUVE(buffer, offset, first)) {
        ++m_stats.messagesIgnoredMalformed;
        return;
    }
    if (first == kReplicationControlMarkerUVE) {
        if (buffer.size() != kTransferControlBytesUVE) {
            ++m_stats.messagesIgnoredMalformed;
            return;
        }
        const std::uint8_t kind = std::to_integer<std::uint8_t>(buffer[offset++]);
        NetworkEntityIdUVE netId = 0U;
        if (kind != kTransferRequestKindUVE && kind != kTransferAcceptKindUVE &&
            kind != kTransferDenyKindUVE) {
            ++m_stats.messagesIgnoredMalformed;
            return;
        }
        if (!Utilities::ReadUint32LeFromBufferUVE(buffer, offset, netId)) {
            ++m_stats.messagesIgnoredMalformed;
            return;
        }
        HandleControlUVE(kind, netId);
        return;
    }
    if (first == kReplicationPropertyMarkerUVE) {
        HandlePropertiesUVE(buffer, offset);
        return;
    }
    const bool delta = first == kReplicationDeltaMarkerUVE;
    std::uint16_t entryCount = first;
    std::size_t headerBytes = sizeof(std::uint16_t);
    if (delta) {
        headerBytes += sizeof(std::uint16_t);
        if (!Utilities::ReadUint16LeFromBufferUVE(buffer, offset, entryCount)) {
            ++m_stats.messagesIgnoredMalformed;
            return;
        }
    }
    if (buffer.size() !=
        headerBytes + static_cast<std::size_t>(entryCount) *
                          (sizeof(NetworkEntityIdUVE) + 3U * sizeof(float))) {
        ++m_stats.messagesIgnoredMalformed;
        return;
    }
    std::vector<std::pair<NetworkEntityIdUVE, Math::Vector3UVE>> parsed;
    parsed.reserve(entryCount);
    for (std::uint16_t i = 0U; i < entryCount; ++i) {
        NetworkEntityIdUVE netId = 0U;
        Math::Vector3UVE position{};
        if (!Utilities::ReadUint32LeFromBufferUVE(buffer, offset, netId) ||
            !Utilities::ReadFloatLeFromBufferUVE(buffer, offset, position.x) ||
            !Utilities::ReadFloatLeFromBufferUVE(buffer, offset, position.y) ||
            !Utilities::ReadFloatLeFromBufferUVE(buffer, offset, position.z)) {
            ++m_stats.messagesIgnoredMalformed;
            return;
        }
        parsed.emplace_back(netId, position);
    }
    ++m_stats.snapshotsReceived;
    if (delta) {
        ++m_stats.deltaSnapshotsReceived;
    }
    for (const auto& [netId, position] : parsed) {
        const auto entry = m_entries.find(netId);
        if (entry == m_entries.end()) {
            ++m_stats.entriesIgnoredUnknown;
            continue;
        }
        if (entry->second.authority == ReplicationAuthorityUVE::Owner) {
            ++m_stats.entriesIgnoredOwned;
            continue;
        }
        entry->second.applier(position);
        ++m_stats.entriesApplied;
    }
}

void NetworkReplicatorUVE::HandleControlUVE(const std::uint8_t kind,
                                            const NetworkEntityIdUVE netId) {
    if (kind == kTransferRequestKindUVE) {
        const auto entry = m_entries.find(netId);
        // Only an owner with a handler can decide: anything else answers deny on the spot,
        // best-effort - a lost deny degrades to the requester's timeout, which tells the
        // same story one second later.
        if (entry == m_entries.end() || entry->second.authority != ReplicationAuthorityUVE::Owner ||
            m_transferHandler == nullptr) {
            if (SendControlUVE(kTransferDenyKindUVE, netId)) {
                ++m_stats.transfersDenied;
            }
            return;
        }
        m_awaitingDecision.insert(netId);
        NotifyUVE(netId, OwnershipTransferEventUVE::Requested);
        return;
    }
    const auto open = m_openRequests.find(netId);
    if (open == m_openRequests.end()) {
        ++m_stats.transfersIgnoredStale;
        return;
    }
    if (kind == kTransferAcceptKindUVE) {
        const auto entry = m_entries.find(netId);
        if (entry == m_entries.end()) {
            ++m_stats.transfersIgnoredStale;
            return;
        }
        m_openRequests.erase(open);
        entry->second.authority = ReplicationAuthorityUVE::Owner;
        ++m_stats.transfersCompleted;
        NotifyUVE(netId, OwnershipTransferEventUVE::Completed);
        return;
    }
    m_openRequests.erase(open);
    ++m_stats.transfersDenied;
    NotifyUVE(netId, OwnershipTransferEventUVE::Denied);
}

void NetworkReplicatorUVE::HandlePropertiesUVE(const std::vector<std::byte>& buffer,
                                               std::size_t offset) {
    // Strict like snapshots, but variable-length: entries parse in order and the frame must
    // end exactly where the last value does - anything short, over, or undecodable fails
    // the whole frame unapplied.
    std::uint16_t entryCount = 0U;
    if (!Utilities::ReadUint16LeFromBufferUVE(buffer, offset, entryCount)) {
        ++m_stats.messagesIgnoredMalformed;
        return;
    }
    std::vector<std::tuple<NetworkEntityIdUVE, ReplicationPropertyIdUVE, UVScript::ValueUVE>> parsed;
    parsed.reserve(entryCount);
    for (std::uint16_t i = 0U; i < entryCount; ++i) {
        NetworkEntityIdUVE netId = 0U;
        if (!Utilities::ReadUint32LeFromBufferUVE(buffer, offset, netId) ||
            offset >= buffer.size()) {
            ++m_stats.messagesIgnoredMalformed;
            return;
        }
        const ReplicationPropertyIdUVE propId =
            std::to_integer<ReplicationPropertyIdUVE>(buffer[offset++]);
        std::optional<UVScript::ValueUVE> value = ReadWireValueUVE(buffer, offset, 0U);
        if (!value.has_value()) {
            ++m_stats.messagesIgnoredMalformed;
            return;
        }
        parsed.emplace_back(netId, propId, std::move(*value));
    }
    if (offset != buffer.size()) {
        ++m_stats.messagesIgnoredMalformed;
        return;
    }
    ++m_stats.propertyFramesReceived;
    for (auto& [netId, propId, value] : parsed) {
        const auto entry = m_entries.find(netId);
        const auto prop = m_properties.find({netId, propId});
        if (entry == m_entries.end() || prop == m_properties.end() ||
            prop->second.applier == nullptr) {
            ++m_stats.propertiesIgnoredUnknown;
            continue;
        }
        if (entry->second.authority == ReplicationAuthorityUVE::Owner) {
            ++m_stats.propertiesIgnoredOwned;
            continue;
        }
        prop->second.applier(value);
        ++m_stats.propertiesApplied;
    }
}

bool NetworkReplicatorUVE::SendControlUVE(const std::uint8_t kind,
                                          const NetworkEntityIdUVE netId) {
    std::vector<std::byte> framed;
    Utilities::AppendUint16LeUVE(framed, kReplicationControlMarkerUVE);
    framed.push_back(static_cast<std::byte>(kind));
    Utilities::AppendUint32LeUVE(framed, netId);
    std::vector<std::uint8_t> datagram;
    datagram.reserve(framed.size());
    for (const std::byte byte : framed) {
        datagram.push_back(static_cast<std::uint8_t>(byte));
    }
    return SendPayloadUVE(datagram);
}

void NetworkReplicatorUVE::ExpireRequestsUVE(const std::uint64_t nowMs) {
    for (auto open = m_openRequests.begin(); open != m_openRequests.end();) {
        if (nowMs - open->second < m_ownershipTimeoutMs) {
            ++open;
            continue;
        }
        const NetworkEntityIdUVE netId = open->first;
        open = m_openRequests.erase(open);
        ++m_stats.transfersTimedOut;
        NotifyUVE(netId, OwnershipTransferEventUVE::TimedOut);
    }
}

void NetworkReplicatorUVE::NotifyUVE(const NetworkEntityIdUVE netId,
                                     const OwnershipTransferEventUVE event) {
    if (m_transferHandler != nullptr) {
        m_transferHandler(netId, event);
    }
}

} // namespace UVE::Network
