// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include <array>
#include <limits>
#include <string>

#include <gtest/gtest.h>

#include "entity.h"
#include "blueprint_object_host.h"
#include "entity_manager.h"
#include "event_system.h"
#include "gameplay_attributes.h"
#include "gameplay_tags.h"
#include "memory_manager.h"
#include "blueprint_compiler.h"
#include "blueprint_instance.h"
#include "blueprint_value.h"

namespace UVE::Scene::Tests {
namespace {

class UVScriptGameplayBindingsTest : public ::testing::Test {
protected:
    Memory::MemoryManager memoryManager;
    Events::EventSystem eventSystem;
    EntityManager entityManager{memoryManager.GetDefaultAllocator(), eventSystem};

    Entity MakePooled(const float current, const float maximum) {
        const Entity entity = entityManager.CreateEntity();
        GameplayAttributesComponent pools;
        EXPECT_TRUE(AddGameplayAttribute(pools, "stamina", maximum, current));
        entityManager.AddComponent<GameplayAttributesComponent>(entity, pools);
        return entity;
    }

    Entity MakeTagged(const std::vector<std::string>& tags) {
        const Entity entity = entityManager.CreateEntity();
        GameplayTagComponent component;
        for (const std::string& tag : tags) {
            EXPECT_TRUE(AddGameplayTag(component, std::string{tag}));
        }
        entityManager.AddComponent<GameplayTagComponent>(entity, component);
        return entity;
    }

    [[nodiscard]] std::shared_ptr<const Blueprint::Program> CompileOrFail(
        const std::string& source, Core::BlueprintObjectHost& host) {
        const Blueprint::CompileResult result = Blueprint::CompileUVScriptSource(source, host);
        for (const Blueprint::Diagnostic& d : result.diagnostics) {
            ADD_FAILURE() << d.at.line << ":" << d.at.column << " " << d.message;
        }
        return result.program;
    }
};

TEST_F(UVScriptGameplayBindingsTest, Host_DescribesAttributesSurfaceOnlyWithComponent) {
    const Entity pooled = MakePooled(10.0F, 10.0F);
    Core::BlueprintObjectHost host{entityManager, nullptr, pooled};
    for (const char* name : {"attributes.has", "attributes.get", "attributes.max"}) {
        const std::optional<Blueprint::HostFunction> described = host.DescribeFunction(name);
        ASSERT_TRUE(described.has_value()) << name;
        ASSERT_EQ(described->params.size(), 1U) << name;
        EXPECT_EQ(described->params[0], Blueprint::Type::Str()) << name;
        const bool boolean = std::string{name} == "attributes.has";
        EXPECT_EQ(described->result, boolean ? Blueprint::Type::Bool() : Blueprint::Type::Float()) << name;
    }
    for (const char* name : {"attributes.damage", "attributes.heal"}) {
        const std::optional<Blueprint::HostFunction> described = host.DescribeFunction(name);
        ASSERT_TRUE(described.has_value()) << name;
        ASSERT_EQ(described->params.size(), 2U) << name;
        EXPECT_EQ(described->params[0], Blueprint::Type::Str()) << name;
        EXPECT_EQ(described->params[1], Blueprint::Type::Float()) << name;
        EXPECT_EQ(described->result, Blueprint::Type::Bool()) << name;
    }

    Core::BlueprintObjectHost bare{entityManager, nullptr, entityManager.CreateEntity()};
    for (const char* name :
         {"attributes.has", "attributes.get", "attributes.max", "attributes.damage", "attributes.heal"}) {
        EXPECT_FALSE(bare.DescribeFunction(name).has_value()) << name;
    }
    const Blueprint::CompileResult compiled =
        Blueprint::CompileUVScriptSource("on ready:\n    print(attributes.get(\"stamina\"))\n", bare);
    ASSERT_EQ(compiled.diagnostics.size(), 1U);
    EXPECT_EQ(compiled.diagnostics[0].message, "there is no function 'attributes.get'");
}

TEST_F(UVScriptGameplayBindingsTest, Host_DescribesTagsSurfaceOnlyWithComponent) {
    const Entity tagged = MakeTagged({"undead"});
    Core::BlueprintObjectHost host{entityManager, nullptr, tagged};
    for (const char* name : {"tags.has", "tags.add", "tags.remove"}) {
        const std::optional<Blueprint::HostFunction> described = host.DescribeFunction(name);
        ASSERT_TRUE(described.has_value()) << name;
        ASSERT_EQ(described->params.size(), 1U) << name;
        EXPECT_EQ(described->params[0], Blueprint::Type::Str()) << name;
        EXPECT_EQ(described->result, Blueprint::Type::Bool()) << name;
    }

    Core::BlueprintObjectHost bare{entityManager, nullptr, entityManager.CreateEntity()};
    for (const char* name : {"tags.has", "tags.add", "tags.remove"}) {
        EXPECT_FALSE(bare.DescribeFunction(name).has_value()) << name;
    }
}

TEST_F(UVScriptGameplayBindingsTest, Script_DamagesHealsAndReadsPools) {
    const Entity pooled = MakePooled(10.0F, 10.0F);
    Core::BlueprintObjectHost host{entityManager, nullptr, pooled};
    const auto program = CompileOrFail(R"(on ready:
    if attributes.has("stamina"):
        attributes.damage("stamina", 3)
    attributes.heal("stamina", 1)
    attributes.damage("stamina", 100)
    attributes.heal("stamina", 50)

fn stamina() -> float:
    return attributes.get("stamina")

fn stamina_max() -> float:
    return attributes.max("stamina")

fn has_mana() -> bool:
    return attributes.has("mana")
)",
                                          host);
    ASSERT_NE(program, nullptr);
    Blueprint::ScriptInstance instance(program, host);
    ASSERT_TRUE(instance.RaiseEvent("ready")) << instance.GetLastError();
    // 10 - 3 + 1, pinned at 0 by the overkill, then pinned at the ceiling by the overheal.
    EXPECT_FLOAT_EQ(entityManager.GetComponent<GameplayAttributesComponent>(pooled).attributes[0].current, 10.0F);
    EXPECT_EQ(std::get<double>(*instance.Call("stamina")), 10.0);
    EXPECT_EQ(std::get<double>(*instance.Call("stamina_max")), 10.0);
    EXPECT_FALSE(std::get<bool>(*instance.Call("has_mana")));
}

TEST_F(UVScriptGameplayBindingsTest, Script_ManagesTags) {
    const Entity tagged = MakeTagged({"undead"});
    Core::BlueprintObjectHost host{entityManager, nullptr, tagged};
    const auto program = CompileOrFail(R"(on ready:
    tags.add("boss")
    tags.add("flammable")
    tags.remove("undead")

fn is_boss() -> bool:
    return tags.has("boss")

fn is_undead() -> bool:
    return tags.has("undead")
)",
                                          host);
    ASSERT_NE(program, nullptr);
    Blueprint::ScriptInstance instance(program, host);
    ASSERT_TRUE(instance.RaiseEvent("ready")) << instance.GetLastError();
    EXPECT_EQ(entityManager.GetComponent<GameplayTagComponent>(tagged).tags,
              (std::vector<std::string>{"boss", "flammable"}));
    EXPECT_TRUE(std::get<bool>(*instance.Call("is_boss")));
    EXPECT_FALSE(std::get<bool>(*instance.Call("is_undead")));
}

