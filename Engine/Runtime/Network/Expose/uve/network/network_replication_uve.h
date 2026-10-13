// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
#pragma once
//
// Scope: the replication core - fixed-rate position snapshots over an established session.
// Each side registers the entities it replicates by stable net id with a sampler (read the
// local position) and an applier (write a received one); the replicator frames snapshots as
// little-endian session payloads and applies what arrives. Authority is per-entity direction:
// owners sample and send but never take inbound back, proxies apply but never send, and the
// default shared role does both, exactly like before. Relevancy is a per-entity send filter:
// the app's predicate runs on the send tick for sending roles, and irrelevant entities are
// neither sampled nor sent (an all-irrelevant tick still emits the empty keepalive snapshot).
// Sends are delta-compressed: between periodic full snapshots, only entities whose position
// moved past the epsilon (or never shipped) cross the wire, so a quiet world costs keepalive
// bytes. Callbacks are the layering: Network never touches the scene graph or the entity
// manager, so production wires samplers to world reads and appliers to SceneGraphUVE writes
// while tests use plain locals. Authority and relevancy are local policy - nothing about
// them crosses the wire. Ownership moves over a request/accept/deny handshake between the
// sides - no more app-coordinated both-ends rebinding. Non-position properties ride
// alongside: named script values pushed per entity and delta-carried like positions - set
// locally, applied remotely. The Gameplay wiring lives beside the character mover
// (Objects/3D/Physics replicated_entity_uve.h): world-read samplers with last-known hold,
// scene-graph appliers, and the net-id map that is this file's local-entity mapping.
//
#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <set>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

#include "uve/math/vector3_uve.h"
#include "uve/network/network_session_uve.h"
#include "uve/uvscript/uvscript_value_uve.h"

