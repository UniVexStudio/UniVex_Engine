// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include <array>
#include <limits>
#include <string>

#include <gtest/gtest.h>

#include "uve/component/entity_uve.h"
#include "uve/core/uvscript_object_host_uve.h"
#include "uve/entity/entity_manager_uve.h"
#include "uve/events/event_system_uve.h"
#include "uve/gameplay/gameplay_attributes_uve.h"
#include "uve/gameplay/gameplay_tags_uve.h"
#include "uve/memory/memory_manager_uve.h"
#include "uve/uvscript/uvscript_compiler_uve.h"
#include "uve/uvscript/uvscript_instance_uve.h"
#include "uve/uvscript/uvscript_value_uve.h"

namespace UVE::Scene::Tests {
namespace {

class UVScriptGameplayBindingsUVETest : public ::testing::Test {
protected:
    Memory::MemoryManagerUVE memoryManager;
    Events::EventSystemUVE eventSystem;
    EntityManagerUVE entityManager{memoryManager.GetDefaultAllocatorUVE(), eventSystem};

    EntityUVE MakePooledUVE(const float current, const float maximum) {
        const EntityUVE entity = entityManager.CreateEntityUVE();
        GameplayAttributesComponentUVE pools;
        EXPECT_TRUE(AddGameplayAttributeUVE(pools, "stamina", maximum, current));
        entityManager.AddComponentUVE<GameplayAttributesComponentUVE>(entity, pools);
        return entity;
    }

    EntityUVE MakeTaggedUVE(const std::vector<std::string>& tags) {
        const EntityUVE entity = entityManager.CreateEntityUVE();
        GameplayTagComponentUVE component;
        for (const std::string& tag : tags) {
            EXPECT_TRUE(AddGameplayTagUVE(component, std::string{tag}));
        }
        entityManager.AddComponentUVE<GameplayTagComponentUVE>(entity, component);
        return entity;
    }