TEST_F(UVScriptGameplayBindingsTest, Host_FailsClosedOnMissingIdsAndBadAmounts) {
    const Entity pooled = MakePooled(5.0F, 10.0F);
    Core::BlueprintObjectHost host{entityManager, nullptr, pooled};
    const std::array<Blueprint::Value, 1> missing{Blueprint::Value{std::string{"mana"}}};
    EXPECT_FALSE(std::get<bool>(host.CallFunction("attributes.has", missing)));
    EXPECT_EQ(std::get<double>(host.CallFunction("attributes.get", missing)), 0.0);
    EXPECT_EQ(std::get<double>(host.CallFunction("attributes.max", missing)), 0.0);
    const std::array<Blueprint::Value, 2> missingDamage{Blueprint::Value{std::string{"mana"}},
                                                          Blueprint::Value{1.0}};
    EXPECT_FALSE(std::get<bool>(host.CallFunction("attributes.damage", missingDamage)));

    // Health lives in the strike pipeline, never in these pools.
    const std::array<Blueprint::Value, 1> health{Blueprint::Value{std::string{"health"}}};
    EXPECT_FALSE(std::get<bool>(host.CallFunction("attributes.has", health)));

    const double nan = std::numeric_limits<double>::quiet_NaN();
    for (const double bad : {nan, -2.0, 0.0}) {
        const std::array<Blueprint::Value, 2> args{Blueprint::Value{std::string{"stamina"}},
                                                     Blueprint::Value{bad}};
        EXPECT_FALSE(std::get<bool>(host.CallFunction("attributes.damage", args))) << bad;
        EXPECT_FALSE(std::get<bool>(host.CallFunction("attributes.heal", args))) << bad;
    }
    EXPECT_FLOAT_EQ(entityManager.GetComponent<GameplayAttributesComponent>(pooled).attributes[0].current, 5.0F);

    Core::BlueprintObjectHost bare{entityManager, nullptr, entityManager.CreateEntity()};
    const std::array<Blueprint::Value, 1> id{Blueprint::Value{std::string{"stamina"}}};
    EXPECT_FALSE(std::get<bool>(bare.CallFunction("attributes.has", id)));
    EXPECT_EQ(std::get<double>(bare.CallFunction("attributes.get", id)), 0.0);
    const std::array<Blueprint::Value, 2> damage{Blueprint::Value{std::string{"stamina"}},
                                                   Blueprint::Value{1.0}};
    EXPECT_FALSE(std::get<bool>(bare.CallFunction("attributes.damage", damage)));
    const std::array<Blueprint::Value, 1> tag{Blueprint::Value{std::string{"boss"}}};
    EXPECT_FALSE(std::get<bool>(bare.CallFunction("tags.has", tag)));
    EXPECT_FALSE(std::get<bool>(bare.CallFunction("tags.add", tag)));
    EXPECT_FALSE(std::get<bool>(bare.CallFunction("tags.remove", tag)));

    // Bad tags fail closed too, on an entity that has the component.
    Core::BlueprintObjectHost tagged{entityManager, nullptr, MakeTagged({"undead"})};
    const std::array<Blueprint::Value, 1> empty{Blueprint::Value{std::string{}}};
    EXPECT_FALSE(std::get<bool>(tagged.CallFunction("tags.add", empty)));
    const std::array<Blueprint::Value, 1> dupe{Blueprint::Value{std::string{"undead"}}};
    EXPECT_FALSE(std::get<bool>(tagged.CallFunction("tags.add", dupe)));
    EXPECT_FALSE(std::get<bool>(tagged.CallFunction("tags.remove", empty)));
}

} // namespace
} // namespace UVE::Scene::Tests
