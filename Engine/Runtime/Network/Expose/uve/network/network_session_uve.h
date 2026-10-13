// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
#pragma once
//
// Scope: one client-server session over any transport endpoint (the loopback or UDP sockets
// - the session holds the ITransportEndpointUVE seam, so nothing here changes when the wire
// does). Handshake, disconnect, timeout, and client reconnection; exactly one peer per
// session. Multiplexing is opt-in per session, agreed out of band: both ends enable it,
// data frames gain a channel byte past the tag, and inbound data demuxes into per-channel
// queues - while the handshake stays one per session, untagged. No keepalive or multi-peer
// rooms yet (each is a named follow-up, not a missing piece of this one). Time is an
// explicit monotonic millisecond clock passed in:
// production feeds its own clock, tests drive a fake one, and the module stays
// dependency-free.
//
#include <cstddef>
#include <cstdint>
#include <deque>
#include <map>
#include <memory>
#include <optional>
#include <vector>

#include "uve/network/network_transport_uve.h"
#include "uve/network/network_packet_tap_uve.h"

namespace UVE::Network {

/// Wire protocol version the Hello carries. A server answers a foreign version with Bye rather
/// than ignoring it, so the mismatch surfaces as a remote disconnect instead of a bare timeout.
/// Version 2: the Hello grew an 8-byte session token, so a version 1 Hello (tag plus version,
/// no token) is refused like any other foreign version - old clients fail loud, never hang.
inline constexpr std::uint16_t kSessionProtocolVersionUVE = 2U;
/// Tag plus version plus token: the only accepted Hello size for this version. A shorter
/// Hello is ignored as malformed - unless its version is foreign, which is refused first.
inline constexpr std::size_t kSessionHelloBytesUVE = 11U;
/// Largest SendData payload: one tag byte plus payload must fit one datagram.
inline constexpr std::size_t kSessionMaximumPayloadBytesUVE = kTransportMaximumDatagramBytesUVE - 1U;
/// Largest channel payload on a multiplexed session: the channel byte eats one framing byte.
inline constexpr std::size_t kSessionMaximumChannelPayloadBytesUVE =
    kSessionMaximumPayloadBytesUVE - 1U;
/// Idle silence before a connecting or established session gives up. Zero disables timeouts.
inline constexpr std::uint64_t kDefaultSessionTimeoutMsUVE = 5000U;
/// The token of a client that names none: anonymous, and never resume-matched - two
/// anonymous clients meeting the same server are always strangers, never continuations.
inline constexpr std::uint64_t kAnonymousSessionTokenUVE = 0U;

/// One sub-stream of a multiplexed session. The app owns the namespace, all 256 values, and
/// both ends must agree who rides which out of band - nothing negotiates it.
using NetworkChannelIdUVE = std::uint8_t;

/// First byte of every session datagram. Anything else on the wire is ignored, as are empty
/// datagrams and well-tagged datagrams that arrive in the wrong state.
enum class SessionMessageTagUVE : std::uint8_t { Hello = 0x01, Welcome = 0x02, Bye = 0x03, Data = 0x04 };

enum class SessionRoleUVE : std::uint8_t { Client, Server };
/// Reconnecting is client-only: an unintended drop with redial enabled. The session is down
/// (sends fail, like Disconnected) but a Hello goes out every retryDelayMs until a Welcome
/// lands, the attempts run out, or the app hangs up.
enum class SessionStateUVE : std::uint8_t { Disconnected, Connecting, Reconnecting, Established };
enum class SessionCloseReasonUVE : std::uint8_t {
    None,
    LocalDisconnect,
    RemoteDisconnect,
    Timeout,
    ReconnectFailed
};

/// Client redial policy after an unintended drop (silence timeout or a Bye from the peer -
/// hanging up locally never redials). Zero delay disables reconnection, exactly like a zero
/// session timeout disables timeouts; zero attempts retries forever. Attempts times delay
/// should clear the peer's silence timeout with margin: while the server still considers the
/// old session established it ignores Hellos, and the redial only lands once the server has
/// timed the peer out too.
struct SessionReconnectConfigUVE final {
    std::uint64_t retryDelayMs = 0U;
    std::uint32_t maxAttempts = 0U;

    [[nodiscard]] bool operator==(const SessionReconnectConfigUVE&) const noexcept = default;
};

/// One peer's side of a connection. The pump model is explicit: UpdateUVE drains the endpoint,
/// fires a due redial Hello, and runs the timeout check; TryReceiveDataUVE only sees what a
/// previous Update queued, and nothing here threads or ticks itself. Move-only (it owns its
/// endpoint); single-threaded like everything below it. A multiplexed session demuxes inbound
/// data by the channel byte into per-channel queues; the untagged queue then stays empty and
/// untagged sends refuse.
class NetworkSessionUVE final {
public:
    NetworkSessionUVE(SessionRoleUVE role, std::unique_ptr<ITransportEndpointUVE> endpoint,
                      std::uint64_t timeoutMs = kDefaultSessionTimeoutMsUVE) noexcept;

