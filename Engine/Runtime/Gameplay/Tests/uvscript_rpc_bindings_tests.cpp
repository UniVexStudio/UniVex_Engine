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

struct ScriptRpcPair final {
    Network::LoopbackTransportPair link;
    Network::NetworkRpc client;
    Network::NetworkRpc server;

    void UpdateBoth(const std::uint64_t nowMs) {
        client.Update(nowMs);
        server.Update(nowMs);
    }

    [[nodiscard]] bool Establish(const std::uint64_t nowMs) {
        if (!client.Session().Connect(nowMs)) {
            return false;
        }
        UpdateBoth(nowMs);
        UpdateBoth(nowMs);
        return client.Session().State() == Network::SessionState::Established &&
               server.Session().State() == Network::SessionState::Established;
    }
};

[[nodiscard]] ScriptRpcPair MakeScriptRpc() {
    Network::LoopbackTransportPair pair = Network::CreateLoopbackTransportPair(256U);
    Network::NetworkSession clientSession(Network::SessionRole::Client,
                                             std::make_unique<Network::LoopbackEndpoint>(pair.a));
    Network::NetworkSession serverSession(Network::SessionRole::Server,
                                             std::make_unique<Network::LoopbackEndpoint>(pair.b));
    Network::NetworkRpc client(std::move(clientSession));
    Network::NetworkRpc server(std::move(serverSession));
    return ScriptRpcPair{std::move(pair), std::move(client), std::move(server)};
}

[[nodiscard]] Network::RpcMethodHandler AddHandler() {
    return [](const std::span<const Blueprint::Value> args) {
        if (args.size() < 2) {
            return Network::RpcError("add requires 2 arguments");
        }
        const std::int64_t lhs = std::get<std::int64_t>(args[0]);
        const std::int64_t rhs = std::get<std::int64_t>(args[1]);
        return Network::RpcOk(Blueprint::Value{lhs + rhs});
    };
}

// One script for every test below: the calls under test plus the readers, so each test
// compiles the same source and drives a different path through it.
inline constexpr const char* kRpcScript = R"(on rpc_response(id, ok):
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

struct RpcSignature final {
    const char* name;
    std::vector<Blueprint::Type> params;
    Blueprint::Type result;
};

class UVScriptRpcBindingsTest : public ::testing::Test {
protected:
    Memory::MemoryManager memoryManager;
    Events::EventSystem eventSystem;
    EntityManager entityManager{memoryManager.GetDefaultAllocator(), eventSystem};

    [[nodiscard]] std::shared_ptr<const Blueprint::Program> CompileOrFail(
        const std::string& source, Core::BlueprintObjectHost& host) {
        const Blueprint::CompileResult result = Blueprint::CompileUVScriptSource(source, host);
        for (const Blueprint::Diagnostic& d : result.diagnostics) {
            ADD_FAILURE() << d.at.line << ":" << d.at.column << " " << d.message;
        }
        return result.program;
    }

    [[nodiscard]] std::int64_t CallInt(Blueprint::ScriptInstance& instance,
                                          const std::string& function) {
        return std::get<std::int64_t>(*instance.Call(function));
    }

    [[nodiscard]] std::int64_t CallInt(Blueprint::ScriptInstance& instance,
                                          const std::string& function, const std::int64_t id) {
        const std::array<Blueprint::Value, 1> args{Blueprint::Value{id}};
        return std::get<std::int64_t>(*instance.Call(function, args));
    }
};

