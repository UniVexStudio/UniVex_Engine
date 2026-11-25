// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
#pragma once
//
// Scope: client-side prediction and server reconciliation for many pawns - ROADMAP section 6's
// "responsive movement", as a channel beside replication and RPC (one session per channel, like
// them). The client captures input per tick, steps its local sim immediately, and sends inputs
// to the server; the server steps the same inputs authoritatively and returns states; the
// client drops acked history when the server agrees and snaps plus replays when it doesn't.
//
// One instance predicts a whole table of pawns (a splitscreen pair, a rider plus its mount):
// every pawn runs its own tick stream from 1, its own histories and reorder buffer, and its own
// gap clock, so a stall on one pawn never holds another. Pawn 0 is the primary - the original
// single-pawn calls are exactly pawn 0 at the configured step, so old call sites keep working
// unchanged. Every pawn but 0 needs seeding (SeedPawn) with the same spawn pose on both
// sides before its first tick, like the constructor's `initial` for pawn 0; a pawn that never
// seeds starts at the default state instead, and re-spawns take a fresh pawn id. Past sixteen
// live pawns the table is full: the client refuses the tick and the server refuses the input,
// so a hostile peer cannot grow the maps without bound.
//
// Ticks are no longer fixed: every input carries the dt it stepped with, and the server steps
// - and the client replays - with the transmitted dt, so mixed-rate streams (a 30 Hz pawn
// beside a 60 Hz one) reproduce exactly. The config's fixed step stays as the default when a
// tick doesn't name one. A dt must be finite and within a second; anything else refuses at the
// call on the client and fails as malformed on the wire.
//
// The server's answers fan out to spectators through PredictionRelay: the app publishes each
// applied state and every established subscriber session gets the bytes, live only (a late
// joiner sees newer states, never the backlog). Subscribers sink them through
// PredictionObserver, which tracks the newest tick per pawn and ignores the stale. Both own
// their sessions and pump them, like the predictor itself.
//
// State is position, velocity, grounded, and one opaque app word: the determinism bits the
// stepper needs restarted on a replay (a character packs its coyote and jump-buffer clocks).
// Both sides must pack it identically, but lossy packing is fine - the same packer runs on
// both ends, so equal simulations pack equal words. Cross-machine float determinism is NOT
// required: position and velocity compare within epsilons, and drift just reconciles.
//
// Ticks are u64 from 1 per pawn and never wrap in practice. Each client tick sends the three
// oldest unacked inputs round-robin across pawns, so a dropped datagram rides again on the next
// tick; the server buffers out-of-order inputs (cap 16 per pawn), applies them strictly in
// order per pawn, and answers every applied tick with a state. Acks are cumulative per pawn (a
// state for tick N acks everything at or before N on that pawn), and the server retains its
// last state per pawn to resend when a duplicate input hints its ack was lost. A gap no repeat
// fills within half a second is declared unfillable (the client evicted those ticks an age ago)
// and skipped per pawn, so one outage cannot hold any stream forever. The client predicts while
// disconnected and drains its backlog once the session is back - reconnects resume mid-stream,
// ticks monotonic throughout.
//
// The production Gameplay adapter landed beside the character mover
// (Objects/3D/Physics character_prediction_uve.h: component <-> PredictedState, the real
// intent plus kinematic integration as the stepper, per-step tick/sync drivers with pawn
// overloads, and a mover refusal for tagged bodies so the two never double-step).
//
// Wire rev 2: input entries gained a pawn byte and a dt float (30 bytes each), states gained
// a pawn byte (39 bytes). The lengths stay exact, so rev-1 frames fail as malformed both ways:
// peers must run the same wire rev.
//
#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <utility>
#include <variant>
#include <vector>

#include "vector3.h"
#include "network_session.h"

