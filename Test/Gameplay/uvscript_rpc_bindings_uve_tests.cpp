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

#include "uve/component/entity_uve.h"
#include "uve/core/uvscript_object_host_uve.h"
#include "uve/entity/entity_manager_uve.h"
#include "uve/events/event_system_uve.h"
#include "uve/memory/memory_manager_uve.h"
#include "uve/network/network_rpc_uve.h"
#include "uve/uvscript/uvscript_compiler_uve.h"
#include "uve/uvscript/uvscript_instance_uve.h"
#include "uve/uvscript/uvscript_value_uve.h"

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
    return [](const std::span<const UVScript::ValueUVE> args) {
        const std::int64_t lhs = std::get<std::int64_t>(args[0]);
        const std::int64_t rhs = std::get<std::int64_t>(args[1]);
        return Network::RpcOkUVE(UVScript::ValueUVE{lhs + rhs});
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
    std::vector<UVScript::TypeUVE> params;
    UVScript::TypeUVE result;
};

class UVScriptRpcBindingsUVETest : public ::testing::Test {
protected:
    Memory::MemoryManagerUVE memoryManager;
    Events::EventSystemUVE eventSystem;
    EntityManagerUVE entityManager{memoryManager.GetDefaultAllocatorUVE(), eventSystem};

    [[nodiscard]] std::shared_ptr<const UVScript::ProgramUVE> CompileOrFailUVE(
        const std::string& source, Core::UVScriptObjectHostUVE& host) {
        const UVScript::CompileResultUVE result = UVScript::CompileUVScriptSourceUVE(source, host);
        for (const UVScript::DiagnosticUVE& d : result.diagnostics) {
            ADD_FAILURE() << d.at.line << ":" << d.at.column << " " << d.message;
        }
        return result.program;
    }

    [[nodiscard]] std::int64_t CallIntUVE(UVScript::ScriptInstanceUVE& instance,
                                          const std::string& function) {
        return std::get<std::int64_t>(*instance.CallUVE(function));
    }

    [[nodiscard]] std::int64_t CallIntUVE(UVScript::ScriptInstanceUVE& instance,
                                          const std::string& function, const std::int64_t id) {
        const std::array<UVScript::ValueUVE, 1> args{UVScript::ValueUVE{id}};
        return std::get<std::int64_t>(*instance.CallUVE(function, args));
    }
};

TEST_F(UVScriptRpcBindingsUVETest, Host_DescribesRpcSurfaceUngated) {
    // Ungated means ungated: a bare entity with no peer at all still describes every name,
    // and every call through them fails closed at run time instead.
    Core::UVScriptObjectHostUVE host{entityManager, nullptr, entityManager.CreateEntityUVE()};
    const RpcSignatureUVE cases[] = {
        {"rpc.begin", {UVScript::TypeUVE::StrUVE()}, UVScript::TypeUVE::IntUVE()},
        {"rpc.push_bool",
         {UVScript::TypeUVE::IntUVE(), UVScript::TypeUVE::BoolUVE()},
         UVScript::TypeUVE::BoolUVE()},
        {"rpc.push_int",
         {UVScript::TypeUVE::IntUVE(), UVScript::TypeUVE::IntUVE()},
         UVScript::TypeUVE::BoolUVE()},
        {"rpc.push_float",
         {UVScript::TypeUVE::IntUVE(), UVScript::TypeUVE::FloatUVE()},
         UVScript::TypeUVE::BoolUVE()},
        {"rpc.push_str",
         {UVScript::TypeUVE::IntUVE(), UVScript::TypeUVE::StrUVE()},
         UVScript::TypeUVE::BoolUVE()},
        {"rpc.push_vec3",
         {UVScript::TypeUVE::IntUVE(), UVScript::TypeUVE::Vec3UVE()},
         UVScript::TypeUVE::BoolUVE()},
        {"rpc.send", {UVScript::TypeUVE::IntUVE()}, UVScript::TypeUVE::IntUVE()},
        {"rpc.abort", {UVScript::TypeUVE::IntUVE()}, UVScript::TypeUVE::BoolUVE()},
        {"rpc.cancel", {UVScript::TypeUVE::IntUVE()}, UVScript::TypeUVE::BoolUVE()},
        {"rpc.release", {UVScript::TypeUVE::IntUVE()}, UVScript::TypeUVE::BoolUVE()},
        {"rpc.result_kind", {UVScript::TypeUVE::IntUVE()}, UVScript::TypeUVE::StrUVE()},
        {"rpc.error", {UVScript::TypeUVE::IntUVE()}, UVScript::TypeUVE::StrUVE()},
        {"rpc.result_bool", {UVScript::TypeUVE::IntUVE()}, UVScript::TypeUVE::BoolUVE()},
        {"rpc.result_int", {UVScript::TypeUVE::IntUVE()}, UVScript::TypeUVE::IntUVE()},
        {"rpc.result_float", {UVScript::TypeUVE::IntUVE()}, UVScript::TypeUVE::FloatUVE()},
        {"rpc.result_str", {UVScript::TypeUVE::IntUVE()}, UVScript::TypeUVE::StrUVE()},
        {"rpc.result_vec3", {UVScript::TypeUVE::IntUVE()}, UVScript::TypeUVE::Vec3UVE()},
    };
    for (const RpcSignatureUVE& expected : cases) {
        const std::optional<UVScript::HostFunctionUVE> described =
            host.DescribeFunctionUVE(expected.name);
        ASSERT_TRUE(described.has_value()) << expected.name;
        EXPECT_EQ(described->params, expected.params) << expected.name;
        EXPECT_EQ(described->result, expected.result) << expected.name;
    }

    const std::optional<std::vector<UVScript::TypeUVE>> event =
        host.DescribeEventUVE("rpc_response");
    ASSERT_TRUE(event.has_value());
    EXPECT_EQ(*event, (std::vector<UVScript::TypeUVE>{UVScript::TypeUVE::IntUVE(),
                                                      UVScript::TypeUVE::BoolUVE()}));
}

