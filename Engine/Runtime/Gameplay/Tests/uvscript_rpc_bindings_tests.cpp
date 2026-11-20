// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include <array>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include <gtest/gtest.h>

#include "entity.h"
#include "blueprint_object_host.h"
#include "entity_manager.h"
#include "event_system.h"
#include "memory_manager.h"
#include "network_rpc.h"
#include "blueprint_compiler.h"
#include "blueprint_instance.h"
#include "blueprint_value.h"

namespace UVE::Scene::Tests {
namespace {

struct ScriptRpcPairUVE final {
    Network::LoopbackTransportPairUVE link;
    Network::NetworkRpcUVE client;
    Network::NetworkRpcUVE server;

    void UpdateBothUVE(const std::uint64_t nowMs) {
        client.UpdateUVE(nowMs);
        server.UpdateUVE(nowMs);
    }

    [[nodiscard]] bool EstablishUVE(const std::uint64_t nowMs) {
        if (!client.SessionUVE().ConnectUVE(nowMs)) {
            return false;
        }
        UpdateBothUVE(nowMs);
        UpdateBothUVE(nowMs);
        return client.SessionUVE().StateUVE() == Network::SessionStateUVE::Established &&
               server.SessionUVE().StateUVE() == Network::SessionStateUVE::Established;
    }
};

[[nodiscard]] ScriptRpcPairUVE MakeScriptRpcUVE() {
    Network::LoopbackTransportPairUVE pair = Network::CreateLoopbackTransportPairUVE(256U);
    Network::NetworkSessionUVE clientSession(Network::SessionRoleUVE::Client,
                                             std::make_unique<Network::LoopbackEndpointUVE>(pair.a));
    Network::NetworkSessionUVE serverSession(Network::SessionRoleUVE::Server,
                                             std::make_unique<Network::LoopbackEndpointUVE>(pair.b));
    Network::NetworkRpcUVE client(std::move(clientSession));
    Network::NetworkRpcUVE server(std::move(serverSession));
    return ScriptRpcPairUVE{std::move(pair), std::move(client), std::move(server)};
}

[[nodiscard]] Network::RpcMethodHandlerUVE AddHandlerUVE() {
    return [](const std::span<const Blueprint::ValueUVE> args) {
        if (args.size() < 2) {
            return Network::RpcErrorUVE("add requires 2 arguments");
        }
        const std::int64_t lhs = std::get<std::int64_t>(args[0]);
        const std::int64_t rhs = std::get<std::int64_t>(args[1]);
        return Network::RpcOkUVE(Blueprint::ValueUVE{lhs + rhs});
    };
}

// One script for every test below: the calls under test plus the readers, so each test
// compiles the same source and drives a different path through it.
inline constexpr const char* kRpcScriptUVE = R"(on rpc_response(id, ok):
    print(rpc.result_str(id))

fn send() -> int:
    let staged = rpc.begin("add")
    rpc.push_int(staged, 20)
    rpc.push_int(staged, 22)
    return rpc.send(staged)

fn send_bad() -> int:
    let staged = rpc.begin("nope")
    return rpc.send(staged)

fn answer(id: int) -> int:
    return rpc.result_int(id)

fn kind(id: int) -> str:
    return rpc.result_kind(id)

fn err(id: int) -> str:
    return rpc.error(id)

fn cancel(id: int) -> bool:
    return rpc.cancel(id)

fn release(id: int) -> bool:
    return rpc.release(id)
)";

struct RpcSignatureUVE final {
    const char* name;
    std::vector<Blueprint::TypeUVE> params;
    Blueprint::TypeUVE result;
};

class UVScriptRpcBindingsUVETest : public ::testing::Test {
protected:
    Memory::MemoryManagerUVE memoryManager;
    Events::EventSystemUVE eventSystem;
    EntityManagerUVE entityManager{memoryManager.GetDefaultAllocatorUVE(), eventSystem};

