// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
#include "uve/object/object_registry_uve.h"
#include <memory>
#include <mutex>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>
#include <gtest/gtest.h>

namespace {
using namespace UVE::Core;
static_assert(!std::is_move_constructible_v<ObjectUVE>);
static_assert(!std::is_copy_constructible_v<ObjectUVE>);
static_assert(!std::is_move_constructible_v<ObjectRegistryUVE>);
struct CountedUVE final : ObjectUVE {
    explicit CountedUVE(int &count) : deaths(count) {}
    ~CountedUVE() override { ++deaths; }
    int &deaths;
};
TEST(ObjectRegistryUVETest, IdentityAndConstLookup) {
    ObjectRegistryUVE registry;
    ObjectUVE a, b;
    ObjectHandleUVE ha, hb;
    ASSERT_TRUE(registry.RegisterUVE(a, ha));
    ASSERT_TRUE(registry.RegisterUVE(b, hb));
    EXPECT_TRUE(ha.IsValidUVE());
    EXPECT_EQ(a.GetHandleUVE(), ha);
    EXPECT_NE(ha, hb);
    EXPECT_EQ(registry.FindUVE(ha), &a);
    EXPECT_EQ(std::as_const(registry).FindUVE(hb), &b);
    EXPECT_EQ(registry.FindUVE({}), nullptr);
    EXPECT_EQ(registry.GetLiveCountUVE(), 2U);
}
TEST(ObjectRegistryUVETest, DuplicateAndForeignRegistrationFailAtomically) {
    ObjectRegistryUVE first, second;
    ObjectUVE a, b;
    ObjectHandleUVE ha, hb, output;
    ASSERT_TRUE(first.RegisterUVE(a, ha));
    ASSERT_TRUE(second.RegisterUVE(b, hb));
    EXPECT_FALSE(first.RegisterUVE(a, output));
    EXPECT_FALSE(second.RegisterUVE(a, output));
    EXPECT_EQ(output, ObjectHandleUVE{});
    EXPECT_EQ(ha.index, hb.index);
    EXPECT_EQ(ha.generation, hb.generation);
    EXPECT_NE(ha.registry, hb.registry);
    EXPECT_EQ(first.FindUVE(hb), nullptr);
    EXPECT_FALSE(second.UnregisterUVE(ha));
    EXPECT_EQ(a.GetHandleUVE(), ha);
}
TEST(ObjectRegistryUVETest, DestructionRejectsStaleHandleAcrossSlotReuse) {
    ObjectRegistryUVE registry;
    ObjectHandleUVE stale, live;
    int deaths = 0;
    {
        CountedUVE object{deaths};
        ASSERT_TRUE(registry.RegisterUVE(object, stale));
    }
    EXPECT_EQ(deaths, 1);
    EXPECT_EQ(registry.GetLiveCountUVE(), 0U);
    ObjectUVE next;
    ASSERT_TRUE(registry.RegisterUVE(next, live));
    EXPECT_EQ(stale.index, live.index);
    EXPECT_NE(stale.generation, live.generation);
    EXPECT_EQ(registry.FindUVE(stale), nullptr);
    EXPECT_FALSE(registry.UnregisterUVE(stale));
}
TEST(ObjectRegistryUVETest, DetachTransfersRegistrationNotOwnership) {
    ObjectRegistryUVE first, second;
    int deaths = 0;
    CountedUVE object{deaths};
    ObjectHandleUVE handle;
    ASSERT_TRUE(first.RegisterUVE(object, handle));
    ASSERT_TRUE(first.UnregisterUVE(handle));
    EXPECT_FALSE(first.UnregisterUVE(handle));
    EXPECT_FALSE(object.GetHandleUVE().IsValidUVE());
    EXPECT_EQ(deaths, 0);
    EXPECT_TRUE(second.RegisterUVE(object, handle));
}
TEST(ObjectRegistryUVETest, ClearNeverResurrectsIssuedHandles) {
    ObjectRegistryUVE registry;
    ObjectUVE object;
    std::vector<ObjectHandleUVE> old;
    for (int i = 0; i < 128; ++i) {
        ObjectHandleUVE handle;
        ASSERT_TRUE(registry.RegisterUVE(object, handle));
        for (auto stale : old)
            EXPECT_EQ(registry.FindUVE(stale), nullptr);
        old.push_back(handle);
        registry.ClearUVE();
        EXPECT_FALSE(object.GetHandleUVE().IsValidUVE());
    }
}
TEST(ObjectRegistryUVETest, ShutdownIsPermanentAndDoesNotDeleteObjects) {
    int deaths = 0;
    CountedUVE object{deaths};
    ObjectHandleUVE handle;
    {
        ObjectRegistryUVE registry;
        ASSERT_TRUE(registry.RegisterUVE(object, handle));
        registry.ShutdownUVE();
        registry.ShutdownUVE();
        EXPECT_TRUE(registry.IsShutdownUVE());
        auto output = handle;
        EXPECT_FALSE(registry.RegisterUVE(object, output));
        EXPECT_EQ(output, handle);
        EXPECT_EQ(registry.FindUVE(handle), nullptr);
    }
    EXPECT_FALSE(object.GetHandleUVE().IsValidUVE());
    EXPECT_EQ(deaths, 0);
    ObjectRegistryUVE replacement;
    EXPECT_TRUE(replacement.RegisterUVE(object, handle));
}
TEST(ObjectRegistryUVETest, DestructorDetachesWithoutExplicitShutdown) {
    ObjectUVE object;
    {
        ObjectRegistryUVE registry;
        ObjectHandleUVE handle;
        ASSERT_TRUE(registry.RegisterUVE(object, handle));
    }
    EXPECT_FALSE(object.GetHandleUVE().IsValidUVE());
}
TEST(ObjectRegistryUVETest, GrowthKeepsObjectAddressesStable) {
    ObjectRegistryUVE registry;
    std::vector<std::unique_ptr<ObjectUVE>> objects;
    std::vector<ObjectHandleUVE> handles;
    for (int i = 0; i < 512; ++i) {
        objects.push_back(std::make_unique<ObjectUVE>());
        ObjectHandleUVE handle;
        ASSERT_TRUE(registry.RegisterUVE(*objects.back(), handle));
        handles.push_back(handle);
    }
    for (std::size_t i = 0; i < objects.size(); ++i)
        EXPECT_EQ(registry.FindUVE(handles[i]), objects[i].get());
    objects.clear();
    EXPECT_EQ(registry.GetLiveCountUVE(), 0U);
    for (auto handle : handles)
        EXPECT_EQ(registry.FindUVE(handle), nullptr);
}
TEST(ObjectRegistryUVETest, ExternalLockProtectsLookupAndDestructionAcrossThreads) {
    ObjectRegistryUVE registry;
    auto object = std::make_unique<ObjectUVE>();
    ObjectHandleUVE handle;
    ASSERT_TRUE(registry.RegisterUVE(*object, handle));
    std::mutex mutex;
    std::thread destroyer([&] {
        const std::lock_guard lock(mutex);
        EXPECT_EQ(registry.FindUVE(handle), object.get());
        object.reset();
    });
    {
        const std::lock_guard lock(mutex);
        if (object)
            EXPECT_EQ(registry.FindUVE(handle), object.get());
        else
            EXPECT_EQ(registry.FindUVE(handle), nullptr);
    }
    destroyer.join();
    EXPECT_EQ(registry.FindUVE(handle), nullptr);
    EXPECT_EQ(registry.GetLiveCountUVE(), 0U);
}
} // namespace
