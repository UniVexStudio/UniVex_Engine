// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
#include "object_registry.h"
#include <memory>
#include <mutex>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>
#include <gtest/gtest.h>

namespace {
using namespace UVE::Core;
static_assert(!std::is_move_constructible_v<Object>);
static_assert(!std::is_copy_constructible_v<Object>);
static_assert(!std::is_move_constructible_v<ObjectRegistry>);
struct Counted final : Object {
    explicit Counted(int &count) : deaths(count) {}
    ~Counted() override { ++deaths; }
    int &deaths;
};
TEST(ObjectRegistryTest, IdentityAndConstLookup) {
    ObjectRegistry registry;
    Object a, b;
    ObjectHandle ha, hb;
    ASSERT_TRUE(registry.Register(a, ha));
    ASSERT_TRUE(registry.Register(b, hb));
    EXPECT_TRUE(ha.IsValid());
    EXPECT_EQ(a.GetHandle(), ha);
    EXPECT_NE(ha, hb);
    EXPECT_EQ(registry.Find(ha), &a);
    EXPECT_EQ(std::as_const(registry).Find(hb), &b);
    EXPECT_EQ(registry.Find({}), nullptr);
    EXPECT_EQ(registry.GetLiveCount(), 2U);
}
TEST(ObjectRegistryTest, DuplicateAndForeignRegistrationFailAtomically) {
    ObjectRegistry first, second;
    Object a, b;
    ObjectHandle ha, hb, output;
    ASSERT_TRUE(first.Register(a, ha));
    ASSERT_TRUE(second.Register(b, hb));
    EXPECT_FALSE(first.Register(a, output));
    EXPECT_FALSE(second.Register(a, output));
    EXPECT_EQ(output, ObjectHandle{});
    EXPECT_EQ(ha.index, hb.index);
    EXPECT_EQ(ha.generation, hb.generation);
    EXPECT_NE(ha.registry, hb.registry);
    EXPECT_EQ(first.Find(hb), nullptr);
    EXPECT_FALSE(second.Unregister(ha));
    EXPECT_EQ(a.GetHandle(), ha);
}
TEST(ObjectRegistryTest, DestructionRejectsStaleHandleAcrossSlotReuse) {
    ObjectRegistry registry;
    ObjectHandle stale, live;
    int deaths = 0;
    {
        Counted object{deaths};
        ASSERT_TRUE(registry.Register(object, stale));
    }
    EXPECT_EQ(deaths, 1);
    EXPECT_EQ(registry.GetLiveCount(), 0U);
    Object next;
    ASSERT_TRUE(registry.Register(next, live));
    EXPECT_EQ(stale.index, live.index);
    EXPECT_NE(stale.generation, live.generation);
    EXPECT_EQ(registry.Find(stale), nullptr);
    EXPECT_FALSE(registry.Unregister(stale));
}
TEST(ObjectRegistryTest, DetachTransfersRegistrationNotOwnership) {
    ObjectRegistry first, second;
    int deaths = 0;
    Counted object{deaths};
    ObjectHandle handle;
    ASSERT_TRUE(first.Register(object, handle));
    ASSERT_TRUE(first.Unregister(handle));
    EXPECT_FALSE(first.Unregister(handle));
    EXPECT_FALSE(object.GetHandle().IsValid());
    EXPECT_EQ(deaths, 0);
    EXPECT_TRUE(second.Register(object, handle));
}
TEST(ObjectRegistryTest, ClearNeverResurrectsIssuedHandles) {
    ObjectRegistry registry;
    Object object;
    std::vector<ObjectHandle> old;
    for (int i = 0; i < 128; ++i) {
        ObjectHandle handle;
        ASSERT_TRUE(registry.Register(object, handle));
        for (auto stale : old)
            EXPECT_EQ(registry.Find(stale), nullptr);
        old.push_back(handle);
        registry.Clear();
        EXPECT_FALSE(object.GetHandle().IsValid());
    }
}
TEST(ObjectRegistryTest, ShutdownIsPermanentAndDoesNotDeleteObjects) {
    int deaths = 0;
    Counted object{deaths};
    ObjectHandle handle;
    {
        ObjectRegistry registry;
        ASSERT_TRUE(registry.Register(object, handle));
        registry.Shutdown();
        registry.Shutdown();
        EXPECT_TRUE(registry.IsShutdown());
        auto output = handle;
        EXPECT_FALSE(registry.Register(object, output));
        EXPECT_EQ(output, handle);
        EXPECT_EQ(registry.Find(handle), nullptr);
    }
    EXPECT_FALSE(object.GetHandle().IsValid());
    EXPECT_EQ(deaths, 0);
    ObjectRegistry replacement;
    EXPECT_TRUE(replacement.Register(object, handle));
}
TEST(ObjectRegistryTest, DestructorDetachesWithoutExplicitShutdown) {
    Object object;
    {
        ObjectRegistry registry;
        ObjectHandle handle;
        ASSERT_TRUE(registry.Register(object, handle));
    }
    EXPECT_FALSE(object.GetHandle().IsValid());
}
TEST(ObjectRegistryTest, GrowthKeepsObjectAddressesStable) {
    ObjectRegistry registry;
    std::vector<std::unique_ptr<Object>> objects;
    std::vector<ObjectHandle> handles;
    for (int i = 0; i < 512; ++i) {
        objects.push_back(std::make_unique<Object>());
        ObjectHandle handle;
        ASSERT_TRUE(registry.Register(*objects.back(), handle));
        handles.push_back(handle);
    }
    for (std::size_t i = 0; i < objects.size(); ++i)
        EXPECT_EQ(registry.Find(handles[i]), objects[i].get());
    objects.clear();
    EXPECT_EQ(registry.GetLiveCount(), 0U);
    for (auto handle : handles)
        EXPECT_EQ(registry.Find(handle), nullptr);
}
TEST(ObjectRegistryTest, ExternalLockProtectsLookupAndDestructionAcrossThreads) {
    ObjectRegistry registry;
    auto object = std::make_unique<Object>();
    ObjectHandle handle;
    ASSERT_TRUE(registry.Register(*object, handle));
    std::mutex mutex;
    std::thread destroyer([&] {
        const std::lock_guard lock(mutex);
        EXPECT_EQ(registry.Find(handle), object.get());
        object.reset();
    });
    {
        const std::lock_guard lock(mutex);
        if (object)
            EXPECT_EQ(registry.Find(handle), object.get());
        else
            EXPECT_EQ(registry.Find(handle), nullptr);
    }
    destroyer.join();
    EXPECT_EQ(registry.Find(handle), nullptr);
    EXPECT_EQ(registry.GetLiveCount(), 0U);
}
} // namespace
