// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#include "save_game_system.h"

#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "uve_file_envelope.h"
#include "event_system.h"
#include "memory_manager.h"
#include "save_payload_compression.h"
#include "hierarchy_component.h"
#include "light_component.h"
#include "mesh_component.h"
#include "rigid_3d_component.h"
#include "entity_manager.h"
#include "scene_serializer.h"

namespace UVE::Save::Tests {
namespace {

using Scene::EntityManager;
using Scene::Entity;
using Scene::HierarchyComponent;
using Scene::kInvalidEntity;
using Scene::LightComponent;
using Scene::MeshComponent;
using Scene::Rigid3DComponent;
using Scene::SceneSerializer;

class CountingSceneSerializer final : public Scene::ISceneSerializer {
public:
    [[nodiscard]] std::optional<Scene::SceneSnapshot> Capture(
        Scene::IEntityManager&, const std::vector<Entity>&, Scene::SceneAssetType) const override {
        return std::nullopt;
    }

    [[nodiscard]] std::vector<Entity> Restore(
        Scene::IEntityManager&, const Scene::SceneSnapshot&) const override {
        return {};
    }

    [[nodiscard]] bool Save(Scene::IEntityManager&, const std::vector<Entity>&,
                               const std::filesystem::path& scratchPath, Scene::SceneAssetType) override {
        ++saveCallCount;
        if (throwOnSave) {
            std::ofstream scratch(scratchPath, std::ios::binary);
            scratch << "partial scratch";
            throw std::runtime_error("injected scene serializer save failure");
        }
        if (scratchPayloadBytes != 0U) {
            return Asset::WriteUveFile(
                scratchPath, scratchAssetType,
                std::vector<std::byte>(scratchPayloadBytes, std::byte{0x5A}));
        }
        return false;
    }

    [[nodiscard]] std::vector<Entity> Load(
        Scene::IEntityManager&, const std::filesystem::path&) override {
        if (throwOnLoad) {
            throw std::runtime_error("injected scene serializer failure");
        }
        return {};
    }

    bool throwOnLoad = false;
    bool throwOnSave = false;
    std::size_t scratchPayloadBytes = 0U;
    Scene::SceneAssetType scratchAssetType = Scene::SceneAssetType::Scene;
    int saveCallCount = 0;
};

class SaveGameSystemTest : public ::testing::Test {
protected:
    Memory::MemoryManager memoryManager;
    Events::EventSystem eventSystem;
    EntityManager entityManager{memoryManager.GetDefaultAllocator(), eventSystem};
    SceneSerializer sceneSerializer;
    std::filesystem::path saveDirectory = "uve_save_game_system_tests_saves";
    SaveGameSystem saveGameSystem{sceneSerializer, saveDirectory};