namespace UVE::Network {

/// Stable identity shared by both peers; the local-entity mapping lives outside Network.
using NetworkEntityIdUVE = std::uint32_t;
/// Reads the current local position of a registered entity. Called on the send tick.
using PositionSamplerUVE = std::function<Math::Vector3UVE()>;
/// Writes a received position for a registered entity. Called while pumping, in net-id order.
using PositionApplierUVE = std::function<void(const Math::Vector3UVE&)>;
/// Interest check for one registered entity. Called on the send tick for sending roles only
/// (owners and shared); false skips the entity - neither sampled nor sent. Empty means
/// always relevant.
using RelevancyCheckUVE = std::function<bool()>;
/// Identifies one property on an entity; the app owns the namespace, all 256 values.
using ReplicationPropertyIdUVE = std::uint8_t;
/// Writes a received property value for a registered (entity, property) pair. Called while
/// pumping, in (net-id, property-id) order.
using PropertyApplierUVE = std::function<void(const UVScript::ValueUVE&)>;

/// Ownership-handoff outcomes, reported through the transfer handler: Requested means the
/// peer asks for one of my owned entities (answer with Accept or Deny); Completed means a
/// flip happened on my side; Denied and TimedOut close my own open request.
enum class OwnershipTransferEventUVE : std::uint8_t { Requested, Completed, Denied, TimedOut };
/// Ownership-handoff notifications: the entity plus what happened. Answering a Requested
/// immediately is safe - accepting or denying reentrantly from inside the handler sends
/// from the pump without disturbing it.
using OwnershipTransferHandlerUVE =
    std::function<void(NetworkEntityIdUVE, OwnershipTransferEventUVE)>;

/// Per-entity direction. Shared is the original symmetric behavior (send and apply); Owner
/// samples and sends but ignores inbound entries for the entity (what I simulate, I don't
/// take back); Proxy applies but never samples or sends. Local policy only: the peer never
/// sees my roles, so both sides must agree out of band - two proxies means a frozen entity,
/// two owners means last-writer-wins flapping, exactly as configured.
enum class ReplicationAuthorityUVE : std::uint8_t { Shared, Owner, Proxy };

/// Default snapshot cadence: 10 Hz, the genre-standard floor for position replication.
inline constexpr std::uint64_t kDefaultReplicationIntervalMsUVE = 100U;
/// Entries per snapshot: a u16 count plus 16-byte entries must fit one session payload (74).
inline constexpr std::size_t kReplicationMaximumEntriesPerSnapshotUVE =
    (kSessionMaximumPayloadBytesUVE - sizeof(std::uint16_t)) /
    (sizeof(NetworkEntityIdUVE) + 3U * sizeof(float));
/// Delta marker: a snapshot whose first u16 is this carries only changed entries (a second
/// u16 then holds their count). No full snapshot can ever declare this many entries - the
/// payload fits dozens - so the marker is permanently spare, and full snapshots stay byte
/// for byte what they always were.
inline constexpr std::uint16_t kReplicationDeltaMarkerUVE = 0xFFFFU;
/// Full snapshots every 30 sends (3 seconds at the default cadence): the backstop that
/// re-baselines everything a lost delta left behind. Period 1 means every send is full -
/// the legacy behavior, kept as the escape hatch.
inline constexpr std::uint32_t kDefaultFullSnapshotPeriodUVE = 30U;
/// Per-component movement that counts as a change (metres): the prediction slice's position
/// epsilon, so "moved" means the same everywhere. Zero means exact equality.
inline constexpr float kDefaultDeltaPositionEpsilonUVE = 0.001F;
/// Control marker: a replication payload starting with this u16 is one ownership-control
/// frame ([marker, kind, netId], 7 bytes), never entries. Spare for the same reason as the
/// delta marker - no snapshot can declare this many entries.
inline constexpr std::uint16_t kReplicationControlMarkerUVE = 0xFFFEU;
/// Ownership requests expire after this long without an answer: the requester is notified
/// and may ask again. Ten ticks at the default cadence - generous on a LAN, loud on loss.
inline constexpr std::uint64_t kDefaultOwnershipTimeoutMsUVE = 1000U;
/// Property marker: a replication payload starting with this u16 carries (entity, property,
/// value) entries - a u16 count, then netId u32, propId u8, and a tagged wire value each.
/// Spare for the same reason as the delta and control markers; properties share the
/// replication session, never a channel of their own.
inline constexpr std::uint16_t kReplicationPropertyMarkerUVE = 0xFFFDU;
/// Largest encoded value a property carries: one entry alone must still fit a payload
/// (marker, count, netId, propId: 9 bytes of framing). SetPropertyUVE refuses anything
/// bigger, counted, before storing.
inline constexpr std::size_t kReplicationMaximumPropertyBytesUVE =
    kSessionMaximumPayloadBytesUVE - 9U;

struct ReplicationStatsUVE final {
    std::uint64_t snapshotsSent = 0U;
    std::uint64_t snapshotsReceived = 0U;
    std::uint64_t entriesApplied = 0U;
    std::uint64_t entriesIgnoredUnknown = 0U;
    std::uint64_t entriesTruncated = 0U;
    std::uint64_t messagesIgnoredMalformed = 0U;
    // Appended, never inserted: positional initializers elsewhere bind by order.
    std::uint64_t entriesSkippedIrrelevant = 0U;
    std::uint64_t entriesIgnoredOwned = 0U;
    std::uint64_t deltaSnapshotsSent = 0U;
    std::uint64_t deltaSnapshotsReceived = 0U;
    std::uint64_t entriesSkippedUnchanged = 0U;
    std::uint64_t transfersRequested = 0U;
    std::uint64_t transfersCompleted = 0U;
    std::uint64_t transfersDenied = 0U;
    std::uint64_t transfersTimedOut = 0U;
    std::uint64_t transfersIgnoredStale = 0U;
    std::uint64_t propertyFramesSent = 0U;
    std::uint64_t propertyFramesReceived = 0U;
    std::uint64_t propertiesApplied = 0U;
    std::uint64_t propertiesIgnoredUnknown = 0U;
    std::uint64_t propertiesIgnoredOwned = 0U;
    std::uint64_t propertiesSkippedOversized = 0U;
    std::uint64_t propertiesTruncated = 0U;