namespace UVE::Network {

/// One fixed tick's identity. 0 is invalid; client ticks start at 1 per pawn and never repeat.
using PredictionTick = std::uint64_t;
inline constexpr PredictionTick kInvalidPredictionTick = 0U;

/// One pawn's address within an instance: 0 is the primary (the whole v1 API), the rest are
/// seeded siblings. Eight bits name far more pawns than the live cap below ever holds.
using PredictionPawn = std::uint8_t;
inline constexpr PredictionPawn kDefaultPredictionPawn = 0U;

/// Unacked inputs repeated per tick: enough to survive two consecutive drops, small enough
/// to stay one short message (three inputs are 92 of the session's 1199 payload bytes).
inline constexpr std::size_t kPredictionMaximumInputsPerMessage = 3U;
/// Ticks of input/state history past the last ack, per pawn: about a second at 60 Hz, then the
/// oldest goes. A server state older than that still reconciles, replaying only what survived.
inline constexpr std::size_t kPredictionMaximumHistoryTicks = 64U;
/// Out-of-order inputs the server holds while waiting on a gap, per pawn: deeper than any
/// loss burst the triple repeat cannot already cover. Past this, the newest input is dropped
/// and counted. How long the server waits on a gap before declaring it unfillable and skipping
/// past it. Triple repeats fill transient loss within a few ticks; only evicted ticks outwait
/// this.
inline constexpr std::size_t kPredictionMaximumServerBuffer = 16U;
inline constexpr std::uint64_t kPredictionGapTimeoutMs = 500U;
/// Live pawns per instance: past this the client refuses the tick (answering 0) and the server
/// refuses the input (counted), so a hostile peer cannot grow the maps without bound.
inline constexpr std::size_t kPredictionMaximumPawns = 16U;
/// The widest tick a peer may claim: a real tick is milliseconds, so anything past a second is
/// a lie (or a bug) and refuses - at the call on the client, as malformed on the wire.
inline constexpr float kPredictionMaximumTickDtSeconds = 1.0F;

/// One fixed step's player intent, in the character mover's own shape (move, rise, jump).
struct PredictedInput final {
    Math::Vector3 move{};
    float rise = 0.0F;
    bool jumpPressed = false;

    [[nodiscard]] bool operator==(const PredictedInput&) const = default;
};

/// One pawn's simulated state: where it is, how it moves, whether it stands, and the app's
/// opaque determinism word (see the scope note above).
struct PredictedState final {
    Math::Vector3 position{};
    Math::Vector3 velocity{};
    bool grounded = false;
    std::uint32_t appState = 0U;

    [[nodiscard]] bool operator==(const PredictedState&) const = default;
};

/// One state message decoded: whose tick, which pawn, and the simulated state.
struct PredictedStateMessage final {
    PredictionTick tick = kInvalidPredictionTick;
    PredictionPawn pawn = kDefaultPredictionPawn;
    PredictedState state;

    [[nodiscard]] bool operator==(const PredictedStateMessage&) const = default;
};

/// Advances one state by one input over dtSeconds. Must be deterministic for equal arguments
/// (same binary replays bitwise); across machines it should be close, and the epsilons absorb
/// the rest. Empty is allowed - a missing stepper predicts and applies as standing still.
using PredictionStepper = std::function<PredictedState(
    const PredictedState&, const PredictedInput&, float)>;

struct PredictionConfig final {
    /// The default step, in seconds: ticks that don't name a dt step with this. It is the
    /// project's fixed step, agreed out of band; it never crosses the wire (each input carries
    /// its own), and the server never reads this - it steps the transmitted dts.
    float fixedDtSeconds = 1.0F / 60.0F;
    /// Per-component agreement for positions (metres) and velocities (metres/second). Grounded
    /// and the app word compare exact: a lost jump buffer changes the future.
    float positionEpsilon = 0.001F;
    float velocityEpsilon = 0.01F;

    [[nodiscard]] bool operator==(const PredictionConfig&) const = default;
};

struct PredictionStats final {
    /// Client: input messages sent (each carries up to three unacked inputs).
    std::uint64_t inputsSent = 0U;
    /// Server: individual inputs decoded from well-formed messages, duplicates included.
    std::uint64_t inputsReceived = 0U;
    /// Server: inputs stepped into the authoritative state, strictly in tick order.
    std::uint64_t inputsApplied = 0U;
    /// Server: inputs already applied or already buffered (a resent unacked input, or one
    /// whose ack was lost - the last state goes out again).
    std::uint64_t inputsIgnoredDuplicate = 0U;
    /// Server: the reorder buffer under pressure - newest inputs dropped while full, plus
    /// gaps skipped as unfillable after the timeout.
    std::uint64_t inputBufferOverruns = 0U;
    /// Server: inputs for a pawn past the live cap. Never buffered, never applied.
    std::uint64_t inputsRefusedOverPawnCap = 0U;
    /// Server: state messages sent (one per applied input, plus resends on duplicates).
    std::uint64_t statesSent = 0U;
    /// Client: well-formed states decoded, stale ones included.
    std::uint64_t statesReceived = 0U;
    /// Client: states at or before the last ack.
    std::uint64_t statesIgnoredStale = 0U;
    /// Client: states that disagreed (or arrived past pruned history): snapped and replayed.
    std::uint64_t reconciles = 0U;
    /// Client: reconciles where the tick had already left history (replay ran on survivors).
    std::uint64_t stateHistoryMisses = 0U;
    /// Both: payloads of the wrong kind, shape, or length - or for a pawn the client never
    /// ran. Never applied, always counted.
    std::uint64_t framesIgnoredMalformed = 0U;
    /// Both: sends attempted while established that the transport refused (queue full).
    std::uint64_t sendsFailed = 0U;

