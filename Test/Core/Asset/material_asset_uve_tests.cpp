// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#include "uve/asset/material_asset_uve.h"

#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <limits>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

#include "uve/asset/asset_database_uve.h"
#include "uve/asset/asset_handle_uve.h"
#include "uve/asset/asset_manager_uve.h"
#include "uve/asset/uve_file_envelope_uve.h"
#include "uve/logging/log_sink_uve.h"
#include "uve/logging/logger_uve.h"
#include "uve/events/event_system_uve.h"
#include "uve/threading/thread_pool_uve.h"

namespace UVE::Asset::Tests {
namespace {

[[nodiscard]] MaterialAssetUVE MakeTestMaterialUVE() {
    MaterialAssetUVE material;
    material.albedoColor = Math::ColorUVE{0.8F, 0.2F, 0.2F};
    material.albedoTexture = AssetGuidUVE{111};
    material.normalTexture = AssetGuidUVE{222};
    material.metallic = 0.5F;
    material.roughness = 0.3F;
    material.aoTexture = AssetGuidUVE{333};
    material.emissiveColor = Math::ColorUVE{0.1F, 0.0F, 0.0F};
    material.vertexShader = AssetGuidUVE{444};
    material.fragmentShader = AssetGuidUVE{555};
    material.isTransparent = true;
    material.billboardMode = MaterialBillboardModeUVE::Y;
    material.metallicRoughnessTexture = AssetGuidUVE{666};
    material.emissiveTexture = AssetGuidUVE{777};
    material.emissiveEnergy = 2.5F;
    material.normalScale = 0.75F;
    material.occlusionStrength = 0.4F;
    material.uvScale = Math::Vector2UVE{2.0F, 3.0F};
    material.uvOffset = Math::Vector2UVE{0.1F, 0.2F};
    material.unshaded = true;
    material.alphaCutoff = 0.35F;
    return material;
}

TEST(MaterialAssetUVETest, DefaultConstruction_HasSensibleDefaults) {
    constexpr MaterialAssetUVE material;
    EXPECT_EQ(material.albedoTexture, kInvalidAssetGuidUVE);
    EXPECT_EQ(material.normalTexture, kInvalidAssetGuidUVE);
    EXPECT_EQ(material.aoTexture, kInvalidAssetGuidUVE);
    EXPECT_EQ(material.vertexShader, kInvalidAssetGuidUVE);
    EXPECT_EQ(material.fragmentShader, kInvalidAssetGuidUVE);
    EXPECT_EQ(material.metallic, 0.0F);
    EXPECT_EQ(material.roughness, 0.5F);
    EXPECT_FALSE(material.isTransparent);
    EXPECT_EQ(material.billboardMode, MaterialBillboardModeUVE::Disabled);
    EXPECT_EQ(material.metallicRoughnessTexture, kInvalidAssetGuidUVE);
    EXPECT_EQ(material.emissiveTexture, kInvalidAssetGuidUVE);
    EXPECT_EQ(material.emissiveEnergy, 1.0F);
    EXPECT_EQ(material.normalScale, 1.0F);
    EXPECT_EQ(material.occlusionStrength, 1.0F);
    EXPECT_EQ(material.uvScale, (Math::Vector2UVE{1.0F, 1.0F}));
    EXPECT_EQ(material.uvOffset, (Math::Vector2UVE{0.0F, 0.0F}));
    EXPECT_FALSE(material.unshaded);
    EXPECT_EQ(material.alphaCutoff, 0.0F);
}

TEST(MaterialAssetUVETest, SaveThenLoad_RoundTripsFieldExact) {
    const std::filesystem::path path = "uve_material_asset_tests_round_trip.uvmat";
    std::filesystem::remove(path);
    const MaterialAssetUVE original = MakeTestMaterialUVE();
    ASSERT_TRUE(SaveMaterialAssetUVE(original, path));

    MaterialAssetUVE loaded;
    ASSERT_TRUE(LoadMaterialAssetUVE(path, loaded));

    EXPECT_EQ(loaded.albedoColor, original.albedoColor);
    EXPECT_EQ(loaded.albedoTexture, original.albedoTexture);
    EXPECT_EQ(loaded.normalTexture, original.normalTexture);
    EXPECT_EQ(loaded.metallic, original.metallic);
    EXPECT_EQ(loaded.roughness, original.roughness);
    EXPECT_EQ(loaded.aoTexture, original.aoTexture);
    EXPECT_EQ(loaded.emissiveColor, original.emissiveColor);
    EXPECT_EQ(loaded.vertexShader, original.vertexShader);
    EXPECT_EQ(loaded.fragmentShader, original.fragmentShader);
    EXPECT_EQ(loaded.isTransparent, original.isTransparent);
    EXPECT_EQ(loaded.billboardMode, original.billboardMode);
    EXPECT_EQ(loaded.metallicRoughnessTexture, original.metallicRoughnessTexture);
    EXPECT_EQ(loaded.emissiveTexture, original.emissiveTexture);
    EXPECT_EQ(loaded.emissiveEnergy, original.emissiveEnergy);
    EXPECT_EQ(loaded.normalScale, original.normalScale);
    EXPECT_EQ(loaded.occlusionStrength, original.occlusionStrength);
    EXPECT_EQ(loaded.uvScale, original.uvScale);
    EXPECT_EQ(loaded.uvOffset, original.uvOffset);
    EXPECT_EQ(loaded.unshaded, original.unshaded);
    EXPECT_EQ(loaded.alphaCutoff, original.alphaCutoff);

    std::filesystem::remove(path);
}

TEST(MaterialAssetUVETest, SaveMaterialAssetUVE_RejectsInvalidValuesBeforeReplacingDestination) {
    const std::filesystem::path path = "uve_material_asset_tests_invalid_save.uvmat";
    std::filesystem::remove(path);
    const MaterialAssetUVE original = MakeTestMaterialUVE();
    ASSERT_TRUE(SaveMaterialAssetUVE(original, path));

    MaterialAssetUVE invalid = original;
    invalid.metallic = 2.0F;
    EXPECT_FALSE(SaveMaterialAssetUVE(invalid, path));
    invalid = original;
    invalid.roughness = std::numeric_limits<float>::quiet_NaN();
    EXPECT_FALSE(SaveMaterialAssetUVE(invalid, path));
    invalid = original;
    invalid.emissiveColor.r = -1.0F;
    EXPECT_FALSE(SaveMaterialAssetUVE(invalid, path));
    invalid = original;
    invalid.emissiveEnergy = -0.1F;
    EXPECT_FALSE(SaveMaterialAssetUVE(invalid, path));
    invalid = original;
    invalid.normalScale = std::numeric_limits<float>::infinity();
    EXPECT_FALSE(SaveMaterialAssetUVE(invalid, path));
    invalid = original;
    invalid.occlusionStrength = 1.5F;
    EXPECT_FALSE(SaveMaterialAssetUVE(invalid, path));
    invalid = original;
    invalid.uvScale.x = std::numeric_limits<float>::quiet_NaN();
    EXPECT_FALSE(SaveMaterialAssetUVE(invalid, path));
    invalid = original;
    invalid.alphaCutoff = 2.0F;
    EXPECT_FALSE(SaveMaterialAssetUVE(invalid, path));

    MaterialAssetUVE loaded;
    ASSERT_TRUE(LoadMaterialAssetUVE(path, loaded));
    EXPECT_EQ(loaded.metallic, original.metallic);
    EXPECT_EQ(loaded.roughness, original.roughness);
    EXPECT_EQ(loaded.emissiveColor, original.emissiveColor);
    std::filesystem::remove(path);
}

TEST(MaterialAssetUVETest, LoadMaterialAssetUVE_RejectsOutOfRangeDecodedValuesBeforePublication) {
    const std::filesystem::path path = "uve_material_asset_tests_invalid_values.uvmat";
    std::filesystem::remove(path);
    const std::string invalidJson =
        R"({"albedoColor":{"x":1.0,"y":1.0,"z":1.0},"albedoTexture":0,"normalTexture":0,"metallic":2.0,"roughness":0.5,"aoTexture":0,"emissiveColor":{"x":0.0,"y":0.0,"z":0.0},"vertexShader":0,"fragmentShader":0,"isTransparent":false})";
    const auto* const jsonBytes = reinterpret_cast<const std::byte*>(invalidJson.data());
    ASSERT_TRUE(WriteUveFileUVE(path, AssetKindUVE::Material,
                                 std::vector<std::byte>(jsonBytes, jsonBytes + invalidJson.size())));

    MaterialAssetUVE output;
    output.metallic = 0.25F;
    EXPECT_FALSE(LoadMaterialAssetUVE(path, output));
    EXPECT_EQ(output.metallic, 0.25F);
    std::filesystem::remove(path);
}

TEST(MaterialAssetUVETest, LoadMaterialAssetUVE_WrongAssetKind_FailsCleanlyAndLogsError) {
    const std::filesystem::path path = "uve_material_asset_tests_wrong_kind.uvblob";
    std::filesystem::remove(path);
    ASSERT_TRUE(WriteUveFileUVE(path, AssetKindUVE::Blob, {}));

    Debug::LoggerUVE logger;
    logger.Init(Debug::LogLevelUVE::Trace);
    auto memorySink = std::make_unique<Debug::MemorySinkUVE>();
    Debug::MemorySinkUVE* const memorySinkPtr = memorySink.get();
    logger.AddSink(std::move(memorySink));

    MaterialAssetUVE material;
    EXPECT_FALSE(LoadMaterialAssetUVE(path, material));

    const std::vector<Debug::LogMessageUVE> messages = memorySinkPtr->GetMessagesUVE();
    const bool foundError =
        std::any_of(messages.begin(), messages.end(), [](const Debug::LogMessageUVE& message) {
            return message.level == Debug::LogLevelUVE::Error &&
                   message.message.contains("not a material file");
        });
    EXPECT_TRUE(foundError);

    logger.Shutdown();
    std::filesystem::remove(path);
}

TEST(MaterialAssetUVETest, LoadMaterialAssetUVE_MissingFile_ReturnsFalse) {
    const std::filesystem::path path = "uve_material_asset_tests_nonexistent.uvmat";
    std::filesystem::remove(path);

    MaterialAssetUVE material;
    EXPECT_FALSE(LoadMaterialAssetUVE(path, material));
}

TEST(MaterialAssetUVETest, LoadMaterialAssetUVE_MissingField_FailsAndLogsError) {
    const std::filesystem::path path = "uve_material_asset_tests_missing_field.uvmat";
    std::filesystem::remove(path);
    const std::string incompleteJson = "{\"albedoColor\": {\"x\": 1.0, \"y\": 1.0, \"z\": 1.0}}";
    const auto* const jsonBytes = reinterpret_cast<const std::byte*>(incompleteJson.data());
    ASSERT_TRUE(WriteUveFileUVE(path, AssetKindUVE::Material,
                                 std::vector<std::byte>(jsonBytes, jsonBytes + incompleteJson.size())));

    Debug::LoggerUVE logger;
    logger.Init(Debug::LogLevelUVE::Trace);
    auto memorySink = std::make_unique<Debug::MemorySinkUVE>();
    Debug::MemorySinkUVE* const memorySinkPtr = memorySink.get();
    logger.AddSink(std::move(memorySink));

    MaterialAssetUVE material;
    EXPECT_FALSE(LoadMaterialAssetUVE(path, material));

    const std::vector<Debug::LogMessageUVE> messages = memorySinkPtr->GetMessagesUVE();
    const bool foundError =
        std::any_of(messages.begin(), messages.end(), [](const Debug::LogMessageUVE& message) {
            return message.level == Debug::LogLevelUVE::Error &&
                   message.message.contains("missing an expected field");
        });
    EXPECT_TRUE(foundError);

    logger.Shutdown();
    std::filesystem::remove(path);
}

TEST(MaterialAssetUVETest, EndToEnd_RegisterLoaderThenLoadUVE_ReachesLoadedWithMatchingData) {
    const std::filesystem::path path = "uve_material_asset_tests_end_to_end.uvmat";
    std::filesystem::remove(path);
    const MaterialAssetUVE original = MakeTestMaterialUVE();
    ASSERT_TRUE(SaveMaterialAssetUVE(original, path));

    Threading::ThreadPoolUVE threadPool(2);
    Events::EventSystemUVE eventSystem;
    AssetDatabaseUVE assetDatabase;
    AssetManagerUVE assetManager(threadPool, eventSystem);
    assetManager.RegisterLoaderUVE<MaterialAssetUVE>(&LoadMaterialAssetUVE);

    const AssetGuidUVE guid = assetDatabase.RegisterUVE(path);
    const AssetHandleUVE<MaterialAssetUVE> handle = assetManager.LoadUVE<MaterialAssetUVE>(guid, assetDatabase);

    bool ready = false;
    for (int iteration = 0; iteration < 200000 && !ready; ++iteration) {
        ready = handle.IsReadyUVE() || handle.HasFailedUVE();
        if (!ready) {
            std::this_thread::yield();
        }
    }
    ASSERT_TRUE(ready);
    ASSERT_TRUE(handle.IsReadyUVE());
    const MaterialAssetUVE* const loaded = handle.TryGetUVE();
    ASSERT_NE(loaded, nullptr);
    EXPECT_EQ(loaded->albedoTexture, original.albedoTexture);
    EXPECT_EQ(loaded->isTransparent, original.isTransparent);
    EXPECT_EQ(loaded->billboardMode, original.billboardMode);

    std::filesystem::remove(path);
}

TEST(MaterialAssetUVETest, LoadMaterialAssetUVE_MissingBillboardMode_DefaultsToDisabled) {
    const std::filesystem::path path = "uve_material_asset_tests_legacy_billboard.uvmat";
    std::filesystem::remove(path);
    const std::string legacyJson =
        R"({"albedoColor":{"x":1.0,"y":1.0,"z":1.0},"albedoTexture":0,"normalTexture":0,"metallic":0.0,"roughness":0.5,"aoTexture":0,"emissiveColor":{"x":0.0,"y":0.0,"z":0.0},"vertexShader":0,"fragmentShader":0,"isTransparent":false})";
    const auto* const jsonBytes = reinterpret_cast<const std::byte*>(legacyJson.data());
    ASSERT_TRUE(WriteUveFileUVE(path, AssetKindUVE::Material,
                                 std::vector<std::byte>(jsonBytes, jsonBytes + legacyJson.size())));

    MaterialAssetUVE loaded;
    loaded.billboardMode = MaterialBillboardModeUVE::Enabled;
    ASSERT_TRUE(LoadMaterialAssetUVE(path, loaded));
    EXPECT_EQ(loaded.billboardMode, MaterialBillboardModeUVE::Disabled);
    EXPECT_EQ(loaded.metallicRoughnessTexture, kInvalidAssetGuidUVE);
    EXPECT_EQ(loaded.emissiveTexture, kInvalidAssetGuidUVE);
    EXPECT_EQ(loaded.emissiveEnergy, 1.0F);
    EXPECT_EQ(loaded.normalScale, 1.0F);
    EXPECT_EQ(loaded.occlusionStrength, 1.0F);
    EXPECT_EQ(loaded.uvScale, (Math::Vector2UVE{1.0F, 1.0F}));
    EXPECT_EQ(loaded.uvOffset, (Math::Vector2UVE{0.0F, 0.0F}));
    EXPECT_FALSE(loaded.unshaded);
    EXPECT_EQ(loaded.alphaCutoff, 0.0F);
    std::filesystem::remove(path);
}

TEST(MaterialAssetUVETest, IsMaterialAssetValidUVE_RejectsUnknownBillboardMode) {
    MaterialAssetUVE material = MakeTestMaterialUVE();
    material.billboardMode = static_cast<MaterialBillboardModeUVE>(9U);
    EXPECT_FALSE(IsMaterialAssetValidUVE(material));
}

TEST(MaterialAssetUVETest, TryMakeMaterialBillboardRotationUVE_DisabledLeavesTheRotation) {
    Math::QuaternionUVE rotation{0.1F, 0.2F, 0.3F, 0.4F};
    EXPECT_FALSE(TryMakeMaterialBillboardRotationUVE(MaterialBillboardModeUVE::Disabled,
                                                     Math::Vector3UVE{0.0F, 0.0F, -10.0F},
                                                     Math::Vector3UVE{0.0F, 0.0F, 0.0F}, rotation));
    EXPECT_EQ(rotation.x, 0.1F);
    EXPECT_EQ(rotation.y, 0.2F);
    EXPECT_EQ(rotation.z, 0.3F);
    EXPECT_EQ(rotation.w, 0.4F);
}

TEST(MaterialAssetUVETest, TryMakeMaterialBillboardRotationUVE_EnabledPointsLocalZAtTheCamera) {
    Math::QuaternionUVE rotation{};
    ASSERT_TRUE(TryMakeMaterialBillboardRotationUVE(MaterialBillboardModeUVE::Enabled,
                                                   Math::Vector3UVE{0.0F, 0.0F, -10.0F},
                                                   Math::Vector3UVE{0.0F, 0.0F, 0.0F}, rotation));
    const Math::Vector3UVE localZ = Math::RotateVectorUVE(rotation, Math::Vector3UVE{0.0F, 0.0F, 1.0F});
    EXPECT_NEAR(localZ.x, 0.0F, 1.0e-4F);
    EXPECT_NEAR(localZ.y, 0.0F, 1.0e-4F);
    EXPECT_NEAR(localZ.z, 1.0F, 1.0e-4F);
}

TEST(MaterialAssetUVETest, TryMakeMaterialBillboardRotationUVE_YIgnoresCameraHeight) {
    Math::QuaternionUVE rotation{};
    ASSERT_TRUE(TryMakeMaterialBillboardRotationUVE(MaterialBillboardModeUVE::Y, Math::Vector3UVE{0.0F, 0.0F, 0.0F},
                                                   Math::Vector3UVE{10.0F, 50.0F, 0.0F}, rotation));
    const Math::Vector3UVE localZ = Math::RotateVectorUVE(rotation, Math::Vector3UVE{0.0F, 0.0F, 1.0F});
    EXPECT_NEAR(localZ.x, 1.0F, 1.0e-4F);
    EXPECT_NEAR(localZ.y, 0.0F, 1.0e-4F);
    EXPECT_NEAR(localZ.z, 0.0F, 1.0e-4F);
}

} // namespace
} // namespace UVE::Asset::Tests
