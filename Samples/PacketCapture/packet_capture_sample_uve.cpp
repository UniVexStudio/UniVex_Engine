// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "packet_capture_sample_uve.h"

#include <cstdint>
#include <map>
#include <memory>
#include <span>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "uve/math/vector3_uve.h"
#include "uve/network/network_inspector_uve.h"
#include "uve/network/network_packet_capture_uve.h"
#include "uve/network/network_prediction_uve.h"
#include "uve/network/network_replication_uve.h"
#include "uve/network/network_rpc_uve.h"
#include "uve/network/network_session_uve.h"
#include "uve/network/network_shot_uve.h"
#include "uve/network/network_transport_uve.h"
#include "uve/uvscript/uvscript_value_uve.h"

namespace UVE::Samples {
namespace {

constexpr Network::NetworkChannelIdUVE kReplicationChannelUVE = 21U;
constexpr Network::NetworkChannelIdUVE kRpcChannelUVE = 22U;
constexpr Network::NetworkChannelIdUVE kShotChannelUVE = 23U;
constexpr std::size_t kRingCapUVE = 8U;

[[nodiscard]] Network::InspectedChannelUVE MapChannelUVE(
    const std::optional<std::uint8_t>& channel) noexcept {
    if (!channel.has_value()) {
        return Network::InspectedChannelUVE::Unknown;
    }
    switch (*channel) {
        case kReplicationChannelUVE:
            return Network::InspectedChannelUVE::Replication;
        case kRpcChannelUVE:
            return Network::InspectedChannelUVE::Rpc;
        case kShotChannelUVE:
            return Network::InspectedChannelUVE::Shot;
        default:
            return Network::InspectedChannelUVE::Unknown;
    }
}

} // namespace

PacketCaptureSampleResultUVE RunPacketCaptureSampleUVE(
    const PacketCaptureSampleConfigUVE& config) {
    PacketCaptureSampleResultUVE result;
    auto log = [&](const std::string& line) {
        if (config.log) {
            config.log(line);
        }
    };

    Network::LoopbackTransportPairUVE pair =
        Network::CreateLoopbackTransportPairUVE(256U);
    Network::NetworkSessionUVE clientSession(
        Network::SessionRoleUVE::Client,
        std::make_unique<Network::LoopbackEndpointUVE>(pair.a));
    Network::NetworkSessionUVE serverSession(
        Network::SessionRoleUVE::Server,
        std::make_unique<Network::LoopbackEndpointUVE>(pair.b));
    if (!clientSession.EnableMultiplexingUVE() || !serverSession.EnableMultiplexingUVE()) {
        result.failures.push_back("sessions refused multiplexing");
        result.success = false;
        return result;
    }

    Network::PacketCaptureConfigUVE captureConfig;
    captureConfig.maxPackets = kRingCapUVE;
    Network::PacketCaptureUVE capture(captureConfig);
    std::uint64_t pushed = 0U;
    serverSession.SetTapUVE([&](const Network::PacketTapUVE& tap) {
        capture.PushUVE(tap.datagram, tap.direction, tap.timestampMs,
                        MapChannelUVE(tap.channel), tap.multiplexed);
        ++pushed;
    });

    Network::NetworkReplicatorUVE clientRep(clientSession, kReplicationChannelUVE);
    Network::NetworkReplicatorUVE serverRep(serverSession, kReplicationChannelUVE);
    Network::NetworkRpcUVE clientRpc(clientSession, kRpcChannelUVE);
    Network::NetworkRpcUVE serverRpc(serverSession, kRpcChannelUVE);
    Network::ShotSenderUVE shots(clientSession, kShotChannelUVE);
    Network::ShotReceiverUVE shotInbox(serverSession, kShotChannelUVE);

    auto pump = [&](const std::uint64_t nowMs) {
        clientRep.UpdateUVE(nowMs);
        clientRpc.UpdateUVE(nowMs);
        serverRep.UpdateUVE(nowMs);
        serverRpc.UpdateUVE(nowMs);
    };

    if (!clientSession.ConnectUVE(1000U, 9U)) {
        result.failures.push_back("client failed to connect");
        result.success = false;
        return result;
    }
    pump(1000U);
    pump(1000U);
    pump(1000U);
    if (clientSession.StateUVE() != Network::SessionStateUVE::Established ||
        serverSession.StateUVE() != Network::SessionStateUVE::Established) {
        result.failures.push_back("session never established");
        result.success = false;
        return result;
    }

    if (!clientRep.RegisterEntityUVE(
            7U, [] { return Math::Vector3UVE{1.0F, 2.0F, 3.0F}; },
            [](const Math::Vector3UVE&) {}) ||
        !clientRep.RegisterEntityUVE(
            9U, [] { return Math::Vector3UVE{4.0F, 5.0F, 6.0F}; },
            [](const Math::Vector3UVE&) {})) {
        result.failures.push_back("replication registration refused");
        result.success = false;
        return result;
    }
    pump(1100U);
    pump(1100U);

    if (!serverRpc.RegisterMethodUVE(
            "add", [](const std::span<const UVScript::ValueUVE> args) {
                const std::int64_t x = std::get<std::int64_t>(args[0]);
                const std::int64_t y = std::get<std::int64_t>(args[1]);
                return Network::RpcOkUVE(UVScript::ValueUVE{x + y});
            })) {
        result.failures.push_back("rpc method registration refused");
        result.success = false;
        return result;
    }
    std::map<Network::RpcCallIdUVE, Network::RpcOutcomeUVE> done;
    const Network::RpcCallIdUVE callId = clientRpc.CallUVE(
        "add", {UVScript::ValueUVE{std::int64_t{1}}, UVScript::ValueUVE{std::int64_t{2}}},
        [&](const Network::RpcCallIdUVE id, const Network::RpcOutcomeUVE& outcome) {
            done.emplace(id, outcome);
        });
    if (callId == Network::kInvalidRpcCallIdUVE) {
        result.failures.push_back("rpc call refused");
        result.success = false;
        return result;
    }
    pump(1150U);
    pump(1150U);
    pump(1150U);
    const auto answered = done.find(callId);
    if (answered == done.end() || !answered->second.ok ||
        answered->second.value != UVScript::ValueUVE{std::int64_t{3}}) {
        result.failures.push_back("rpc round trip never answered 3");
        result.success = false;
        return result;
    }

    if (!shots.FireUVE(Network::kDefaultPredictionPawnUVE, 40U, 0.5F) ||
        !shots.FireUVE(Network::kDefaultPredictionPawnUVE, 41U, 0.75F)) {
        result.failures.push_back("shot fires refused");
        result.success = false;
        return result;
    }
    pump(1180U);
    pump(1180U);
    if (shotInbox.PollUVE().size() != 2U) {
        result.failures.push_back("shot intents never arrived");
        result.success = false;
        return result;
    }

    if (!pair.a.SendUVE(std::vector<std::uint8_t>{0xFFU}) ||
        !pair.a.SendUVE(std::vector<std::uint8_t>{
            static_cast<std::uint8_t>(Network::SessionMessageTagUVE::Data)})) {
        result.failures.push_back("garbage injection refused");
        result.success = false;
        return result;
    }
    serverSession.UpdateUVE(1200U);

    PacketCaptureSampleStatsUVE stats;
    stats.pushed = pushed;
    stats.size = capture.SizeUVE();
    stats.drops = capture.DropsUVE();
    stats.malformed = capture.MalformedUVE();
    Network::PacketCaptureFilterUVE inbound;
    inbound.direction = Network::PacketCaptureDirectionUVE::Inbound;
    stats.inbound = capture.FilteredUVE(inbound).size();
    Network::PacketCaptureFilterUVE outbound;
    outbound.direction = Network::PacketCaptureDirectionUVE::Outbound;
    stats.outbound = capture.FilteredUVE(outbound).size();
    Network::PacketCaptureFilterUVE replication;
    replication.channel = Network::InspectedChannelUVE::Replication;
    stats.replication = capture.FilteredUVE(replication).size();
    Network::PacketCaptureFilterUVE rpc;
    rpc.channel = Network::InspectedChannelUVE::Rpc;
    stats.rpc = capture.FilteredUVE(rpc).size();
    Network::PacketCaptureFilterUVE shot;
    shot.channel = Network::InspectedChannelUVE::Shot;
    stats.shots = capture.FilteredUVE(shot).size();
    Network::PacketCaptureFilterUVE fire;
    fire.text = "fire";
    stats.fireHits = capture.FilteredUVE(fire).size();

    auto check = [&](const bool ok, const std::string& message) {
        if (!ok) {
            result.failures.push_back(message);
        }
    };
    check(capture.CapturedUVE() == pushed, "capture counted wrong");
    check(stats.size == kRingCapUVE,
          "ring holds " + std::to_string(stats.size) + ", not the cap");
    check(stats.drops == pushed - kRingCapUVE,
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
    for (std::size_t i = 1U; i < capture.SizeUVE(); ++i) {
        const Network::InspectedPacketUVE* const prev = capture.TryAtUVE(i - 1U);
        const Network::InspectedPacketUVE* const next = capture.TryAtUVE(i);
        if (prev == nullptr || next == nullptr ||
            next->timestampMs < prev->timestampMs) {
            ordered = false;
        }
    }
    check(ordered, "ring not oldest-first");

    const Network::InspectedPacketUVE* const oldest = capture.TryAtUVE(0U);
    check(oldest != nullptr, "ring empty");
    std::string hexdump;
    if (oldest != nullptr) {
        stats.hexdumpBytes = oldest->raw.size();
        hexdump = Network::HexdumpPacketUVE(oldest->raw);
        check(!hexdump.empty() && hexdump.rfind("00000000:", 0U) == 0U,
              "hexdump malformed");
    }

    for (std::size_t i = 0U; i < capture.SizeUVE(); ++i) {
        const Network::InspectedPacketUVE* const packet = capture.TryAtUVE(i);
        if (packet != nullptr) {
            log(Network::DescribePacketUVE(*packet));
        }
    }
    log("capture: " + std::to_string(stats.size) + "/" +
        std::to_string(capture.CapacityUVE()) + " packets (" +
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