    void TearDown() override {
        std::error_code errorCode;
        std::filesystem::remove_all(saveDirectory, errorCode);
    }
};

TEST_F(SaveGameSystemTest, SaveThenLoad_SingleEntityWithComponents_RoundTripsExactly) {
    const Entity entity = entityManager.CreateEntity();
    entityManager.AddComponent<MeshComponent>(
        entity, MeshComponent{Asset::AssetGuid{111}, Asset::AssetGuid{222}});
    entityManager.AddComponent<LightComponent>(entity,
                                                       LightComponent{Math::Color{0.2F, 0.4F, 0.6F}, 2.5F});

    GameStateMetadata metadata;
    metadata.saveName = "Before the Dragon Fight";
    metadata.playtimeSeconds = 123.5;
    ASSERT_TRUE(saveGameSystem.Save(5, entityManager, {entity}, metadata));

    EntityManager loadedManager(memoryManager.GetDefaultAllocator(), eventSystem);
    const std::vector<Entity> roots = saveGameSystem.Load(5, loadedManager);
    ASSERT_EQ(roots.size(), 1U);
    const Entity loaded = roots[0];

    EXPECT_EQ(loadedManager.GetComponent<MeshComponent>(loaded).meshGuid, Asset::AssetGuid{111});
    EXPECT_FLOAT_EQ(loadedManager.GetComponent<LightComponent>(loaded).intensity, 2.5F);
}

TEST_F(SaveGameSystemTest, SaveThenLoad_MultipleRootEntitiesWithHierarchy_RoundTripsExactly) {
    const Entity parent = entityManager.CreateEntity();
    entityManager.AddComponent<HierarchyComponent>(parent, HierarchyComponent{kInvalidEntity});
    const Entity child = entityManager.CreateEntity();
    entityManager.AddComponent<HierarchyComponent>(child, HierarchyComponent{parent});

    ASSERT_TRUE(saveGameSystem.Save(7, entityManager, {parent}, GameStateMetadata{}));

    EntityManager loadedManager(memoryManager.GetDefaultAllocator(), eventSystem);
    const std::vector<Entity> roots = saveGameSystem.Load(7, loadedManager);
    ASSERT_EQ(roots.size(), 1U);

    Entity loadedChild = kInvalidEntity;
    loadedManager.ForEach<HierarchyComponent>(
        [&loadedChild, parentEntity = roots[0]](Entity entity, HierarchyComponent& hierarchy) {
            if (hierarchy.parent == parentEntity) {
                loadedChild = entity;
            }
        });
    EXPECT_NE(loadedChild, kInvalidEntity);
}

TEST_F(SaveGameSystemTest, GetSaveMetadata_RejectsTruncatedAndTrailingWorldSections) {
    const Entity entity = entityManager.CreateEntity();
    ASSERT_TRUE(saveGameSystem.Save(16, entityManager, {entity}, GameStateMetadata{}));
    const std::filesystem::path slotPath = saveDirectory / "slot_16.uvsave";
    const auto originalFile = Asset::ReadUveFile(slotPath);
    ASSERT_TRUE(originalFile.has_value());

    std::vector<std::byte> expandedPayload;
    ASSERT_TRUE(DecompressSavePayload(originalFile->second, expandedPayload));
    ASSERT_GT(expandedPayload.size(), sizeof(std::uint32_t) + sizeof(std::uint64_t));

    std::vector<std::byte> truncatedPayload = expandedPayload;
    truncatedPayload.pop_back();
    ASSERT_TRUE(Asset::WriteUveFile(slotPath, Asset::AssetKind::Save,
                                       CompressSavePayload(truncatedPayload)));
    EXPECT_FALSE(saveGameSystem.GetSaveMetadata(16).has_value());

    ASSERT_TRUE(Asset::WriteUveFile(slotPath, Asset::AssetKind::Save,
                                       CompressSavePayload(expandedPayload)));
    expandedPayload.push_back(std::byte{0xA5});
    ASSERT_TRUE(Asset::WriteUveFile(slotPath, Asset::AssetKind::Save,
                                       CompressSavePayload(expandedPayload)));
    EXPECT_FALSE(saveGameSystem.GetSaveMetadata(16).has_value());
}

TEST_F(SaveGameSystemTest, Load_RejectsTrailingWorldSection) {
    const Entity entity = entityManager.CreateEntity();
    ASSERT_TRUE(saveGameSystem.Save(21, entityManager, {entity}, GameStateMetadata{}));

    const std::filesystem::path slotPath = saveDirectory / "slot_21.uvsave";
    const auto originalFile = Asset::ReadUveFile(slotPath);
    ASSERT_TRUE(originalFile.has_value());
    std::vector<std::byte> expandedPayload;
    ASSERT_TRUE(DecompressSavePayload(originalFile->second, expandedPayload));
    expandedPayload.push_back(std::byte{0xA5});
    ASSERT_TRUE(Asset::WriteUveFile(slotPath, Asset::AssetKind::Save,
                                       CompressSavePayload(expandedPayload)));

    EntityManager loadedManager(memoryManager.GetDefaultAllocator(), eventSystem);
    EXPECT_TRUE(saveGameSystem.Load(21, loadedManager).empty());
    EXPECT_EQ(loadedManager.GetEntityCount(), 0U);
}

TEST_F(SaveGameSystemTest, Save_OverwritesExistingSlot) {
    const Entity firstEntity = entityManager.CreateEntity();
    entityManager.AddComponent<Rigid3DComponent>(firstEntity, Rigid3DComponent{1.0F, false});
    ASSERT_TRUE(saveGameSystem.Save(2, entityManager, {firstEntity}, GameStateMetadata{}));

    const Entity secondEntity = entityManager.CreateEntity();
    entityManager.AddComponent<Rigid3DComponent>(secondEntity, Rigid3DComponent{9.0F, true});
    ASSERT_TRUE(saveGameSystem.Save(2, entityManager, {secondEntity}, GameStateMetadata{}));

    EntityManager loadedManager(memoryManager.GetDefaultAllocator(), eventSystem);
    const std::vector<Entity> roots = saveGameSystem.Load(2, loadedManager);
    ASSERT_EQ(roots.size(), 1U);
    EXPECT_FLOAT_EQ(loadedManager.GetComponent<Rigid3DComponent>(roots[0]).mass, 9.0F);
    EXPECT_TRUE(loadedManager.GetComponent<Rigid3DComponent>(roots[0]).isKinematic);
}

TEST_F(SaveGameSystemTest, HasSave_ReflectsPresenceAfterSaveAndDelete) {
    EXPECT_FALSE(saveGameSystem.HasSave(4));

    const Entity entity = entityManager.CreateEntity();
    ASSERT_TRUE(saveGameSystem.Save(4, entityManager, {entity}, GameStateMetadata{}));
    EXPECT_TRUE(saveGameSystem.HasSave(4));

    EXPECT_TRUE(saveGameSystem.DeleteSave(4));
    EXPECT_FALSE(saveGameSystem.HasSave(4));
}

TEST_F(SaveGameSystemTest, DeleteSave_EmptySlot_ReturnsFalseWithoutError) {
    EXPECT_FALSE(saveGameSystem.DeleteSave(10));
}

TEST_F(SaveGameSystemTest, GetSaveMetadata_ReturnsMetadataWithoutCreatingEntities) {
    const Entity entity = entityManager.CreateEntity();
    GameStateMetadata metadata;
    metadata.saveName = "My Save";
    metadata.playtimeSeconds = 42.0;
    ASSERT_TRUE(saveGameSystem.Save(8, entityManager, {entity}, metadata));

    EntityManager freshManager(memoryManager.GetDefaultAllocator(), eventSystem);
    const std::optional<GameStateMetadata> loaded = saveGameSystem.GetSaveMetadata(8);
    ASSERT_TRUE(loaded.has_value());
    EXPECT_EQ(loaded->saveName, "My Save");
    EXPECT_DOUBLE_EQ(loaded->playtimeSeconds, 42.0);
    EXPECT_EQ(loaded->slotIndex, 8);
    EXPECT_GT(loaded->savedAtUnixSeconds, 0);
    EXPECT_EQ(freshManager.GetEntityCount(), 0U);
}

TEST_F(SaveGameSystemTest, GetSaveMetadata_RejectsEmbeddedSlotIdentityMismatch) {
    const Entity entity = entityManager.CreateEntity();
    ASSERT_TRUE(saveGameSystem.Save(16, entityManager, {entity}, GameStateMetadata{}));
    const std::filesystem::path slotPath = saveDirectory / "slot_16.uvsave";
    const auto originalFile = Asset::ReadUveFile(slotPath);
    ASSERT_TRUE(originalFile.has_value());

    std::vector<std::byte> expandedPayload;
    ASSERT_TRUE(DecompressSavePayload(originalFile->second, expandedPayload));
    ASSERT_GE(expandedPayload.size(), sizeof(std::uint32_t));
    std::uint32_t metadataLength = 0U;
    std::memcpy(&metadataLength, expandedPayload.data(), sizeof(metadataLength));
    ASSERT_LE(sizeof(metadataLength) + static_cast<std::size_t>(metadataLength), expandedPayload.size());
    std::string metadata(reinterpret_cast<const char*>(expandedPayload.data() + sizeof(metadataLength)), metadataLength);
    const std::string expectedSlot = "\"slotIndex\":16";
    const std::size_t slotOffset = metadata.find(expectedSlot);
    ASSERT_NE(slotOffset, std::string::npos);
    metadata.replace(slotOffset, expectedSlot.size(), "\"slotIndex\":17");
    std::memcpy(expandedPayload.data() + sizeof(metadataLength), metadata.data(), metadata.size());
    ASSERT_TRUE(Asset::WriteUveFile(slotPath, Asset::AssetKind::Save,
                                       CompressSavePayload(expandedPayload)));

    EXPECT_FALSE(saveGameSystem.GetSaveMetadata(16).has_value());
    EntityManager loadedManager(memoryManager.GetDefaultAllocator(), eventSystem);
    EXPECT_TRUE(saveGameSystem.Load(16, loadedManager).empty());
    EXPECT_EQ(loadedManager.GetEntityCount(), 0U);
}

TEST_F(SaveGameSystemTest, Load_RejectsTamperedNegativeTimestampMetadata) {
    const Entity entity = entityManager.CreateEntity();
    ASSERT_TRUE(saveGameSystem.Save(18, entityManager, {entity}, GameStateMetadata{}));

    const std::filesystem::path slotPath = saveDirectory / "slot_18.uvsave";
    const auto originalFile = Asset::ReadUveFile(slotPath);
    ASSERT_TRUE(originalFile.has_value());
    std::vector<std::byte> payload = originalFile->second;
    ASSERT_GE(payload.size(), sizeof(std::uint32_t));
    std::uint32_t metadataLength = 0U;
    std::memcpy(&metadataLength, payload.data(), sizeof(metadataLength));
    ASSERT_LE(sizeof(metadataLength) + static_cast<std::size_t>(metadataLength), payload.size());
    std::string metadataText(reinterpret_cast<const char*>(payload.data() + sizeof(metadataLength)), metadataLength);
    const std::string validToken = "\"savedAtUnixSeconds\":";
    const std::size_t timestampOffset = metadataText.find(validToken);
    ASSERT_NE(timestampOffset, std::string::npos);
    const std::size_t timestampValueOffset = timestampOffset + validToken.size();
    const std::size_t timestampValueEnd = metadataText.find(',', timestampValueOffset);
    ASSERT_NE(timestampValueEnd, std::string::npos);
    metadataText.replace(timestampValueOffset, timestampValueEnd - timestampValueOffset, "-1");
    std::memcpy(payload.data() + sizeof(metadataLength), metadataText.data(), metadataLength);
    ASSERT_TRUE(Asset::WriteUveFile(slotPath, Asset::AssetKind::Save, payload));

    EntityManager loadedManager(memoryManager.GetDefaultAllocator(), eventSystem);
    EXPECT_TRUE(saveGameSystem.Load(18, loadedManager).empty());
    EXPECT_EQ(loadedManager.GetEntityCount(), 0U);
    EXPECT_FALSE(saveGameSystem.GetSaveMetadata(18).has_value());
}

TEST_F(SaveGameSystemTest, Load_RejectsTamperedNegativePlaytimeMetadata) {
    const Entity entity = entityManager.CreateEntity();
    GameStateMetadata metadata;
    metadata.playtimeSeconds = 100.0;
    ASSERT_TRUE(saveGameSystem.Save(17, entityManager, {entity}, metadata));

    const std::filesystem::path slotPath = saveDirectory / "slot_17.uvsave";
    const auto originalFile = Asset::ReadUveFile(slotPath);
    ASSERT_TRUE(originalFile.has_value());
    std::vector<std::byte> payload = originalFile->second;
    ASSERT_GE(payload.size(), sizeof(std::uint32_t));
    std::uint32_t metadataLength = 0U;
    std::memcpy(&metadataLength, payload.data(), sizeof(metadataLength));
    ASSERT_LE(sizeof(metadataLength) + static_cast<std::size_t>(metadataLength), payload.size());
    std::string metadataText(reinterpret_cast<const char*>(payload.data() + sizeof(metadataLength)), metadataLength);
    const std::string validToken = "\"playtimeSeconds\":100.0";
    const std::string invalidToken = "\"playtimeSeconds\":-10.0";
    const std::size_t playtimeOffset = metadataText.find(validToken);
    ASSERT_NE(playtimeOffset, std::string::npos);
    ASSERT_EQ(validToken.size(), invalidToken.size());
    metadataText.replace(playtimeOffset, validToken.size(), invalidToken);
    std::memcpy(payload.data() + sizeof(metadataLength), metadataText.data(), metadataLength);
    ASSERT_TRUE(Asset::WriteUveFile(slotPath, Asset::AssetKind::Save, payload));

    EntityManager loadedManager(memoryManager.GetDefaultAllocator(), eventSystem);
    EXPECT_TRUE(saveGameSystem.Load(17, loadedManager).empty());
    EXPECT_EQ(loadedManager.GetEntityCount(), 0U);
    EXPECT_FALSE(saveGameSystem.GetSaveMetadata(17).has_value());
}

TEST_F(SaveGameSystemTest, GetSaveMetadata_EmptySlot_ReturnsNullopt) {
    EXPECT_FALSE(saveGameSystem.GetSaveMetadata(11).has_value());
}

TEST_F(SaveGameSystemTest, ListUsedSlots_ReturnsAscendingUsedSlotsOnly_ExcludesAutoSaveSlot) {
    const Entity entity = entityManager.CreateEntity();
    ASSERT_TRUE(saveGameSystem.Save(40, entityManager, {entity}, GameStateMetadata{}));
    ASSERT_TRUE(saveGameSystem.Save(1, entityManager, {entity}, GameStateMetadata{}));
    ASSERT_TRUE(saveGameSystem.Save(3, entityManager, {entity}, GameStateMetadata{}));
    ASSERT_TRUE(saveGameSystem.Save(kAutoSaveSlotIndex, entityManager, {entity}, GameStateMetadata{}));

    const std::vector<int> used = saveGameSystem.ListUsedSlots();
    EXPECT_EQ(used, (std::vector<int>{1, 3, 40}));
}

TEST_F(SaveGameSystemTest, ListUsedSlots_NoSaveDirectoryYet_ReturnsEmptyNotError) {
    EXPECT_TRUE(saveGameSystem.ListUsedSlots().empty());
}

TEST_F(SaveGameSystemTest, Save_RejectsOversizedOrNulSaveNameBeforeFilesystemWork) {
    const Entity entity = entityManager.CreateEntity();
    GameStateMetadata metadata;
    metadata.saveName.assign(kMaximumSaveNameBytes + 1U, 'x');
    EXPECT_FALSE(saveGameSystem.Save(18, entityManager, {entity}, metadata));

    metadata.saveName = std::string{"Visible"} + '\0' + "Hidden";
    EXPECT_FALSE(saveGameSystem.Save(19, entityManager, {entity}, metadata));
    EXPECT_FALSE(std::filesystem::exists(saveDirectory));
}

TEST_F(SaveGameSystemTest, Save_OutOfRangeSlotIndex_ReturnsFalse) {
    const Entity entity = entityManager.CreateEntity();
    EXPECT_FALSE(saveGameSystem.Save(-3, entityManager, {entity}, GameStateMetadata{}));
    EXPECT_FALSE(saveGameSystem.Save(kSaveSlotCount, entityManager, {entity}, GameStateMetadata{}));
    EXPECT_FALSE(saveGameSystem.Save(1000, entityManager, {entity}, GameStateMetadata{}));
}

TEST_F(SaveGameSystemTest, Load_OutOfRangeSlotIndex_ReturnsEmptyVector) {
    EXPECT_TRUE(saveGameSystem.Load(-3, entityManager).empty());
    EXPECT_TRUE(saveGameSystem.Load(kSaveSlotCount, entityManager).empty());
}

TEST_F(SaveGameSystemTest, Load_MissingSlot_ReturnsEmptyVector) {
    EXPECT_TRUE(saveGameSystem.Load(20, entityManager).empty());
}

TEST_F(SaveGameSystemTest, Save_ReservedSlots_RoundTripIndependentlyOfNumberedSlots) {
    const Entity numberedEntity = entityManager.CreateEntity();
    entityManager.AddComponent<Rigid3DComponent>(numberedEntity, Rigid3DComponent{1.0F, false});
    ASSERT_TRUE(saveGameSystem.Save(0, entityManager, {numberedEntity}, GameStateMetadata{}));

    const Entity autoSaveEntity = entityManager.CreateEntity();
    entityManager.AddComponent<Rigid3DComponent>(autoSaveEntity, Rigid3DComponent{5.0F, true});
    ASSERT_TRUE(saveGameSystem.Save(kAutoSaveSlotIndex, entityManager, {autoSaveEntity}, GameStateMetadata{}));

    const Entity manualCheckpointEntity = entityManager.CreateEntity();
    entityManager.AddComponent<Rigid3DComponent>(manualCheckpointEntity, Rigid3DComponent{9.0F, true});
    ASSERT_TRUE(saveGameSystem.Save(kManualCheckpointSlotIndex, entityManager, {manualCheckpointEntity},
                                       GameStateMetadata{}));

    EntityManager loadedManager(memoryManager.GetDefaultAllocator(), eventSystem);
    const std::vector<Entity> numberedRoots = saveGameSystem.Load(0, loadedManager);
    ASSERT_EQ(numberedRoots.size(), 1U);
    EXPECT_FLOAT_EQ(loadedManager.GetComponent<Rigid3DComponent>(numberedRoots[0]).mass, 1.0F);

    EntityManager loadedAutoSaveManager(memoryManager.GetDefaultAllocator(), eventSystem);
    const std::vector<Entity> autoSaveRoots = saveGameSystem.Load(kAutoSaveSlotIndex, loadedAutoSaveManager);
    ASSERT_EQ(autoSaveRoots.size(), 1U);
    EXPECT_FLOAT_EQ(loadedAutoSaveManager.GetComponent<Rigid3DComponent>(autoSaveRoots[0]).mass, 5.0F);

    EntityManager loadedManualManager(memoryManager.GetDefaultAllocator(), eventSystem);
    const std::vector<Entity> manualRoots = saveGameSystem.Load(kManualCheckpointSlotIndex, loadedManualManager);
    ASSERT_EQ(manualRoots.size(), 1U);
    EXPECT_FLOAT_EQ(loadedManualManager.GetComponent<Rigid3DComponent>(manualRoots[0]).mass, 9.0F);

    EXPECT_EQ(saveGameSystem.ListUsedSlots(), (std::vector<int>{0}));
}

TEST_F(SaveGameSystemTest, SaveThenLoad_ScratchFilesAreCleanedUpAfterward) {
    const Entity entity = entityManager.CreateEntity();
    ASSERT_TRUE(saveGameSystem.Save(9, entityManager, {entity}, GameStateMetadata{}));

    EntityManager loadedManager(memoryManager.GetDefaultAllocator(), eventSystem);
    ASSERT_FALSE(saveGameSystem.Load(9, loadedManager).empty());

    for (const auto& entry : std::filesystem::directory_iterator(saveDirectory)) {
        const std::string fileName = entry.path().filename().string();
        EXPECT_TRUE(fileName.starts_with("slot_")) << "unexpected leftover file: " << fileName;
    }
}

TEST_F(SaveGameSystemTest, Load_TruncatedFile_ReturnsEmptyVectorWithoutCrashing) {
    const Entity entity = entityManager.CreateEntity();
    ASSERT_TRUE(saveGameSystem.Save(6, entityManager, {entity}, GameStateMetadata{}));

    const std::filesystem::path slotPath = saveDirectory / "slot_06.uvsave";
    ASSERT_TRUE(std::filesystem::exists(slotPath));
    std::error_code errorCode;
    std::filesystem::resize_file(slotPath, 10, errorCode);
    ASSERT_FALSE(errorCode);

    EntityManager freshManager(memoryManager.GetDefaultAllocator(), eventSystem);
    EXPECT_TRUE(saveGameSystem.Load(6, freshManager).empty());
}

TEST_F(SaveGameSystemTest, HasSave_RejectsTruncatedSlotAndListUsedSlotsHidesIt) {
    const Entity entity = entityManager.CreateEntity();
    ASSERT_TRUE(saveGameSystem.Save(12, entityManager, {entity}, GameStateMetadata{}));

    const std::filesystem::path slotPath = saveDirectory / "slot_12.uvsave";
    std::error_code errorCode;
    std::filesystem::resize_file(slotPath, 10, errorCode);
    ASSERT_FALSE(errorCode);

    EXPECT_FALSE(saveGameSystem.HasSave(12));
    EXPECT_TRUE(saveGameSystem.ListUsedSlots().empty());
}

TEST_F(SaveGameSystemTest, Load_PayloadWithBogusLengthPrefix_ReturnsEmptyVectorWithoutCrashing) {
    const Entity entity = entityManager.CreateEntity();
    ASSERT_TRUE(saveGameSystem.Save(12, entityManager, {entity}, GameStateMetadata{}));

    const std::filesystem::path slotPath = saveDirectory / "slot_12.uvsave";
    std::optional<std::pair<Asset::UveFileHeader, std::vector<std::byte>>> file =
        Asset::ReadUveFile(slotPath);
    ASSERT_TRUE(file.has_value());
    std::vector<std::byte> payload = file->second;
    ASSERT_GE(payload.size(), sizeof(std::uint32_t));
    // Overwrite the metadataJsonLength prefix with an absurdly large value, so
    // SplitSavePayload's bounds check fails deterministically.
    const std::uint32_t bogusLength = 0xFFFFFFFFU;
    std::memcpy(payload.data(), &bogusLength, sizeof(bogusLength));
    ASSERT_TRUE(Asset::WriteUveFile(slotPath, Asset::AssetKind::Save, payload));

    EntityManager freshManager(memoryManager.GetDefaultAllocator(), eventSystem);
    EXPECT_TRUE(saveGameSystem.Load(12, freshManager).empty());
    EXPECT_FALSE(saveGameSystem.GetSaveMetadata(12).has_value());
}

TEST_F(SaveGameSystemTest, Load_WorldLengthUint64OverflowIsRejectedWithoutCrashing) {
    const Entity entity = entityManager.CreateEntity();
    ASSERT_TRUE(saveGameSystem.Save(15, entityManager, {entity}, GameStateMetadata{}));

    const std::filesystem::path slotPath = saveDirectory / "slot_15.uvsave";
    std::optional<std::pair<Asset::UveFileHeader, std::vector<std::byte>>> file =
        Asset::ReadUveFile(slotPath);
    ASSERT_TRUE(file.has_value());
    std::vector<std::byte> expandedPayload;
    ASSERT_TRUE(DecompressSavePayload(file->second, expandedPayload));

    std::uint32_t metadataLength = 0U;
    ASSERT_GE(expandedPayload.size(), sizeof(metadataLength));
    std::memcpy(&metadataLength, expandedPayload.data(), sizeof(metadataLength));
    const std::size_t worldLengthOffset = sizeof(metadataLength) + static_cast<std::size_t>(metadataLength);
    ASSERT_LE(worldLengthOffset + sizeof(std::uint64_t), expandedPayload.size());
    const std::uint64_t absurdWorldLength = std::numeric_limits<std::uint64_t>::max();
    std::memcpy(expandedPayload.data() + worldLengthOffset, &absurdWorldLength, sizeof(absurdWorldLength));
    ASSERT_TRUE(Asset::WriteUveFile(slotPath, Asset::AssetKind::Save, expandedPayload));

    EntityManager freshManager(memoryManager.GetDefaultAllocator(), eventSystem);
    EXPECT_TRUE(saveGameSystem.Load(15, freshManager).empty());
    EXPECT_FALSE(saveGameSystem.GetSaveMetadata(15).has_value());
}

TEST_F(SaveGameSystemTest, SavePayloadMigrationRegistry_ValidatesRegistrationAndFailureAtomicity) {
    SavePayloadMigrationRegistry registry;
    EXPECT_EQ(registry.Register(1U, 1U, [](std::vector<std::byte>&, std::string&) { return true; }).code,
              SaveMigrationRegistrationCode::InvalidVersionRange);
    EXPECT_EQ(registry.Register(0U, 1U, {}).code, SaveMigrationRegistrationCode::MissingTransform);

    ASSERT_TRUE(registry.Register(0U, 1U, [](std::vector<std::byte>& payload, std::string&) {
        payload.push_back(std::byte{0x02});
        return true;
    }).IsAccepted());
    EXPECT_EQ(registry.Register(0U, 1U, [](std::vector<std::byte>&, std::string&) { return true; }).code,
              SaveMigrationRegistrationCode::DuplicateTransform);

    std::vector<std::byte> migratedPayload{std::byte{0x01}};
    const SaveMigrationDiagnostics migrated = registry.Migrate(0U, 1U, migratedPayload);
    EXPECT_EQ(migrated.status, SaveMigrationStatus::Migrated);
    ASSERT_EQ(migratedPayload.size(), 2U);
    EXPECT_EQ(migratedPayload[1], std::byte{0x02});

    ASSERT_TRUE(registry.Register(2U, 1U, [](std::vector<std::byte>& payload, std::string& reason) {
        payload.clear();
        reason = "legacy payload rejected";
        return false;
    }).IsAccepted());
    std::vector<std::byte> rejectedPayload{std::byte{0x03}};
    const SaveMigrationDiagnostics rejected = registry.Migrate(2U, 1U, rejectedPayload);
    EXPECT_EQ(rejected.status, SaveMigrationStatus::InvalidPayload);
    EXPECT_EQ(rejectedPayload, std::vector<std::byte>{std::byte{0x03}});
    EXPECT_EQ(registry.GetTransformCount(), 2U);

    for (std::uint32_t index = 0U; index < kMaximumSaveMigrationTransforms - 2U; ++index) {
        ASSERT_TRUE(registry.Register(100U + index, 200U + index,
                                         [](std::vector<std::byte>& payload, std::string&) {
                                             payload.push_back(std::byte{0x04});
                                             return true;
                                         }).IsAccepted());
    }
    EXPECT_EQ(registry.GetTransformCount(), kMaximumSaveMigrationTransforms);
    EXPECT_EQ(registry.Register(999U, 1000U, [](std::vector<std::byte>&, std::string&) { return true; }).code,
              SaveMigrationRegistrationCode::CapacityExceeded);
}

TEST_F(SaveGameSystemTest, SavePayloadMigrationRegistry_ComposesDeterministicMultiHopPath) {
    SavePayloadMigrationRegistry registry;
    ASSERT_TRUE(registry.Register(0U, 1U, [](std::vector<std::byte>& payload, std::string&) {
        payload.push_back(std::byte{0x02});
        return true;
    }).IsAccepted());
    ASSERT_TRUE(registry.Register(1U, 2U, [](std::vector<std::byte>& payload, std::string&) {
        payload.push_back(std::byte{0x03});
        return true;
    }).IsAccepted());

    std::vector<std::byte> payload{std::byte{0x01}};
    const SaveMigrationDiagnostics diagnostics = registry.Migrate(0U, 2U, payload);

    EXPECT_EQ(diagnostics.status, SaveMigrationStatus::Migrated);
    EXPECT_EQ(diagnostics.appliedStepCount, 2U);
    EXPECT_EQ(payload, (std::vector<std::byte>{std::byte{0x01}, std::byte{0x02}, std::byte{0x03}}));
}

TEST_F(SaveGameSystemTest, SavePayloadMigrationRegistry_ChoosesShortestRegistrationOrderPath) {
    SavePayloadMigrationRegistry registry;
    ASSERT_TRUE(registry.Register(0U, 1U, [](std::vector<std::byte>& payload, std::string&) {
        payload.push_back(std::byte{0x01});
        return true;
    }).IsAccepted());
    ASSERT_TRUE(registry.Register(1U, 3U, [](std::vector<std::byte>& payload, std::string&) {
        payload.push_back(std::byte{0x13});
        return true;
    }).IsAccepted());
    ASSERT_TRUE(registry.Register(0U, 2U, [](std::vector<std::byte>& payload, std::string&) {
        payload.push_back(std::byte{0x02});
        return true;
    }).IsAccepted());
    ASSERT_TRUE(registry.Register(2U, 3U, [](std::vector<std::byte>& payload, std::string&) {
        payload.push_back(std::byte{0x23});
        return true;
    }).IsAccepted());
    ASSERT_TRUE(registry.Register(0U, 3U, [](std::vector<std::byte>& payload, std::string&) {
        payload.push_back(std::byte{0x33});
        return true;
    }).IsAccepted());

    std::vector<std::byte> payload{std::byte{0x00}};
    const SaveMigrationDiagnostics diagnostics = registry.Migrate(0U, 3U, payload);

    EXPECT_EQ(diagnostics.status, SaveMigrationStatus::Migrated);
    EXPECT_EQ(diagnostics.appliedStepCount, 1U);
    EXPECT_EQ(payload, (std::vector<std::byte>{std::byte{0x00}, std::byte{0x33}}));
}

TEST_F(SaveGameSystemTest, SavePayloadMigrationRegistry_MultiHopFailureIsAtomic) {
    SavePayloadMigrationRegistry registry;
    ASSERT_TRUE(registry.Register(0U, 1U, [](std::vector<std::byte>& payload, std::string&) {
        payload.push_back(std::byte{0x02});
        return true;
    }).IsAccepted());
    ASSERT_TRUE(registry.Register(1U, 2U, [](std::vector<std::byte>& payload, std::string& reason) {
        payload.push_back(std::byte{0x03});
        reason = "second migration rejected";
        return false;
    }).IsAccepted());

    const std::vector<std::byte> originalPayload{std::byte{0x01}};
    std::vector<std::byte> payload = originalPayload;
    const SaveMigrationDiagnostics diagnostics = registry.Migrate(0U, 2U, payload);

    EXPECT_EQ(diagnostics.status, SaveMigrationStatus::InvalidPayload);
    EXPECT_EQ(diagnostics.appliedStepCount, 1U);
    EXPECT_EQ(payload, originalPayload);
    EXPECT_EQ(diagnostics.reason, "second migration rejected");
}

TEST_F(SaveGameSystemTest, SavePayloadMigrationRegistry_RejectsTransformOutputOverCapAtomically) {
    SavePayloadMigrationRegistry registry;
    ASSERT_TRUE(registry.Register(0U, 1U, [](std::vector<std::byte>& payload, std::string&) {
        payload.resize(kMaximumSaveMigrationPayloadBytes + 1U, std::byte{0x7F});
        return true;
    }).IsAccepted());

    const std::vector<std::byte> originalPayload{std::byte{0x01}};
    std::vector<std::byte> payload = originalPayload;
    const SaveMigrationDiagnostics diagnostics = registry.Migrate(0U, 1U, payload);

    EXPECT_EQ(diagnostics.status, SaveMigrationStatus::InvalidPayload);
    EXPECT_EQ(diagnostics.appliedStepCount, 0U);
    EXPECT_EQ(payload, originalPayload);
    EXPECT_EQ(diagnostics.reason, "save migration transform exceeded migration cap");
}

TEST_F(SaveGameSystemTest, SavePayloadMigrationRegistry_RejectsOversizedCurrentVersionInputAtomically) {
    SavePayloadMigrationRegistry registry;
    std::vector<std::byte> payload(kMaximumSaveMigrationPayloadBytes + 1U, std::byte{0x5A});
    const std::byte originalFirstByte = payload.front();

    const SaveMigrationDiagnostics diagnostics =
        registry.Migrate(kCurrentSavePayloadSchemaVersion, kCurrentSavePayloadSchemaVersion, payload);

    EXPECT_EQ(diagnostics.status, SaveMigrationStatus::InvalidPayload);
    EXPECT_EQ(diagnostics.appliedStepCount, 0U);
    EXPECT_EQ(diagnostics.reason, "save payload exceeds migration cap");
    ASSERT_EQ(payload.size(), kMaximumSaveMigrationPayloadBytes + 1U);
    EXPECT_EQ(payload.front(), originalFirstByte);
}

TEST_F(SaveGameSystemTest, SaveGameSystem_LoadsRegisteredVersionTransformAtomically) {
    const Entity entity = entityManager.CreateEntity();
    ASSERT_TRUE(saveGameSystem.Save(15, entityManager, {entity}, GameStateMetadata{}));

    const std::filesystem::path slotPath = saveDirectory / "slot_15.uvsave";
    std::optional<std::pair<Asset::UveFileHeader, std::vector<std::byte>>> file =
        Asset::ReadUveFile(slotPath);
    ASSERT_TRUE(file.has_value());
    std::vector<std::byte> payload = file->second;
    ASSERT_GE(payload.size(), sizeof(std::uint32_t));
    std::uint32_t metadataLength = 0U;
    std::memcpy(&metadataLength, payload.data(), sizeof(metadataLength));
    ASSERT_GT(metadataLength, 0U);
    ASSERT_LE(sizeof(metadataLength) + metadataLength, payload.size());
    std::string metadataText(reinterpret_cast<const char*>(payload.data() + sizeof(metadataLength)), metadataLength);
    const std::string currentVersionToken = "\"payloadSchemaVersion\":1";
    const std::size_t tokenOffset = metadataText.find(currentVersionToken);
    ASSERT_NE(tokenOffset, std::string::npos);
    metadataText.replace(tokenOffset, currentVersionToken.size(), "\"payloadSchemaVersion\":0");
    std::memcpy(payload.data() + sizeof(metadataLength), metadataText.data(), metadataLength);
    ASSERT_TRUE(Asset::WriteUveFile(slotPath, Asset::AssetKind::Save, payload));

    ASSERT_TRUE(saveGameSystem.RegisterMigration(0U, 1U, [](std::vector<std::byte>& payloadBytes, std::string& reason) {
        const std::string oldToken = "\"payloadSchemaVersion\":0";
        const std::string newToken = "\"payloadSchemaVersion\":1";
        const std::string text(reinterpret_cast<const char*>(payloadBytes.data()), payloadBytes.size());
        const std::size_t offset = text.find(oldToken);
        if (offset == std::string::npos) {
            reason = "legacy metadata schema token is missing";
            return false;
        }
        std::string migrated = text;
        migrated.replace(offset, oldToken.size(), newToken);
        const auto* bytes = reinterpret_cast<const std::byte*>(migrated.data());
        payloadBytes.assign(bytes, bytes + migrated.size());
        return true;
    }).IsAccepted());

    EntityManager loadedManager(memoryManager.GetDefaultAllocator(), eventSystem);
    ASSERT_FALSE(saveGameSystem.Load(15, loadedManager).empty());
    const SaveMigrationDiagnostics diagnostics = saveGameSystem.GetLastMigrationDiagnostics();
    EXPECT_EQ(diagnostics.status, SaveMigrationStatus::Migrated);
    EXPECT_EQ(diagnostics.sourceSchemaVersion, 0U);
    EXPECT_EQ(diagnostics.targetSchemaVersion, 1U);
}

TEST_F(SaveGameSystemTest, LegacyMetadataMigrationRunsBeforeDecodeForLoadAndMetadata) {
    const Entity entity = entityManager.CreateEntity();
    GameStateMetadata metadata;
    metadata.playtimeSeconds = 321.5;
    ASSERT_TRUE(saveGameSystem.Save(16, entityManager, {entity}, metadata));

    const std::filesystem::path slotPath = saveDirectory / "slot_16.uvsave";
    std::optional<std::pair<Asset::UveFileHeader, std::vector<std::byte>>> file =
        Asset::ReadUveFile(slotPath);
    ASSERT_TRUE(file.has_value());
    std::vector<std::byte> payload = file->second;
    ASSERT_GE(payload.size(), sizeof(std::uint32_t));
    std::uint32_t metadataLength = 0U;
    std::memcpy(&metadataLength, payload.data(), sizeof(metadataLength));
    ASSERT_GT(metadataLength, 0U);
    ASSERT_LE(sizeof(metadataLength) + metadataLength, payload.size());
    std::string metadataText(reinterpret_cast<const char*>(payload.data() + sizeof(metadataLength)), metadataLength);
    const std::string currentVersionToken = "\"payloadSchemaVersion\":1";
    const std::size_t versionOffset = metadataText.find(currentVersionToken);
    ASSERT_NE(versionOffset, std::string::npos);
    metadataText.replace(versionOffset, currentVersionToken.size(), "\"payloadSchemaVersion\":0");
    const std::string currentPlaytimeKey = "\"playtimeSeconds\"";
    const std::string legacyPlaytimeKey = "\"legacyTimeValue\"";
    const std::size_t playtimeOffset = metadataText.find(currentPlaytimeKey);
    ASSERT_NE(playtimeOffset, std::string::npos);
    ASSERT_EQ(currentPlaytimeKey.size(), legacyPlaytimeKey.size());
    metadataText.replace(playtimeOffset, currentPlaytimeKey.size(), legacyPlaytimeKey);
    ASSERT_EQ(metadataText.size(), metadataLength);
    std::memcpy(payload.data() + sizeof(metadataLength), metadataText.data(), metadataLength);
    ASSERT_TRUE(Asset::WriteUveFile(slotPath, Asset::AssetKind::Save, payload));

    ASSERT_TRUE(saveGameSystem.RegisterMigration(0U, 1U, [](std::vector<std::byte>& payloadBytes, std::string& reason) {
        std::string text(reinterpret_cast<const char*>(payloadBytes.data()), payloadBytes.size());
        const std::string oldVersionToken = "\"payloadSchemaVersion\":0";
        const std::string newVersionToken = "\"payloadSchemaVersion\":1";
        const std::size_t migratedVersionOffset = text.find(oldVersionToken);
        const std::size_t legacyPlaytimeOffset = text.find("\"legacyTimeValue\"");
        if (migratedVersionOffset == std::string::npos || legacyPlaytimeOffset == std::string::npos) {
            reason = "legacy metadata fields are missing from the framed payload";
            return false;
        }
        text.replace(migratedVersionOffset, oldVersionToken.size(), newVersionToken);
        text.replace(legacyPlaytimeOffset, std::string{"\"legacyTimeValue\""}.size(),
                     "\"playtimeSeconds\"");
        const auto* bytes = reinterpret_cast<const std::byte*>(text.data());
        payloadBytes.assign(bytes, bytes + text.size());
        return true;
    }).IsAccepted());

    const std::optional<GameStateMetadata> migratedMetadata = saveGameSystem.GetSaveMetadata(16);
    ASSERT_TRUE(migratedMetadata.has_value());
    EXPECT_DOUBLE_EQ(migratedMetadata->playtimeSeconds, 321.5);
    EXPECT_EQ(saveGameSystem.GetLastMigrationDiagnostics().status, SaveMigrationStatus::Migrated);

    EntityManager loadedManager(memoryManager.GetDefaultAllocator(), eventSystem);
    ASSERT_FALSE(saveGameSystem.Load(16, loadedManager).empty());
    EXPECT_EQ(saveGameSystem.GetLastMigrationDiagnostics().status, SaveMigrationStatus::Migrated);
}

TEST_F(SaveGameSystemTest, Load_SerializerExceptionReturnsEmptyAndCleansScratchFile) {
    const Entity entity = entityManager.CreateEntity();
    ASSERT_TRUE(saveGameSystem.Save(22, entityManager, {entity}, GameStateMetadata{}));

    CountingSceneSerializer throwingSerializer;
    throwingSerializer.throwOnLoad = true;
    SaveGameSystem throwingLoadSystem(throwingSerializer, saveDirectory);
    EntityManager loadedManager(memoryManager.GetDefaultAllocator(), eventSystem);
    EXPECT_TRUE(throwingLoadSystem.Load(22, loadedManager).empty());
    EXPECT_FALSE(std::filesystem::exists(saveDirectory / ".slot_22_scratch.uvscene"));
}

TEST_F(SaveGameSystemTest, SaveThenLoad_CurrentSchemaReportsNoMigrationRequired) {
    const Entity entity = entityManager.CreateEntity();
    ASSERT_TRUE(saveGameSystem.Save(13, entityManager, {entity}, GameStateMetadata{}));

    EntityManager loadedManager(memoryManager.GetDefaultAllocator(), eventSystem);
    ASSERT_FALSE(saveGameSystem.Load(13, loadedManager).empty());
    const SaveMigrationDiagnostics diagnostics = saveGameSystem.GetLastMigrationDiagnostics();
    EXPECT_EQ(diagnostics.status, SaveMigrationStatus::NotRequired);
    EXPECT_EQ(diagnostics.sourceSchemaVersion, kCurrentSavePayloadSchemaVersion);
    EXPECT_EQ(diagnostics.targetSchemaVersion, kCurrentSavePayloadSchemaVersion);
    EXPECT_TRUE(diagnostics.reason.empty());
}

TEST_F(SaveGameSystemTest, Load_UnsupportedSchemaReportsBoundedMigrationDiagnostics) {
    const Entity entity = entityManager.CreateEntity();
    ASSERT_TRUE(saveGameSystem.Save(14, entityManager, {entity}, GameStateMetadata{}));

    const std::filesystem::path slotPath = saveDirectory / "slot_14.uvsave";
    std::optional<std::pair<Asset::UveFileHeader, std::vector<std::byte>>> file =
        Asset::ReadUveFile(slotPath);
    ASSERT_TRUE(file.has_value());
    std::vector<std::byte> payload = file->second;
    ASSERT_GE(payload.size(), sizeof(std::uint32_t));

    std::uint32_t metadataLength = 0U;
    std::memcpy(&metadataLength, payload.data(), sizeof(metadataLength));
    ASSERT_GT(metadataLength, 0U);
    ASSERT_LE(sizeof(metadataLength) + metadataLength, payload.size());
    std::string metadataText(reinterpret_cast<const char*>(payload.data() + sizeof(metadataLength)), metadataLength);
    const std::string currentVersionToken = "\"payloadSchemaVersion\":1";
    const std::size_t tokenOffset = metadataText.find(currentVersionToken);
    ASSERT_NE(tokenOffset, std::string::npos);
    metadataText.replace(tokenOffset, currentVersionToken.size(), "\"payloadSchemaVersion\":9");
    ASSERT_EQ(metadataText.size(), metadataLength);
    std::memcpy(payload.data() + sizeof(metadataLength), metadataText.data(), metadataLength);
    ASSERT_TRUE(Asset::WriteUveFile(slotPath, Asset::AssetKind::Save, payload));

    EntityManager freshManager(memoryManager.GetDefaultAllocator(), eventSystem);
    EXPECT_TRUE(saveGameSystem.Load(14, freshManager).empty());
    SaveMigrationDiagnostics diagnostics = saveGameSystem.GetLastMigrationDiagnostics();
    EXPECT_EQ(diagnostics.status, SaveMigrationStatus::UnsupportedSourceVersion);
    EXPECT_EQ(diagnostics.sourceSchemaVersion, 9U);
    EXPECT_EQ(diagnostics.targetSchemaVersion, kCurrentSavePayloadSchemaVersion);
    EXPECT_FALSE(diagnostics.reason.empty());

    EXPECT_FALSE(saveGameSystem.GetSaveMetadata(14).has_value());
    diagnostics = saveGameSystem.GetLastMigrationDiagnostics();
    EXPECT_EQ(diagnostics.status, SaveMigrationStatus::UnsupportedSourceVersion);
    EXPECT_EQ(diagnostics.sourceSchemaVersion, 9U);
}

TEST_F(SaveGameSystemTest, Save_RejectsWrongScratchAssetTypeBeforePublication) {
    const Entity entity = entityManager.CreateEntity();
    const std::filesystem::path finalPath = saveDirectory / "slot_14.uvsave";
    ASSERT_TRUE(saveGameSystem.Save(14, entityManager, {entity}, GameStateMetadata{}));
    const auto priorFile = Asset::ReadUveFile(finalPath);
    ASSERT_TRUE(priorFile.has_value());

    CountingSceneSerializer wrongTypeSerializer;
    wrongTypeSerializer.scratchPayloadBytes = 32U;
    wrongTypeSerializer.scratchAssetType = Scene::SceneAssetType::Prefab;
    SaveGameSystem wrongTypeSystem(wrongTypeSerializer, saveDirectory);

    EXPECT_FALSE(wrongTypeSystem.Save(14, entityManager, {entity}, GameStateMetadata{}));
    EXPECT_EQ(wrongTypeSerializer.saveCallCount, 1);
    const auto afterFile = Asset::ReadUveFile(finalPath);
    ASSERT_TRUE(afterFile.has_value());
    EXPECT_EQ(afterFile->second, priorFile->second);
    EXPECT_FALSE(std::filesystem::exists(saveDirectory / ".slot_14_scratch.uvscene"));
    EXPECT_FALSE(std::filesystem::exists(saveDirectory / "slot_14.uvsave.tmp"));
}

TEST_F(SaveGameSystemTest, Save_OversizedCompressedPayloadPreservesPriorSaveAndCleansScratch) {
    const Entity entity = entityManager.CreateEntity();
    const std::filesystem::path finalPath = saveDirectory / "slot_15.uvsave";
    ASSERT_TRUE(saveGameSystem.Save(15, entityManager, {entity}, GameStateMetadata{}));
    const auto priorFile = Asset::ReadUveFile(finalPath);
    ASSERT_TRUE(priorFile.has_value());

    CountingSceneSerializer oversizedSerializer;
    oversizedSerializer.scratchPayloadBytes = kMaximumCompressedSavePayloadBytes + 1U;
    SaveGameSystem oversizedSystem(oversizedSerializer, saveDirectory);

    EXPECT_FALSE(oversizedSystem.Save(15, entityManager, {entity}, GameStateMetadata{}));
    EXPECT_EQ(oversizedSerializer.saveCallCount, 1);
    const auto afterFile = Asset::ReadUveFile(finalPath);
    ASSERT_TRUE(afterFile.has_value());
    EXPECT_EQ(afterFile->second, priorFile->second);
    EXPECT_FALSE(std::filesystem::exists(saveDirectory / ".slot_15_scratch.uvscene"));
    EXPECT_FALSE(std::filesystem::exists(saveDirectory / "slot_15.uvsave.tmp"));
}

TEST_F(SaveGameSystemTest, Save_SerializerExceptionPreservesPriorSaveAndCleansScratch) {
    const Entity entity = entityManager.CreateEntity();
    const std::filesystem::path finalPath = saveDirectory / "slot_15.uvsave";
    ASSERT_TRUE(saveGameSystem.Save(15, entityManager, {entity}, GameStateMetadata{}));
    ASSERT_TRUE(std::filesystem::exists(finalPath));

    CountingSceneSerializer throwingSerializer;
    throwingSerializer.throwOnSave = true;
    SaveGameSystem throwingSystem(throwingSerializer, saveDirectory);
    EXPECT_FALSE(throwingSystem.Save(15, entityManager, {entity}, GameStateMetadata{}));
    EXPECT_EQ(throwingSerializer.saveCallCount, 1);
    EXPECT_TRUE(std::filesystem::exists(finalPath));
    EXPECT_FALSE(std::filesystem::exists(saveDirectory / ".slot_15_scratch.uvscene"));
    EXPECT_FALSE(std::filesystem::exists(saveDirectory / "slot_15.uvsave.tmp"));
}

TEST_F(SaveGameSystemTest, Save_NegativePlaytimeMetadataFailsBeforeDirectoryMutation) {
    GameStateMetadata metadata;
    metadata.playtimeSeconds = -1.0;
    const std::filesystem::path invalidMetadataDirectory = "uve_save_game_system_tests_invalid_metadata_negative";
    std::filesystem::remove_all(invalidMetadataDirectory);

    EXPECT_FALSE(saveGameSystem.Save(3, entityManager, {}, metadata));
    EXPECT_FALSE(std::filesystem::exists(invalidMetadataDirectory));
}

TEST_F(SaveGameSystemTest, Save_NonFinitePlaytimeMetadataFailsBeforeDirectoryMutation) {
    GameStateMetadata metadata;
    metadata.playtimeSeconds = std::numeric_limits<double>::quiet_NaN();
    const std::filesystem::path invalidMetadataDirectory = "uve_save_game_system_tests_invalid_metadata_nan";
    std::filesystem::remove_all(invalidMetadataDirectory);

    SaveGameSystem invalidMetadataSystem(sceneSerializer, invalidMetadataDirectory);
    EXPECT_FALSE(invalidMetadataSystem.Save(3, entityManager, {}, metadata));
    EXPECT_FALSE(std::filesystem::exists(invalidMetadataDirectory));
}

TEST_F(SaveGameSystemTest, Save_DirectoryCannotBeCreated_FailsWithoutLeavingAnyFile) {
    // Point the save directory at a path where a path component is a regular file rather than a
    // directory - directory creation fails structurally (ENOTDIR) regardless of privilege level
    // (unlike a permission-based failure, which root would bypass in this sandbox).
    const std::filesystem::path blockerFile = "uve_save_game_system_tests_blocker_file";
    {
        std::ofstream blocker(blockerFile);
        blocker << "not a directory";
    }
    const std::filesystem::path badSaveDirectory = blockerFile / "saves";
    SaveGameSystem brokenSaveGameSystem(sceneSerializer, badSaveDirectory);

    const Entity entity = entityManager.CreateEntity();
    EXPECT_FALSE(brokenSaveGameSystem.Save(3, entityManager, {entity}, GameStateMetadata{}));
    EXPECT_FALSE(std::filesystem::exists(badSaveDirectory));

    std::filesystem::remove(blockerFile);
}

TEST(SaveGameSystem, Save_DirectoryPreparationFailureSkipsSceneSerialization) {
    Memory::MemoryManager memoryManager;
    Events::EventSystem eventSystem;
    EntityManager entityManager{memoryManager.GetDefaultAllocator(), eventSystem};
    CountingSceneSerializer sceneSerializer;
    const std::filesystem::path blockerFile = "uve_save_game_system_tests_counting_blocker";
    {
        std::ofstream blocker(blockerFile);
        blocker << "not a directory";
    }

    SaveGameSystem saveGameSystem(sceneSerializer, blockerFile / "saves");
    const Entity entity = entityManager.CreateEntity();
    EXPECT_FALSE(saveGameSystem.Save(3, entityManager, {entity}, GameStateMetadata{}));
    EXPECT_EQ(sceneSerializer.saveCallCount, 0);

    std::filesystem::remove(blockerFile);
}

} // namespace
} // namespace UVE::Save::Tests