    [[nodiscard]] bool operator==(const ReplicationStatsUVE&) const noexcept = default;
};

/// Fixed-rate snapshot exchange over one session. Drives the session's pump (Update drives the
/// session first, so handshakes and timeouts flow through the same call), sends while
/// established, and applies whatever well-formed snapshots arrive - minus what authority
/// forbids and relevancy skips. Sends are delta-compressed between periodic full snapshots:
/// an entity ships when it has no baseline (first sighting, or re-registered), when it moved
/// past the epsilon, or when the full comes due. Move-only (owning its session, or borrowing
/// a shared one);
/// single-threaded like everything below it. Properties ride the same tick: pushed values,
/// carried when dirty or when the full comes due, at most one frame per tick - and no frame
/// at all when nothing waits, since snapshots already keep the session alive.
/// Shared mode borrows a multiplexed session and rides one channel; the app owns the
/// session and both ends agree channels out of band.
class NetworkReplicatorUVE final {
public:
    /// Owning mode: moves the session in. The sink is an rvalue reference, not by value: a
    /// by-value sink would tie with the shared overload below for lvalue sessions, since the
    /// deleted copy still participates in overload resolution.
    explicit NetworkReplicatorUVE(NetworkSessionUVE&& session,
                                  std::uint64_t sendIntervalMs = kDefaultReplicationIntervalMsUVE) noexcept;

    /// Shares a multiplexed session: snapshots and property frames ride `channel` while other
    /// features ride theirs. The session must be multiplexed (both ends agree out of band)
    /// and must outlive the replicator; UpdateUVE pumps it like an owned one, so any
    /// feature's Update drives the handshake for all.
    explicit NetworkReplicatorUVE(NetworkSessionUVE& sharedSession, NetworkChannelIdUVE channel,
                                  std::uint64_t sendIntervalMs = kDefaultReplicationIntervalMsUVE) noexcept;