    [[nodiscard]] bool operator==(const PredictionStats&) const noexcept = default;
};

/// One side of a table of predicted pawns. Drives its session's pump (Update drives the session
/// first, so handshakes and timeouts flow through the same call). Move-only (owning its session,
/// or borrowing a shared one); single-threaded like everything below it. Shared mode borrows a
/// multiplexed session and rides one channel; the app owns the session and both ends agree
/// channels out of band.
class NetworkPrediction final {
public:
    /// `role` must match the session's own role (the session never says which it is, so the
    /// app keeps them in agreement): Client predicts and sends, Server applies and answers.
    /// `initial` is pawn 0's agreed spawn state on both sides, like the replicator's net-id
    /// mapping; siblings seed through SeedPawn. An empty stepper stands still instead of
    /// crashing. The session sink is an rvalue reference, not by value: a by-value sink would
    /// tie with the shared overload below for lvalue sessions, since the deleted copy still
    /// participates in overload resolution.
    explicit NetworkPrediction(NetworkSession&& session, SessionRole role,
                                  PredictionStepper stepper = PredictionStepper{},
                                  PredictedState initial = PredictedState{},
                                  PredictionConfig config = PredictionConfig{});

    /// Shares a multiplexed session: inputs and states ride `channel` while other features
    /// ride theirs. The session must be multiplexed (both ends agree out of band) and must
    /// outlive the predictor; Update pumps it like an owned one, so any feature's Update
    /// drives the handshake for all.
    explicit NetworkPrediction(NetworkSession& sharedSession, NetworkChannelId channel,
                                  SessionRole role,
                                  PredictionStepper stepper = PredictionStepper{},
                                  PredictedState initial = PredictedState{},
                                  PredictionConfig config = PredictionConfig{});

    /// Client: predicts one tick for the primary pawn at the configured step (steps, stores,
    /// sends the oldest unacked window) and answers its tick. Predicting needs no session:
    /// disconnected ticks still simulate and queue, draining once the session is back.
    [[nodiscard]] PredictionTick Tick(const PredictedInput& input);
    /// Client: predicts one tick for `pawn` at the configured step. Same refusals as the full
    /// call below; the stream stays dense whatever the entity does.
    [[nodiscard]] PredictionTick Tick(PredictionPawn pawn, const PredictedInput& input);
    /// Client: predicts one tick for `pawn` stepped over `dtSeconds`, stored for the replay.
    /// Answers 0 - stepping nothing, storing nothing, sending nothing - on a server, past the
    /// pawn cap, or for a dt that is not finite and within the second.
    [[nodiscard]] PredictionTick Tick(PredictionPawn pawn, const PredictedInput& input,
                                            float dtSeconds);

    /// Seeds `pawn` with its spawn pose: both sides must seed alike before the pawn's first
    /// tick, like the constructor's `initial` for pawn 0. False - and the pawn untouched -
    /// for pawn 0 (the constructor owns it), for a pawn that already runs, or past the cap.
    bool SeedPawn(PredictionPawn pawn, const PredictedState& state);

    /// Pumps the session, then (client) reconciles every queued state or (server) buffers,
    /// applies, and answers every queued input. Either side ignores the other's message kind
    /// as malformed - a state on the server is as wrong as an input on the client. Gap clocks
    /// run per pawn: only the stalled pawn's stream jumps.
    void Update(std::uint64_t nowMs);

