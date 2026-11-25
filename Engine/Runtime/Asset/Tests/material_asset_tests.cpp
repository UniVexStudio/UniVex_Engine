// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#include "material_asset.h"

#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

#include "asset_database.h"
#include "asset_handle.h"
#include "asset_import_sidecar.h"
#include "asset_manager.h"
#include "texture_import_settings.h"
#include "uve_file_envelope.h"
#include "log_sink.h"
#include "logger.h"
#include "event_system.h"
#include "thread_pool.h"

namespace UVE::Asset::Tests {
namespace {

[[nodiscard]] MaterialAsset MakeTestMaterial() {
    MaterialAsset material;
    material.albedoColor = Math::Color{0.8F, 0.2F, 0.2F};
    material.albedoTexture = AssetGuid{111};
    material.normalTexture = AssetGuid{222};
    material.metallic = 0.5F;
    material.roughness = 0.3F;
    material.aoTexture = AssetGuid{333};
    material.emissiveColor = Math::Color{0.1F, 0.0F, 0.0F};
    material.vertexShader = AssetGuid{444};
    material.fragmentShader = AssetGuid{555};
    material.isTransparent = true;
    material.billboardMode = MaterialBillboardMode::Y;
    material.metallicRoughnessTexture = AssetGuid{666};
    material.emissiveTexture = AssetGuid{777};
    material.emissiveEnergy = 2.5F;
    material.normalScale = 0.75F;
    material.occlusionStrength = 0.4F;
    material.uvScale = Math::Vector2{2.0F, 3.0F};
    material.uvOffset = Math::Vector2{0.1F, 0.2F};
    material.unshaded = true;
    material.alphaCutoff = 0.35F;
    return material;
}

TEST(MaterialAssetTest, DefaultConstruction_HasSensibleDefaults) {
    constexpr MaterialAsset material;
    EXPECT_EQ(material.albedoTexture, kInvalidAssetGuid);
    EXPECT_EQ(material.normalTexture, kInvalidAssetGuid);
    EXPECT_EQ(material.aoTexture, kInvalidAssetGuid);
    EXPECT_EQ(material.vertexShader, kInvalidAssetGuid);
    EXPECT_EQ(material.fragmentShader, kInvalidAssetGuid);
    EXPECT_EQ(material.metallic, 0.0F);
    EXPECT_EQ(material.roughness, 0.5F);
    EXPECT_FALSE(material.isTransparent);
    EXPECT_EQ(material.billboardMode, MaterialBillboardMode::Disabled);
    EXPECT_EQ(material.metallicRoughnessTexture, kInvalidAssetGuid);
    EXPECT_EQ(material.emissiveTexture, kInvalidAssetGuid);
    EXPECT_EQ(material.emissiveEnergy, 1.0F);
    EXPECT_EQ(material.normalScale, 1.0F);
    EXPECT_EQ(material.occlusionStrength, 1.0F);
    EXPECT_EQ(material.uvScale, (Math::Vector2{1.0F, 1.0F}));
    EXPECT_EQ(material.uvOffset, (Math::Vector2{0.0F, 0.0F}));
    EXPECT_FALSE(material.unshaded);
    EXPECT_EQ(material.alphaCutoff, 0.0F);
    EXPECT_EQ(material.kind, MaterialKind::Standard);
}

TEST(MaterialAssetTest, SaveThenLoad_RoundTripsFieldExact) {
    const std::filesystem::path path = "uve_material_asset_tests_round_trip.uvmat";
    std::filesystem::remove(path);
    const MaterialAsset original = MakeTestMaterial();
    ASSERT_TRUE(SaveMaterialAsset(original, path));

    MaterialAsset loaded;
    ASSERT_TRUE(LoadMaterialAsset(path, loaded));

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

// The Inspector writes these five fields and the renderer reads all of them, so losing any of them
// on save is invisible until a material is reopened and quietly reverts to its default blend, cull
// or depth behaviour. Also checks the two non-default orientations (cull Front, depth Test off) -
// a serializer that writes a field but reads back only "was it set" would pass a Back/true check.
TEST(MaterialAssetTest, SaveThenLoad_RoundTripsTheRenderingStateFields) {
    const std::filesystem::path path = "uve_material_asset_tests_round_trip_rendering_state.uvmat";
    std::filesystem::remove(path);

    MaterialAsset original = MakeTestMaterial();
    original.blendMode = MaterialBlendMode::Subtractive;
    original.cullMode = MaterialCullMode::Front;
    original.depthDrawMode = MaterialDepthDrawMode::Never;
    original.depthTestEnabled = false;
    original.depthWriteEnabled = false;
    original.alphaSource = MaterialAlphaSource::None;
    original.alphaHashingEnabled = true;
    original.renderPriority = 7;
    original.albedoAlpha = 0.25F;
    original.isTransparent = true;
    ASSERT_TRUE(SaveMaterialAsset(original, path));

    MaterialAsset loaded;
    ASSERT_TRUE(LoadMaterialAsset(path, loaded));
    EXPECT_EQ(loaded.blendMode, MaterialBlendMode::Subtractive);
    EXPECT_EQ(loaded.cullMode, MaterialCullMode::Front);
    EXPECT_EQ(loaded.depthDrawMode, MaterialDepthDrawMode::Never);
    EXPECT_FALSE(loaded.depthTestEnabled);
    EXPECT_FALSE(loaded.depthWriteEnabled);
    EXPECT_EQ(loaded.alphaSource, MaterialAlphaSource::None);
    EXPECT_TRUE(loaded.alphaHashingEnabled);
    EXPECT_EQ(loaded.renderPriority, 7);
    EXPECT_FLOAT_EQ(loaded.albedoAlpha, 0.25F);
    EXPECT_TRUE(loaded.isTransparent);

    // And the defaults have to survive the other direction: a material that never touched these
    // must not acquire a non-default state by being saved once.
    MaterialAsset defaults = MakeTestMaterial();
    ASSERT_TRUE(SaveMaterialAsset(defaults, path));
    MaterialAsset defaultsLoaded;
    ASSERT_TRUE(LoadMaterialAsset(path, defaultsLoaded));
    EXPECT_EQ(defaultsLoaded.blendMode, MaterialBlendMode::Opaque);
    EXPECT_EQ(defaultsLoaded.cullMode, MaterialCullMode::Back);
    EXPECT_EQ(defaultsLoaded.depthDrawMode, MaterialDepthDrawMode::OpaqueOnly);
    EXPECT_TRUE(defaultsLoaded.depthTestEnabled);
    EXPECT_TRUE(defaultsLoaded.depthWriteEnabled);
    EXPECT_EQ(defaultsLoaded.alphaSource, MaterialAlphaSource::AlbedoAlpha);
    EXPECT_FALSE(defaultsLoaded.alphaHashingEnabled);
    EXPECT_EQ(defaultsLoaded.renderPriority, 0);

    std::filesystem::remove(path);
}

TEST(MaterialAssetTest, SaveThenLoad_RoundTripsUvSetsAndTheDetailsSurfaceFields) {
    const std::filesystem::path path = "uve_material_asset_tests_round_trip_uv_sets.uvmat";
    std::filesystem::remove(path);

    MaterialAsset original = MakeTestMaterial();
    original.uv2Scale = Math::Vector2{2.0F, 3.0F};
    original.uv2Offset = Math::Vector2{0.25F, 0.5F};
    original.triplanarEnabled = true;
    original.worldSpaceTriplanar = true;
    original.triplanarBlendSharpness = 4.0F;
    original.uv2Triplanar = true;
    original.uv2TriplanarSharpness = 2.0F;
    original.aoOnUv2 = true;
    original.emissionOnUv2 = true;
    original.dielectricSpecular = 0.25F;
    original.normalFlipGreen = true;
    original.useVertexColorAsAlbedo = true;
    original.disableFog = true;
    original.receiveShadows = false;
    original.rimAmount = 0.5F;
    original.rimTint = Math::Color{0.5F, 0.25F, 1.0F};
    original.textureTint = Math::Color{0.3F, 0.6F, 0.9F};
    original.refractionAmount = 0.75F;
    original.renderLayerMask = 0x5U;
    ASSERT_TRUE(SaveMaterialAsset(original, path));

    MaterialAsset loaded;
    ASSERT_TRUE(LoadMaterialAsset(path, loaded));
    EXPECT_EQ(loaded.uv2Scale, (Math::Vector2{2.0F, 3.0F}));
    EXPECT_EQ(loaded.uv2Offset, (Math::Vector2{0.25F, 0.5F}));
    EXPECT_TRUE(loaded.triplanarEnabled);
    EXPECT_TRUE(loaded.worldSpaceTriplanar);
    EXPECT_FLOAT_EQ(loaded.triplanarBlendSharpness, 4.0F);
    EXPECT_TRUE(loaded.uv2Triplanar);
    EXPECT_FALSE(loaded.uv2WorldTriplanar);
    EXPECT_FLOAT_EQ(loaded.uv2TriplanarSharpness, 2.0F);
    EXPECT_TRUE(loaded.aoOnUv2);
    EXPECT_TRUE(loaded.emissionOnUv2);
    EXPECT_FLOAT_EQ(loaded.dielectricSpecular, 0.25F);
    EXPECT_TRUE(loaded.normalFlipGreen);
    EXPECT_TRUE(loaded.useVertexColorAsAlbedo);
    EXPECT_TRUE(loaded.disableFog);
    EXPECT_FALSE(loaded.receiveShadows);
    EXPECT_FLOAT_EQ(loaded.rimAmount, 0.5F);
    EXPECT_FLOAT_EQ(loaded.rimTint.g, 0.25F);
    EXPECT_FLOAT_EQ(loaded.textureTint.r, 0.3F);
    EXPECT_FLOAT_EQ(loaded.textureTint.g, 0.6F);
    EXPECT_FLOAT_EQ(loaded.textureTint.b, 0.9F);
    EXPECT_FLOAT_EQ(loaded.refractionAmount, 0.75F);
    EXPECT_EQ(loaded.renderLayerMask, 0x5U);

    // Untouched, they stay at their defaults through a save.
    ASSERT_TRUE(SaveMaterialAsset(MakeTestMaterial(), path));
    MaterialAsset defaultsLoaded;
    ASSERT_TRUE(LoadMaterialAsset(path, defaultsLoaded));
    EXPECT_FALSE(defaultsLoaded.triplanarEnabled);
    EXPECT_FALSE(defaultsLoaded.aoOnUv2);
    EXPECT_FLOAT_EQ(defaultsLoaded.dielectricSpecular, 0.5F);
    EXPECT_TRUE(defaultsLoaded.receiveShadows);
    EXPECT_EQ(defaultsLoaded.renderLayerMask, 0xFFFFFFFFU);

    MaterialAsset invalidParallax = MakeTestMaterial();
    invalidParallax.parallaxMinLayers = 32U;
    invalidParallax.parallaxMaxLayers = 8U;
    EXPECT_FALSE(IsMaterialAssetValid(invalidParallax));

    std::filesystem::remove(path);
}

TEST(MaterialAssetTest, SaveThenLoad_RoundTripsEveryKind) {
    const std::filesystem::path path = "uve_material_asset_tests_round_trip_kind.uvmat";
    for (const MaterialKind kind : {MaterialKind::Standard, MaterialKind::Orm, MaterialKind::Shader}) {
        std::filesystem::remove(path);
        MaterialAsset original = MakeTestMaterial();
        original.kind = kind;
        ASSERT_TRUE(SaveMaterialAsset(original, path));

        MaterialAsset loaded;
        ASSERT_TRUE(LoadMaterialAsset(path, loaded));
        EXPECT_EQ(loaded.kind, kind);
    }
    std::filesystem::remove(path);
}

TEST(MaterialAssetTest, SaveMaterialAsset_WritesKindOnlyForNonStandardPresets) {
    const std::filesystem::path path = "uve_material_asset_tests_kind_payload.uvmat";
    std::filesystem::remove(path);
    MaterialAsset material = MakeTestMaterial();
    material.kind = MaterialKind::Standard;
    ASSERT_TRUE(SaveMaterialAsset(material, path));

    const auto payloadText = [&path]() {
        const std::optional<std::pair<UveFileHeader, std::vector<std::byte>>> file = ReadUveFile(path);
        if (!file.has_value()) {
            return std::string{};
        }
        const std::vector<std::byte>& payload = file->second;
        return std::string(reinterpret_cast<const char*>(payload.data()), payload.size());
    };
    // A Standard material keeps the exact payload it had before the field existed.
    EXPECT_EQ(payloadText().find("\"kind\""), std::string::npos);

    material.kind = MaterialKind::Orm;
    ASSERT_TRUE(SaveMaterialAsset(material, path));
    EXPECT_NE(payloadText().find("\"kind\":1"), std::string::npos);
    std::filesystem::remove(path);
}

TEST(MaterialAssetTest, SaveThenLoad_RoundTripsEachMapsOwnReading) {
    const std::filesystem::path path = "uve_material_asset_tests_map_settings.uvmat";
    std::filesystem::remove(path);
    MaterialAsset original = MakeTestMaterial();
    MaterialMapSettings& albedo = original.Map(MaterialMap::Albedo);
    albedo.tiling = Math::Vector2{4.0F, 2.0F};
    albedo.offset = Math::Vector2{0.25F, -0.5F};
    MaterialMapSettings& roughness = original.Map(MaterialMap::Roughness);
    roughness.channel = MaterialTextureChannel::Green;
    roughness.invert = true;
    roughness.strength = 0.5F;
    original.Map(MaterialMap::Occlusion).onUv2 = true;
    ASSERT_TRUE(SaveMaterialAsset(original, path));

    MaterialAsset loaded;
    ASSERT_TRUE(LoadMaterialAsset(path, loaded));
    EXPECT_EQ(loaded.maps, original.maps);
    // The maps nobody touched read as authored.
    EXPECT_EQ(loaded.Map(MaterialMap::Normal), MaterialMapSettings{});
    std::filesystem::remove(path);
}

TEST(MaterialAssetTest, SaveMaterialAsset_LeavesMapsOutWhenEveryMapReadsAsAuthored) {
    const std::filesystem::path path = "uve_material_asset_tests_map_payload.uvmat";
    std::filesystem::remove(path);
    MaterialAsset material = MakeTestMaterial();
    ASSERT_TRUE(SaveMaterialAsset(material, path));
    const std::optional<std::pair<UveFileHeader, std::vector<std::byte>>> file = ReadUveFile(path);
    ASSERT_TRUE(file.has_value());
    const std::string payload(reinterpret_cast<const char*>(file->second.data()), file->second.size());
    EXPECT_EQ(payload.find("\"maps\""), std::string::npos);
    std::filesystem::remove(path);
}

TEST(MaterialAssetTest, IsMaterialAssetValid_RejectsAMapStrengthOutsideZeroToOne) {
    MaterialAsset material = MakeTestMaterial();
    EXPECT_TRUE(IsMaterialAssetValid(material));
    material.Map(MaterialMap::Emissive).strength = 1.5F;
    EXPECT_FALSE(IsMaterialAssetValid(material));
    material.Map(MaterialMap::Emissive).strength = 1.0F;
    material.Map(MaterialMap::Albedo).tiling.x = std::numeric_limits<float>::infinity();
    EXPECT_FALSE(IsMaterialAssetValid(material));
}

TEST(MaterialAssetTest, LoadMaterialAsset_RejectsUnknownKind) {
    const std::filesystem::path path = "uve_material_asset_tests_unknown_kind.uvmat";
    std::filesystem::remove(path);
    // Every required field is present, so the only reason this can fail is the unknown preset.
    const std::string invalidJson =
        R"({"albedoColor":{"x":1.0,"y":1.0,"z":1.0},"albedoTexture":0,"normalTexture":0,"metallic":0.0,"roughness":0.5,"aoTexture":0,"emissiveColor":{"x":0.0,"y":0.0,"z":0.0},"vertexShader":0,"fragmentShader":0,"isTransparent":false,"kind":7})";
    const auto* const jsonBytes = reinterpret_cast<const std::byte*>(invalidJson.data());
    ASSERT_TRUE(WriteUveFile(path, AssetKind::Material,
                                 std::vector<std::byte>(jsonBytes, jsonBytes + invalidJson.size())));

    MaterialAsset output;
    output.kind = MaterialKind::Standard;
    EXPECT_FALSE(LoadMaterialAsset(path, output));
    EXPECT_EQ(output.kind, MaterialKind::Standard);
    std::filesystem::remove(path);
}

TEST(MaterialAssetTest, SaveMaterialAsset_RejectsInvalidValuesBeforeReplacingDestination) {
    const std::filesystem::path path = "uve_material_asset_tests_invalid_save.uvmat";
    std::filesystem::remove(path);
    const MaterialAsset original = MakeTestMaterial();
    ASSERT_TRUE(SaveMaterialAsset(original, path));

    MaterialAsset invalid = original;
    invalid.metallic = 2.0F;
    EXPECT_FALSE(SaveMaterialAsset(invalid, path));
    invalid = original;
    invalid.roughness = std::numeric_limits<float>::quiet_NaN();
    EXPECT_FALSE(SaveMaterialAsset(invalid, path));
    invalid = original;
    invalid.emissiveColor.r = -1.0F;
    EXPECT_FALSE(SaveMaterialAsset(invalid, path));
    invalid = original;
    invalid.emissiveEnergy = -0.1F;
    EXPECT_FALSE(SaveMaterialAsset(invalid, path));
    invalid = original;
    invalid.normalScale = std::numeric_limits<float>::infinity();
    EXPECT_FALSE(SaveMaterialAsset(invalid, path));
    invalid = original;
    invalid.occlusionStrength = 1.5F;
    EXPECT_FALSE(SaveMaterialAsset(invalid, path));
    invalid = original;
    invalid.uvScale.x = std::numeric_limits<float>::quiet_NaN();
    EXPECT_FALSE(SaveMaterialAsset(invalid, path));
    invalid = original;
    invalid.alphaCutoff = 2.0F;
    EXPECT_FALSE(SaveMaterialAsset(invalid, path));

    MaterialAsset loaded;
    ASSERT_TRUE(LoadMaterialAsset(path, loaded));
    EXPECT_EQ(loaded.metallic, original.metallic);
    EXPECT_EQ(loaded.roughness, original.roughness);
    EXPECT_EQ(loaded.emissiveColor, original.emissiveColor);
    std::filesystem::remove(path);
}

TEST(MaterialAssetTest, LoadMaterialAsset_RejectsOutOfRangeDecodedValuesBeforePublication) {
    const std::filesystem::path path = "uve_material_asset_tests_invalid_values.uvmat";
    std::filesystem::remove(path);
    const std::string invalidJson =
        R"({"albedoColor":{"x":1.0,"y":1.0,"z":1.0},"albedoTexture":0,"normalTexture":0,"metallic":2.0,"roughness":0.5,"aoTexture":0,"emissiveColor":{"x":0.0,"y":0.0,"z":0.0},"vertexShader":0,"fragmentShader":0,"isTransparent":false})";
    const auto* const jsonBytes = reinterpret_cast<const std::byte*>(invalidJson.data());
    ASSERT_TRUE(WriteUveFile(path, AssetKind::Material,
                                 std::vector<std::byte>(jsonBytes, jsonBytes + invalidJson.size())));

    MaterialAsset output;
    output.metallic = 0.25F;
    EXPECT_FALSE(LoadMaterialAsset(path, output));
    EXPECT_EQ(output.metallic, 0.25F);
    std::filesystem::remove(path);
}

TEST(MaterialAssetTest, LoadMaterialAsset_WrongAssetKind_FailsCleanlyAndLogsError) {
    const std::filesystem::path path = "uve_material_asset_tests_wrong_kind.uvblob";
    std::filesystem::remove(path);
    ASSERT_TRUE(WriteUveFile(path, AssetKind::Blob, {}));

    Debug::Logger logger;
    logger.Init(Debug::LogLevel::Trace);
    auto memorySink = std::make_unique<Debug::MemorySink>();
    Debug::MemorySink* const memorySinkPtr = memorySink.get();
    logger.AddSink(std::move(memorySink));

    MaterialAsset material;
    EXPECT_FALSE(LoadMaterialAsset(path, material));

    const std::vector<Debug::LogMessage> messages = memorySinkPtr->GetMessages();
    const bool foundError =
        std::any_of(messages.begin(), messages.end(), [](const Debug::LogMessage& message) {
            return message.level == Debug::LogLevel::Error &&
                   message.message.contains("not a material file");
        });
    EXPECT_TRUE(foundError);

    logger.Shutdown();
    std::filesystem::remove(path);
}

TEST(MaterialAssetTest, LoadMaterialAsset_MissingFile_ReturnsFalse) {
    const std::filesystem::path path = "uve_material_asset_tests_nonexistent.uvmat";
    std::filesystem::remove(path);

    MaterialAsset material;
    EXPECT_FALSE(LoadMaterialAsset(path, material));
}

TEST(MaterialAssetTest, LoadMaterialAsset_MissingField_FailsAndLogsError) {
    const std::filesystem::path path = "uve_material_asset_tests_missing_field.uvmat";
    std::filesystem::remove(path);
    const std::string incompleteJson = "{\"albedoColor\": {\"x\": 1.0, \"y\": 1.0, \"z\": 1.0}}";
    const auto* const jsonBytes = reinterpret_cast<const std::byte*>(incompleteJson.data());
    ASSERT_TRUE(WriteUveFile(path, AssetKind::Material,
                                 std::vector<std::byte>(jsonBytes, jsonBytes + incompleteJson.size())));

    Debug::Logger logger;
    logger.Init(Debug::LogLevel::Trace);
    auto memorySink = std::make_unique<Debug::MemorySink>();
    Debug::MemorySink* const memorySinkPtr = memorySink.get();
    logger.AddSink(std::move(memorySink));

    MaterialAsset material;
    EXPECT_FALSE(LoadMaterialAsset(path, material));

    const std::vector<Debug::LogMessage> messages = memorySinkPtr->GetMessages();
    const bool foundError =
        std::any_of(messages.begin(), messages.end(), [](const Debug::LogMessage& message) {
            return message.level == Debug::LogLevel::Error &&
                   message.message.contains("missing an expected field");
        });
    EXPECT_TRUE(foundError);

    logger.Shutdown();
    std::filesystem::remove(path);
}

TEST(MaterialAssetTest, EndToEnd_RegisterLoaderThenLoad_ReachesLoadedWithMatchingData) {
    const std::filesystem::path path = "uve_material_asset_tests_end_to_end.uvmat";
    std::filesystem::remove(path);
    const MaterialAsset original = MakeTestMaterial();
    ASSERT_TRUE(SaveMaterialAsset(original, path));

    Threading::ThreadPool threadPool(2);
    Events::EventSystem eventSystem;
    AssetDatabase assetDatabase;
    AssetManager assetManager(threadPool, eventSystem);
    assetManager.RegisterLoader<MaterialAsset>(&LoadMaterialAsset);

    const AssetGuid guid = assetDatabase.Register(path);
    const AssetHandle<MaterialAsset> handle = assetManager.Load<MaterialAsset>(guid, assetDatabase);

    bool ready = false;
    for (int iteration = 0; iteration < 200000 && !ready; ++iteration) {
        ready = handle.IsReady() || handle.HasFailed();
        if (!ready) {
            std::this_thread::yield();
        }
    }
    ASSERT_TRUE(ready);
    ASSERT_TRUE(handle.IsReady());
    const MaterialAsset* const loaded = handle.TryGet();
    ASSERT_NE(loaded, nullptr);
    EXPECT_EQ(loaded->albedoTexture, original.albedoTexture);
    EXPECT_EQ(loaded->isTransparent, original.isTransparent);
    EXPECT_EQ(loaded->billboardMode, original.billboardMode);

    std::filesystem::remove(path);
}

TEST(MaterialAssetTest, LoadMaterialAsset_MissingBillboardMode_DefaultsToDisabled) {
    const std::filesystem::path path = "uve_material_asset_tests_legacy_billboard.uvmat";
    std::filesystem::remove(path);
    const std::string legacyJson =
        R"({"albedoColor":{"x":1.0,"y":1.0,"z":1.0},"albedoTexture":0,"normalTexture":0,"metallic":0.0,"roughness":0.5,"aoTexture":0,"emissiveColor":{"x":0.0,"y":0.0,"z":0.0},"vertexShader":0,"fragmentShader":0,"isTransparent":false})";
    const auto* const jsonBytes = reinterpret_cast<const std::byte*>(legacyJson.data());
    ASSERT_TRUE(WriteUveFile(path, AssetKind::Material,
                                 std::vector<std::byte>(jsonBytes, jsonBytes + legacyJson.size())));

    MaterialAsset loaded;
    loaded.billboardMode = MaterialBillboardMode::Enabled;
    ASSERT_TRUE(LoadMaterialAsset(path, loaded));
    EXPECT_EQ(loaded.billboardMode, MaterialBillboardMode::Disabled);
    EXPECT_EQ(loaded.metallicRoughnessTexture, kInvalidAssetGuid);
    EXPECT_EQ(loaded.emissiveTexture, kInvalidAssetGuid);
    EXPECT_EQ(loaded.emissiveEnergy, 1.0F);
    EXPECT_EQ(loaded.normalScale, 1.0F);
    EXPECT_EQ(loaded.occlusionStrength, 1.0F);
    EXPECT_EQ(loaded.uvScale, (Math::Vector2{1.0F, 1.0F}));
    EXPECT_EQ(loaded.uvOffset, (Math::Vector2{0.0F, 0.0F}));
    EXPECT_FALSE(loaded.unshaded);
    EXPECT_EQ(loaded.alphaCutoff, 0.0F);
    std::filesystem::remove(path);
}

TEST(MaterialAssetTest, IsMaterialAssetValid_RejectsUnknownBillboardMode) {
    MaterialAsset material = MakeTestMaterial();
    material.billboardMode = static_cast<MaterialBillboardMode>(9U);
    EXPECT_FALSE(IsMaterialAssetValid(material));
}

TEST(MaterialAssetTest, TryMakeMaterialBillboardRotation_DisabledLeavesTheRotation) {
    Math::Quaternion rotation{0.1F, 0.2F, 0.3F, 0.4F};
    EXPECT_FALSE(TryMakeMaterialBillboardRotation(MaterialBillboardMode::Disabled,
                                                     Math::Vector3{0.0F, 0.0F, -10.0F},
                                                     Math::Vector3{0.0F, 0.0F, 0.0F}, rotation));
    EXPECT_EQ(rotation.x, 0.1F);
    EXPECT_EQ(rotation.y, 0.2F);
    EXPECT_EQ(rotation.z, 0.3F);
    EXPECT_EQ(rotation.w, 0.4F);
}

TEST(MaterialAssetTest, TryMakeMaterialBillboardRotation_EnabledPointsLocalZAtTheCamera) {
    Math::Quaternion rotation{};
    ASSERT_TRUE(TryMakeMaterialBillboardRotation(MaterialBillboardMode::Enabled,
                                                   Math::Vector3{0.0F, 0.0F, -10.0F},
                                                   Math::Vector3{0.0F, 0.0F, 0.0F}, rotation));
    const Math::Vector3 localZ = Math::RotateVector(rotation, Math::Vector3{0.0F, 0.0F, 1.0F});
    EXPECT_NEAR(localZ.x, 0.0F, 1.0e-4F);
    EXPECT_NEAR(localZ.y, 0.0F, 1.0e-4F);
    EXPECT_NEAR(localZ.z, 1.0F, 1.0e-4F);
}

TEST(MaterialAssetTest, TryMakeMaterialBillboardRotation_YIgnoresCameraHeight) {
    Math::Quaternion rotation{};
    ASSERT_TRUE(TryMakeMaterialBillboardRotation(MaterialBillboardMode::Y, Math::Vector3{0.0F, 0.0F, 0.0F},
                                                   Math::Vector3{10.0F, 50.0F, 0.0F}, rotation));
    const Math::Vector3 localZ = Math::RotateVector(rotation, Math::Vector3{0.0F, 0.0F, 1.0F});
    EXPECT_NEAR(localZ.x, 1.0F, 1.0e-4F);
    EXPECT_NEAR(localZ.y, 0.0F, 1.0e-4F);
    EXPECT_NEAR(localZ.z, 0.0F, 1.0e-4F);
}

TEST(MaterialAssetTest, ExtendedMaterialTextureAndImportSidecarRoundTripAndValidateDeterministically) {
    MaterialAsset material = MakeTestMaterial();
    material.albedoAlpha = 0.5F;
    material.metallicTexture = AssetGuid{101U};
    material.roughnessTexture = AssetGuid{102U};
    material.cullMode = MaterialCullMode::Disabled;
    material.blendMode = MaterialBlendMode::AlphaBlend;
    material.clearcoatAmount = 0.6F;
    material.parentMaterial = AssetGuid{103U};
    material.nextPassMaterial = AssetGuid{104U};

    EXPECT_TRUE(ShouldBucketMaterialTransparent(material));
    const std::filesystem::path matPath = "uve_extended_material_asset_test.uvmat";
    std::filesystem::remove(matPath);
    ASSERT_TRUE(SaveMaterialAsset(material, matPath));

    MaterialAsset loadedMat;
    ASSERT_TRUE(LoadMaterialAsset(matPath, loadedMat));
    EXPECT_FLOAT_EQ(loadedMat.albedoAlpha, 0.5F);
    EXPECT_EQ(loadedMat.metallicTexture, AssetGuid{101U});
    EXPECT_EQ(loadedMat.roughnessTexture, AssetGuid{102U});
    EXPECT_EQ(loadedMat.cullMode, MaterialCullMode::Disabled);
    EXPECT_EQ(loadedMat.blendMode, MaterialBlendMode::AlphaBlend);
    EXPECT_FLOAT_EQ(loadedMat.clearcoatAmount, 0.6F);
    EXPECT_EQ(loadedMat.parentMaterial, AssetGuid{103U});
    EXPECT_EQ(loadedMat.nextPassMaterial, AssetGuid{104U});
    std::filesystem::remove(matPath);

    TextureImportSettings texSettings{};
    texSettings.generateMipmaps = false;
    texSettings.compressionMode = TextureCompressionMode::None;
    EXPECT_EQ(EstimateImportedTextureMemoryBytes(64U, 64U, texSettings), 64U * 64U * 4U);

    const std::filesystem::path sidecarPath = GetImportSidecarPath("hero_mesh.fbx");
    EXPECT_EQ(sidecarPath.filename(), "hero_mesh.fbx.uvimport");
    AssetImportSidecar sidecar{};
    sidecar.importerVersion = "v2";
    sidecar.presetName = "CharacterHigh";
    sidecar.reimportNeeded = true;
    sidecar.warnings.push_back("Degenerate triangle skipped");
    sidecar.options["scaleFactor"] = "0.01";
    ASSERT_TRUE(SaveImportSidecar(sidecar, sidecarPath));

    AssetImportSidecar loadedSidecar{};
    ASSERT_TRUE(LoadImportSidecar(sidecarPath, loadedSidecar));
    EXPECT_EQ(loadedSidecar.importerVersion, "v2");
    EXPECT_EQ(loadedSidecar.presetName, "CharacterHigh");
    EXPECT_TRUE(loadedSidecar.reimportNeeded);
    ASSERT_EQ(loadedSidecar.warnings.size(), 1U);
    EXPECT_EQ(loadedSidecar.options.at("scaleFactor"), "0.01");
    std::filesystem::remove(sidecarPath);

    MeshImportSettings meshSettings{};
    meshSettings.generateLods = true;
    meshSettings.lodCount = 4U;
    EXPECT_NE(meshSettings.GetCacheVersion().find("lods=4"), std::string::npos);

    PrimitiveGeometryParameters primitives{};
    EXPECT_TRUE(IsPrimitiveGeometryParametersValid(primitives));
    primitives.torusOuterRadius = 0.1F;
    EXPECT_FALSE(IsPrimitiveGeometryParametersValid(primitives));

    AudioImportSettings audioSettings{};
    audioSettings.forceMono = true;
    EXPECT_NE(audioSettings.GetCacheVersion().find("mono=1"), std::string::npos);

    FontImportSettings fontSettings{};
    fontSettings.baseSizePixels = 48U;
    EXPECT_NE(fontSettings.GetCacheVersion().find("size=48"), std::string::npos);
}

} // namespace
} // namespace UVE::Asset::Tests
