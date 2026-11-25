// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "network_replication.h"
#include "network_value_codec.h"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <set>
#include <tuple>
#include <utility>
#include <vector>

#include "binary_buffer.h"

namespace UVE::Network {
namespace {

// Per-component stillness: every axis within the epsilon means the entity never moved. A NaN
// sample compares unequal to everything (including the epsilon), so corrupt samplers ship
// every tick - loud, like before, instead of silently going quiet.
[[nodiscard]] bool Unchanged(const Math::Vector3& sampled, const Math::Vector3& baseline,
                                const float epsilon) noexcept {
    return std::fabs(sampled.x - baseline.x) <= epsilon &&
           std::fabs(sampled.y - baseline.y) <= epsilon &&
           std::fabs(sampled.z - baseline.z) <= epsilon;
}

// Ownership-control kinds: the third byte of a control frame, after the marker.
inline constexpr std::uint8_t kTransferRequestKind = 0x01U;
inline constexpr std::uint8_t kTransferAcceptKind = 0x02U;
inline constexpr std::uint8_t kTransferDenyKind = 0x03U;
inline constexpr std::size_t kTransferControlBytes = 7U;

} // namespace

NetworkReplicator::NetworkReplicator(NetworkSession&& session,
                                           const std::uint64_t sendIntervalMs) noexcept
    : m_session(std::move(session)), m_sendIntervalMs(sendIntervalMs) {}

NetworkReplicator::NetworkReplicator(NetworkSession& sharedSession,
                                           const NetworkChannelId channel,
                                           const std::uint64_t sendIntervalMs) noexcept
    : m_session(&sharedSession), m_channel(channel), m_sendIntervalMs(sendIntervalMs) {}

bool NetworkReplicator::SendPayload(const std::vector<std::uint8_t>& payload) {
    if (std::holds_alternative<NetworkSession*>(m_session)) {
        return Session().SendChannelData(m_channel, payload);
    }
    return Session().SendData(payload);
}

std::optional<std::vector<std::uint8_t>> NetworkReplicator::ReceivePayload() {
    if (std::holds_alternative<NetworkSession*>(m_session)) {
        return Session().TryReceiveChannelData(m_channel);
    }
    return Session().TryReceiveData();
}

bool NetworkReplicator::RegisterEntity(const NetworkEntityId netId, PositionSampler sampler,
                                             PositionApplier applier,
                                             const ReplicationAuthority authority,
                                             RelevancyCheck relevancy) {
    if (sampler == nullptr || applier == nullptr || m_entries.contains(netId)) {
        return false;
    }
    ReplicationEntry entry;
    entry.sampler = std::move(sampler);
    entry.applier = std::move(applier);
    entry.authority = authority;
    entry.relevancy = std::move(relevancy);
    m_entries.emplace(netId, std::move(entry));
    return true;
}

bool NetworkReplicator::UnregisterEntity(const NetworkEntityId netId) noexcept {
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

bool NetworkReplicator::SetAuthority(const NetworkEntityId netId,
                                           const ReplicationAuthority authority) noexcept {
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

bool NetworkReplicator::SetRelevancy(const NetworkEntityId netId,
                                           RelevancyCheck relevancy) noexcept {
    const auto entry = m_entries.find(netId);
    if (entry == m_entries.end()) {
        return false;
    }
    entry->second.relevancy = std::move(relevancy);
    return true;
}

bool NetworkReplicator::RegisterProperty(const NetworkEntityId netId,
                                               const ReplicationPropertyId propId,
                                               PropertyApplier applier) {
    if (m_entries.find(netId) == m_entries.end() || applier == nullptr) {
        return false;
    }
    PropertyState& prop = m_properties[{netId, propId}];
    if (prop.applier != nullptr) {
        return false;
    }
    prop.applier = std::move(applier);
    return true;
}

bool NetworkReplicator::UnregisterProperty(const NetworkEntityId netId,
                                                 const ReplicationPropertyId propId) noexcept {
    return m_properties.erase({netId, propId}) > 0U;
}

bool NetworkReplicator::SetProperty(const NetworkEntityId netId,
                                          const ReplicationPropertyId propId,
                                          const Blueprint::Value& value) {
    if (m_entries.find(netId) == m_entries.end()) {
        return false;
    }
    // Encode-at-Set: the wire bytes are validated and cached here, so the send tick never
    // encodes (never fails) and the dirty check is an exact byte compare - two values with
    // identical bytes are the same value on the wire, whatever == says.
    std::vector<std::byte> encoded;
    if (!AppendWireValue(encoded, value, 0U) ||
        encoded.size() > kReplicationMaximumPropertyBytes) {
        ++m_stats.propertiesSkippedOversized;
        return false;
    }
    PropertyState& prop = m_properties[{netId, propId}];
    if (prop.hasValue && prop.encoded == encoded) {
        return true;
    }
    prop.encoded = std::move(encoded);
    prop.hasValue = true;
    prop.dirty = true;
    return true;
}

bool NetworkReplicator::SetFullSnapshotPeriod(const std::uint32_t period) noexcept {
    if (period == 0U) {
        return false;
    }
    m_fullPeriod = period;
    return true;
}

bool NetworkReplicator::SetPositionEpsilon(const float epsilon) noexcept {
    if (!std::isfinite(epsilon) || epsilon < 0.0F) {
        return false;
    }
    m_positionEpsilon = epsilon;
    return true;
}

bool NetworkReplicator::SetMaxSnapshotChunksPerTick(const std::size_t maxChunks) noexcept {
    if (maxChunks == 0U) {
        return false;
    }
    m_maxSnapshotChunksPerTick = maxChunks;
    return true;
}

bool NetworkReplicator::RequestOwnership(const NetworkEntityId netId,
                                              const std::uint64_t nowMs) {
    const auto entry = m_entries.find(netId);
    if (entry == m_entries.end() || entry->second.authority != ReplicationAuthority::Proxy ||
        m_openRequests.find(netId) != m_openRequests.end() ||
        !SendControl(kTransferRequestKind, netId)) {
        return false;
    }
    m_openRequests.emplace(netId, nowMs);
    ++m_stats.transfersRequested;
    return true;
}

bool NetworkReplicator::AcceptTransfer(const NetworkEntityId netId) {
    const auto entry = m_entries.find(netId);
    if (entry == m_entries.end() || m_awaitingDecision.find(netId) == m_awaitingDecision.end() ||
        !SendControl(kTransferAcceptKind, netId)) {
        return false;
    }
    m_awaitingDecision.erase(netId);
    entry->second.authority = ReplicationAuthority::Proxy;
    ++m_stats.transfersCompleted;
    Notify(netId, OwnershipTransferEvent::Completed);
    return true;
}

bool NetworkReplicator::DenyTransfer(const NetworkEntityId netId) {
    if (m_entries.find(netId) == m_entries.end() ||
        m_awaitingDecision.find(netId) == m_awaitingDecision.end() ||
        !SendControl(kTransferDenyKind, netId)) {
        return false;
    }
    m_awaitingDecision.erase(netId);
    ++m_stats.transfersDenied;
    return true;
}

void NetworkReplicator::SetOwnershipTransferHandler(OwnershipTransferHandler handler) {
    m_transferHandler = std::move(handler);
}

bool NetworkReplicator::SetOwnershipTimeout(const std::uint64_t timeoutMs) noexcept {
    if (timeoutMs == 0U) {
        return false;
    }
    m_ownershipTimeoutMs = timeoutMs;
    return true;
}

void NetworkReplicator::Update(const std::uint64_t nowMs) {
    Session().Update(nowMs);
    for (;;) {
        const std::optional<std::vector<std::uint8_t>> payload = ReceivePayload();
        if (!payload.has_value()) {
            break;
        }
        HandleSnapshot(*payload);
    }
    // Answers land before deadlines: anything the pump just completed is already gone from
    // the open set, so only the truly unanswered expire. Runs unconditionally - a dropped
    // session must not freeze a request past its time.
    ExpireRequests(nowMs);
    if (Session().State() != SessionState::Established ||
        nowMs - m_lastSendMs < m_sendIntervalMs) {
        return;
    }
    // Fulls carry every sending entity and re-baseline the world; deltas carry only what has
    // no baseline or moved past the epsilon. The first send is always full, and a lost delta
    // heals at worst by the next full - the full is the backstop for loss, truncation, and
    // anything an authority flip hid.
    const bool full = m_sendsCompleted % m_fullPeriod == 0U;
    std::vector<std::pair<NetworkEntityId, Math::Vector3>> included;
    const std::size_t maxTotalEntries =
        kReplicationMaximumEntriesPerSnapshot * std::max<std::size_t>(1U, m_maxSnapshotChunksPerTick);
    // Relevancy runs once per entity per tick: the property frame reuses these verdicts
    // instead of re-running app predicates.
    std::set<NetworkEntityId> relevantSenders;
    for (const auto& [netId, entry] : m_entries) {
        if (entry.authority == ReplicationAuthority::Proxy) {
            continue;
        }
        if (entry.relevancy != nullptr && !entry.relevancy()) {
            ++m_stats.entriesSkippedIrrelevant;
            continue;
        }
        relevantSenders.insert(netId);
        const Math::Vector3 position = entry.sampler();
        if (!full) {
            const auto baseline = m_baselines.find(netId);
            if (baseline != m_baselines.end() &&
                Unchanged(position, baseline->second, m_positionEpsilon)) {
                ++m_stats.entriesSkippedUnchanged;
                continue;
            }
        }
        if (included.size() >= maxTotalEntries) {
            ++m_stats.entriesTruncated;
            continue;
        }
        included.emplace_back(netId, position);
    }
    const std::size_t chunkCount =
        included.empty()
            ? 1U
            : ((included.size() + kReplicationMaximumEntriesPerSnapshot - 1U) /
               kReplicationMaximumEntriesPerSnapshot);
    bool anyChunkSent = false;
    for (std::size_t chunkIndex = 0U; chunkIndex < chunkCount; ++chunkIndex) {
        const std::size_t beginIndex = chunkIndex * kReplicationMaximumEntriesPerSnapshot;
        const std::size_t endIndex =
            std::min(included.size(), beginIndex + kReplicationMaximumEntriesPerSnapshot);
        const std::size_t chunkSize = endIndex - beginIndex;
        std::vector<std::byte> framed;
        if (!full) {
            Utilities::AppendUint16Le(framed, kReplicationDeltaMarker);
        }
        Utilities::AppendUint16Le(framed, static_cast<std::uint16_t>(chunkSize));
        for (std::size_t i = beginIndex; i < endIndex; ++i) {
            const auto& [netId, position] = included[i];
            Utilities::AppendUint32Le(framed, netId);
            Utilities::AppendFloatLe(framed, position.x);
            Utilities::AppendFloatLe(framed, position.y);
            Utilities::AppendFloatLe(framed, position.z);
        }
        std::vector<std::uint8_t> datagram;
        datagram.reserve(framed.size());
        for (const std::byte byte : framed) {
            datagram.push_back(static_cast<std::uint8_t>(byte));
        }
        if (!SendPayload(datagram)) {
            break;
        }
        for (std::size_t i = beginIndex; i < endIndex; ++i) {
            const auto& [netId, position] = included[i];
            m_baselines[netId] = position;
        }
        ++m_stats.snapshotsSent;
        if (!full) {
            ++m_stats.deltaSnapshotsSent;
        }
        anyChunkSent = true;
    }
    if (anyChunkSent) {
        ++m_sendsCompleted;
        m_lastSendMs = nowMs;
    }
    // Property frames ride the snapshot tick - same cadence, same full schedule - but send
    // independently: a failed snapshot still leaves the properties their own attempt, and a
    // quiet tick (nothing dirty, no full due) sends no frame at all.
    SendProperties(full, relevantSenders);
}

void NetworkReplicator::SendProperties(
    const bool full, const std::set<NetworkEntityId>& relevantSenders) {
    std::vector<std::pair<NetworkEntityId, ReplicationPropertyId>> included;
    std::vector<std::byte> entries;
    for (const auto& [key, prop] : m_properties) {
        const auto entry = m_entries.find(key.first);
        if (entry == m_entries.end() ||
            entry->second.authority == ReplicationAuthority::Proxy ||
            relevantSenders.find(key.first) == relevantSenders.end() || !prop.hasValue ||
            (!full && !prop.dirty)) {
            continue;
        }
        constexpr std::size_t kPropertyHeaderBytes = 2U * sizeof(std::uint16_t);
        const std::size_t entryBytes = sizeof(NetworkEntityId) +
                                       sizeof(ReplicationPropertyId) + prop.encoded.size();
        if (kPropertyHeaderBytes + entries.size() + entryBytes >
            kSessionMaximumPayloadBytes) {
            ++m_stats.propertiesTruncated;
            continue;
        }
        Utilities::AppendUint32Le(entries, key.first);
        entries.push_back(static_cast<std::byte>(key.second));
        entries.insert(entries.end(), prop.encoded.begin(), prop.encoded.end());
        included.push_back(key);
    }
    if (included.empty()) {
        return;
    }
    std::vector<std::byte> framed;
    Utilities::AppendUint16Le(framed, kReplicationPropertyMarker);
    Utilities::AppendUint16Le(framed, static_cast<std::uint16_t>(included.size()));
    framed.insert(framed.end(), entries.begin(), entries.end());
    std::vector<std::uint8_t> datagram;
    datagram.reserve(framed.size());
    for (const std::byte byte : framed) {
        datagram.push_back(static_cast<std::uint8_t>(byte));
    }
    if (!SendPayload(datagram)) {
        return;
    }
    for (const auto& key : included) {
        m_properties[key].dirty = false;
    }
    ++m_stats.propertyFramesSent;
}

void NetworkReplicator::HandleSnapshot(const std::vector<std::uint8_t>& payload) {
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
    if (!Utilities::ReadUint16LeFromBuffer(buffer, offset, first)) {
        ++m_stats.messagesIgnoredMalformed;
        return;
    }
    if (first == kReplicationControlMarker) {
        if (buffer.size() != kTransferControlBytes) {
            ++m_stats.messagesIgnoredMalformed;
            return;
        }
        const std::uint8_t kind = std::to_integer<std::uint8_t>(buffer[offset++]);
        NetworkEntityId netId = 0U;
        if (kind != kTransferRequestKind && kind != kTransferAcceptKind &&
            kind != kTransferDenyKind) {
            ++m_stats.messagesIgnoredMalformed;
            return;
        }
        if (!Utilities::ReadUint32LeFromBuffer(buffer, offset, netId)) {
            ++m_stats.messagesIgnoredMalformed;
            return;
        }
        HandleControl(kind, netId);
        return;
    }
    if (first == kReplicationPropertyMarker) {
        HandleProperties(buffer, offset);
        return;
    }
    const bool delta = first == kReplicationDeltaMarker;
    std::uint16_t entryCount = first;
    std::size_t headerBytes = sizeof(std::uint16_t);
    if (delta) {
        headerBytes += sizeof(std::uint16_t);
        if (!Utilities::ReadUint16LeFromBuffer(buffer, offset, entryCount)) {
            ++m_stats.messagesIgnoredMalformed;
            return;
        }
    }
    if (buffer.size() !=
        headerBytes + static_cast<std::size_t>(entryCount) *
                          (sizeof(NetworkEntityId) + 3U * sizeof(float))) {
        ++m_stats.messagesIgnoredMalformed;
        return;
    }
    std::vector<std::pair<NetworkEntityId, Math::Vector3>> parsed;
    parsed.reserve(entryCount);
    for (std::uint16_t i = 0U; i < entryCount; ++i) {
        NetworkEntityId netId = 0U;
        Math::Vector3 position{};
        if (!Utilities::ReadUint32LeFromBuffer(buffer, offset, netId) ||
            !Utilities::ReadFloatLeFromBuffer(buffer, offset, position.x) ||
            !Utilities::ReadFloatLeFromBuffer(buffer, offset, position.y) ||
            !Utilities::ReadFloatLeFromBuffer(buffer, offset, position.z)) {
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
        if (entry->second.authority == ReplicationAuthority::Owner) {
            ++m_stats.entriesIgnoredOwned;
            continue;
        }
        entry->second.applier(position);
        ++m_stats.entriesApplied;
    }
}

void NetworkReplicator::HandleControl(const std::uint8_t kind,
                                            const NetworkEntityId netId) {
    if (kind == kTransferRequestKind) {
        const auto entry = m_entries.find(netId);
        // Only an owner with a handler can decide: anything else answers deny on the spot,
        // best-effort - a lost deny degrades to the requester's timeout, which tells the
        // same story one second later.
        if (entry == m_entries.end() || entry->second.authority != ReplicationAuthority::Owner ||
            m_transferHandler == nullptr) {
            if (SendControl(kTransferDenyKind, netId)) {
                ++m_stats.transfersDenied;
            }
            return;
        }
        m_awaitingDecision.insert(netId);
        Notify(netId, OwnershipTransferEvent::Requested);
        return;
    }
    const auto open = m_openRequests.find(netId);
    if (open == m_openRequests.end()) {
        ++m_stats.transfersIgnoredStale;
        return;
    }
    if (kind == kTransferAcceptKind) {
        const auto entry = m_entries.find(netId);
        if (entry == m_entries.end()) {
            ++m_stats.transfersIgnoredStale;
            return;
        }
        m_openRequests.erase(open);
        entry->second.authority = ReplicationAuthority::Owner;
        ++m_stats.transfersCompleted;
        Notify(netId, OwnershipTransferEvent::Completed);
        return;
    }
    m_openRequests.erase(open);
    ++m_stats.transfersDenied;
    Notify(netId, OwnershipTransferEvent::Denied);
}

void NetworkReplicator::HandleProperties(const std::vector<std::byte>& buffer,
                                               std::size_t offset) {
    // Strict like snapshots, but variable-length: entries parse in order and the frame must
    // end exactly where the last value does - anything short, over, or undecodable fails
    // the whole frame unapplied.
    std::uint16_t entryCount = 0U;
    if (!Utilities::ReadUint16LeFromBuffer(buffer, offset, entryCount)) {
        ++m_stats.messagesIgnoredMalformed;
        return;
    }
    std::vector<std::tuple<NetworkEntityId, ReplicationPropertyId, Blueprint::Value>> parsed;
    parsed.reserve(entryCount);
    for (std::uint16_t i = 0U; i < entryCount; ++i) {
        NetworkEntityId netId = 0U;
        if (!Utilities::ReadUint32LeFromBuffer(buffer, offset, netId) ||
            offset >= buffer.size()) {
            ++m_stats.messagesIgnoredMalformed;
            return;
        }
        const ReplicationPropertyId propId =
            std::to_integer<ReplicationPropertyId>(buffer[offset++]);
        std::optional<Blueprint::Value> value = ReadWireValue(buffer, offset, 0U);
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
        if (entry->second.authority == ReplicationAuthority::Owner) {
            ++m_stats.propertiesIgnoredOwned;
            continue;
        }
        prop->second.applier(value);
        ++m_stats.propertiesApplied;
    }
}

bool NetworkReplicator::SendControl(const std::uint8_t kind,
                                          const NetworkEntityId netId) {
    std::vector<std::byte> framed;
    Utilities::AppendUint16Le(framed, kReplicationControlMarker);
    framed.push_back(static_cast<std::byte>(kind));
    Utilities::AppendUint32Le(framed, netId);
    std::vector<std::uint8_t> datagram;
    datagram.reserve(framed.size());
    for (const std::byte byte : framed) {
        datagram.push_back(static_cast<std::uint8_t>(byte));
    }
    return SendPayload(datagram);
}

void NetworkReplicator::ExpireRequests(const std::uint64_t nowMs) {
    for (auto open = m_openRequests.begin(); open != m_openRequests.end();) {
        if (nowMs - open->second < m_ownershipTimeoutMs) {
            ++open;
            continue;
        }
        const NetworkEntityId netId = open->first;
        open = m_openRequests.erase(open);
        ++m_stats.transfersTimedOut;
        Notify(netId, OwnershipTransferEvent::TimedOut);
    }
}

void NetworkReplicator::Notify(const NetworkEntityId netId,
                                     const OwnershipTransferEvent event) {
    if (m_transferHandler != nullptr) {
        m_transferHandler(netId, event);
    }
}

} // namespace UVE::Network
