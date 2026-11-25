// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
#pragma once
//
// Scope boundary - read before concluding that this module is dead code.
//
// This is a wire-format library: it encodes and decodes reliable packet headers and bounded
// payload fragments, and nothing more. It has no caller inside Engine/ by design, because the
// engine currently ships no socket transport - the only transport is the in-memory loopback
// seam (uve/network/network_transport_uve.h); there is no socket, peer, connection, or timer
// layer anywhere in the tree (grep for socket(/sendto(/recvfrom( across Engine/ returns nothing).
// The only in-tree consumer is Test/Network/reliable_packet_window_uve_tests.cpp, which is the
// intended one: the helpers are pure and stateless, so tests are their natural driver.
//
// Every function below repeats the same contract in its own doc comment - "owns no socket, timer,
// peer, retransmission, or transport lifecycle" - and that contract is the reason for the absence
// of callers, not evidence of an unfinished integration. Wiring these up means writing the
// socket/session layers on the transport seam; do not treat them as an orphaned call site
// waiting to be connected.
//
#include <cstddef>
#include <cstdint>
#include <vector>
namespace UVE::Network {
inline constexpr std::uint32_t kReliablePacketMaximumSelectiveAckBits = 32U;
inline constexpr std::size_t kReliablePacketMaximumPayloadBytes = 1200U;
inline constexpr std::size_t kReliablePacketMaximumFragmentCount = 1024U;
inline constexpr std::size_t kReliablePacketMaximumReassembledPayloadBytes =
    kReliablePacketMaximumPayloadBytes * kReliablePacketMaximumFragmentCount;
inline constexpr std::size_t kReliablePacketHeaderWireBytes = 12U;
inline constexpr std::size_t kReliablePayloadFragmentHeaderWireBytes = 16U;
inline constexpr std::size_t kReliablePacketFragmentWireHeaderBytes =
    kReliablePacketHeaderWireBytes + kReliablePayloadFragmentHeaderWireBytes;
struct ReliablePacketHeader final {
    std::uint32_t sequence = 0U;
    std::uint32_t acknowledgedSequence = 0U;
    std::uint32_t selectiveAcknowledgementBits = 0U;
};
struct ReliablePayloadFragment final {
    std::uint32_t messageId = 0U;
    std::uint32_t fragmentIndex = 0U;
    std::uint32_t fragmentCount = 0U;
    std::vector<std::uint8_t> payloadBytes;