    /// Client only, from Disconnected: emits Hello and enters Connecting. Returns false (leaving
    /// everything untouched) for a server or an already-live session - Reconnecting counts as
    /// live, so hang up before dialing over a redial. The token names this client to the
    /// server: pass a fresh unique one per client run (anonymous clients can never resume),
    /// and every redial Hello reuses it. A dropped first Hello still transitions, and the
    /// timeout is the backstop.
    [[nodiscard]] bool ConnectUVE(std::uint64_t nowMs,
                                  std::uint64_t token = kAnonymousSessionTokenUVE);
    /// From Connecting/Reconnecting/Established: best-effort Bye, redial cancelled, queued
    /// data cleared, Disconnected with LocalDisconnect - and on a server, the remembered peer
    /// token forgotten, so the next Hello is always a stranger. Returns whether a Bye was
    /// emitted (false when already Disconnected, or when the queue was full - in which case
    /// the peer times out instead).
    [[nodiscard]] bool DisconnectUVE();
    /// Drains every pending datagram through the state machine, fires a due redial Hello, then
    /// times out a silent Connecting/Established session (Reconnecting answers to its own
    /// scheduler, never to this timeout). Only recognized datagrams refresh the activity
    /// clock; garbage and wrong-state datagrams neither queue nor keep alive. `nowMs` must be
    /// monotonic per session - a backward jump merely delays the timeout, never triggers it early.
    void UpdateUVE(const std::uint64_t nowMs);
    /// Queues one tagged payload for the peer. Established only, bounded by
    /// `kSessionMaximumPayloadBytesUVE`; a full transport queue reports false. Always refuses
    /// on a multiplexed session - channels carry data there.
    [[nodiscard]] bool SendDataUVE(const std::vector<std::uint8_t>& payload);
    /// Pops the oldest payload a previous Update queued, or empty. Disconnects clear the queue.
    [[nodiscard]] std::optional<std::vector<std::uint8_t>> TryReceiveDataUVE();
    /// Enables channel multiplexing: data frames gain a channel byte past the tag, sends go
    /// through SendChannelDataUVE, and inbound data lands in per-channel queues. One-way and
    /// handshake-scoped: both ends must enable before connecting, since a muxed session
    /// misroutes legacy data by its first payload byte (and legacy features choke on the
    /// channel byte as payload). Returns false (leaving the mode untouched) unless
    /// Disconnected.
    [[nodiscard]] bool EnableMultiplexingUVE() noexcept;
    /// Sends one tagged payload on `channel`. Established multiplexed sessions only, bounded
    /// by `kSessionMaximumChannelPayloadBytesUVE`; a full transport queue reports false.
    [[nodiscard]] bool SendChannelDataUVE(NetworkChannelIdUVE channel,
                                          const std::vector<std::uint8_t>& payload);
    /// Pops the oldest payload a previous Update queued on `channel`, or empty. Disconnects
    /// clear every channel queue with the untagged one.
    [[nodiscard]] std::optional<std::vector<std::uint8_t>> TryReceiveChannelDataUVE(
        NetworkChannelIdUVE channel);
    /// Pops the oldest inbound data payload as raw wire bytes: post-tag, channel byte included
    /// when multiplexed (re-tagged with the channel it arrived on). Powers byte-forwarding
    /// relays, which move payloads without parsing them. Drains the untagged queue first,
    /// then channels in id order - cross-channel order is not arrival order, since channels
    /// are independent sub-streams. Disconnects clear everything this could return.
    [[nodiscard]] std::optional<std::vector<std::uint8_t>> TryReceiveRawDataUVE();
    /// Sends already-framed data bytes: tag plus `payload`, channel byte included by the
    /// caller when multiplexed. Established only, bounded by
    /// `kSessionMaximumPayloadBytesUVE`; a full transport queue reports false. Unlike
    /// SendDataUVE this works multiplexed - the bytes arrive pre-channeled, so nothing is
    /// ambiguous - which is exactly what a relay forwards.
    [[nodiscard]] bool SendRawDataUVE(const std::vector<std::uint8_t>& payload);

    /// Swaps the redial policy live. Disabling mid-redial cancels it: the session falls to
    /// Disconnected carrying the reason that started the redial. Client-only in effect (a
    /// server never redials) but harmless to set on either role.
    void SetReconnectConfigUVE(const SessionReconnectConfigUVE config) noexcept {
        m_reconnect = config;
    }

