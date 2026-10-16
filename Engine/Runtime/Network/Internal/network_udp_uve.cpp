// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/network/network_udp_uve.h"

#include <algorithm>
#include <cstddef>
#include <deque>
#include <map>

#if defined(_WIN32)
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace UVE::Network {

namespace {

#if defined(_WIN32)
using SocketHandleUVE = SOCKET;
inline constexpr SocketHandleUVE kInvalidSocketUVE = INVALID_SOCKET;

// Winsock starts on first use and never stops: the process owns one implicit WSAStartup,
// and cleaning up under live sockets would break copies sharing them.
bool EnsureSocketLibraryUVE() {
    static const bool initialized = [] {
        WSADATA data{};
        return ::WSAStartup(MAKEWORD(2, 2), &data) == 0;
    }();
    return initialized;
}

void CloseSocketUVE(const SocketHandleUVE handle) noexcept {
    if (handle != kInvalidSocketUVE) {
        ::closesocket(handle);
    }
}

bool SetNonBlockingUVE(const SocketHandleUVE handle) noexcept {
    u_long mode = 1UL;
    return ::ioctlsocket(handle, static_cast<long>(FIONBIO), &mode) == 0;
}
#else
using SocketHandleUVE = int;
inline constexpr SocketHandleUVE kInvalidSocketUVE = -1;

bool EnsureSocketLibraryUVE() noexcept {
    return true;
}

void CloseSocketUVE(const SocketHandleUVE handle) noexcept {
    if (handle != kInvalidSocketUVE) {
        ::close(handle);
    }
}

bool SetNonBlockingUVE(const SocketHandleUVE handle) noexcept {
    const int flags = ::fcntl(handle, F_GETFL, 0);
    if (flags == -1) {
        return false;
    }
    return ::fcntl(handle, F_SETFL, flags | O_NONBLOCK) != -1;
}
#endif

// Big enough to swallow any UDP payload whole (65507 max on the wire): oversize datagrams
// arrive intact and count as dropped-oversize, never truncated into something deliverable.
inline constexpr std::size_t kUdpReceiveBufferBytesUVE = 65535U;

// True when the socket visibly holds at least one datagram. A zero-length read means an
// empty datagram waits (UDP has no EOF); any error means nothing provable, so false.
bool SocketHasMailUVE(const SocketHandleUVE socket) noexcept {
    char probe = 0;
#if defined(_WIN32)
    const int probed = ::recv(socket, &probe, 1, MSG_PEEK);
#else
    const ssize_t probed = ::recv(socket, &probe, 1, MSG_PEEK);
#endif
    return probed >= 0;
}

} // namespace

// Forward-declared in the header at UVE::Network scope and defined HERE, outside the
// anonymous namespace above, so it is the same type the endpoint's shared_ptr names.
struct UdpSocketStateUVE {
    ~UdpSocketStateUVE() {
        CloseSocketUVE(socket);
    }

    SocketHandleUVE socket = kInvalidSocketUVE;
    std::uint16_t localPort = 0U;
    std::size_t maxPendingDatagrams = 256U;
    std::deque<std::vector<std::uint8_t>> inbound;
    UdpTransportStatsUVE stats;
};

// Same rule: forward-declared in the header, defined here at UVE::Network scope.
struct UdpListenerStateUVE {
    struct PeerRecordUVE final {
        std::deque<std::vector<std::uint8_t>> inbound;
        std::size_t maxPendingDatagrams = 256U;
        UdpTransportStatsUVE stats;
    };

    ~UdpListenerStateUVE() {
        CloseSocketUVE(socket);
    }

    SocketHandleUVE socket = kInvalidSocketUVE;
    std::uint16_t localPort = 0U;
    std::size_t maxStrangers = 64U;
    std::size_t maxStrangerDatagrams = 16U;
    std::size_t maxPumpDatagrams = 1024U;
    std::map<UdpEndpointAddressUVE, PeerRecordUVE> peers;
    std::map<UdpEndpointAddressUVE, std::deque<std::vector<std::uint8_t>>> strangers;
    // Arrival order for oldest-first accept; the map above cannot keep it. Entries are
    // unique and mirror the stranger map exactly - never an address without held mail.
    std::deque<UdpEndpointAddressUVE> strangerOrder;
    UdpListenerStatsUVE stats;
};

namespace {

// Moves wire datagrams into the queue until the socket runs dry or the budget runs out.
// The budget (queue bound plus one) keeps a flooding peer from pinning the pump in recv;
// any socket error ends the drain, and the next pump retries. Only called when the queue
// is empty, and peeked first, so an idle pump costs one syscall and no allocation. Lives
// down here (same anonymous namespace, reopened) because it needs the state complete.
void DrainSocketUVE(UdpSocketStateUVE& state) {
    if (!SocketHasMailUVE(state.socket)) {
        return;
    }
    const std::size_t budget = std::min(state.maxPendingDatagrams, std::size_t{65536U}) + 1U;
    std::vector<std::uint8_t> buffer(kUdpReceiveBufferBytesUVE);
    for (std::size_t i = 0U; i < budget; ++i) {
#if defined(_WIN32)
        const int length = static_cast<int>(buffer.size());
#else
        const std::size_t length = buffer.size();
#endif
        const auto received =
            ::recv(state.socket, reinterpret_cast<char*>(buffer.data()), length, 0);
        if (received < 0) {
            return;
        }
        // Zero is an empty datagram, not an error: it queues like the loopback's empty
        // payload, and the layers above decide what emptiness means.
        const auto bytes = static_cast<std::size_t>(received);
        if (bytes > kTransportMaximumDatagramBytesUVE) {
            ++state.stats.datagramsDroppedOversize;
            continue;
        }
        if (state.inbound.size() >= state.maxPendingDatagrams) {
            ++state.stats.datagramsDroppedFull;
            continue;
        }
        state.inbound.emplace_back(buffer.begin(),
                                   buffer.begin() + static_cast<std::ptrdiff_t>(bytes));
    }
}

} // namespace

UdpTransportEndpointUVE::UdpTransportEndpointUVE(
    std::shared_ptr<UdpSocketStateUVE> state) noexcept
    : m_state(std::move(state)) {}

UdpTransportEndpointUVE::~UdpTransportEndpointUVE() = default;

bool UdpTransportEndpointUVE::SendUVE(const std::vector<std::uint8_t>& payload) {
    if (m_state == nullptr) {
        return false;
    }
    if (payload.size() > kTransportMaximumDatagramBytesUVE) {
        ++m_state->stats.datagramsDroppedOversize;
        return false;
    }
#if defined(_WIN32)
    const int length = static_cast<int>(payload.size());
#else
    const std::size_t length = payload.size();
#endif
    const char* bytes =
        payload.empty() ? "" : reinterpret_cast<const char*>(payload.data());
    const auto sent = ::send(m_state->socket, bytes, length, 0);
    if (sent < 0) {
        ++m_state->stats.datagramsDroppedFull;
        return false;
    }
    ++m_state->stats.datagramsSent;
    return true;
}

std::optional<std::vector<std::uint8_t>> UdpTransportEndpointUVE::TryReceiveUVE() {
    if (m_state == nullptr) {
        return std::nullopt;
    }
    if (m_state->inbound.empty()) {
        DrainSocketUVE(*m_state);
    }
    if (m_state->inbound.empty()) {
        return std::nullopt;
    }
    std::vector<std::uint8_t> datagram = std::move(m_state->inbound.front());
    m_state->inbound.pop_front();
    ++m_state->stats.datagramsReceived;
    return datagram;
}

bool UdpTransportEndpointUVE::HasPendingUVE() const noexcept {
    if (m_state == nullptr) {
        return false;
    }
    if (!m_state->inbound.empty()) {
        return true;
    }
    return SocketHasMailUVE(m_state->socket);
}

std::size_t UdpTransportEndpointUVE::PendingCountUVE() const noexcept {
    if (m_state == nullptr) {
        return 0U;
    }
    return m_state->inbound.size() + (SocketHasMailUVE(m_state->socket) ? 1U : 0U);
}

UdpTransportStatsUVE UdpTransportEndpointUVE::StatsUVE() const noexcept {
    if (m_state == nullptr) {
        return UdpTransportStatsUVE{};
    }
    return m_state->stats;
}

std::uint16_t UdpTransportEndpointUVE::LocalPortUVE() const noexcept {
    if (m_state == nullptr) {
        return 0U;
    }
    return m_state->localPort;
}

std::optional<std::uint32_t> ResolveUdpHostUVE(const std::string& host) {
    // DNS names top out at 253 characters; longer is garbage, not a lookup.
    if (host.empty() || host.size() > 253U) {
        return std::nullopt;
    }
    // Numeric fast path: dotted quads never touch the name service (no block, no DNS).
    in_addr parsed{};
    if (::inet_pton(AF_INET, host.c_str(), &parsed) == 1) {
        return ::ntohl(parsed.s_addr);
    }
    if (!EnsureSocketLibraryUVE()) {
        return std::nullopt;
    }
    addrinfo hints{};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;
    addrinfo* list = nullptr;
    if (::getaddrinfo(host.c_str(), nullptr, &hints, &list) != 0 || list == nullptr ||
        list->ai_addr == nullptr) {
        if (list != nullptr) {
            ::freeaddrinfo(list);
        }
        return std::nullopt;
    }
    const auto* ipv4 = reinterpret_cast<const sockaddr_in*>(list->ai_addr);
    const std::uint32_t hostOrder = ::ntohl(ipv4->sin_addr.s_addr);
    ::freeaddrinfo(list);
    return hostOrder;
}

std::optional<UdpTransportEndpointUVE> CreateUdpTransportEndpointUVE(
    const UdpTransportConfigUVE& config) {
    if (!EnsureSocketLibraryUVE()) {
        return std::nullopt;
    }
    SocketHandleUVE socket = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (socket == kInvalidSocketUVE) {
        return std::nullopt;
    }
    const auto fail = [&socket]() -> std::optional<UdpTransportEndpointUVE> {
        CloseSocketUVE(socket);
        return std::nullopt;
    };
    if (!SetNonBlockingUVE(socket)) {
        return fail();
    }
    sockaddr_in local{};
    local.sin_family = AF_INET;
    local.sin_addr.s_addr = htonl(INADDR_ANY);
    local.sin_port = htons(config.localPort);
    if (::bind(socket, reinterpret_cast<sockaddr*>(&local), sizeof(local)) != 0) {
        return fail();
    }
    const std::optional<std::uint32_t> resolved = ResolveUdpHostUVE(config.remoteHost);
    if (!resolved.has_value()) {
        return fail();
    }
    sockaddr_in remote{};
    remote.sin_family = AF_INET;
    remote.sin_addr.s_addr = ::htonl(*resolved);
    remote.sin_port = htons(config.remotePort);
    if (::connect(socket, reinterpret_cast<sockaddr*>(&remote), sizeof(remote)) != 0) {
        return fail();
    }
    std::uint16_t localPort = config.localPort;
    sockaddr_in bound{};
#if defined(_WIN32)
    int boundLength = static_cast<int>(sizeof(bound));
#else
    socklen_t boundLength = static_cast<socklen_t>(sizeof(bound));
#endif
    if (::getsockname(socket, reinterpret_cast<sockaddr*>(&bound), &boundLength) == 0) {
        localPort = ::ntohs(bound.sin_port);
    }
    auto state = std::make_shared<UdpSocketStateUVE>();
    state->socket = socket;
    state->localPort = localPort;
    state->maxPendingDatagrams = config.maxPendingDatagrams;
    return UdpTransportEndpointUVE(std::move(state));
}

namespace {

// Addresses cross in host byte order; the wire speaks network order, converted here.
[[nodiscard]] UdpEndpointAddressUVE AddressFromSockaddrUVE(const sockaddr_in& from) noexcept {
    return UdpEndpointAddressUVE{::ntohl(from.sin_addr.s_addr), ::ntohs(from.sin_port)};
}

[[nodiscard]] sockaddr_in SockaddrFromAddressUVE(const UdpEndpointAddressUVE& address) noexcept {
    sockaddr_in to{};
    to.sin_family = AF_INET;
    to.sin_addr.s_addr = ::htonl(address.host);
    to.sin_port = ::htons(address.port);
    return to;
}

void ForgetStrangerOrderUVE(UdpListenerStateUVE& state,
                             const UdpEndpointAddressUVE& stranger) {
    const auto order =
        std::find(state.strangerOrder.begin(), state.strangerOrder.end(), stranger);
    if (order != state.strangerOrder.end()) {
        state.strangerOrder.erase(order);
    }
}

} // namespace

std::string UdpEndpointAddressUVE::ToStringUVE() const {
    return std::to_string((host >> 24U) & 0xFFU) + "." + std::to_string((host >> 16U) & 0xFFU) +
           "." + std::to_string((host >> 8U) & 0xFFU) + "." + std::to_string(host & 0xFFU) + ":" +
           std::to_string(port);
}

UdpListenerPeerUVE::UdpListenerPeerUVE(std::shared_ptr<UdpListenerStateUVE> state,
                                       const UdpEndpointAddressUVE remote) noexcept
    : m_state(std::move(state)), m_remote(remote) {}

UdpListenerPeerUVE::~UdpListenerPeerUVE() = default;

bool UdpListenerPeerUVE::SendUVE(const std::vector<std::uint8_t>& payload) {
    if (m_state == nullptr) {
        return false;
    }
    const auto peer = m_state->peers.find(m_remote);
    if (peer == m_state->peers.end()) {
        return false;
    }
    if (payload.size() > kTransportMaximumDatagramBytesUVE) {
        ++peer->second.stats.datagramsDroppedOversize;
        return false;
    }
    const sockaddr_in to = SockaddrFromAddressUVE(m_remote);
#if defined(_WIN32)
    const int length = static_cast<int>(payload.size());
#else
    const std::size_t length = payload.size();
#endif
    const char* bytes =
        payload.empty() ? "" : reinterpret_cast<const char*>(payload.data());
    const auto sent = ::sendto(m_state->socket, bytes, length, 0,
                               reinterpret_cast<const sockaddr*>(&to), sizeof(to));
    if (sent < 0) {
        ++peer->second.stats.datagramsDroppedFull;
        return false;
    }
    ++peer->second.stats.datagramsSent;
    return true;
}

std::optional<std::vector<std::uint8_t>> UdpListenerPeerUVE::TryReceiveUVE() {
    if (m_state == nullptr) {
        return std::nullopt;
    }
    const auto peer = m_state->peers.find(m_remote);
    if (peer == m_state->peers.end() || peer->second.inbound.empty()) {
        return std::nullopt;
    }
    std::vector<std::uint8_t> datagram = std::move(peer->second.inbound.front());
    peer->second.inbound.pop_front();
    ++peer->second.stats.datagramsReceived;
    return datagram;
}

bool UdpListenerPeerUVE::HasPendingUVE() const noexcept {
    if (m_state == nullptr) {
        return false;
    }
    const auto peer = m_state->peers.find(m_remote);
    return peer != m_state->peers.end() && !peer->second.inbound.empty();
}

std::size_t UdpListenerPeerUVE::PendingCountUVE() const noexcept {
    if (m_state == nullptr) {
        return 0U;
    }
    const auto peer = m_state->peers.find(m_remote);
    return peer == m_state->peers.end() ? 0U : peer->second.inbound.size();
}

UdpTransportStatsUVE UdpListenerPeerUVE::StatsUVE() const noexcept {
    if (m_state == nullptr) {
        return UdpTransportStatsUVE{};
    }
    const auto peer = m_state->peers.find(m_remote);
    return peer == m_state->peers.end() ? UdpTransportStatsUVE{} : peer->second.stats;
}

UdpEndpointAddressUVE UdpListenerPeerUVE::RemoteUVE() const noexcept {
    return m_remote;
}

UdpListenerUVE::UdpListenerUVE(std::shared_ptr<UdpListenerStateUVE> state) noexcept
    : m_state(std::move(state)) {}

void UdpListenerUVE::UpdateUVE() {
    if (m_state == nullptr) {
        return;
    }
    std::vector<std::uint8_t> buffer(kUdpReceiveBufferBytesUVE);
    for (std::size_t i = 0U; i < m_state->maxPumpDatagrams; ++i) {
        sockaddr_in from{};
#if defined(_WIN32)
        int fromLength = static_cast<int>(sizeof(from));
        const int length = static_cast<int>(buffer.size());
#else
        socklen_t fromLength = static_cast<socklen_t>(sizeof(from));
        const std::size_t length = buffer.size();
#endif
        const auto received =
            ::recvfrom(m_state->socket, reinterpret_cast<char*>(buffer.data()), length, 0,
                       reinterpret_cast<sockaddr*>(&from), &fromLength);
        if (received < 0) {
            return;
        }
        ++m_state->stats.datagramsReceived;
        const auto bytes = static_cast<std::size_t>(received);
        if (bytes > kTransportMaximumDatagramBytesUVE) {
            ++m_state->stats.datagramsDroppedOversize;
            continue;
        }
        const UdpEndpointAddressUVE sender = AddressFromSockaddrUVE(from);
        const auto peer = m_state->peers.find(sender);
        if (peer != m_state->peers.end()) {
            if (peer->second.inbound.size() >= peer->second.maxPendingDatagrams) {
                ++m_state->stats.datagramsDroppedFull;
                continue;
            }
            peer->second.inbound.emplace_back(buffer.begin(),
                                              buffer.begin() +
                                                  static_cast<std::ptrdiff_t>(bytes));
            ++m_state->stats.datagramsRouted;
            continue;
        }
        auto stranger = m_state->strangers.find(sender);
        if (stranger == m_state->strangers.end()) {
            // No room for the sender - or no room for even one datagram - drops before any
            // entry exists, so the order never names an address without held mail.
            if (m_state->strangers.size() >= m_state->maxStrangers ||
                m_state->maxStrangerDatagrams == 0U) {
                ++m_state->stats.strangersDroppedFull;
                continue;
            }
            stranger = m_state->strangers
                           .emplace(sender, std::deque<std::vector<std::uint8_t>>{})
                           .first;
            m_state->strangerOrder.push_back(sender);
        }
        if (stranger->second.size() >= m_state->maxStrangerDatagrams) {
            ++m_state->stats.strangersDroppedFull;
            continue;
        }
        stranger->second.emplace_back(buffer.begin(),
                                      buffer.begin() + static_cast<std::ptrdiff_t>(bytes));
        ++m_state->stats.datagramsHeldForStrangers;
    }
}

bool UdpListenerUVE::HasStrangersUVE() const noexcept {
    return m_state != nullptr && !m_state->strangerOrder.empty();
}

std::optional<UdpEndpointAddressUVE> UdpListenerUVE::NextStrangerUVE() const {
    if (m_state == nullptr || m_state->strangerOrder.empty()) {
        return std::nullopt;
    }
    return m_state->strangerOrder.front();
}

std::size_t UdpListenerUVE::StrangerCountUVE() const noexcept {
    return m_state == nullptr ? 0U : m_state->strangers.size();
}

std::optional<UdpListenerPeerUVE> UdpListenerUVE::AcceptPeerUVE(
    const std::size_t maxPendingDatagrams) {
    if (m_state == nullptr || m_state->strangerOrder.empty()) {
        return std::nullopt;
    }
    return AcceptPeerUVE(m_state->strangerOrder.front(), maxPendingDatagrams);
}

std::optional<UdpListenerPeerUVE> UdpListenerUVE::AcceptPeerUVE(
    const UdpEndpointAddressUVE stranger, const std::size_t maxPendingDatagrams) {
    if (m_state == nullptr) {
        return std::nullopt;
    }
    const auto held = m_state->strangers.find(stranger);
    if (held == m_state->strangers.end()) {
        return std::nullopt;
    }
    UdpListenerStateUVE::PeerRecordUVE record;
    record.inbound = std::move(held->second);
    record.maxPendingDatagrams = maxPendingDatagrams;
    m_state->peers.emplace(stranger, std::move(record));
    m_state->strangers.erase(held);
    ForgetStrangerOrderUVE(*m_state, stranger);
    return UdpListenerPeerUVE(m_state, stranger);
}

bool UdpListenerUVE::RefuseStrangerUVE(const UdpEndpointAddressUVE stranger) {
    if (m_state == nullptr) {
        return false;
    }
    const auto held = m_state->strangers.find(stranger);
    if (held == m_state->strangers.end()) {
        return false;
    }
    m_state->strangers.erase(held);
    ForgetStrangerOrderUVE(*m_state, stranger);
    return true;
}

bool UdpListenerUVE::RemovePeerUVE(const UdpEndpointAddressUVE peer) {
    if (m_state == nullptr) {
        return false;
    }
    return m_state->peers.erase(peer) == 1U;
}

bool UdpListenerUVE::HasPeerUVE(const UdpEndpointAddressUVE peer) const noexcept {
    return m_state != nullptr && m_state->peers.contains(peer);
}

std::size_t UdpListenerUVE::PeerCountUVE() const noexcept {
    return m_state == nullptr ? 0U : m_state->peers.size();
}

UdpListenerStatsUVE UdpListenerUVE::StatsUVE() const noexcept {
    if (m_state == nullptr) {
        return UdpListenerStatsUVE{};
    }
    return m_state->stats;
}

std::uint16_t UdpListenerUVE::LocalPortUVE() const noexcept {
    if (m_state == nullptr) {
        return 0U;
    }
    return m_state->localPort;
}

std::optional<UdpListenerUVE> CreateUdpListenerUVE(const UdpListenerConfigUVE& config) {
    if (!EnsureSocketLibraryUVE()) {
        return std::nullopt;
    }
    SocketHandleUVE socket = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (socket == kInvalidSocketUVE) {
        return std::nullopt;
    }
    const auto fail = [&socket]() -> std::optional<UdpListenerUVE> {
        CloseSocketUVE(socket);
        return std::nullopt;
    };
    if (!SetNonBlockingUVE(socket)) {
        return fail();
    }
    sockaddr_in local{};
    local.sin_family = AF_INET;
    local.sin_addr.s_addr = htonl(INADDR_ANY);
    local.sin_port = htons(config.localPort);
    if (::bind(socket, reinterpret_cast<sockaddr*>(&local), sizeof(local)) != 0) {
        return fail();
    }
    std::uint16_t localPort = config.localPort;
    sockaddr_in bound{};
#if defined(_WIN32)
    int boundLength = static_cast<int>(sizeof(bound));
#else
    socklen_t boundLength = static_cast<socklen_t>(sizeof(bound));
#endif
    if (::getsockname(socket, reinterpret_cast<sockaddr*>(&bound), &boundLength) == 0) {
        localPort = ::ntohs(bound.sin_port);
    }
    auto state = std::make_shared<UdpListenerStateUVE>();
    state->socket = socket;
    state->localPort = localPort;
    state->maxStrangers = config.maxStrangers;
    state->maxStrangerDatagrams = config.maxStrangerDatagrams;
    state->maxPumpDatagrams = config.maxPumpDatagrams;
    return UdpListenerUVE(std::move(state));
}

} // namespace UVE::Network