TEST_F(UVScriptRpcBindingsTest, Host_DescribesRpcSurfaceUngated) {
    // Ungated means ungated: a bare entity with no peer at all still describes every name,
    // and every call through them fails closed at run time instead.
    Core::BlueprintObjectHost host{entityManager, nullptr, entityManager.CreateEntity()};
    const RpcSignature cases[] = {
        {"rpc.begin", {Blueprint::Type::Str()}, Blueprint::Type::Int()},
        {"rpc.push_bool",
         {Blueprint::Type::Int(), Blueprint::Type::Bool()},
         Blueprint::Type::Bool()},
        {"rpc.push_int",
         {Blueprint::Type::Int(), Blueprint::Type::Int()},
         Blueprint::Type::Bool()},
        {"rpc.push_float",
         {Blueprint::Type::Int(), Blueprint::Type::Float()},
         Blueprint::Type::Bool()},
        {"rpc.push_str",
         {Blueprint::Type::Int(), Blueprint::Type::Str()},
         Blueprint::Type::Bool()},
        {"rpc.push_vec3",
         {Blueprint::Type::Int(), Blueprint::Type::Vec3()},
         Blueprint::Type::Bool()},
        {"rpc.send", {Blueprint::Type::Int()}, Blueprint::Type::Int()},
        {"rpc.abort", {Blueprint::Type::Int()}, Blueprint::Type::Bool()},
        {"rpc.cancel", {Blueprint::Type::Int()}, Blueprint::Type::Bool()},
        {"rpc.release", {Blueprint::Type::Int()}, Blueprint::Type::Bool()},
        {"rpc.result_kind", {Blueprint::Type::Int()}, Blueprint::Type::Str()},
        {"rpc.error", {Blueprint::Type::Int()}, Blueprint::Type::Str()},
        {"rpc.result_bool", {Blueprint::Type::Int()}, Blueprint::Type::Bool()},
        {"rpc.result_int", {Blueprint::Type::Int()}, Blueprint::Type::Int()},
        {"rpc.result_float", {Blueprint::Type::Int()}, Blueprint::Type::Float()},
        {"rpc.result_str", {Blueprint::Type::Int()}, Blueprint::Type::Str()},
        {"rpc.result_vec3", {Blueprint::Type::Int()}, Blueprint::Type::Vec3()},
    };
    for (const RpcSignature& expected : cases) {
        const std::optional<Blueprint::HostFunction> described =
            host.DescribeFunction(expected.name);
        ASSERT_TRUE(described.has_value()) << expected.name;
        EXPECT_EQ(described->params, expected.params) << expected.name;
        EXPECT_EQ(described->result, expected.result) << expected.name;
    }

    const std::optional<std::vector<Blueprint::Type>> event =
        host.DescribeEvent("rpc_response");
    ASSERT_TRUE(event.has_value());
    EXPECT_EQ(*event, (std::vector<Blueprint::Type>{Blueprint::Type::Int(),
                                                      Blueprint::Type::Bool()}));
}

TEST_F(UVScriptRpcBindingsTest, Host_FailsClosedWithoutPeer) {
    Core::BlueprintObjectHost host{entityManager, nullptr, entityManager.CreateEntity()};
    const std::array<Blueprint::Value, 1> begin{Blueprint::Value{std::string{"add"}}};
    EXPECT_EQ(std::get<std::int64_t>(host.CallFunction("rpc.begin", begin)), 0);
    const std::array<Blueprint::Value, 2> push{Blueprint::Value{std::int64_t{1}},
                                                 Blueprint::Value{std::int64_t{2}}};
    EXPECT_FALSE(std::get<bool>(host.CallFunction("rpc.push_int", push)));
    const std::array<Blueprint::Value, 1> id{Blueprint::Value{std::int64_t{1}}};
    EXPECT_EQ(std::get<std::int64_t>(host.CallFunction("rpc.send", id)), 0);
    EXPECT_FALSE(std::get<bool>(host.CallFunction("rpc.abort", id)));
    EXPECT_FALSE(std::get<bool>(host.CallFunction("rpc.cancel", id)));
    EXPECT_FALSE(std::get<bool>(host.CallFunction("rpc.release", id)));
    EXPECT_EQ(std::get<std::string>(host.CallFunction("rpc.result_kind", id)), "none");
    EXPECT_FALSE(std::get<bool>(host.CallFunction("rpc.result_bool", id)));
    EXPECT_EQ(std::get<std::int64_t>(host.CallFunction("rpc.result_int", id)), 0);
    EXPECT_EQ(std::get<double>(host.CallFunction("rpc.result_float", id)), 0.0);
    EXPECT_EQ(std::get<std::string>(host.CallFunction("rpc.result_str", id)), "");
    EXPECT_EQ(std::get<Blueprint::Vec3Value>(host.CallFunction("rpc.result_vec3", id)),
              Blueprint::Vec3Value{});
    EXPECT_EQ(std::get<std::string>(host.CallFunction("rpc.error", id)), "");
}