    /// The session, owned or shared: handshake, disconnect, and state live here, not duplicated.
    [[nodiscard]] NetworkSession& Session() noexcept {
        if (std::holds_alternative<NetworkSession>(m_session)) {
            return std::get<NetworkSession>(m_session);
        }
        return *std::get<NetworkSession*>(m_session);
    }
    [[nodiscard]] const NetworkSession& Session() const noexcept {
        if (std::holds_alternative<NetworkSession>(m_session)) {
            return std::get<NetworkSession>(m_session);
        }
        return *std::get<NetworkSession*>(m_session);
    }
    [[nodiscard]] PredictionStats Stats() const noexcept { return m_stats; }
    /// The local sim state: predicted present on the client, authoritative on the server.
    /// The default state for a pawn that never ran.
    [[nodiscard]] PredictedState LocalState() const {
        return LocalState(kDefaultPredictionPawn);
    }
    [[nodiscard]] PredictedState LocalState(PredictionPawn pawn) const;
    /// Client: the newest tick the server has acked (0 before the first state). Always 0 on
    /// a server - the difference to the newest issued tick is the round trip in ticks.
    [[nodiscard]] PredictionTick LastAckedTick() const noexcept {
        return LastAckedTick(kDefaultPredictionPawn);
    }
    [[nodiscard]] PredictionTick LastAckedTick(PredictionPawn pawn) const noexcept;
    /// Server: the newest tick stepped into the authoritative state (0 before the first
    /// input). Always 0 on a client.
    [[nodiscard]] PredictionTick LastAppliedTick() const noexcept {
        return LastAppliedTick(kDefaultPredictionPawn);
    }
    [[nodiscard]] PredictionTick LastAppliedTick(PredictionPawn pawn) const noexcept;

private:
    // One pawn's client side: its own numbering, ack, and histories. Inputs carry their dt so
    // replays re-step what the live tick stepped.
    struct ClientPawnStream {
        PredictionTick nextTick = 1U;
        PredictionTick lastAcked = kInvalidPredictionTick;
        std::map<PredictionTick, std::pair<PredictedInput, float>> inputs;
        std::map<PredictionTick, PredictedState> states;
    };
    // One pawn's server side: its own ordering, buffer (dts ride along, consumed at apply),
    // resend state, and gap clock.
    struct ServerPawnStream {
        PredictionTick lastApplied = kInvalidPredictionTick;
        std::map<PredictionTick, std::pair<PredictedInput, float>> buffer;
        bool hasLastState = false;
        PredictedState lastState;
        std::uint64_t lastApplyMs = 0U;
    };

    [[nodiscard]] static bool ValidTickDt(float dtSeconds) noexcept;
    [[nodiscard]] PredictedState Step(const PredictedState& state,
                                            const PredictedInput& input, float dtSeconds) const;
    // Lazily materializes the pawn's stream (client or server to match the role), or null
    // past the cap. Materializing also opens the pawn's local state at the default pose.
    [[nodiscard]] ClientPawnStream* ClientStream(PredictionPawn pawn);
    [[nodiscard]] ServerPawnStream* ServerStream(PredictionPawn pawn);
    void HandleInputMessage(const std::vector<std::uint8_t>& payload);
    void HandleStateMessage(const std::vector<std::uint8_t>& payload);
    void ApplyBuffered(ServerPawnStream& stream, PredictionPawn pawn);
    void SendState(PredictionPawn pawn, PredictionTick tick, const PredictedState& state);
    static void PruneThrough(ClientPawnStream& stream, PredictionTick tick);
    [[nodiscard]] bool SendPayload(const std::vector<std::uint8_t>& payload);
    [[nodiscard]] std::optional<std::vector<std::uint8_t>> ReceivePayload();

    // Owned or borrowed: owning mode moves the session in, shared mode borrows a multiplexed
    // one (which must outlive the feature). Moves stay correct either way - a moved borrow
    // still points at the same shared session.
    std::variant<NetworkSession, NetworkSession*> m_session;
    // The multiplexed channel, meaningful only when shared; owned mode sends untagged.
    NetworkChannelId m_channel = 0U;
    SessionRole m_role = SessionRole::Client;
    PredictionStepper m_stepper;
    PredictionConfig m_config;
    PredictionStats m_stats;
    // The local sim per pawn: predicted presents on the client, authoritative states on the
    // server. Pawn 0 starts at the constructor's initial; siblings start default until seeded.
    // The keys always match the role's streams - materializing one opens the other.
    std::map<PredictionPawn, PredictedState> m_local;
    std::map<PredictionPawn, ClientPawnStream> m_clientPawns;
    std::map<PredictionPawn, ServerPawnStream> m_serverPawns;
    // The pump's clock, shared the way v1 shared it: every gap wait measures from here.
    std::uint64_t m_lastSeenMs = 0U;
};

struct PredictionRelayStats final {
    /// Individual subscriber sends: one publish fans out to every established subscriber.
    std::uint64_t statesRelayed = 0U;
    /// Sends attempted while established that the transport refused (queue full).
    std::uint64_t sendsFailed = 0U;

