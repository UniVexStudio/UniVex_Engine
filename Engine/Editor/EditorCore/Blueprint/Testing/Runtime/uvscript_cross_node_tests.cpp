// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include <gtest/gtest.h>

#include "entity.h"
#include "name_component.h"
#include "visibility_component.h"
#include "blueprint_object_host.h"
#include "entity_manager.h"
#include "event_system.h"
#include "memory_manager.h"
#include "blueprint_compiler.h"
#include "blueprint_instance.h"
#include "blueprint_value.h"

namespace UVE::Scene::Tests {
namespace {

struct ScriptSlot final {
    std::unique_ptr<Core::BlueprintObjectHost> host;
    std::unique_ptr<Blueprint::ScriptInstance> instance;
};

class UVScriptCrossNodeTest : public ::testing::Test {
protected:
    Memory::MemoryManager memoryManager;
    Events::EventSystem eventSystem;
    EntityManager entityManager{memoryManager.GetDefaultAllocator(), eventSystem};
    std::unordered_map<Entity, ScriptSlot> scripts;
    std::vector<std::string> lastDiagnostics;

    Entity AddScripted(const std::string& name, const std::string& source) {
        const Entity entity = entityManager.CreateEntity();
        NameComponent named;
        named.name = name;
        entityManager.AddComponent<NameComponent>(entity, named);
        auto host = std::make_unique<Core::BlueprintObjectHost>(
            entityManager, nullptr, entity, nullptr, nullptr,
            [this](const Entity other) -> Blueprint::ScriptInstance* {
                const auto it = scripts.find(other);
                return it == scripts.end() ? nullptr : it->second.instance.get();
            });
        const Blueprint::CompileResult compiled = Blueprint::CompileUVScriptSource(source, *host);
        lastDiagnostics.clear();
        for (const Blueprint::Diagnostic& diagnostic : compiled.diagnostics) {
            lastDiagnostics.push_back(std::to_string(diagnostic.at.line) + ": " + diagnostic.message);
        }
        if (!compiled.IsSuccess()) {
            return kInvalidEntity;
        }
        auto instance = std::make_unique<Blueprint::ScriptInstance>(compiled.program, *host);
        scripts.emplace(entity, ScriptSlot{std::move(host), std::move(instance)});
        return entity;
    }

    Blueprint::ScriptInstance& Instance(const Entity entity) {
        return *scripts.find(entity)->second.instance;
    }

    Core::BlueprintObjectHost& Host(const Entity entity) {
        return *scripts.find(entity)->second.host;
    }

    std::int64_t Field(const Entity entity, const std::string& field) {
        return std::get<std::int64_t>(*Instance(entity).GetField(field));
    }