    [[nodiscard]] std::shared_ptr<const Blueprint::ProgramUVE> CompileOrFailUVE(
        const std::string& source, Core::BlueprintObjectHostUVE& host) {
        const Blueprint::CompileResultUVE result = Blueprint::CompileUVScriptSourceUVE(source, host);
        for (const Blueprint::DiagnosticUVE& d : result.diagnostics) {
            ADD_FAILURE() << d.at.line << ":" << d.at.column << " " << d.message;
        }
        return result.program;
    }

    [[nodiscard]] std::int64_t CallIntUVE(Blueprint::ScriptInstanceUVE& instance,
                                          const std::string& function) {
        return std::get<std::int64_t>(*instance.CallUVE(function));
    }

    [[nodiscard]] std::int64_t CallIntUVE(Blueprint::ScriptInstanceUVE& instance,
                                          const std::string& function, const std::int64_t id) {
        const std::array<Blueprint::ValueUVE, 1> args{Blueprint::ValueUVE{id}};
        return std::get<std::int64_t>(*instance.CallUVE(function, args));
    }
};

TEST_F(UVScriptRpcBindingsUVETest, Host_DescribesRpcSurfaceUngated) {
    // Ungated means ungated: a bare entity with no peer at all still describes every name,
    // and every call through them fails closed at run time instead.
    Core::BlueprintObjectHostUVE host{entityManager, nullptr, entityManager.CreateEntityUVE()};
    const RpcSignatureUVE cases[] = {
        {"rpc.begin", {Blueprint::TypeUVE::StrUVE()}, Blueprint::TypeUVE::IntUVE()},
        {"rpc.push_bool",
         {Blueprint::TypeUVE::IntUVE(), Blueprint::TypeUVE::BoolUVE()},
         Blueprint::TypeUVE::BoolUVE()},
        {"rpc.push_int",
         {Blueprint::TypeUVE::IntUVE(), Blueprint::TypeUVE::IntUVE()},
         Blueprint::TypeUVE::BoolUVE()},
        {"rpc.push_float",
         {Blueprint::TypeUVE::IntUVE(), Blueprint::TypeUVE::FloatUVE()},
         Blueprint::TypeUVE::BoolUVE()},
        {"rpc.push_str",
         {Blueprint::TypeUVE::IntUVE(), Blueprint::TypeUVE::StrUVE()},
         Blueprint::TypeUVE::BoolUVE()},
        {"rpc.push_vec3",
         {Blueprint::TypeUVE::IntUVE(), Blueprint::TypeUVE::Vec3UVE()},
         Blueprint::TypeUVE::BoolUVE()},
        {"rpc.send", {Blueprint::TypeUVE::IntUVE()}, Blueprint::TypeUVE::IntUVE()},
        {"rpc.abort", {Blueprint::TypeUVE::IntUVE()}, Blueprint::TypeUVE::BoolUVE()},
        {"rpc.cancel", {Blueprint::TypeUVE::IntUVE()}, Blueprint::TypeUVE::BoolUVE()},
        {"rpc.release", {Blueprint::TypeUVE::IntUVE()}, Blueprint::TypeUVE::BoolUVE()},
        {"rpc.result_kind", {Blueprint::TypeUVE::IntUVE()}, Blueprint::TypeUVE::StrUVE()},
        {"rpc.error", {Blueprint::TypeUVE::IntUVE()}, Blueprint::TypeUVE::StrUVE()},
        {"rpc.result_bool", {Blueprint::TypeUVE::IntUVE()}, Blueprint::TypeUVE::BoolUVE()},
        {"rpc.result_int", {Blueprint::TypeUVE::IntUVE()}, Blueprint::TypeUVE::IntUVE()},
        {"rpc.result_float", {Blueprint::TypeUVE::IntUVE()}, Blueprint::TypeUVE::FloatUVE()},
        {"rpc.result_str", {Blueprint::TypeUVE::IntUVE()}, Blueprint::TypeUVE::StrUVE()},
        {"rpc.result_vec3", {Blueprint::TypeUVE::IntUVE()}, Blueprint::TypeUVE::Vec3UVE()},
    };
    for (const RpcSignatureUVE& expected : cases) {
        const std::optional<Blueprint::HostFunctionUVE> described =
            host.DescribeFunctionUVE(expected.name);
        ASSERT_TRUE(described.has_value()) << expected.name;
        EXPECT_EQ(described->params, expected.params) << expected.name;
        EXPECT_EQ(described->result, expected.result) << expected.name;
    }

    const std::optional<std::vector<Blueprint::TypeUVE>> event =
        host.DescribeEventUVE("rpc_response");
    ASSERT_TRUE(event.has_value());
    EXPECT_EQ(*event, (std::vector<Blueprint::TypeUVE>{Blueprint::TypeUVE::IntUVE(),
                                                      Blueprint::TypeUVE::BoolUVE()}));
}

TEST_F(UVScriptRpcBindingsUVETest, Host_FailsClosedWithoutPeer) {
    Core::BlueprintObjectHostUVE host{entityManager, nullptr, entityManager.CreateEntityUVE()};
    const std::array<Blueprint::ValueUVE, 1> begin{Blueprint::ValueUVE{std::string{"add"}}};
    EXPECT_EQ(std::get<std::int64_t>(host.CallFunctionUVE("rpc.begin", begin)), 0);
    const std::array<Blueprint::ValueUVE, 2> push{Blueprint::ValueUVE{std::int64_t{1}},
                                                 Blueprint::ValueUVE{std::int64_t{2}}};
    EXPECT_FALSE(std::get<bool>(host.CallFunctionUVE("rpc.push_int", push)));
    const std::array<Blueprint::ValueUVE, 1> id{Blueprint::ValueUVE{std::int64_t{1}}};
    EXPECT_EQ(std::get<std::int64_t>(host.CallFunctionUVE("rpc.send", id)), 0);
    EXPECT_FALSE(std::get<bool>(host.CallFunctionUVE("rpc.abort", id)));
    EXPECT_FALSE(std::get<bool>(host.CallFunctionUVE("rpc.cancel", id)));
    EXPECT_FALSE(std::get<bool>(host.CallFunctionUVE("rpc.release", id)));
    EXPECT_EQ(std::get<std::string>(host.CallFunctionUVE("rpc.result_kind", id)), "none");
    EXPECT_FALSE(std::get<bool>(host.CallFunctionUVE("rpc.result_bool", id)));
    EXPECT_EQ(std::get<std::int64_t>(host.CallFunctionUVE("rpc.result_int", id)), 0);
    EXPECT_EQ(std::get<double>(host.CallFunctionUVE("rpc.result_float", id)), 0.0);
    EXPECT_EQ(std::get<std::string>(host.CallFunctionUVE("rpc.result_str", id)), "");
    EXPECT_EQ(std::get<Blueprint::Vec3ValueUVE>(host.CallFunctionUVE("rpc.result_vec3", id)),
              Blueprint::Vec3ValueUVE{});
    EXPECT_EQ(std::get<std::string>(host.CallFunctionUVE("rpc.error", id)), "");
}

TEST_F(UVScriptRpcBindingsUVETest, Script_SendsCallAndReadsResult) {
    ScriptRpcPairUVE pair = MakeScriptRpcUVE();
    ASSERT_TRUE(pair.server.RegisterMethodUVE("add", AddHandlerUVE()));
    ASSERT_TRUE(pair.EstablishUVE(1000U));

    // The game loop in miniature: the sink records the completion and raises the event on
    // the instance, exactly as a running game would from its own response sink.
    std::vector<std::pair<std::uint32_t, bool>> responses;
    Blueprint::ScriptInstanceUVE* live = nullptr;
    Core::RpcResponseSinkUVE sink = [&](const std::uint32_t id, const bool ok) {
        responses.emplace_back(id, ok);
        if (live != nullptr) {
            const std::array<Blueprint::ValueUVE, 2> args{Blueprint::ValueUVE{static_cast<std::int64_t>(id)},
                                                         Blueprint::ValueUVE{ok}};
            static_cast<void>(live->RaiseEventUVE("rpc_response", args));
        }
    };
    Core::BlueprintObjectHostUVE host{entityManager,
                                     nullptr,
                                     entityManager.CreateEntityUVE(),
                                     nullptr,
                                     nullptr,
                                     Core::BlueprintObjectHostUVE::InstanceResolverUVE{},
                                     &pair.client,
                                     std::move(sink)};
    const auto program = CompileOrFailUVE(kRpcScriptUVE, host);
    ASSERT_NE(program, nullptr);
    Blueprint::ScriptInstanceUVE instance(program, host);
    live = &instance;

    const std::int64_t call = CallIntUVE(instance, "send");
    EXPECT_NE(call, 0);
    pair.UpdateBothUVE(1000U);
    pair.UpdateBothUVE(1000U);

    ASSERT_EQ(responses.size(), 1U);
    EXPECT_EQ(responses[0].first, static_cast<std::uint32_t>(call));
    EXPECT_TRUE(responses[0].second);
    EXPECT_EQ(CallIntUVE(instance, "answer", call), 42);

    const std::array<Blueprint::ValueUVE, 1> kindArgs{Blueprint::ValueUVE{call}};
    EXPECT_EQ(std::get<std::string>(*instance.CallUVE("kind", kindArgs)), "int");

    // The handler from the shared script ran on the sink's raise (a print, so the log owns
    // the proof); raising again directly still finds it.
    const std::array<Blueprint::ValueUVE, 2> eventArgs{Blueprint::ValueUVE{call},
                                                      Blueprint::ValueUVE{true}};
    EXPECT_TRUE(instance.RaiseEventUVE("rpc_response", eventArgs));
}

TEST_F(UVScriptRpcBindingsUVETest, Script_SeesErrorsPendingAndCancel) {
    ScriptRpcPairUVE pair = MakeScriptRpcUVE();
    ASSERT_TRUE(pair.server.RegisterMethodUVE("add", AddHandlerUVE()));
    ASSERT_TRUE(pair.EstablishUVE(1000U));

    std::vector<std::pair<std::uint32_t, bool>> responses;
    Core::BlueprintObjectHostUVE host{entityManager,
                                     nullptr,
                                     entityManager.CreateEntityUVE(),
                                     nullptr,
                                     nullptr,
                                     Core::BlueprintObjectHostUVE::InstanceResolverUVE{},
                                     &pair.client,
                                     [&](const std::uint32_t id, const bool ok) {
                                         responses.emplace_back(id, ok);
                                     }};
    const auto program = CompileOrFailUVE(kRpcScriptUVE, host);
    ASSERT_NE(program, nullptr);
    Blueprint::ScriptInstanceUVE instance(program, host);

    const std::int64_t bad = CallIntUVE(instance, "send_bad");
    EXPECT_NE(bad, 0);
    const std::array<Blueprint::ValueUVE, 1> badArgs{Blueprint::ValueUVE{bad}};
    EXPECT_EQ(std::get<std::string>(*instance.CallUVE("kind", badArgs)), "pending");
    pair.UpdateBothUVE(1000U);
    pair.UpdateBothUVE(1000U);

    ASSERT_EQ(responses.size(), 1U);
    EXPECT_FALSE(responses[0].second);
    EXPECT_EQ(std::get<std::string>(*instance.CallUVE("kind", badArgs)), "error");
    EXPECT_FALSE(std::get<std::string>(*instance.CallUVE("err", badArgs)).empty());

    // Released means gone: the kind falls back to "none" and releasing twice fails.
    EXPECT_TRUE(std::get<bool>(*instance.CallUVE("release", badArgs)));
    EXPECT_EQ(std::get<std::string>(*instance.CallUVE("kind", badArgs)), "none");
    EXPECT_FALSE(std::get<bool>(*instance.CallUVE("release", badArgs)));

    // A cancelled call never completes: no sink fire, no retained outcome, and the late
    // response lands as stale instead of resurrecting it.
    const std::int64_t doomed = CallIntUVE(instance, "send_bad");
    const std::array<Blueprint::ValueUVE, 1> doomedArgs{Blueprint::ValueUVE{doomed}};
    EXPECT_TRUE(std::get<bool>(*instance.CallUVE("cancel", doomedArgs)));
    EXPECT_EQ(std::get<std::string>(*instance.CallUVE("kind", doomedArgs)), "none");
    pair.UpdateBothUVE(1000U);
    pair.UpdateBothUVE(1000U);
    EXPECT_EQ(responses.size(), 1U);
    EXPECT_EQ(std::get<std::string>(*instance.CallUVE("kind", doomedArgs)), "none");
    EXPECT_EQ(pair.client.StatsUVE().responsesMatched, 1U);
    EXPECT_EQ(pair.client.StatsUVE().responsesIgnoredStale, 1U);
}

TEST_F(UVScriptRpcBindingsUVETest, Host_CancelsPendingCallsOnDestruction) {
    ScriptRpcPairUVE pair = MakeScriptRpcUVE();
    ASSERT_TRUE(pair.server.RegisterMethodUVE("add", AddHandlerUVE()));
    ASSERT_TRUE(pair.EstablishUVE(1000U));

    std::vector<std::pair<std::uint32_t, bool>> responses;
    std::int64_t call = 0;
    {
        Core::BlueprintObjectHostUVE host{entityManager,
                                         nullptr,
                                         entityManager.CreateEntityUVE(),
                                         nullptr,
                                         nullptr,
                                         Core::BlueprintObjectHostUVE::InstanceResolverUVE{},
                                         &pair.client,
                                         [&](const std::uint32_t id, const bool ok) {
                                             responses.emplace_back(id, ok);
                                         }};
        const std::array<Blueprint::ValueUVE, 1> begin{Blueprint::ValueUVE{std::string{"add"}}};
        const std::int64_t staged =
            std::get<std::int64_t>(host.CallFunctionUVE("rpc.begin", begin));
        ASSERT_NE(staged, 0);
        const std::array<Blueprint::ValueUVE, 2> push1{Blueprint::ValueUVE{staged},
                                                     Blueprint::ValueUVE{std::int64_t{1}}};
        ASSERT_TRUE(std::get<bool>(host.CallFunctionUVE("rpc.push_int", push1)));
        const std::array<Blueprint::ValueUVE, 2> push2{Blueprint::ValueUVE{staged},
                                                     Blueprint::ValueUVE{std::int64_t{2}}};
        ASSERT_TRUE(std::get<bool>(host.CallFunctionUVE("rpc.push_int", push2)));
        const std::array<Blueprint::ValueUVE, 1> send{Blueprint::ValueUVE{staged}};
        call = std::get<std::int64_t>(host.CallFunctionUVE("rpc.send", send));
        ASSERT_NE(call, 0);
        const std::array<Blueprint::ValueUVE, 1> kind{Blueprint::ValueUVE{call}};
        EXPECT_EQ(std::get<std::string>(host.CallFunctionUVE("rpc.result_kind", kind)), "pending");
    }

    // The host is gone but the peer lives on: the exchange still runs, the completion has
    // nowhere to go, and the late response reads as stale.
    pair.UpdateBothUVE(1000U);
    pair.UpdateBothUVE(1000U);
    EXPECT_TRUE(responses.empty());
    EXPECT_FALSE(pair.client.ResultUVE(static_cast<Network::RpcCallIdUVE>(call)).has_value());
    EXPECT_EQ(pair.client.StatsUVE().responsesMatched, 0U);
    EXPECT_EQ(pair.client.StatsUVE().responsesIgnoredStale, 1U);
    EXPECT_EQ(pair.server.StatsUVE().responsesSent, 1U);
}

} // namespace
} // namespace UVE::Scene::Tests
