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

class UVScriptNodeStateUVETest : public ::testing::Test {
protected:
    Memory::MemoryManagerUVE memoryManager;
    Events::EventSystemUVE eventSystem;
    EntityManagerUVE entityManager{memoryManager.GetDefaultAllocatorUVE(), eventSystem};

    EntityUVE MakeLightUVE() {
        const EntityUVE entity = entityManager.CreateEntityUVE();
        entityManager.AddComponentUVE<LightComponentUVE>(entity, LightComponentUVE{});
        return entity;
    }

    EntityUVE MakeCameraUVE() {
        const EntityUVE entity = entityManager.CreateEntityUVE();
        entityManager.AddComponentUVE<CameraComponentUVE>(entity, CameraComponentUVE{});
        return entity;
    }
};

TEST_F(UVScriptNodeStateUVETest, Host_DescribesNodeStateSurface) {
    const Core::BlueprintObjectHostUVE bare{entityManager, nullptr, entityManager.CreateEntityUVE()};
    const std::optional<Blueprint::HostPropertyUVE> visible = bare.DescribePropertyUVE("visible");
    ASSERT_TRUE(visible.has_value());
    EXPECT_EQ(visible->type.kind, Blueprint::TypeUVE::KindUVE::Bool);
    EXPECT_TRUE(visible->writable);
    EXPECT_FALSE(bare.DescribePropertyUVE("intensity").has_value());
    EXPECT_FALSE(bare.DescribePropertyUVE("fov").has_value());

    const Core::BlueprintObjectHostUVE light{entityManager, nullptr, MakeLightUVE()};
    const std::optional<Blueprint::HostPropertyUVE> intensity =
        light.DescribePropertyUVE("intensity");
    ASSERT_TRUE(intensity.has_value());
    EXPECT_EQ(intensity->type.kind, Blueprint::TypeUVE::KindUVE::Float);
    EXPECT_TRUE(intensity->writable);
    EXPECT_FALSE(light.DescribePropertyUVE("fov").has_value());

    const Core::BlueprintObjectHostUVE camera{entityManager, nullptr, MakeCameraUVE()};
    const std::optional<Blueprint::HostPropertyUVE> fov = camera.DescribePropertyUVE("fov");
    ASSERT_TRUE(fov.has_value());
    EXPECT_EQ(fov->type.kind, Blueprint::TypeUVE::KindUVE::Float);
    EXPECT_TRUE(fov->writable);
    EXPECT_FALSE(camera.DescribePropertyUVE("intensity").has_value());
}

TEST_F(UVScriptNodeStateUVETest, Visible_DefaultsTrueAndWriteAddsComponent) {
    Core::BlueprintObjectHostUVE host{entityManager, nullptr, entityManager.CreateEntityUVE()};
    EXPECT_TRUE(std::get<bool>(host.GetPropertyUVE("visible")));

    host.SetPropertyUVE("visible", Blueprint::ValueUVE{false});
    EXPECT_FALSE(std::get<bool>(host.GetPropertyUVE("visible")));

    host.SetPropertyUVE("visible", Blueprint::ValueUVE{true});
    EXPECT_TRUE(std::get<bool>(host.GetPropertyUVE("visible")));
}

TEST_F(UVScriptNodeStateUVETest, Visible_AddsComponentWithoutTouchingDerivedHierarchy) {
    const EntityUVE entity = entityManager.CreateEntityUVE();
    Core::BlueprintObjectHostUVE host{entityManager, nullptr, entity};
    ASSERT_FALSE(entityManager.HasComponentUVE<VisibilityComponentUVE>(entity));

    host.SetPropertyUVE("visible", Blueprint::ValueUVE{false});
    ASSERT_TRUE(entityManager.HasComponentUVE<VisibilityComponentUVE>(entity));
    const VisibilityComponentUVE& stored = entityManager.GetComponentUVE<VisibilityComponentUVE>(entity);
    EXPECT_FALSE(stored.visible);
    EXPECT_TRUE(stored.visibleInHierarchy);

    // A hidden ancestor leaves the switch on while the derived flag reads hidden; the host
    // reports the switch it owns, and a write still leaves the derived flag alone.
    VisibilityComponentUVE hidden;
    hidden.visible = true;
    hidden.visibleInHierarchy = false;
    const EntityUVE child = entityManager.CreateEntityUVE();
    entityManager.AddComponentUVE<VisibilityComponentUVE>(child, hidden);
    Core::BlueprintObjectHostUVE childHost{entityManager, nullptr, child};
    EXPECT_TRUE(std::get<bool>(childHost.GetPropertyUVE("visible")));
    childHost.SetPropertyUVE("visible", Blueprint::ValueUVE{false});
    const VisibilityComponentUVE& childStored =
        entityManager.GetComponentUVE<VisibilityComponentUVE>(child);
    EXPECT_FALSE(childStored.visible);
    EXPECT_FALSE(childStored.visibleInHierarchy);
}

TEST_F(UVScriptNodeStateUVETest, Intensity_RoundTripsAndRejectsInvalid) {
    Core::BlueprintObjectHostUVE host{entityManager, nullptr, MakeLightUVE()};
    EXPECT_DOUBLE_EQ(std::get<double>(host.GetPropertyUVE("intensity")), 1.0);

    host.SetPropertyUVE("intensity", Blueprint::ValueUVE{2.5});
    EXPECT_DOUBLE_EQ(std::get<double>(host.GetPropertyUVE("intensity")), 2.5);

    const double nan = static_cast<double>(std::numeric_limits<float>::quiet_NaN());
    const double inf = std::numeric_limits<double>::infinity();
    host.SetPropertyUVE("intensity", Blueprint::ValueUVE{-1.0});
    host.SetPropertyUVE("intensity", Blueprint::ValueUVE{nan});
    host.SetPropertyUVE("intensity", Blueprint::ValueUVE{inf});
    EXPECT_DOUBLE_EQ(std::get<double>(host.GetPropertyUVE("intensity")), 2.5);
}

TEST_F(UVScriptNodeStateUVETest, Fov_RoundTripsAndRejectsInvalid) {
    Core::BlueprintObjectHostUVE host{entityManager, nullptr, MakeCameraUVE()};
    EXPECT_FLOAT_EQ(static_cast<float>(std::get<double>(host.GetPropertyUVE("fov"))), 60.0);

    host.SetPropertyUVE("fov", Blueprint::ValueUVE{90.0});
    EXPECT_FLOAT_EQ(static_cast<float>(std::get<double>(host.GetPropertyUVE("fov"))), 90.0);
    host.SetPropertyUVE("fov", Blueprint::ValueUVE{0.1});
    host.SetPropertyUVE("fov", Blueprint::ValueUVE{179.9});
    EXPECT_FLOAT_EQ(static_cast<float>(std::get<double>(host.GetPropertyUVE("fov"))), 179.9F);

    const double nan = static_cast<double>(std::numeric_limits<float>::quiet_NaN());
    host.SetPropertyUVE("fov", Blueprint::ValueUVE{0.05});
    host.SetPropertyUVE("fov", Blueprint::ValueUVE{0.0});
    host.SetPropertyUVE("fov", Blueprint::ValueUVE{180.0});
    host.SetPropertyUVE("fov", Blueprint::ValueUVE{1000.0});
    host.SetPropertyUVE("fov", Blueprint::ValueUVE{-90.0});
    host.SetPropertyUVE("fov", Blueprint::ValueUVE{nan});
    EXPECT_FLOAT_EQ(static_cast<float>(std::get<double>(host.GetPropertyUVE("fov"))), 179.9F);
}

} // namespace
} // namespace UVE::Scene::Tests
