// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include <gtest/gtest.h>

#include "uve/component/entity_uve.h"
#include "uve/component/name_component_uve.h"
#include "uve/component/visibility_component_uve.h"
#include "uve/core/uvscript_object_host_uve.h"
#include "uve/entity/entity_manager_uve.h"
#include "uve/events/event_system_uve.h"
#include "uve/memory/memory_manager_uve.h"
#include "uve/uvscript/uvscript_compiler_uve.h"
#include "uve/uvscript/uvscript_instance_uve.h"
#include "uve/uvscript/uvscript_value_uve.h"

namespace UVE::Scene::Tests {
namespace {

struct ScriptSlotUVE final {
    std::unique_ptr<Core::UVScriptObjectHostUVE> host;
    std::unique_ptr<UVScript::ScriptInstanceUVE> instance;
};

class UVScriptCrossNodeUVETest : public ::testing::Test {
protected:
    Memory::MemoryManagerUVE memoryManager;
    Events::EventSystemUVE eventSystem;
    EntityManagerUVE entityManager{memoryManager.GetDefaultAllocatorUVE(), eventSystem};
    std::unordered_map<EntityUVE, ScriptSlotUVE> scripts;
    std::vector<std::string> lastDiagnostics;

    EntityUVE AddScriptedUVE(const std::string& name, const std::string& source) {
        const EntityUVE entity = entityManager.CreateEntityUVE();
        NameComponentUVE named;
        named.name = name;
        entityManager.AddComponentUVE<NameComponentUVE>(entity, named);
        auto host = std::make_unique<Core::UVScriptObjectHostUVE>(
            entityManager, nullptr, entity, nullptr, nullptr,
            [this](const EntityUVE other) -> UVScript::ScriptInstanceUVE* {
                const auto it = scripts.find(other);
                return it == scripts.end() ? nullptr : it->second.instance.get();
            });
        const UVScript::CompileResultUVE compiled = UVScript::CompileUVScriptSourceUVE(source, *host);
        lastDiagnostics.clear();
        for (const UVScript::DiagnosticUVE& diagnostic : compiled.diagnostics) {
            lastDiagnostics.push_back(std::to_string(diagnostic.at.line) + ": " + diagnostic.message);
        }
        if (!compiled.IsSuccessUVE()) {
            return kInvalidEntityUVE;
        }
        auto instance = std::make_unique<UVScript::ScriptInstanceUVE>(compiled.program, *host);
        scripts.emplace(entity, ScriptSlotUVE{std::move(host), std::move(instance)});
        return entity;
    }

    UVScript::ScriptInstanceUVE& InstanceUVE(const EntityUVE entity) {
        return *scripts.find(entity)->second.instance;
    }

    Core::UVScriptObjectHostUVE& HostUVE(const EntityUVE entity) {
        return *scripts.find(entity)->second.host;
    }

    std::int64_t FieldUVE(const EntityUVE entity, const std::string& field) {
        return std::get<std::int64_t>(*InstanceUVE(entity).GetFieldUVE(field));
    }