    [[nodiscard]] std::shared_ptr<const UVScript::ProgramUVE> CompileOrFailUVE(
        const std::string& source, Core::UVScriptObjectHostUVE& host) {
        const UVScript::CompileResultUVE result = UVScript::CompileUVScriptSourceUVE(source, host);
        for (const UVScript::DiagnosticUVE& d : result.diagnostics) {
            ADD_FAILURE() << d.at.line << ":" << d.at.column << " " << d.message;
        }
        return result.program;
    }
};

TEST_F(UVScriptGameplayBindingsUVETest, Host_DescribesAttributesSurfaceOnlyWithComponent) {
    const EntityUVE pooled = MakePooledUVE(10.0F, 10.0F);
    Core::UVScriptObjectHostUVE host{entityManager, nullptr, pooled};
    for (const char* name : {"attributes.has", "attributes.get", "attributes.max"}) {
        const std::optional<UVScript::HostFunctionUVE> described = host.DescribeFunctionUVE(name);
        ASSERT_TRUE(described.has_value()) << name;
        ASSERT_EQ(described->params.size(), 1U) << name;
        EXPECT_EQ(described->params[0], UVScript::TypeUVE::StrUVE()) << name;
        const bool boolean = std::string{name} == "attributes.has";
        EXPECT_EQ(described->result, boolean ? UVScript::TypeUVE::BoolUVE() : UVScript::TypeUVE::FloatUVE()) << name;
    }
    for (const char* name : {"attributes.damage", "attributes.heal"}) {
        const std::optional<UVScript::HostFunctionUVE> described = host.DescribeFunctionUVE(name);
        ASSERT_TRUE(described.has_value()) << name;
        ASSERT_EQ(described->params.size(), 2U) << name;
        EXPECT_EQ(described->params[0], UVScript::TypeUVE::StrUVE()) << name;
        EXPECT_EQ(described->params[1], UVScript::TypeUVE::FloatUVE()) << name;
        EXPECT_EQ(described->result, UVScript::TypeUVE::BoolUVE()) << name;
    }

    Core::UVScriptObjectHostUVE bare{entityManager, nullptr, entityManager.CreateEntityUVE()};
    for (const char* name :
         {"attributes.has", "attributes.get", "attributes.max", "attributes.damage", "attributes.heal"}) {
        EXPECT_FALSE(bare.DescribeFunctionUVE(name).has_value()) << name;
    }
    const UVScript::CompileResultUVE compiled =
        UVScript::CompileUVScriptSourceUVE("on ready:\n    print(attributes.get(\"stamina\"))\n", bare);
    ASSERT_EQ(compiled.diagnostics.size(), 1U);
    EXPECT_EQ(compiled.diagnostics[0].message, "there is no function 'attributes.get'");
}

TEST_F(UVScriptGameplayBindingsUVETest, Host_DescribesTagsSurfaceOnlyWithComponent) {
    const EntityUVE tagged = MakeTaggedUVE({"undead"});
    Core::UVScriptObjectHostUVE host{entityManager, nullptr, tagged};
    for (const char* name : {"tags.has", "tags.add", "tags.remove"}) {
        const std::optional<UVScript::HostFunctionUVE> described = host.DescribeFunctionUVE(name);
        ASSERT_TRUE(described.has_value()) << name;
        ASSERT_EQ(described->params.size(), 1U) << name;
        EXPECT_EQ(described->params[0], UVScript::TypeUVE::StrUVE()) << name;
        EXPECT_EQ(described->result, UVScript::TypeUVE::BoolUVE()) << name;
    }

    Core::UVScriptObjectHostUVE bare{entityManager, nullptr, entityManager.CreateEntityUVE()};
    for (const char* name : {"tags.has", "tags.add", "tags.remove"}) {
        EXPECT_FALSE(bare.DescribeFunctionUVE(name).has_value()) << name;
    }
}

TEST_F(UVScriptGameplayBindingsUVETest, Script_DamagesHealsAndReadsPools) {
    const EntityUVE pooled = MakePooledUVE(10.0F, 10.0F);
    Core::UVScriptObjectHostUVE host{entityManager, nullptr, pooled};
    const auto program = CompileOrFailUVE(R"(on ready:
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
    UVScript::ScriptInstanceUVE instance(program, host);
    ASSERT_TRUE(instance.RaiseEventUVE("ready")) << instance.GetLastErrorUVE();
    // 10 - 3 + 1, pinned at 0 by the overkill, then pinned at the ceiling by the overheal.
    EXPECT_FLOAT_EQ(entityManager.GetComponentUVE<GameplayAttributesComponentUVE>(pooled).attributes[0].current, 10.0F);
    EXPECT_EQ(std::get<double>(*instance.CallUVE("stamina")), 10.0);
    EXPECT_EQ(std::get<double>(*instance.CallUVE("stamina_max")), 10.0);
    EXPECT_FALSE(std::get<bool>(*instance.CallUVE("has_mana")));
}

TEST_F(UVScriptGameplayBindingsUVETest, Script_ManagesTags) {
    const EntityUVE tagged = MakeTaggedUVE({"undead"});
    Core::UVScriptObjectHostUVE host{entityManager, nullptr, tagged};
    const auto program = CompileOrFailUVE(R"(on ready:
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
    UVScript::ScriptInstanceUVE instance(program, host);
    ASSERT_TRUE(instance.RaiseEventUVE("ready")) << instance.GetLastErrorUVE();
    EXPECT_EQ(entityManager.GetComponentUVE<GameplayTagComponentUVE>(tagged).tags,
              (std::vector<std::string>{"boss", "flammable"}));
    EXPECT_TRUE(std::get<bool>(*instance.CallUVE("is_boss")));
    EXPECT_FALSE(std::get<bool>(*instance.CallUVE("is_undead")));
}

TEST_F(UVScriptGameplayBindingsUVETest, Host_FailsClosedOnMissingIdsAndBadAmounts) {
    const EntityUVE pooled = MakePooledUVE(5.0F, 10.0F);
    Core::UVScriptObjectHostUVE host{entityManager, nullptr, pooled};
    const std::array<UVScript::ValueUVE, 1> missing{UVScript::ValueUVE{std::string{"mana"}}};
    EXPECT_FALSE(std::get<bool>(host.CallFunctionUVE("attributes.has", missing)));
    EXPECT_EQ(std::get<double>(host.CallFunctionUVE("attributes.get", missing)), 0.0);
    EXPECT_EQ(std::get<double>(host.CallFunctionUVE("attributes.max", missing)), 0.0);
    const std::array<UVScript::ValueUVE, 2> missingDamage{UVScript::ValueUVE{std::string{"mana"}},
                                                          UVScript::ValueUVE{1.0}};
    EXPECT_FALSE(std::get<bool>(host.CallFunctionUVE("attributes.damage", missingDamage)));

    // Health lives in the strike pipeline, never in these pools.
    const std::array<UVScript::ValueUVE, 1> health{UVScript::ValueUVE{std::string{"health"}}};
    EXPECT_FALSE(std::get<bool>(host.CallFunctionUVE("attributes.has", health)));

    const double nan = std::numeric_limits<double>::quiet_NaN();
    for (const double bad : {nan, -2.0, 0.0}) {
        const std::array<UVScript::ValueUVE, 2> args{UVScript::ValueUVE{std::string{"stamina"}},
                                                     UVScript::ValueUVE{bad}};
        EXPECT_FALSE(std::get<bool>(host.CallFunctionUVE("attributes.damage", args))) << bad;
        EXPECT_FALSE(std::get<bool>(host.CallFunctionUVE("attributes.heal", args))) << bad;
    }
    EXPECT_FLOAT_EQ(entityManager.GetComponentUVE<GameplayAttributesComponentUVE>(pooled).attributes[0].current, 5.0F);

    Core::UVScriptObjectHostUVE bare{entityManager, nullptr, entityManager.CreateEntityUVE()};
    const std::array<UVScript::ValueUVE, 1> id{UVScript::ValueUVE{std::string{"stamina"}}};
    EXPECT_FALSE(std::get<bool>(bare.CallFunctionUVE("attributes.has", id)));
    EXPECT_EQ(std::get<double>(bare.CallFunctionUVE("attributes.get", id)), 0.0);
    const std::array<UVScript::ValueUVE, 2> damage{UVScript::ValueUVE{std::string{"stamina"}},
                                                   UVScript::ValueUVE{1.0}};
    EXPECT_FALSE(std::get<bool>(bare.CallFunctionUVE("attributes.damage", damage)));
    const std::array<UVScript::ValueUVE, 1> tag{UVScript::ValueUVE{std::string{"boss"}}};
    EXPECT_FALSE(std::get<bool>(bare.CallFunctionUVE("tags.has", tag)));
    EXPECT_FALSE(std::get<bool>(bare.CallFunctionUVE("tags.add", tag)));
    EXPECT_FALSE(std::get<bool>(bare.CallFunctionUVE("tags.remove", tag)));

    // Bad tags fail closed too, on an entity that has the component.
    Core::UVScriptObjectHostUVE tagged{entityManager, nullptr, MakeTaggedUVE({"undead"})};
    const std::array<UVScript::ValueUVE, 1> empty{UVScript::ValueUVE{std::string{}}};
    EXPECT_FALSE(std::get<bool>(tagged.CallFunctionUVE("tags.add", empty)));
    const std::array<UVScript::ValueUVE, 1> dupe{UVScript::ValueUVE{std::string{"undead"}}};
    EXPECT_FALSE(std::get<bool>(tagged.CallFunctionUVE("tags.add", dupe)));
    EXPECT_FALSE(std::get<bool>(tagged.CallFunctionUVE("tags.remove", empty)));
}

} // namespace
} // namespace UVE::Scene::Tests