    /// Registers `netId` with a direction and an interest filter. Shared sends and applies;
    /// Owner sends but ignores inbound; Proxy applies but never sends. Wire both callbacks
    /// regardless - a later flip to Owner starts sampling immediately. The relevancy check
    /// runs on the send tick for sending roles; empty means always relevant. A fresh
    /// registration has no delta baseline, so it ships on the next send. Returns false
    /// (registering nothing) on a duplicate id or an empty sampler/applier - unregister
    /// first to rebind.
    [[nodiscard]] bool RegisterEntityUVE(NetworkEntityIdUVE netId, PositionSamplerUVE sampler,
                                         PositionApplierUVE applier,
                                         ReplicationAuthorityUVE authority = ReplicationAuthorityUVE::Shared,
                                         RelevancyCheckUVE relevancy = RelevancyCheckUVE{});
    /// Drops `netId`'s registration and forgets its delta baseline and cached properties,
    /// so a re-registration ships unconditionally. Returns false when it was never registered.
    [[nodiscard]] bool UnregisterEntityUVE(NetworkEntityIdUVE netId) noexcept;
    /// Rebinds `netId`'s direction live: this is the app-coordinated ownership handoff (both
    /// sides call it; no handshake travels the wire yet). The delta baseline and cached
    /// properties survive the flip - what was true before is still true after - and the next
    /// full re-baselines anything the silence hid. Returns false when never registered.
    [[nodiscard]] bool SetAuthorityUVE(NetworkEntityIdUVE netId,
                                       ReplicationAuthorityUVE authority) noexcept;
    /// Rebinds `netId`'s interest filter live; empty means always relevant. Consulted for
    /// sending roles only, so setting one on a proxy is stored but never called. Returns
    /// false when never registered.
    [[nodiscard]] bool SetRelevancyUVE(NetworkEntityIdUVE netId, RelevancyCheckUVE relevancy) noexcept;
    /// Every `period` sends is a full snapshot (the first send always is); the rest carry
    /// only changed entries. Period 1 disables deltas. Returns false (keeping the old
    /// period) for zero.
    [[nodiscard]] bool SetFullSnapshotPeriodUVE(std::uint32_t period) noexcept;
    /// Per-component movement that marks an entity changed. Zero means exact equality.
    /// Returns false (keeping the old epsilon) for NaN or a negative.
    [[nodiscard]] bool SetPositionEpsilonUVE(float epsilon) noexcept;
    /// Asks the peer for ownership of `netId`: sends a request immediately and reports the
    /// answer (or the timeout) through the transfer handler. Single-flight per entity -
    /// one open request at a time. Returns false (sending nothing) unless the entity is
    /// registered, locally a Proxy, has no open request, and the session is established.
    [[nodiscard]] bool RequestOwnershipUVE(NetworkEntityIdUVE netId, std::uint64_t nowMs);
    /// Grants a requested entity to the peer: sends the accept and flips local authority
    /// to Proxy - the requester flips to Owner when the accept lands. Returns false unless
    /// a request for `netId` is awaiting my decision, or the accept will not send (the
    /// request stays open: answer again).
    [[nodiscard]] bool AcceptTransferUVE(NetworkEntityIdUVE netId);
    /// Refuses a requested entity: sends the deny, nothing flips. Returns false unless a
    /// request for `netId` is awaiting my decision, or the deny will not send (the request
    /// stays open: answer again).
    [[nodiscard]] bool DenyTransferUVE(NetworkEntityIdUVE netId);
    /// Sets the handoff notification handler; empty clears it. Without a handler, incoming
    /// requests cannot be decided and are denied automatically.
    void SetOwnershipTransferHandlerUVE(OwnershipTransferHandlerUVE handler);
    /// Requests expire after `timeoutMs` without an answer, open requests included.
    /// Returns false (keeping the old timeout) for zero.
    [[nodiscard]] bool SetOwnershipTimeoutUVE(std::uint64_t timeoutMs) noexcept;
    /// Registers an applier for one (entity, property) pair: received values for `propId`
    /// on `netId` land here. The entity must already be registered; the sender needs no
    /// registration - SetPropertyUVE carries the value. Returns false (registering nothing)
    /// unless the entity is registered, the applier is non-empty, and the pair has none yet.
    [[nodiscard]] bool RegisterPropertyUVE(NetworkEntityIdUVE netId, ReplicationPropertyIdUVE propId,
                                           PropertyApplierUVE applier);
    /// Drops the (entity, property) pair: the applier and any cached value go, so sending
    /// stops and inbound entries for it count unknown. Returns false when the pair holds
    /// nothing - never registered and never Set.
    [[nodiscard]] bool UnregisterPropertyUVE(NetworkEntityIdUVE netId,
                                             ReplicationPropertyIdUVE propId) noexcept;
    /// Pushes a value for one of a registered entity's properties: encodes it now
    /// (unencodable or oversized values refuse, counted, nothing stored) and ships it on the
    /// next send tick - then re-ships only when the bytes change, or when a full tick
    /// re-carries everything as the loss backstop. The property needs no local registration
    /// (receivers register appliers); proxies hold the value and ship it on a flip to a
    /// sending role. Byte-identical Sets are no-ops that still return true. Returns false
    /// (storing nothing) unless the entity is registered and the value encodes within the
    /// property budget.
    [[nodiscard]] bool SetPropertyUVE(NetworkEntityIdUVE netId, ReplicationPropertyIdUVE propId,
                                      const UVScript::ValueUVE& value);
    /// Pumps the session, applies every queued snapshot, then sends one when established and due -
    /// plus a property frame when dirty (or full-timed) properties are waiting.
    /// A zero interval sends on every update; a full transport queue skips the send and retries
    /// next update without advancing the cadence, the full schedule, or any baseline. Empty
    /// registrations still send (an empty snapshot doubles as a keepalive: inbound traffic
    /// refreshes the peer's timeout clock), and so do all-irrelevant and all-unchanged
    /// ticks - neither relevancy nor deltas ever starve the keepalive.
    void UpdateUVE(std::uint64_t nowMs);

    /// The session, owned or shared: handshake, disconnect, and state live here, not duplicated.
    [[nodiscard]] NetworkSessionUVE& SessionUVE() noexcept {
        if (std::holds_alternative<NetworkSessionUVE>(m_session)) {
            return std::get<NetworkSessionUVE>(m_session);
        }
        return *std::get<NetworkSessionUVE*>(m_session);
    }
    [[nodiscard]] const NetworkSessionUVE& SessionUVE() const noexcept {
        if (std::holds_alternative<NetworkSessionUVE>(m_session)) {
            return std::get<NetworkSessionUVE>(m_session);
        }
        return *std::get<NetworkSessionUVE*>(m_session);
    }
    [[nodiscard]] ReplicationStatsUVE StatsUVE() const noexcept { return m_stats; }
    [[nodiscard]] std::uint32_t FullSnapshotPeriodUVE() const noexcept { return m_fullPeriod; }
    [[nodiscard]] float PositionEpsilonUVE() const noexcept { return m_positionEpsilon; }
    [[nodiscard]] std::uint64_t OwnershipTimeoutUVE() const noexcept {
        return m_ownershipTimeoutMs;
    }

private:
    struct ReplicationEntryUVE final {
        PositionSamplerUVE sampler;
        PositionApplierUVE applier;
        ReplicationAuthorityUVE authority = ReplicationAuthorityUVE::Shared;
        RelevancyCheckUVE relevancy;
    };