    void RaiseTickUVE(const EntityUVE entity) {
        const UVScript::ValueUVE args[] = {0.016};
        ASSERT_TRUE(InstanceUVE(entity).RaiseEventUVE("tick", args))
            << InstanceUVE(entity).GetLastErrorUVE();
    }
};

TEST_F(UVScriptCrossNodeUVETest, CallsAnotherNodesScriptFunction) {
    const EntityUVE target = AddScriptedUVE("Target", "var hp = 100\n\nfn take_damage(amount: int):\n    hp -= amount\n");
    ASSERT_NE(target, kInvalidEntityUVE) << ::testing::PrintToString(lastDiagnostics);
    const EntityUVE caller =
        AddScriptedUVE("Caller", "on tick(dt):\n    node(\"Target\").take_damage(30)\n");
    ASSERT_NE(caller, kInvalidEntityUVE) << ::testing::PrintToString(lastDiagnostics);

    EXPECT_TRUE(InstanceUVE(target).HasFunctionUVE("take_damage", 1U));
    EXPECT_FALSE(InstanceUVE(target).HasFunctionUVE("take_damage", 2U));
    EXPECT_FALSE(InstanceUVE(target).HasFunctionUVE("nope", 0U));

    RaiseTickUVE(caller);
    EXPECT_EQ(FieldUVE(target, "hp"), 70);
}

TEST_F(UVScriptCrossNodeUVETest, HideAndShowReachObjectsWithoutScripts) {
    const EntityUVE prop = entityManager.CreateEntityUVE();
    NameComponentUVE named;
    named.name = "Prop";
    entityManager.AddComponentUVE<NameComponentUVE>(prop, named);
    const EntityUVE caller = AddScriptedUVE("Caller", "on tick(dt):\n    node(\"Prop\").hide()\n");
    ASSERT_NE(caller, kInvalidEntityUVE) << ::testing::PrintToString(lastDiagnostics);

    RaiseTickUVE(caller);
    ASSERT_TRUE(entityManager.HasComponentUVE<VisibilityComponentUVE>(prop));
    EXPECT_FALSE(entityManager.GetComponentUVE<VisibilityComponentUVE>(prop).visible);

    const UVScript::ValueUVE name[] = {UVScript::ValueUVE{std::string{"Prop"}}};
    const UVScript::ObjectRefUVE ref =
        std::get<UVScript::ObjectRefUVE>(HostUVE(caller).CallFunctionUVE("node", name));
    EXPECT_NE(ref.id, 0U);
    HostUVE(caller).CallMethodUVE(ref, "show", {});
    EXPECT_TRUE(entityManager.GetComponentUVE<VisibilityComponentUVE>(prop).visible);

    // Showing an object that was never hidden adds no component: absent already shows.
    const EntityUVE plain = entityManager.CreateEntityUVE();
    NameComponentUVE plainly;
    plainly.name = "Plain";
    entityManager.AddComponentUVE<NameComponentUVE>(plain, plainly);
    const UVScript::ValueUVE plainName[] = {UVScript::ValueUVE{std::string{"Plain"}}};
    const UVScript::ObjectRefUVE plainRef =
        std::get<UVScript::ObjectRefUVE>(HostUVE(caller).CallFunctionUVE("node", plainName));
    HostUVE(caller).CallMethodUVE(plainRef, "show", {});
    EXPECT_FALSE(entityManager.HasComponentUVE<VisibilityComponentUVE>(plain));
}

TEST_F(UVScriptCrossNodeUVETest, MissingTargetsAndMethodsFailClosed) {
    const EntityUVE target = AddScriptedUVE("Target", "var hp = 100\n\nfn take_damage(amount: int):\n    hp -= amount\n");
    ASSERT_NE(target, kInvalidEntityUVE) << ::testing::PrintToString(lastDiagnostics);
    const EntityUVE caller = AddScriptedUVE("Caller",
                                            "on tick(dt):\n"
                                            "    node(\"Ghost\").haunt()\n"
                                            "    node(\"Target\").nope()\n"
                                            "    node(\"Target\").take_damage(\"lots\")\n");
    ASSERT_NE(caller, kInvalidEntityUVE) << ::testing::PrintToString(lastDiagnostics);

    RaiseTickUVE(caller);
    EXPECT_EQ(FieldUVE(target, "hp"), 100);

    // No object, and a handle whose entity is gone, are equally silent.
    HostUVE(caller).CallMethodUVE(UVScript::ObjectRefUVE{}, "hide", {});
    const EntityUVE doomed = entityManager.CreateEntityUVE();
    NameComponentUVE doomedName;
    doomedName.name = "Doomed";
    entityManager.AddComponentUVE<NameComponentUVE>(doomed, doomedName);
    const UVScript::ValueUVE doomedArg[] = {UVScript::ValueUVE{std::string{"Doomed"}}};
    const UVScript::ObjectRefUVE stale =
        std::get<UVScript::ObjectRefUVE>(HostUVE(caller).CallFunctionUVE("node", doomedArg));
    entityManager.DestroyEntityUVE(doomed);
    const EntityUVE recycled = entityManager.CreateEntityUVE();
    HostUVE(caller).CallMethodUVE(stale, "hide", {});
    EXPECT_FALSE(entityManager.IsAliveUVE(doomed));
    EXPECT_FALSE(entityManager.HasComponentUVE<VisibilityComponentUVE>(recycled));
}

TEST_F(UVScriptCrossNodeUVETest, ScriptFunctionOverridesBuiltinHide) {
    const EntityUVE target =
        AddScriptedUVE("Target", "var hidden_calls = 0\n\nfn hide():\n    hidden_calls += 1\n");
    ASSERT_NE(target, kInvalidEntityUVE) << ::testing::PrintToString(lastDiagnostics);
    const EntityUVE caller = AddScriptedUVE("Caller", "on tick(dt):\n    node(\"Target\").hide()\n");
    ASSERT_NE(caller, kInvalidEntityUVE) << ::testing::PrintToString(lastDiagnostics);

    RaiseTickUVE(caller);
    EXPECT_EQ(FieldUVE(target, "hidden_calls"), 1);
    EXPECT_FALSE(entityManager.HasComponentUVE<VisibilityComponentUVE>(target));
}

TEST_F(UVScriptCrossNodeUVETest, PingPongCallsTerminateAtTheDepthCap) {
    const EntityUVE a = AddScriptedUVE("A",
                                       "var calls = 0\n\nfn ping():\n    calls += 1\n    node(\"B\").pong()\n");
    ASSERT_NE(a, kInvalidEntityUVE) << ::testing::PrintToString(lastDiagnostics);
    const EntityUVE b = AddScriptedUVE("B",
                                       "var calls = 0\n\nfn pong():\n    calls += 1\n    node(\"A\").ping()\n");
    ASSERT_NE(b, kInvalidEntityUVE) << ::testing::PrintToString(lastDiagnostics);

    EXPECT_TRUE(InstanceUVE(a).CallUVE("ping").has_value());
    EXPECT_EQ(FieldUVE(a, "calls"), 128);
    EXPECT_EQ(FieldUVE(b, "calls"), 128);
}

} // namespace
} // namespace UVE::Scene::Tests