TEST_F(UVScriptRpcBindingsUVETest, Host_FailsClosedWithoutPeer) {
    Core::UVScriptObjectHostUVE host{entityManager, nullptr, entityManager.CreateEntityUVE()};
    const std::array<UVScript::ValueUVE, 1> begin{UVScript::ValueUVE{std::string{"add"}}};
    EXPECT_EQ(std::get<std::int64_t>(host.CallFunctionUVE("rpc.begin", begin)), 0);
    const std::array<UVScript::ValueUVE, 2> push{UVScript::ValueUVE{std::int64_t{1}},
                                                 UVScript::ValueUVE{std::int64_t{2}}};
    EXPECT_FALSE(std::get<bool>(host.CallFunctionUVE("rpc.push_int", push)));
    const std::array<UVScript::ValueUVE, 1> id{UVScript::ValueUVE{std::int64_t{1}}};
    EXPECT_EQ(std::get<std::int64_t>(host.CallFunctionUVE("rpc.send", id)), 0);
    EXPECT_FALSE(std::get<bool>(host.CallFunctionUVE("rpc.abort", id)));
    EXPECT_FALSE(std::get<bool>(host.CallFunctionUVE("rpc.cancel", id)));
    EXPECT_FALSE(std::get<bool>(host.CallFunctionUVE("rpc.release", id)));
    EXPECT_EQ(std::get<std::string>(host.CallFunctionUVE("rpc.result_kind", id)), "none");
    EXPECT_FALSE(std::get<bool>(host.CallFunctionUVE("rpc.result_bool", id)));
    EXPECT_EQ(std::get<std::int64_t>(host.CallFunctionUVE("rpc.result_int", id)), 0);
    EXPECT_EQ(std::get<double>(host.CallFunctionUVE("rpc.result_float", id)), 0.0);
    EXPECT_EQ(std::get<std::string>(host.CallFunctionUVE("rpc.result_str", id)), "");
    EXPECT_EQ(std::get<UVScript::Vec3ValueUVE>(host.CallFunctionUVE("rpc.result_vec3", id)),
              UVScript::Vec3ValueUVE{});
    EXPECT_EQ(std::get<std::string>(host.CallFunctionUVE("rpc.error", id)), "");
}