    void RaiseTick(const Entity entity) {
        const Blueprint::Value args[] = {0.016};
        ASSERT_TRUE(Instance(entity).RaiseEvent("tick", args))
            << Instance(entity).GetLastError();
    }
};

TEST_F(UVScriptCrossNodeTest, CallsAnotherNodesScriptFunction) {
    const Entity target = AddScripted("Target", "var hp = 100\n\nfn take_damage(amount: int):\n    hp -= amount\n");
    ASSERT_NE(target, kInvalidEntity) << ::testing::PrintToString(lastDiagnostics);
    const Entity caller =
        AddScripted("Caller", "on tick(dt):\n    node(\"Target\").take_damage(30)\n");
    ASSERT_NE(caller, kInvalidEntity) << ::testing::PrintToString(lastDiagnostics);

    EXPECT_TRUE(Instance(target).HasFunction("take_damage", 1U));
    EXPECT_FALSE(Instance(target).HasFunction("take_damage", 2U));
    EXPECT_FALSE(Instance(target).HasFunction("nope", 0U));

    RaiseTick(caller);
    EXPECT_EQ(Field(target, "hp"), 70);
}

TEST_F(UVScriptCrossNodeTest, HideAndShowReachObjectsWithoutScripts) {
    const Entity prop = entityManager.CreateEntity();
    NameComponent named;
    named.name = "Prop";
    entityManager.AddComponent<NameComponent>(prop, named);
    const Entity caller = AddScripted("Caller", "on tick(dt):\n    node(\"Prop\").hide()\n");
    ASSERT_NE(caller, kInvalidEntity) << ::testing::PrintToString(lastDiagnostics);

    RaiseTick(caller);
    ASSERT_TRUE(entityManager.HasComponent<VisibilityComponent>(prop));
    EXPECT_FALSE(entityManager.GetComponent<VisibilityComponent>(prop).visible);

    const Blueprint::Value name[] = {Blueprint::Value{std::string{"Prop"}}};
    const Blueprint::ObjectRef ref =
        std::get<Blueprint::ObjectRef>(Host(caller).CallFunction("node", name));
    EXPECT_NE(ref.id, 0U);
    Host(caller).CallMethod(ref, "show", {});
    EXPECT_TRUE(entityManager.GetComponent<VisibilityComponent>(prop).visible);

    // Showing an object that was never hidden adds no component: absent already shows.
    const Entity plain = entityManager.CreateEntity();
    NameComponent plainly;
    plainly.name = "Plain";
    entityManager.AddComponent<NameComponent>(plain, plainly);
    const Blueprint::Value plainName[] = {Blueprint::Value{std::string{"Plain"}}};
    const Blueprint::ObjectRef plainRef =
        std::get<Blueprint::ObjectRef>(Host(caller).CallFunction("node", plainName));
    Host(caller).CallMethod(plainRef, "show", {});
    EXPECT_FALSE(entityManager.HasComponent<VisibilityComponent>(plain));
}

TEST_F(UVScriptCrossNodeTest, MissingTargetsAndMethodsFailClosed) {
    const Entity target = AddScripted("Target", "var hp = 100\n\nfn take_damage(amount: int):\n    hp -= amount\n");
    ASSERT_NE(target, kInvalidEntity) << ::testing::PrintToString(lastDiagnostics);
    const Entity caller = AddScripted("Caller",
                                            "on tick(dt):\n"
                                            "    node(\"Ghost\").haunt()\n"
                                            "    node(\"Target\").nope()\n"
                                            "    node(\"Target\").take_damage(\"lots\")\n");
    ASSERT_NE(caller, kInvalidEntity) << ::testing::PrintToString(lastDiagnostics);

    RaiseTick(caller);
    EXPECT_EQ(Field(target, "hp"), 100);

    // No object, and a handle whose entity is gone, are equally silent.
    Host(caller).CallMethod(Blueprint::ObjectRef{}, "hide", {});
    const Entity doomed = entityManager.CreateEntity();
    NameComponent doomedName;
    doomedName.name = "Doomed";
    entityManager.AddComponent<NameComponent>(doomed, doomedName);
    const Blueprint::Value doomedArg[] = {Blueprint::Value{std::string{"Doomed"}}};
    const Blueprint::ObjectRef stale =
        std::get<Blueprint::ObjectRef>(Host(caller).CallFunction("node", doomedArg));
    entityManager.DestroyEntity(doomed);
    const Entity recycled = entityManager.CreateEntity();
    Host(caller).CallMethod(stale, "hide", {});
    EXPECT_FALSE(entityManager.IsAlive(doomed));
    EXPECT_FALSE(entityManager.HasComponent<VisibilityComponent>(recycled));
}

TEST_F(UVScriptCrossNodeTest, ScriptFunctionOverridesBuiltinHide) {
    const Entity target =
        AddScripted("Target", "var hidden_calls = 0\n\nfn hide():\n    hidden_calls += 1\n");
    ASSERT_NE(target, kInvalidEntity) << ::testing::PrintToString(lastDiagnostics);
    const Entity caller = AddScripted("Caller", "on tick(dt):\n    node(\"Target\").hide()\n");
    ASSERT_NE(caller, kInvalidEntity) << ::testing::PrintToString(lastDiagnostics);

    RaiseTick(caller);
    EXPECT_EQ(Field(target, "hidden_calls"), 1);
    EXPECT_FALSE(entityManager.HasComponent<VisibilityComponent>(target));
}

TEST_F(UVScriptCrossNodeTest, PingPongCallsTerminateAtTheDepthCap) {
    const Entity a = AddScripted("A",
                                       "var calls = 0\n\nfn ping():\n    calls += 1\n    node(\"B\").pong()\n");
    ASSERT_NE(a, kInvalidEntity) << ::testing::PrintToString(lastDiagnostics);
    const Entity b = AddScripted("B",
                                       "var calls = 0\n\nfn pong():\n    calls += 1\n    node(\"A\").ping()\n");
    ASSERT_NE(b, kInvalidEntity) << ::testing::PrintToString(lastDiagnostics);

    EXPECT_TRUE(Instance(a).Call("ping").has_value());
    EXPECT_EQ(Field(a, "calls"), 128);
    EXPECT_EQ(Field(b, "calls"), 128);
}

} // namespace
} // namespace UVE::Scene::Tests