    void HandleSnapshotUVE(const std::vector<std::uint8_t>& payload);
    void HandleControlUVE(std::uint8_t kind, NetworkEntityIdUVE netId);
    void HandlePropertiesUVE(const std::vector<std::byte>& buffer, std::size_t offset);
    [[nodiscard]] bool SendControlUVE(std::uint8_t kind, NetworkEntityIdUVE netId);
    [[nodiscard]] bool SendPayloadUVE(const std::vector<std::uint8_t>& payload);
    [[nodiscard]] std::optional<std::vector<std::uint8_t>> ReceivePayloadUVE();
    void SendPropertiesUVE(bool full, const std::set<NetworkEntityIdUVE>& relevantSenders);
    void ExpireRequestsUVE(std::uint64_t nowMs);
    void NotifyUVE(NetworkEntityIdUVE netId, OwnershipTransferEventUVE event);

    // Owned or borrowed: owning mode moves the session in, shared mode borrows a multiplexed
    // one (which must outlive the feature). Moves stay correct either way - a moved borrow
    // still points at the same shared session.
    std::variant<NetworkSessionUVE, NetworkSessionUVE*> m_session;
    // The multiplexed channel, meaningful only when shared; owned mode sends untagged.
    NetworkChannelIdUVE m_channel = 0U;
    std::uint64_t m_sendIntervalMs = kDefaultReplicationIntervalMsUVE;
    std::uint64_t m_lastSendMs = 0U;
    std::uint32_t m_fullPeriod = kDefaultFullSnapshotPeriodUVE;
    float m_positionEpsilon = kDefaultDeltaPositionEpsilonUVE;
    std::uint64_t m_sendsCompleted = 0U;
    std::uint64_t m_ownershipTimeoutMs = kDefaultOwnershipTimeoutMsUVE;
    OwnershipTransferHandlerUVE m_transferHandler;
    // Single-flight handoffs, keyed by entity so a duplicate request re-notifies instead
    // of stacking: my open requests (with the clock reading they went out at), and the
    // peer's requests awaiting my decision. Pending implies registered and un-rebound:
    // Unregister and SetAuthority clear both, so at most one side of a handoff is ever
    // open per entity here.
    std::map<NetworkEntityIdUVE, std::uint64_t> m_openRequests;
    std::set<NetworkEntityIdUVE> m_awaitingDecision;
    // Last shipped position per entity: what "unchanged" compares against. Updated only for
    // entries a send actually carries, so truncated entries, failed sends, and relevancy
    // skips all stay dirty and surface on a later tick - nothing included means nothing
    // forgotten.
    std::unordered_map<NetworkEntityIdUVE, Math::Vector3UVE> m_baselines;
    struct PropertyStateUVE final {
        PropertyApplierUVE applier;
        std::vector<std::byte> encoded;
        bool hasValue = false;
        bool dirty = false;
    };
    // Cached property values by (entity, property), ordered so an over-full tick packs
    // deterministically: the lowest pairs win, the losers stay dirty and count truncated.
    // Set creates entries (senders register nothing); RegisterPropertyUVE fills the applier
    // (receivers Set nothing); UnregisterEntityUVE drops the entity's whole row.
    std::map<std::pair<NetworkEntityIdUVE, ReplicationPropertyIdUVE>, PropertyStateUVE> m_properties;
    // Ordered by net id, so an over-full registration truncates deterministically: the lowest
    // ids among relevant senders always win, and the losers count into entriesTruncated
    // every send. Proxies never compete for slots; the irrelevant never even try out.
    std::map<NetworkEntityIdUVE, ReplicationEntryUVE> m_entries;
    ReplicationStatsUVE m_stats;
};

} // namespace UVE::Network
