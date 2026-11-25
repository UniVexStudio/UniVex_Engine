// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include <limits>

#include <gtest/gtest.h>

#include "camera_component.h"
#include "entity.h"
#include "light_component.h"
#include "visibility_component.h"
#include "blueprint_object_host.h"
#include "entity_manager.h"
#include "event_system.h"
#include "memory_manager.h"
#include "blueprint_value.h"

namespace UVE::Scene::Tests {
namespace {

class UVScriptNodeStateTest : public ::testing::Test {
protected:
    Memory::MemoryManager memoryManager;
    Events::EventSystem eventSystem;
    EntityManager entityManager{memoryManager.GetDefaultAllocator(), eventSystem};

    Entity MakeLight() {
        const Entity entity = entityManager.CreateEntity();
        entityManager.AddComponent<LightComponent>(entity, LightComponent{});
        return entity;
    }

    Entity MakeCamera() {
        const Entity entity = entityManager.CreateEntity();
        entityManager.AddComponent<CameraComponent>(entity, CameraComponent{});
        return entity;
    }
};

TEST_F(UVScriptNodeStateTest, Host_DescribesNodeStateSurface) {
    const Core::BlueprintObjectHost bare{entityManager, nullptr, entityManager.CreateEntity()};
    const std::optional<Blueprint::HostProperty> visible = bare.DescribeProperty("visible");
    ASSERT_TRUE(visible.has_value());
    EXPECT_EQ(visible->type.kind, Blueprint::Type::Kind::Bool);
    EXPECT_TRUE(visible->writable);
    EXPECT_FALSE(bare.DescribeProperty("intensity").has_value());
    EXPECT_FALSE(bare.DescribeProperty("fov").has_value());

    const Core::BlueprintObjectHost light{entityManager, nullptr, MakeLight()};
    const std::optional<Blueprint::HostProperty> intensity =
        light.DescribeProperty("intensity");
    ASSERT_TRUE(intensity.has_value());
    EXPECT_EQ(intensity->type.kind, Blueprint::Type::Kind::Float);
    EXPECT_TRUE(intensity->writable);
    EXPECT_FALSE(light.DescribeProperty("fov").has_value());

    const Core::BlueprintObjectHost camera{entityManager, nullptr, MakeCamera()};
    const std::optional<Blueprint::HostProperty> fov = camera.DescribeProperty("fov");
    ASSERT_TRUE(fov.has_value());
    EXPECT_EQ(fov->type.kind, Blueprint::Type::Kind::Float);
    EXPECT_TRUE(fov->writable);
    EXPECT_FALSE(camera.DescribeProperty("intensity").has_value());
}

TEST_F(UVScriptNodeStateTest, Visible_DefaultsTrueAndWriteAddsComponent) {
    Core::BlueprintObjectHost host{entityManager, nullptr, entityManager.CreateEntity()};
    EXPECT_TRUE(std::get<bool>(host.GetProperty("visible")));

    host.SetProperty("visible", Blueprint::Value{false});
    EXPECT_FALSE(std::get<bool>(host.GetProperty("visible")));

    host.SetProperty("visible", Blueprint::Value{true});
    EXPECT_TRUE(std::get<bool>(host.GetProperty("visible")));
}

TEST_F(UVScriptNodeStateTest, Visible_AddsComponentWithoutTouchingDerivedHierarchy) {
    const Entity entity = entityManager.CreateEntity();
    Core::BlueprintObjectHost host{entityManager, nullptr, entity};
    ASSERT_FALSE(entityManager.HasComponent<VisibilityComponent>(entity));

    host.SetProperty("visible", Blueprint::Value{false});
    ASSERT_TRUE(entityManager.HasComponent<VisibilityComponent>(entity));
    const VisibilityComponent& stored = entityManager.GetComponent<VisibilityComponent>(entity);
    EXPECT_FALSE(stored.visible);
    EXPECT_TRUE(stored.visibleInHierarchy);

    // A hidden ancestor leaves the switch on while the derived flag reads hidden; the host
    // reports the switch it owns, and a write still leaves the derived flag alone.
    VisibilityComponent hidden;
    hidden.visible = true;
    hidden.visibleInHierarchy = false;
    const Entity child = entityManager.CreateEntity();
    entityManager.AddComponent<VisibilityComponent>(child, hidden);
    Core::BlueprintObjectHost childHost{entityManager, nullptr, child};
    EXPECT_TRUE(std::get<bool>(childHost.GetProperty("visible")));
    childHost.SetProperty("visible", Blueprint::Value{false});
    const VisibilityComponent& childStored =
        entityManager.GetComponent<VisibilityComponent>(child);
    EXPECT_FALSE(childStored.visible);
    EXPECT_FALSE(childStored.visibleInHierarchy);
}

TEST_F(UVScriptNodeStateTest, Intensity_RoundTripsAndRejectsInvalid) {
    Core::BlueprintObjectHost host{entityManager, nullptr, MakeLight()};
    EXPECT_DOUBLE_EQ(std::get<double>(host.GetProperty("intensity")), 1.0);

    host.SetProperty("intensity", Blueprint::Value{2.5});
    EXPECT_DOUBLE_EQ(std::get<double>(host.GetProperty("intensity")), 2.5);

    const double nan = static_cast<double>(std::numeric_limits<float>::quiet_NaN());
    const double inf = std::numeric_limits<double>::infinity();
    host.SetProperty("intensity", Blueprint::Value{-1.0});
    host.SetProperty("intensity", Blueprint::Value{nan});
    host.SetProperty("intensity", Blueprint::Value{inf});
    EXPECT_DOUBLE_EQ(std::get<double>(host.GetProperty("intensity")), 2.5);
}

TEST_F(UVScriptNodeStateTest, Fov_RoundTripsAndRejectsInvalid) {
    Core::BlueprintObjectHost host{entityManager, nullptr, MakeCamera()};
    EXPECT_FLOAT_EQ(static_cast<float>(std::get<double>(host.GetProperty("fov"))), 60.0);

    host.SetProperty("fov", Blueprint::Value{90.0});
    EXPECT_FLOAT_EQ(static_cast<float>(std::get<double>(host.GetProperty("fov"))), 90.0);
    host.SetProperty("fov", Blueprint::Value{0.1});
    host.SetProperty("fov", Blueprint::Value{179.9});
    EXPECT_FLOAT_EQ(static_cast<float>(std::get<double>(host.GetProperty("fov"))), 179.9F);

    const double nan = static_cast<double>(std::numeric_limits<float>::quiet_NaN());
    host.SetProperty("fov", Blueprint::Value{0.05});
    host.SetProperty("fov", Blueprint::Value{0.0});
    host.SetProperty("fov", Blueprint::Value{180.0});
    host.SetProperty("fov", Blueprint::Value{1000.0});
    host.SetProperty("fov", Blueprint::Value{-90.0});
    host.SetProperty("fov", Blueprint::Value{nan});
    EXPECT_FLOAT_EQ(static_cast<float>(std::get<double>(host.GetProperty("fov"))), 179.9F);
}

} // namespace
} // namespace UVE::Scene::Tests