    [[nodiscard]] SessionStateUVE StateUVE() const noexcept { return m_state; }
    [[nodiscard]] bool IsMultiplexedUVE() const noexcept { return m_multiplexed; }
    [[nodiscard]] SessionCloseReasonUVE CloseReasonUVE() const noexcept { return m_closeReason; }
    /// The token of the current (or most recent) establishment: what this client dialed with,
    /// or what the server bound from the accepted Hello. Anonymous when nothing ever bound.
    [[nodiscard]] std::uint64_t SessionTokenUVE() const noexcept { return m_sessionToken; }
    /// True when the current (or most recent) establishment continues a remembered peer. The
    /// two sides observe it differently: the server matches the accepted Hello against the
    /// token it kept across the drop, while the client only knows its Welcome arrived
    /// mid-redial (the Welcome carries no "I remember you" - that flag is a follow-up, not a
    /// missing piece). Replication resync keys off this later; the session itself treats a
    /// resume exactly like a fresh establishment.
    [[nodiscard]] bool DidResumeUVE() const noexcept { return m_didResume; }
    /// Redial Hellos sent in the current (or most recent) redial: the "3 of 5" numerator.
    /// Reset by Connect and by each new redial, frozen everywhere else - including after the
    /// give-up, so the post-mortem still reads the full count.
    [[nodiscard]] std::uint32_t ReconnectAttemptsUVE() const noexcept {
        return m_reconnectAttempts;
    }
    /// Debug tap: every datagram this session sends or receives is copied to `sink` - sends
    /// only on success (nothing hit the wire otherwise), receives before the state machine
    /// filters them (ignored garbage taps too: that is what the inspector is for).
    /// Outbound taps stamp the last pumped clock (sends take no `nowMs`); inbound taps stamp
    /// the draining pump's. The sink runs synchronously inside the send or pump call and must
    /// not send on this session back. Replaces any previous sink.
    void SetTapUVE(PacketTapSinkUVE sink);
    /// Unhooks the tap: traffic flows untapped again.
    void ClearTapUVE() noexcept;
    [[nodiscard]] bool HasTapUVE() const noexcept { return static_cast<bool>(m_tap); }

private:
    void HandleDatagramUVE(const std::vector<std::uint8_t>& datagram, std::uint64_t nowMs);
    /// Copies one datagram to the tap sink; silent when untapped.
    void TapUVE(const std::vector<std::uint8_t>& datagram, PacketDirectionUVE direction,
                std::uint64_t timestampMs, std::optional<NetworkChannelIdUVE> channel);
    /// Frames tag plus payload onto the endpoint: the established/endpoint/budget check with
    /// the send. SendDataUVE adds the legacy-only refusal on top; SendRawDataUVE calls this
    /// straight, both modes alike.
    [[nodiscard]] bool SendFramedUVE(const std::vector<std::uint8_t>& payload);
    void TransitionToDisconnectedUVE(SessionCloseReasonUVE reason) noexcept;
    /// Frames and sends the Hello for Connect and every redial attempt: tag, version, and the
    /// stored token, little-endian. A failed send still burns the attempt, so attempts bound
    /// the redial window in time; hence no nodiscard - the caller never branches on the fate.
    bool SendHelloUVE();
    /// Timeout and Bye funnel: enters Reconnecting when the policy says so, disconnects outright
    /// otherwise (including from Reconnecting itself - a Bye mid-redial is refused, not re-armed).
    void DropWithRedialUVE(SessionCloseReasonUVE reason, std::uint64_t nowMs);
    /// Runs the redial scheduler: cancels when disabled, gives up when exhausted, fires a due
    /// Hello. Reconnecting only; anything else is a no-op.
    void PumpReconnectUVE(std::uint64_t nowMs);

    SessionRoleUVE m_role = SessionRoleUVE::Client;
    std::unique_ptr<ITransportEndpointUVE> m_endpoint;
    std::uint64_t m_timeoutMs = kDefaultSessionTimeoutMsUVE;
    SessionReconnectConfigUVE m_reconnect;
    SessionStateUVE m_state = SessionStateUVE::Disconnected;
    SessionCloseReasonUVE m_closeReason = SessionCloseReasonUVE::None;
    std::uint64_t m_lastActivityMs = 0U;
    std::deque<std::vector<std::uint8_t>> m_inboundData;
    // Per-channel inbound queues on a multiplexed session: the pump demuxes by the byte past
    // the tag, and disconnects clear these with the untagged queue.
    std::map<NetworkChannelIdUVE, std::deque<std::vector<std::uint8_t>>> m_inboundChannels;
    bool m_multiplexed = false;
    std::uint64_t m_sessionToken = kAnonymousSessionTokenUVE;
    std::uint64_t m_lastPeerToken = kAnonymousSessionTokenUVE;
    bool m_didResume = false;
    SessionCloseReasonUVE m_redialReason = SessionCloseReasonUVE::None;
    std::uint32_t m_reconnectAttempts = 0U;
    std::uint64_t m_nextAttemptAtMs = 0U;
    PacketTapSinkUVE m_tap;
    std::uint64_t m_lastPumpMs = 0U;
};

} // namespace UVE::Network