    [[nodiscard]] bool operator==(const PredictionRelayStats&) const noexcept = default;
};

/// The server's fan-out: applied prediction states forwarded to watching clients. The app
/// polls the server predictor's newest applied tick per pawn and publishes on change; every
/// established subscriber session gets the bytes. Move-only (owning its sessions);
/// single-threaded like everything below it.
class PredictionRelay final {
public:
    PredictionRelay() = default;

    /// Hands the relay a subscriber session (server role, one per watcher) and answers its
    /// index for removal. The relay pumps every session it holds, so handshakes and timeouts
    /// flow through Update below.
    [[nodiscard]] std::size_t AddSubscriber(NetworkSession&& session);
    /// Drops a subscriber by index; later indices shift down. False for an unknown index.
    bool RemoveSubscriber(std::size_t index);
    [[nodiscard]] std::size_t SubscriberCount() const noexcept { return m_subscribers.size(); }

    /// Pumps every subscriber session.
    void Update(std::uint64_t nowMs);

    /// Publishes one applied state to every established subscriber and answers how many
    /// received it. Live only: subscribers that are not established yet are skipped, never
    /// queued - a late joiner sees newer states, not the backlog.
    [[nodiscard]] std::size_t Publish(PredictionTick tick, PredictionPawn pawn,
                                         const PredictedState& state);

    [[nodiscard]] PredictionRelayStats Stats() const noexcept { return m_stats; }

private:
    std::vector<NetworkSession> m_subscribers;
    PredictionRelayStats m_stats;
};

struct PredictionObserverStats final {
    /// Well-formed states decoded, stale ones included.
    std::uint64_t statesReceived = 0U;
    /// States at or before the pawn's newest seen tick.
    std::uint64_t statesIgnoredStale = 0U;
    /// Payloads of the wrong kind, shape, or length. Never stored, always counted.
    std::uint64_t framesIgnoredMalformed = 0U;

    [[nodiscard]] bool operator==(const PredictionObserverStats&) const noexcept = default;
};

/// A subscriber's sink: relayed prediction states land here, newest per pawn. The observer
/// never predicts - it watches, tracking the latest tick per pawn and ignoring the stale, so
/// a proxy doll always reads the freshest pose. Move-only (owning its session);
/// single-threaded like everything below it.
class PredictionObserver final {
public:
    explicit PredictionObserver(NetworkSession&& session);

    /// Pumps the session, then ingests every queued state.
    void Update(std::uint64_t nowMs);

    /// The newest state seen for `pawn`, or nothing when the pawn never arrived.
    [[nodiscard]] std::optional<PredictedStateMessage> Latest(PredictionPawn pawn) const;

    /// The session, owned: handshake, disconnect, and state live here, not duplicated.
    [[nodiscard]] NetworkSession& Session() noexcept { return m_session; }
    [[nodiscard]] const NetworkSession& Session() const noexcept { return m_session; }
    [[nodiscard]] PredictionObserverStats Stats() const noexcept { return m_stats; }

private:
    NetworkSession m_session;
    std::map<PredictionPawn, PredictedStateMessage> m_latest;
    PredictionObserverStats m_stats;
};

/// The state wire shape shared by the predictor, the relay, and the observer: kind 0x02,
/// tick, pawn, position, velocity, grounded, app word - 39 bytes, length-exact, finite floats
/// and a real tick or nothing decodes.
[[nodiscard]] std::vector<std::uint8_t> EncodePredictedState(
    PredictionTick tick, PredictionPawn pawn, const PredictedState& state);
[[nodiscard]] std::optional<PredictedStateMessage> DecodePredictedState(
    const std::vector<std::uint8_t>& payload);

} // namespace UVE::Network
