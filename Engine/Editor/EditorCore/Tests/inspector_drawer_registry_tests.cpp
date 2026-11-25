// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "inspector_drawer_registry.h"

namespace UVE::Editor::Tests {
namespace {

[[nodiscard]] InspectorDrawerEntry MakeAlwaysEligibleDrawer(const std::string id,
                                                                   std::vector<std::string>& invocationOrder) {
    return InspectorDrawerEntry{
        id,
        [](const Scene::Entity) { return true; },
        [&invocationOrder, id](const Scene::Entity) { invocationOrder.push_back(id); },
    };
}

TEST(InspectorDrawerRegistryTest, RegisterDrawer_RejectsIncompleteAndDuplicateEntriesWithoutMutation) {
    InspectorDrawerRegistry registry;
    std::vector<std::string> invocationOrder;

    EXPECT_FALSE(registry.RegisterDrawer(InspectorDrawerEntry{
        "", [](const Scene::Entity) { return true; }, [](const Scene::Entity) {},
    }));
    EXPECT_FALSE(registry.RegisterDrawer(InspectorDrawerEntry{
        "missing-predicate", {}, [](const Scene::Entity) {},
    }));
    EXPECT_FALSE(registry.RegisterDrawer(InspectorDrawerEntry{
        "missing-draw", [](const Scene::Entity) { return true; }, {},
    }));
    EXPECT_EQ(registry.GetDrawerCount(), 0U);
    EXPECT_FALSE(registry.HasDrawer("name"));

    ASSERT_TRUE(registry.RegisterDrawer(MakeAlwaysEligibleDrawer("name", invocationOrder)));
    EXPECT_TRUE(registry.HasDrawer("name"));
    EXPECT_EQ(registry.GetDrawerCount(), 1U);

    EXPECT_FALSE(registry.RegisterDrawer(MakeAlwaysEligibleDrawer("name", invocationOrder)));
    EXPECT_EQ(registry.GetDrawerCount(), 1U);
}

TEST(InspectorDrawerRegistryTest, DrawEligible_PreservesRegistrationOrderAndFiltersIneligibleDrawers) {
    InspectorDrawerRegistry registry;
    std::vector<std::string> invocationOrder;
    const Scene::Entity selected{42U, 7U};
    Scene::Entity predicateEntity{};
    Scene::Entity drawEntity{};

    ASSERT_TRUE(registry.RegisterDrawer(InspectorDrawerEntry{
        "first",
        [&predicateEntity](const Scene::Entity entity) {
            predicateEntity = entity;
            return true;
        },
        [&invocationOrder, &drawEntity](const Scene::Entity entity) {
            drawEntity = entity;
            invocationOrder.emplace_back("first");
        },
    }));
    ASSERT_TRUE(registry.RegisterDrawer(InspectorDrawerEntry{
        "filtered",
        [](const Scene::Entity) { return false; },
        [&invocationOrder](const Scene::Entity) { invocationOrder.emplace_back("filtered"); },
    }));
    ASSERT_TRUE(registry.RegisterDrawer(InspectorDrawerEntry{
        "last",
        [](const Scene::Entity) { return true; },
        [&invocationOrder](const Scene::Entity) { invocationOrder.emplace_back("last"); },
    }));

    registry.DrawEligible(selected);

    EXPECT_EQ(predicateEntity, selected);
    EXPECT_EQ(drawEntity, selected);
    EXPECT_EQ(invocationOrder, (std::vector<std::string>{"first", "last"}));
}

TEST(InspectorDrawerRegistryTest, DrawEligibleMatching_FiltersEligibleSectionsByIdentifier) {
    InspectorDrawerRegistry registry;
    std::vector<std::string> invocationOrder;

    ASSERT_TRUE(registry.RegisterDrawer(MakeAlwaysEligibleDrawer("transform", invocationOrder)));
    ASSERT_TRUE(registry.RegisterDrawer(MakeAlwaysEligibleDrawer("primitive-mesh", invocationOrder)));

    registry.DrawEligibleMatching(Scene::Entity{2U, 1U}, "transform");

    EXPECT_EQ(invocationOrder, (std::vector<std::string>{"transform"}));
}

TEST(InspectorDrawerRegistryTest, DrawEligible_EmptyRegistryDoesNotInvokeCallbacks) {
    InspectorDrawerRegistry registry;

    registry.DrawEligible(Scene::Entity{1U, 0U});

    EXPECT_EQ(registry.GetDrawerCount(), 0U);
}

TEST(InspectorDrawerRegistryTest, DrawEligible_DefersDrawerRegisteredByCallbackUntilNextPass) {
    InspectorDrawerRegistry registry;
    std::vector<std::string> invocationOrder;
    bool registeredDeferredDrawer = false;

    ASSERT_TRUE(registry.RegisterDrawer(InspectorDrawerEntry{
        "first",
        [](const Scene::Entity) { return true; },
        [&registry, &invocationOrder, &registeredDeferredDrawer](const Scene::Entity) {
            invocationOrder.emplace_back("first");
            if (registeredDeferredDrawer) {
                return;
            }
            registeredDeferredDrawer = true;
            ASSERT_TRUE(registry.RegisterDrawer(InspectorDrawerEntry{
                "deferred",
                [](const Scene::Entity) { return true; },
                [&invocationOrder](const Scene::Entity) { invocationOrder.emplace_back("deferred"); },
            }));
        },
    }));

    registry.DrawEligible(Scene::Entity{5U, 3U});
    EXPECT_EQ(invocationOrder, (std::vector<std::string>{"first"}));
    EXPECT_EQ(registry.GetDrawerCount(), 2U);

    registry.DrawEligible(Scene::Entity{5U, 3U});
    EXPECT_EQ(invocationOrder, (std::vector<std::string>{"first", "first", "deferred"}));
}

TEST(InspectorDrawerRegistryTest, GroupHeadersAreDrawnOncePerRunOfEligibleDrawers) {
    InspectorDrawerRegistry registry;
    std::vector<std::string> drawn;
    const auto add = [&](const char* const id, const bool eligible) {
        ASSERT_TRUE(registry.RegisterDrawer(InspectorDrawerEntry{
            id, [eligible](const Scene::Entity) { return eligible; },
            [&drawn, id](const Scene::Entity) { drawn.emplace_back(id); }}));
    };
    add("own", true);
    add("transform", true);
    add("visibility", true);
    add("process", true);
    add("script", false);
    add("metadata", true);
    EXPECT_TRUE(registry.SetDrawerGroup("transform", "Object3D"));
    EXPECT_TRUE(registry.SetDrawerGroup("visibility", "Object3D"));
    EXPECT_TRUE(registry.SetDrawerGroup("process", "Object"));
    EXPECT_TRUE(registry.SetDrawerGroup("script", "Object"));
    EXPECT_TRUE(registry.SetDrawerGroup("metadata", "Object"));
    EXPECT_FALSE(registry.SetDrawerGroup("missing", "Object"));
    registry.SetGroupHeaderDrawer([&drawn](const std::string& label) { drawn.push_back("[" + label + "]"); });

    const Scene::Entity entity{1U, 0U};
    registry.DrawEligible(entity);
    EXPECT_EQ(drawn, (std::vector<std::string>{"own", "[Object3D]", "transform", "visibility", "[Object]", "process",
                                               "metadata"}));
    EXPECT_EQ(registry.GetEligibleGroupHeaders(entity), (std::vector<std::string>{"Object3D", "Object"}));
}

} // namespace
} // namespace UVE::Editor::Tests