TEST_F(UVScriptRpcBindingsUVETest, Script_SendsCallAndReadsResult) {
    ScriptRpcPairUVE pair = MakeScriptRpcUVE();
    ASSERT_TRUE(pair.server.RegisterMethodUVE("add", AddHandlerUVE()));
    ASSERT_TRUE(pair.EstablishUVE(1000U));

    // The game loop in miniature: the sink records the completion and raises the event on
    // the instance, exactly as a running game would from its own response sink.
    std::vector<std::pair<std::uint32_t, bool>> responses;
    UVScript::ScriptInstanceUVE* live = nullptr;
    Core::RpcResponseSinkUVE sink = [&](const std::uint32_t id, const bool ok) {
        responses.emplace_back(id, ok);
        if (live != nullptr) {
            const std::array<UVScript::ValueUVE, 2> args{UVScript::ValueUVE{static_cast<std::int64_t>(id)},
                                                         UVScript::ValueUVE{ok}};
            static_cast<void>(live->RaiseEventUVE("rpc_response", args));
        }
    };
    Core::UVScriptObjectHostUVE host{entityManager,
                                     nullptr,
                                     entityManager.CreateEntityUVE(),
                                     nullptr,
                                     nullptr,
                                     Core::UVScriptObjectHostUVE::InstanceResolverUVE{},
                                     &pair.client,
                                     std::move(sink)};
    const auto program = CompileOrFailUVE(kRpcScriptUVE, host);
    ASSERT_NE(program, nullptr);
    UVScript::ScriptInstanceUVE instance(program, host);
    live = &instance;

    const std::int64_t call = CallIntUVE(instance, "send");
    EXPECT_NE(call, 0);
    pair.UpdateBothUVE(1000U);
    pair.UpdateBothUVE(1000U);

    ASSERT_EQ(responses.size(), 1U);
    EXPECT_EQ(responses[0].first, static_cast<std::uint32_t>(call));
    EXPECT_TRUE(responses[0].second);
    EXPECT_EQ(CallIntUVE(instance, "answer", call), 42);

    const std::array<UVScript::ValueUVE, 1> kindArgs{UVScript::ValueUVE{call}};
    EXPECT_EQ(std::get<std::string>(*instance.CallUVE("kind", kindArgs)), "int");

    // The handler from the shared script ran on the sink's raise (a print, so the log owns
    // the proof); raising again directly still finds it.
    const std::array<UVScript::ValueUVE, 2> eventArgs{UVScript::ValueUVE{call},
                                                      UVScript::ValueUVE{true}};
    EXPECT_TRUE(instance.RaiseEventUVE("rpc_response", eventArgs));
}

TEST_F(UVScriptRpcBindingsUVETest, Script_SeesErrorsPendingAndCancel) {
    ScriptRpcPairUVE pair = MakeScriptRpcUVE();
    ASSERT_TRUE(pair.server.RegisterMethodUVE("add", AddHandlerUVE()));
    ASSERT_TRUE(pair.EstablishUVE(1000U));

    std::vector<std::pair<std::uint32_t, bool>> responses;
    Core::UVScriptObjectHostUVE host{entityManager,
                                     nullptr,
                                     entityManager.CreateEntityUVE(),
                                     nullptr,
                                     nullptr,
                                     Core::UVScriptObjectHostUVE::InstanceResolverUVE{},
                                     &pair.client,
                                     [&](const std::uint32_t id, const bool ok) {
                                         responses.emplace_back(id, ok);
                                     }};
    const auto program = CompileOrFailUVE(kRpcScriptUVE, host);
    ASSERT_NE(program, nullptr);
    UVScript::ScriptInstanceUVE instance(program, host);

    const std::int64_t bad = CallIntUVE(instance, "send_bad");
    EXPECT_NE(bad, 0);
    const std::array<UVScript::ValueUVE, 1> badArgs{UVScript::ValueUVE{bad}};
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
    const std::array<UVScript::ValueUVE, 1> doomedArgs{UVScript::ValueUVE{doomed}};
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
        Core::UVScriptObjectHostUVE host{entityManager,
                                         nullptr,
                                         entityManager.CreateEntityUVE(),
                                         nullptr,
                                         nullptr,
                                         Core::UVScriptObjectHostUVE::InstanceResolverUVE{},
                                         &pair.client,
                                         [&](const std::uint32_t id, const bool ok) {
                                             responses.emplace_back(id, ok);
                                         }};
        const std::array<UVScript::ValueUVE, 1> begin{UVScript::ValueUVE{std::string{"add"}}};
        const std::int64_t staged =
            std::get<std::int64_t>(host.CallFunctionUVE("rpc.begin", begin));
        ASSERT_NE(staged, 0);
        const std::array<UVScript::ValueUVE, 2> push{UVScript::ValueUVE{staged},
                                                     UVScript::ValueUVE{std::int64_t{1}}};
        ASSERT_TRUE(std::get<bool>(host.CallFunctionUVE("rpc.push_int", push)));
        const std::array<UVScript::ValueUVE, 1> send{UVScript::ValueUVE{staged}};
        call = std::get<std::int64_t>(host.CallFunctionUVE("rpc.send", send));
        ASSERT_NE(call, 0);
        const std::array<UVScript::ValueUVE, 1> kind{UVScript::ValueUVE{call}};
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