TEST_F(UVScriptRpcBindingsTest, Script_SendsCallAndReadsResult) {
    ScriptRpcPair pair = MakeScriptRpc();
    ASSERT_TRUE(pair.server.RegisterMethod("add", AddHandler()));
    ASSERT_TRUE(pair.Establish(1000U));

    // The game loop in miniature: the sink records the completion and raises the event on
    // the instance, exactly as a running game would from its own response sink.
    std::vector<std::pair<std::uint32_t, bool>> responses;
    Blueprint::ScriptInstance* live = nullptr;
    Core::RpcResponseSink sink = [&](const std::uint32_t id, const bool ok) {
        responses.emplace_back(id, ok);
        if (live != nullptr) {
            const std::array<Blueprint::Value, 2> args{Blueprint::Value{static_cast<std::int64_t>(id)},
                                                         Blueprint::Value{ok}};
            static_cast<void>(live->RaiseEvent("rpc_response", args));
        }
    };
    Core::BlueprintObjectHost host{entityManager,
                                     nullptr,
                                     entityManager.CreateEntity(),
                                     nullptr,
                                     nullptr,
                                     Core::BlueprintObjectHost::InstanceResolver{},
                                     &pair.client,
                                     std::move(sink)};
    const auto program = CompileOrFail(kRpcScript, host);
    ASSERT_NE(program, nullptr);
    Blueprint::ScriptInstance instance(program, host);
    live = &instance;

    const std::int64_t call = CallInt(instance, "send");
    EXPECT_NE(call, 0);
    pair.UpdateBoth(1000U);
    pair.UpdateBoth(1000U);

    ASSERT_EQ(responses.size(), 1U);
    EXPECT_EQ(responses[0].first, static_cast<std::uint32_t>(call));
    EXPECT_TRUE(responses[0].second);
    EXPECT_EQ(CallInt(instance, "answer", call), 42);

    const std::array<Blueprint::Value, 1> kindArgs{Blueprint::Value{call}};
    EXPECT_EQ(std::get<std::string>(*instance.Call("kind", kindArgs)), "int");

    // The handler from the shared script ran on the sink's raise (a print, so the log owns
    // the proof); raising again directly still finds it.
    const std::array<Blueprint::Value, 2> eventArgs{Blueprint::Value{call},
                                                      Blueprint::Value{true}};
    EXPECT_TRUE(instance.RaiseEvent("rpc_response", eventArgs));
}

