// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "packet_capture_sample.h"

#include <cstdint>
#include <map>
#include <memory>
#include <span>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "vector3.h"
#include "network_inspector.h"
#include "network_packet_capture.h"
#include "network_prediction.h"
#include "network_replication.h"
#include "network_rpc.h"
#include "network_session.h"
#include "network_shot.h"
#include "network_transport.h"
#include "uvscript_value.h"

namespace UVE::Samples {
namespace {

constexpr Network::NetworkChannelId kReplicationChannel = 21U;
constexpr Network::NetworkChannelId kRpcChannel = 22U;
constexpr Network::NetworkChannelId kShotChannel = 23U;
constexpr std::size_t kRingCap = 8U;

[[nodiscard]] Network::InspectedChannel MapChannel(
    const std::optional<std::uint8_t>& channel) noexcept {
    if (!channel.has_value()) {
        return Network::InspectedChannel::Unknown;
    }
    switch (*channel) {
        case kReplicationChannel:
            return Network::InspectedChannel::Replication;
        case kRpcChannel:
            return Network::InspectedChannel::Rpc;
        case kShotChannel:
            return Network::InspectedChannel::Shot;
        default:
            return Network::InspectedChannel::Unknown;
    }
}

} // namespace

PacketCaptureSampleResult RunPacketCaptureSample(
    const PacketCaptureSampleConfig& config) {
    PacketCaptureSampleResult result;
    auto log = [&](const std::string& line) {
        if (config.log) {
            config.log(line);
        }
    };

    Network::LoopbackTransportPair pair =
        Network::CreateLoopbackTransportPair(256U);
    Network::NetworkSession clientSession(
        Network::SessionRole::Client,
        std::make_unique<Network::LoopbackEndpoint>(pair.a));
    Network::NetworkSession serverSession(
        Network::SessionRole::Server,
        std::make_unique<Network::LoopbackEndpoint>(pair.b));
    if (!clientSession.EnableMultiplexing() || !serverSession.EnableMultiplexing()) {
        result.failures.push_back("sessions refused multiplexing");
        result.success = false;
        return result;
    }

    Network::PacketCaptureConfig captureConfig;
    captureConfig.maxPackets = kRingCap;
    Network::PacketCapture capture(captureConfig);
    std::uint64_t pushed = 0U;
    serverSession.SetTap([&](const Network::PacketTap& tap) {
        capture.Push(tap.datagram, tap.direction, tap.timestampMs,
                        MapChannel(tap.channel), tap.multiplexed);
        ++pushed;
    });

    Network::NetworkReplicator clientRep(clientSession, kReplicationChannel);
    Network::NetworkReplicator serverRep(serverSession, kReplicationChannel);
    Network::NetworkRpc clientRpc(clientSession, kRpcChannel);
    Network::NetworkRpc serverRpc(serverSession, kRpcChannel);
    Network::ShotSender shots(clientSession, kShotChannel);
    Network::ShotReceiver shotInbox(serverSession, kShotChannel);

    auto pump = [&](const std::uint64_t nowMs) {
        clientRep.Update(nowMs);
        clientRpc.Update(nowMs);
        serverRep.Update(nowMs);
        serverRpc.Update(nowMs);
    };

    if (!clientSession.Connect(1000U, 9U)) {
        result.failures.push_back("client failed to connect");
        result.success = false;
        return result;
    }
    pump(1000U);
    pump(1000U);
    pump(1000U);
    if (clientSession.State() != Network::SessionState::Established ||
        serverSession.State() != Network::SessionState::Established) {
        result.failures.push_back("session never established");
        result.success = false;
        return result;
    }

    if (!clientRep.RegisterEntity(
            7U, [] { return Math::Vector3{1.0F, 2.0F, 3.0F}; },
            [](const Math::Vector3&) {}) ||
        !clientRep.RegisterEntity(
            9U, [] { return Math::Vector3{4.0F, 5.0F, 6.0F}; },
            [](const Math::Vector3&) {})) {
        result.failures.push_back("replication registration refused");
        result.success = false;
        return result;
    }
    pump(1100U);
    pump(1100U);

    if (!serverRpc.RegisterMethod(
            "add", [](const std::span<const UVScript::Value> args) {
                const std::int64_t x = std::get<std::int64_t>(args[0]);
                const std::int64_t y = std::get<std::int64_t>(args[1]);
                return Network::RpcOk(UVScript::Value{x + y});
            })) {
        result.failures.push_back("rpc method registration refused");
        result.success = false;
        return result;
    }
    std::map<Network::RpcCallId, Network::RpcOutcome> done;
    const Network::RpcCallId callId = clientRpc.Call(
        "add", {UVScript::Value{std::int64_t{1}}, UVScript::Value{std::int64_t{2}}},
        [&](const Network::RpcCallId id, const Network::RpcOutcome& outcome) {
            done.emplace(id, outcome);
        });
    if (callId == Network::kInvalidRpcCallId) {
        result.failures.push_back("rpc call refused");
        result.success = false;
        return result;
    }
    pump(1150U);
    pump(1150U);
    pump(1150U);
    const auto answered = done.find(callId);
    if (answered == done.end() || !answered->second.ok ||
        answered->second.value != UVScript::Value{std::int64_t{3}}) {
        result.failures.push_back("rpc round trip never answered 3");
        result.success = false;
        return result;
    }

    if (!shots.Fire(Network::kDefaultPredictionPawn, 40U, 0.5F) ||
        !shots.Fire(Network::kDefaultPredictionPawn, 41U, 0.75F)) {
        result.failures.push_back("shot fires refused");
        result.success = false;
        return result;
    }
    pump(1180U);
    pump(1180U);
    if (shotInbox.Poll().size() != 2U) {
        result.failures.push_back("shot intents never arrived");
        result.success = false;
        return result;
    }

    if (!pair.a.Send(std::vector<std::uint8_t>{0xFFU}) ||
        !pair.a.Send(std::vector<std::uint8_t>{
            static_cast<std::uint8_t>(Network::SessionMessageTag::Data)})) {
        result.failures.push_back("garbage injection refused");
        result.success = false;
        return result;
    }
    serverSession.Update(1200U);

    PacketCaptureSampleStats stats;
    stats.pushed = pushed;
    stats.size = capture.Size();
    stats.drops = capture.Drops();
    stats.malformed = capture.Malformed();
    Network::PacketCaptureFilter inbound;
    inbound.direction = Network::PacketCaptureDirection::Inbound;
    stats.inbound = capture.Filtered(inbound).size();
    Network::PacketCaptureFilter outbound;
    outbound.direction = Network::PacketCaptureDirection::Outbound;
    stats.outbound = capture.Filtered(outbound).size();
    Network::PacketCaptureFilter replication;
    replication.channel = Network::InspectedChannel::Replication;
    stats.replication = capture.Filtered(replication).size();
    Network::PacketCaptureFilter rpc;
    rpc.channel = Network::InspectedChannel::Rpc;
    stats.rpc = capture.Filtered(rpc).size();
    Network::PacketCaptureFilter shot;
    shot.channel = Network::InspectedChannel::Shot;
    stats.shots = capture.Filtered(shot).size();
    Network::PacketCaptureFilter fire;
    fire.text = "fire";
    stats.fireHits = capture.Filtered(fire).size();

    auto check = [&](const bool ok, const std::string& message) {
        if (!ok) {
            result.failures.push_back(message);
        }
    };
    check(capture.Captured() == pushed, "capture counted wrong");
    check(stats.size == kRingCap,
          "ring holds " + std::to_string(stats.size) + ", not the cap");
    check(stats.drops == pushed - kRingCap,
          "drops " + std::to_string(stats.drops) + " mismatch");
    check(stats.malformed == 2U,
          "malformed " + std::to_string(stats.malformed) + ", not the 2 injected");
    check(stats.inbound + stats.outbound == stats.size, "direction split broken");
    check(stats.inbound > 0U && stats.outbound > 0U, "one direction never tapped");
    check(stats.replication >= 1U, "no replication frames retained");
    check(stats.rpc == 2U, "rpc call/response not both retained");
    check(stats.shots == 2U, "shot intents not both retained");
    check(stats.fireHits == 2U, "text search missed the shot summaries");

    bool ordered = true;
    for (std::size_t i = 1U; i < capture.Size(); ++i) {
        const Network::InspectedPacket* const prev = capture.TryAt(i - 1U);
        const Network::InspectedPacket* const next = capture.TryAt(i);
        if (prev == nullptr || next == nullptr ||
            next->timestampMs < prev->timestampMs) {
            ordered = false;
        }
    }
    check(ordered, "ring not oldest-first");

    const Network::InspectedPacket* const oldest = capture.TryAt(0U);
    check(oldest != nullptr, "ring empty");
    std::string hexdump;
    if (oldest != nullptr) {
        stats.hexdumpBytes = oldest->raw.size();
        hexdump = Network::HexdumpPacket(oldest->raw);
        check(!hexdump.empty() && hexdump.rfind("00000000:", 0U) == 0U,
              "hexdump malformed");
    }

    for (std::size_t i = 0U; i < capture.Size(); ++i) {
        const Network::InspectedPacket* const packet = capture.TryAt(i);
        if (packet != nullptr) {
            log(Network::DescribePacket(*packet));
        }
    }
    log("capture: " + std::to_string(stats.size) + "/" +
        std::to_string(capture.Capacity()) + " packets (" +
        std::to_string(stats.drops) + " dropped, " + std::to_string(stats.malformed) +
        " malformed), " + std::to_string(result.failures.size()) +
        (result.failures.size() == 1U ? " violation" : " violations"));
    if (!hexdump.empty()) {
        log(hexdump);
    }
    result.stats = stats;
    result.success = result.failures.empty();
    return result;
}

} // namespace UVE::Samples