    [[nodiscard]] bool operator==(const ReliablePayloadFragment&) const noexcept = default;
};
/// Serializes one validated reliable header into an exact 12-byte little-endian wire representation.
/// The helper owns only copied bytes and publishes output failure-atomically.
[[nodiscard]] bool SerializeReliablePacketHeader(
    const ReliablePacketHeader& header, std::vector<std::uint8_t>& outBytes) noexcept;
/// Deserializes one exact 12-byte little-endian reliable header with nonzero sequence/ack validation.
/// The helper owns no packet, socket, payload, timer, or transport lifecycle.
[[nodiscard]] bool DeserializeReliablePacketHeader(
    const std::vector<std::uint8_t>& bytes, ReliablePacketHeader& outHeader) noexcept;
/// Serializes one bounded payload fragment as a 16-byte little-endian envelope followed by copied bytes.
/// The helper owns no socket, timer, peer, retransmission, or transport lifecycle.
[[nodiscard]] bool SerializeReliablePayloadFragment(
    const ReliablePayloadFragment& fragment, std::vector<std::uint8_t>& outBytes) noexcept;
/// Deserializes one exact bounded payload fragment envelope and publishes copied output atomically.
[[nodiscard]] bool DeserializeReliablePayloadFragment(
    const std::vector<std::uint8_t>& bytes, ReliablePayloadFragment& outFragment) noexcept;
/// Materializes one bounded caller-owned payload into ordered copied fragments using the existing planner.
/// The helper owns no packet sequence, socket, timer, retransmission, peer, or transport lifecycle.
[[nodiscard]] bool BuildReliablePayloadFragments(
    std::uint32_t messageId, const std::vector<std::uint8_t>& payloadBytes,
    std::size_t maximumFragmentBytes, std::vector<ReliablePayloadFragment>& outFragments) noexcept;
/// Serializes one reliable header and one validated payload fragment into one copied wire packet.
/// The helper owns no socket, timer, peer, retransmission, or transport lifecycle.
[[nodiscard]] bool SerializeReliablePacketFragment(
    const ReliablePacketHeader& header, const ReliablePayloadFragment& fragment,
    std::vector<std::uint8_t>& outBytes) noexcept;
/// Deserializes one exact bounded packet and publishes both decoded values atomically.
[[nodiscard]] bool DeserializeReliablePacketFragment(
    const std::vector<std::uint8_t>& bytes, ReliablePacketHeader& outHeader,
    ReliablePayloadFragment& outFragment) noexcept;
enum class ReliablePacketReceiveStatus : std::uint8_t {
    Accepted = 0,
    Duplicate,
    TooOld,
    Invalid,
};
struct ReliableAcknowledgementState final {
    std::uint32_t latestReceivedSequence = 0U;
    std::uint32_t receivedHistoryBits = 0U;
    bool hasReceivedSequence = false;
};
enum class ReliableRetransmitStatus : std::uint8_t {
    Waiting = 0,
    Due,
    Exhausted,
    Invalid,
};
struct ReliableRetransmitPolicyInput final {
    float elapsedSeconds = 0.0F;
    float retryTimeoutSeconds = 0.0F;
    std::uint32_t retryCount = 0U;
    std::uint32_t maximumRetries = 0U;
};
/// Caller-owned persistent retry timing state. It owns no clock, timer, packet, socket, peer,
/// retransmission queue, or transport lifecycle; callers provide elapsed time and commit a due retry.
class ReliableRetrySchedule final {
public:
    [[nodiscard]] bool Configure(float baseTimeoutSeconds, float maximumTimeoutSeconds,
                                    std::uint32_t maximumRetries) noexcept;
    [[nodiscard]] ReliableRetransmitStatus Advance(float elapsedSeconds) noexcept;
    [[nodiscard]] bool CommitRetry() noexcept;
    void Reset() noexcept;
    [[nodiscard]] bool IsConfigured() const noexcept { return m_configured; }
    [[nodiscard]] std::uint32_t GetRetryCount() const noexcept { return m_retryCount; }
    [[nodiscard]] float GetElapsedSeconds() const noexcept { return m_elapsedSeconds; }

private:
    float m_baseTimeoutSeconds = 0.0F;
    float m_maximumTimeoutSeconds = 0.0F;
    float m_elapsedSeconds = 0.0F;
    std::uint32_t m_retryCount = 0U;
    std::uint32_t m_maximumRetries = 0U;
    bool m_configured = false;
};
/// Computes a bounded caller-owned exponential retry timeout without owning a clock, timer, socket,
/// retry queue, or transport lifecycle. Retry zero returns the base timeout; later retries double it
/// until the caller-selected maximum timeout is reached.
[[nodiscard]] bool ComputeReliableRetryTimeout(
    float baseTimeoutSeconds, std::uint32_t retryCount, float maximumTimeoutSeconds,
    float& outTimeoutSeconds) noexcept;
struct ReliablePayloadFragmentPlan final {
    std::size_t fragmentCount = 0U;
    std::size_t maximumFragmentBytes = 0U;
    std::size_t finalFragmentBytes = 0U;
    bool fragmented = false;
    [[nodiscard]] bool operator==(const ReliablePayloadFragmentPlan&) const noexcept = default;
};
enum class ReliablePayloadReassemblyStatus : std::uint8_t {
    Accepted = 0,
    Duplicate,
    Complete,
    Conflict,
    Invalid,
};
struct ReliablePayloadReassemblyState final {
    std::uint32_t messageId = 0U;
    std::size_t fragmentCount = 0U;
    std::size_t receivedFragmentCount = 0U;
    std::size_t receivedByteCount = 0U;
    std::vector<std::vector<std::uint8_t>> fragments;
    [[nodiscard]] bool IsActive() const noexcept {
        return messageId != 0U;
    }
    void Reset() noexcept {
        messageId = 0U;
        fragmentCount = 0U;
        receivedFragmentCount = 0U;
        receivedByteCount = 0U;
        fragments.clear();
    }
};
/// Accepts one copied caller-owned payload fragment into bounded reassembly state. The state owns
/// only fragment bytes; it does not own packet headers, sockets, timers, peers, retransmission, or
/// transport delivery. A complete message publishes ordered copied bytes and resets the state.
[[nodiscard]] ReliablePayloadReassemblyStatus AcceptReliablePayloadFragment(
    std::uint32_t messageId, std::size_t fragmentIndex, std::size_t fragmentCount,
    const std::vector<std::uint8_t>& fragmentBytes, ReliablePayloadReassemblyState& state,
    std::vector<std::uint8_t>& outPayload) noexcept;
/// Plans bounded caller-owned payload fragments without serializing or retaining reassembly state.
[[nodiscard]] bool PlanReliablePayloadFragments(
    std::size_t payloadBytes, std::size_t fragmentBytes,
    ReliablePayloadFragmentPlan& outPlan,
    std::size_t maximumFragments = kReliablePacketMaximumFragmentCount) noexcept;

/// Validates one caller-owned payload size against a bounded datagram budget. A caller-supplied
/// maximum above `kReliablePacketMaximumPayloadBytes` is clamped to that shared wire cap.
[[nodiscard]] bool ValidateReliablePayloadBudget(
    std::size_t payloadBytes,
    std::size_t maximumBytes = kReliablePacketMaximumPayloadBytes) noexcept;

/// Evaluates one caller-owned retry decision without owning a clock, timer, socket, or packet.
[[nodiscard]] ReliableRetransmitStatus EvaluateReliableRetransmitPolicy(
    const ReliableRetransmitPolicyInput& input) noexcept;
/// Updates a copied receive window using wrap-safe uint32 sequence ordering.
/// Sequence zero is reserved as the initial/no-packet state; this contract owns no socket or payload.
[[nodiscard]] ReliablePacketReceiveStatus AcceptReliableSequence(
    std::uint32_t sequence, ReliableAcknowledgementState& state) noexcept;
/// Applies cumulative and 32-bit forward selective acknowledgements to one caller-owned pending mask.
/// Bit zero acknowledges acknowledgedSequence + 1, bit one acknowledges +2, and so on.
[[nodiscard]] bool ApplyReliableAcknowledgements(
    const ReliablePacketHeader& header, std::uint32_t& pendingSequenceMask,
    std::uint32_t oldestPendingSequence) noexcept;
} // namespace UVE::Network