TEST_F(UVScriptRpcBindingsTest, Script_SeesErrorsPendingAndCancel) {
    ScriptRpcPair pair = MakeScriptRpc();
    ASSERT_TRUE(pair.server.RegisterMethod("add", AddHandler()));
    ASSERT_TRUE(pair.Establish(1000U));

    std::vector<std::pair<std::uint32_t, bool>> responses;
    Core::BlueprintObjectHost host{entityManager,
                                     nullptr,
                                     entityManager.CreateEntity(),
                                     nullptr,
                                     nullptr,
                                     Core::BlueprintObjectHost::InstanceResolver{},
                                     &pair.client,
                                     [&](const std::uint32_t id, const bool ok) {
                                         responses.emplace_back(id, ok);
                                     }};
    const auto program = CompileOrFail(kRpcScript, host);
    ASSERT_NE(program, nullptr);
    Blueprint::ScriptInstance instance(program, host);

    const std::int64_t bad = CallInt(instance, "send_bad");
    EXPECT_NE(bad, 0);
    const std::array<Blueprint::Value, 1> badArgs{Blueprint::Value{bad}};
    EXPECT_EQ(std::get<std::string>(*instance.Call("kind", badArgs)), "pending");
    pair.UpdateBoth(1000U);
    pair.UpdateBoth(1000U);

    ASSERT_EQ(responses.size(), 1U);
    EXPECT_FALSE(responses[0].second);
    EXPECT_EQ(std::get<std::string>(*instance.Call("kind", badArgs)), "error");
    EXPECT_FALSE(std::get<std::string>(*instance.Call("err", badArgs)).empty());

    // Released means gone: the kind falls back to "none" and releasing twice fails.
    EXPECT_TRUE(std::get<bool>(*instance.Call("release", badArgs)));
    EXPECT_EQ(std::get<std::string>(*instance.Call("kind", badArgs)), "none");
    EXPECT_FALSE(std::get<bool>(*instance.Call("release", badArgs)));

    // A cancelled call never completes: no sink fire, no retained outcome, and the late
    // response lands as stale instead of resurrecting it.
    const std::int64_t doomed = CallInt(instance, "send_bad");
    const std::array<Blueprint::Value, 1> doomedArgs{Blueprint::Value{doomed}};
    EXPECT_TRUE(std::get<bool>(*instance.Call("cancel", doomedArgs)));
    EXPECT_EQ(std::get<std::string>(*instance.Call("kind", doomedArgs)), "none");
    pair.UpdateBoth(1000U);
    pair.UpdateBoth(1000U);
    EXPECT_EQ(responses.size(), 1U);
    EXPECT_EQ(std::get<std::string>(*instance.Call("kind", doomedArgs)), "none");
    EXPECT_EQ(pair.client.Stats().responsesMatched, 1U);
    EXPECT_EQ(pair.client.Stats().responsesIgnoredStale, 1U);
}

TEST_F(UVScriptRpcBindingsTest, Host_CancelsPendingCallsOnDestruction) {
    ScriptRpcPair pair = MakeScriptRpc();
    ASSERT_TRUE(pair.server.RegisterMethod("add", AddHandler()));
    ASSERT_TRUE(pair.Establish(1000U));

    std::vector<std::pair<std::uint32_t, bool>> responses;
    std::int64_t call = 0;
    {
        Core::BlueprintObjectHost host{entityManager,
                                         nullptr,
                                         entityManager.CreateEntity(),
                                         nullptr,
                                         nullptr,
                                         Core::BlueprintObjectHost::InstanceResolver{},
                                         &pair.client,
                                         [&](const std::uint32_t id, const bool ok) {
                                             responses.emplace_back(id, ok);
                                         }};
        const std::array<Blueprint::Value, 1> begin{Blueprint::Value{std::string{"add"}}};
        const std::int64_t staged =
            std::get<std::int64_t>(host.CallFunction("rpc.begin", begin));
        ASSERT_NE(staged, 0);
        const std::array<Blueprint::Value, 2> push1{Blueprint::Value{staged},
                                                     Blueprint::Value{std::int64_t{1}}};
        ASSERT_TRUE(std::get<bool>(host.CallFunction("rpc.push_int", push1)));
        const std::array<Blueprint::Value, 2> push2{Blueprint::Value{staged},
                                                     Blueprint::Value{std::int64_t{2}}};
        ASSERT_TRUE(std::get<bool>(host.CallFunction("rpc.push_int", push2)));
        const std::array<Blueprint::Value, 1> send{Blueprint::Value{staged}};
        call = std::get<std::int64_t>(host.CallFunction("rpc.send", send));
        ASSERT_NE(call, 0);
        const std::array<Blueprint::Value, 1> kind{Blueprint::Value{call}};
        EXPECT_EQ(std::get<std::string>(host.CallFunction("rpc.result_kind", kind)), "pending");
    }

    // The host is gone but the peer lives on: the exchange still runs, the completion has
    // nowhere to go, and the late response reads as stale.
    pair.UpdateBoth(1000U);
    pair.UpdateBoth(1000U);
    EXPECT_TRUE(responses.empty());
    EXPECT_FALSE(pair.client.Result(static_cast<Network::RpcCallId>(call)).has_value());
    EXPECT_EQ(pair.client.Stats().responsesMatched, 0U);
    EXPECT_EQ(pair.client.Stats().responsesIgnoredStale, 1U);
    EXPECT_EQ(pair.server.Stats().responsesSent, 1U);
}

} // namespace
} // namespace UVE::Scene::Tests
