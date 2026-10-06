// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#include "uve/scene/scene_serializer_uve.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <memory>
#include <map>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "uve/asset/asset_guid_uve.h"
#include "uve/asset/uve_file_envelope_uve.h"
#include "uve/logging/log_sink_uve.h"
#include "uve/logging/logger_uve.h"
#include "uve/events/event_system_uve.h"
#include "uve/math/vector3_uve.h"
#include "uve/memory/memory_manager_uve.h"
#include "uve/component/animation_sequencer_component_uve.h"
#include "uve/component/animation_graph_component_uve.h"
#include "uve/component/area_component_uve.h"
#include "uve/component/audio_source_component_uve.h"
#include "uve/component/camera_component_uve.h"
#include "uve/component/canvas_component_uve.h"
#include "uve/component/character_controller_component_uve.h"
#include "uve/component/collider_component_uve.h"
#include "uve/component/auto_translate_component_uve.h"
#include "uve/component/editor_description_component_uve.h"
#include "uve/component/object_metadata_component_uve.h"
#include "uve/component/process_component_uve.h"
#include "uve/component/thread_group_component_uve.h"
#include "uve/objects/3d/all_objects_3d_uve.h"
#include "uve/component/hierarchy_component_uve.h"
#include "uve/component/render_instance_component_uve.h"
#include "uve/component/physics_object_component_uve.h"
#include "uve/component/bone_modifier_component_uve.h"
#include "uve/component/light_component_uve.h"
#include "uve/component/mesh_component_uve.h"
#include "uve/component/name_component_uve.h"
#include "uve/component/particle_emitter_component_uve.h"
#include "uve/component/physics_interpolation_component_uve.h"
#include "uve/component/primitive_mesh_component_uve.h"
#include "uve/component/prefab_instance_component_uve.h"
#include "uve/component/rigid_3d_component_uve.h"
#include "uve/component/script_component_uve.h"
#include "uve/component/transform_component_uve.h"
#include "uve/component/visibility_component_uve.h"
#include "uve/component/ui_button_component_uve.h"
#include "uve/component/ui_image_component_uve.h"
#include "uve/component/ui_text_component_uve.h"
#include "uve/component/world_transform_component_uve.h"
#include "uve/component/light_emitter_component_uve.h"
#include "uve/component/surface_instance_component_uve.h"
#include "uve/objects/3d/decal_3d_uve.h"
#include "uve/objects/3d/fog_volume_3d_uve.h"
#include "uve/entity/entity_manager_uve.h"
#include "uve/scene/scene_graph_uve.h"
#include "uve/scene/objects/scene_root_uve.h"

namespace UVE::Scene::Tests {
namespace {

class SceneSerializerUVETest : public ::testing::Test {
protected:
    Memory::MemoryManagerUVE memoryManager;
    Events::EventSystemUVE eventSystem;
    EntityManagerUVE entityManager{memoryManager.GetDefaultAllocatorUVE(), eventSystem};
    SceneSerializerUVE serializer;
};

struct UnregisteredSnapshotComponentUVE final {
    int value = 0;
};

TEST_F(SceneSerializerUVETest, CaptureThenRestore_EmptyRootList_ReturnsValidEmptySnapshotWithoutMutation) {
    const std::size_t entityCountBefore = entityManager.GetEntityCountUVE();

    const std::optional<SceneSnapshotUVE> snapshot =
        serializer.CaptureUVE(entityManager, {}, SceneAssetTypeUVE::Scene);
    ASSERT_TRUE(snapshot.has_value());
    EXPECT_FALSE(snapshot->bytes.empty());
    EXPECT_EQ(snapshot->assetType, SceneAssetTypeUVE::Scene);

    const std::vector<EntityUVE> roots = serializer.RestoreUVE(entityManager, *snapshot);
    EXPECT_TRUE(roots.empty());
    EXPECT_EQ(entityManager.GetEntityCountUVE(), entityCountBefore);
}

TEST_F(SceneSerializerUVETest, CaptureThenRestore_NestedHierarchy_RecreatesFreshHandlesAndRelationships) {
    const EntityUVE sourceRoot = entityManager.CreateEntityUVE();
    entityManager.AddComponentUVE<TransformComponentUVE>(sourceRoot, TransformComponentUVE{});
    entityManager.AddComponentUVE<HierarchyComponentUVE>(sourceRoot, HierarchyComponentUVE{kInvalidEntityUVE});
    entityManager.AddComponentUVE<NameComponentUVE>(sourceRoot, NameComponentUVE{"Source Root"});

    const EntityUVE sourceChild = entityManager.CreateEntityUVE();
    TransformComponentUVE childTransform{};
    childTransform.localPosition = Math::Vector3UVE{2.0F, 3.0F, 4.0F};
    entityManager.AddComponentUVE<TransformComponentUVE>(sourceChild, childTransform);
    entityManager.AddComponentUVE<HierarchyComponentUVE>(sourceChild, HierarchyComponentUVE{sourceRoot});
    entityManager.AddComponentUVE<NameComponentUVE>(sourceChild, NameComponentUVE{"Source Child"});

    const std::optional<SceneSnapshotUVE> snapshot =
        serializer.CaptureUVE(entityManager, {sourceRoot}, SceneAssetTypeUVE::Scene);
    ASSERT_TRUE(snapshot.has_value());

    const std::vector<EntityUVE> restoredRoots = serializer.RestoreUVE(entityManager, *snapshot);
    ASSERT_EQ(restoredRoots.size(), 1U);
    const EntityUVE restoredRoot = restoredRoots.front();
    EXPECT_NE(restoredRoot, sourceRoot);
    EXPECT_EQ(entityManager.GetComponentUVE<NameComponentUVE>(restoredRoot).name, "Source Root");

    EntityUVE restoredChild = kInvalidEntityUVE;
    entityManager.ForEachUVE<HierarchyComponentUVE>(
        [&restoredChild, restoredRoot](const EntityUVE entity, HierarchyComponentUVE& hierarchy) {
            if (hierarchy.parent == restoredRoot) {
                restoredChild = entity;
            }
        });
    ASSERT_NE(restoredChild, kInvalidEntityUVE);
    EXPECT_NE(restoredChild, sourceChild);
    EXPECT_EQ(entityManager.GetComponentUVE<NameComponentUVE>(restoredChild).name, "Source Child");
    EXPECT_TRUE(entityManager.GetComponentUVE<TransformComponentUVE>(restoredChild).localPosition ==
                childTransform.localPosition);
    EXPECT_TRUE(entityManager.HasComponentUVE<WorldTransformComponentUVE>(restoredRoot));
    EXPECT_TRUE(entityManager.HasComponentUVE<WorldTransformComponentUVE>(restoredChild));
}

TEST_F(SceneSerializerUVETest, CaptureThenRestore_AllRegisteredComponentTypes_RoundTrip) {
    const EntityUVE source = entityManager.CreateEntityUVE();
    TransformComponentUVE transform{};
    transform.localPosition = Math::Vector3UVE{1.0F, 2.0F, 3.0F};
    entityManager.AddComponentUVE<TransformComponentUVE>(source, transform);
    entityManager.AddComponentUVE<MeshComponentUVE>(source,
                                                     MeshComponentUVE{Asset::AssetGuidUVE{11}, Asset::AssetGuidUVE{22}});
    entityManager.AddComponentUVE<PrimitiveMeshComponentUVE>(
        source, PrimitiveMeshComponentUVE{PrimitiveMeshKindUVE::UVSphere, Math::Vector3UVE{0.2F, 0.5F, 0.8F}});
    LightComponentUVE light{};
    light.type = LightTypeUVE::Spot;
    light.intensity = 4.0F;
    entityManager.AddComponentUVE<LightComponentUVE>(source, light);
    entityManager.AddComponentUVE<CameraComponentUVE>(source, CameraComponentUVE{75.0F, 0.2F, 250.0F});
    entityManager.AddComponentUVE<NameComponentUVE>(source, NameComponentUVE{"Complete Snapshot"});
    ColliderComponentUVE collider{};
    collider.friction = 0.25F;
    entityManager.AddComponentUVE<ColliderComponentUVE>(source, collider);
    Rigid3DComponentUVE body{};
    body.mass = 9.5F;
    body.isKinematic = true;
    entityManager.AddComponentUVE<Rigid3DComponentUVE>(source, body);
    AudioSourceComponentUVE audio{};
    audio.audioAssetPath = "sounds/lifecycle.wav";
    audio.looping = true;
    entityManager.AddComponentUVE<AudioSourceComponentUVE>(source, audio);
    entityManager.AddComponentUVE<ScriptComponentUVE>(
        source, ScriptComponentUVE{"scripts/lifecycle.lua", {{"speed", "9.5"}, {"label", "hero"}}});
    entityManager.AddComponentUVE<ParticleEmitterComponentUVE>(source, ParticleEmitterComponentUVE{128U});
    entityManager.AddComponentUVE<PrefabInstanceComponentUVE>(source,
                                                               PrefabInstanceComponentUVE{Asset::AssetGuidUVE{9001}, {}});
    PhysicsInterpolationComponentUVE interpolation{};
    interpolation.mode = PoseSmoothingUVE::Blended;
    // Populated pose state to prove it deliberately does NOT round-trip - only `mode` is authored.
    interpolation.hasPreviousPose = true;
    interpolation.currentPosition = Math::Vector3UVE{5.0F, 6.0F, 7.0F};
    entityManager.AddComponentUVE<PhysicsInterpolationComponentUVE>(source, interpolation);
    entityManager.AddComponentUVE<EditorDescriptionComponentUVE>(
        source, EditorDescriptionComponentUVE{"Why this object exists."});

    ProcessComponentUVE process{};
    process.mode = TickModeUVE::PausedOnly;
    process.priority = -5;
    process.physicsPriority = 12;
    // Populated resolved state, to prove it deliberately does NOT round-trip: the scene graph
    // recomputes it from the hierarchy on the first update after load.
    process.resolvedModeInHierarchy = TickModeUVE::Never;
    entityManager.AddComponentUVE<ProcessComponentUVE>(source, process);
    ThreadGroupComponentUVE threadGroup{};
    threadGroup.mode = ThreadGroupModeUVE::SubThread;
    threadGroup.order = 3;
    threadGroup.resolvedModeInHierarchy = ThreadGroupModeUVE::SubThread;
    entityManager.AddComponentUVE<ThreadGroupComponentUVE>(source, threadGroup);
    AutoTranslateComponentUVE autoTranslate{};
    autoTranslate.mode = LocalizeModeUVE::Literal;
    entityManager.AddComponentUVE<AutoTranslateComponentUVE>(source, autoTranslate);
    ObjectMetadataComponentUVE objectMetadata{};
    // One value of each shape the codec handles differently: a scalar, text, a vector, a colour
    // with alpha, a packed array, and a dictionary holding an array - so nesting is covered too.
    Core::VariantUVE spawnOffset = Core::VariantUVE::MakeDefaultUVE(Core::VariantTypeUVE::Vector3);
    *spawnOffset.TryGetMutableUVE<Math::Vector3UVE>() = {1.5F, -2.0F, 0.25F};
    Core::VariantUVE tint = Core::VariantUVE::MakeDefaultUVE(Core::VariantTypeUVE::Color);
    *tint.TryGetMutableUVE<Core::VariantColorUVE>() = {0.1F, 0.2F, 0.3F, 0.5F};
    Core::VariantUVE bytes = Core::VariantUVE::MakeDefaultUVE(Core::VariantTypeUVE::ByteArray);
    *bytes.TryGetMutableUVE<std::vector<std::uint8_t>>() = {0U, 127U, 255U};
    Core::VariantUVE waves = Core::VariantUVE::MakeDefaultUVE(Core::VariantTypeUVE::Array);
    waves.TryGetMutableUVE<std::vector<Core::VariantUVE>>()->push_back(Core::VariantUVE::MakeIntUVE(4));
    waves.TryGetMutableUVE<std::vector<Core::VariantUVE>>()->push_back(Core::VariantUVE::MakeFloatUVE(0.1));
    Core::VariantUVE loot = Core::VariantUVE::MakeDefaultUVE(Core::VariantTypeUVE::Dictionary);
    loot.TryGetMutableUVE<std::vector<Core::VariantDictionaryEntryUVE>>()->push_back({"waves", waves});
    objectMetadata.entries = {
        {"door", Core::VariantUVE::MakeTextUVE(Core::VariantTypeUVE::ObjectPath, "Level/Doors/North")},
        {"charges", Core::VariantUVE::MakeIntUVE(3)},
        {"spawn_offset", spawnOffset},
        {"tint", tint},
        {"bytes", bytes},
        {"loot", loot},
    };
    entityManager.AddComponentUVE<ObjectMetadataComponentUVE>(source, objectMetadata);

    AreaComponentUVE area{};
    area.monitoring = false;
    area.monitorable = false;
    entityManager.AddComponentUVE<AreaComponentUVE>(source, area);
    RayCast3DComponentUVE ray;
    ray.length = 42.0F;
    entityManager.AddComponentUVE<RayCast3DComponentUVE>(source, ray);
    entityManager.AddComponentUVE<Kinematic3DComponentUVE>(
        source, Kinematic3DComponentUVE{Math::Vector3UVE{1.0F, 0.0F, 0.0F}, 0.75F, true});
    NavMeshVolume3DComponentUVE navigationRegion;
    navigationRegion.navigationMeshAssetPath = "navigation/courtyard.uvnav";
    navigationRegion.boundsHalfExtents = Math::Vector3UVE{12.0F, 3.0F, 9.0F};
    navigationRegion.navigationLayers = 0x5U;
    navigationRegion.cellSize = 0.25F;
    navigationRegion.agentRadius = 0.35F;
    navigationRegion.agentHeight = 1.6F;
    navigationRegion.maximumSlopeDegrees = 38.0F;
    navigationRegion.maximumStepHeight = 0.3F;
    entityManager.AddComponentUVE<NavMeshVolume3DComponentUVE>(source, navigationRegion);
    NavSeeker3DComponentUVE navigationAgent;
    navigationAgent.targetPosition = Math::Vector3UVE{8.0F, 0.0F, -4.0F};
    navigationAgent.radius = 0.35F;
    navigationAgent.height = 1.6F;
    navigationAgent.maxSpeed = 5.5F;
    navigationAgent.acceleration = 12.0F;
    navigationAgent.pathUpdateInterval = 0.25F;
    navigationAgent.waypointRadius = 0.6F;
    navigationAgent.targetTolerance = 1.2F;
    navigationAgent.slowDownRadius = 2.5F;
    navigationAgent.avoidanceRadius = 3.5F;
    navigationAgent.avoidanceEnabled = false;
    navigationAgent.navigationLayers = 0x5U;
    // The route state is deliberately left at values the step would never produce: none of it may
    // reach the file, so what a load brings back has to be the defaults, not these.
    navigationAgent.nextPathPosition = Math::Vector3UVE{-1.0F, -2.0F, -3.0F};
    navigationAgent.desiredVelocity = Math::Vector3UVE{7.0F, 7.0F, 7.0F};
    navigationAgent.pathStatus = NavigationAgentPathStatusUVE::Finished;
    navigationAgent.pathChanged = true;
    navigationAgent.targetReached = true;
    entityManager.AddComponentUVE<NavSeeker3DComponentUVE>(source, navigationAgent);
    Skeleton3DComponentUVE skeleton;
    skeleton.bones.push_back(SkeletonBoneUVE{"root", -1, {}, {}, {1.0F, 1.0F, 1.0F}});
    entityManager.AddComponentUVE<Skeleton3DComponentUVE>(source, skeleton);
    BoneAttachment3DComponentUVE attachment;
    attachment.boneName = "root";
    entityManager.AddComponentUVE<BoneAttachment3DComponentUVE>(source, attachment);
    SpringArm3DComponentUVE springArm;
    springArm.armLength = 6.0F;
    springArm.currentLength = 6.0F;
    entityManager.AddComponentUVE<SpringArm3DComponentUVE>(source, springArm);
    entityManager.AddComponentUVE<Marker3DComponentUVE>(source, Marker3DComponentUVE{});
    Hitbox3DComponentUVE hitbox;
    hitbox.damageChannel = "melee";
    entityManager.AddComponentUVE<Hitbox3DComponentUVE>(source, hitbox);
    Hurtbox3DComponentUVE hurtbox;
    hurtbox.damageChannel = "player";
    entityManager.AddComponentUVE<Hurtbox3DComponentUVE>(source, hurtbox);
    Projectile3DComponentUVE projectile;
    projectile.velocity = Math::Vector3UVE{0.0F, 0.0F, 20.0F};
    entityManager.AddComponentUVE<Projectile3DComponentUVE>(source, projectile);
    InteractionArea3DComponentUVE interaction;
    interaction.interactionTag = "door";
    entityManager.AddComponentUVE<InteractionArea3DComponentUVE>(source, interaction);
    WorldEnvironment3DComponentUVE environment;
    environment.skyAssetPath = "environment/day.uesky";
    environment.fogEnabled = true;
    environment.fogDensity = 0.02F;
    entityManager.AddComponentUVE<WorldEnvironment3DComponentUVE>(source, environment);
    ReflectionProbe3DComponentUVE probe;
    probe.updateMode = ReflectionProbeUpdateModeUVE::OnDemand;
    entityManager.AddComponentUVE<ReflectionProbe3DComponentUVE>(source, probe);
    Decal3DComponentUVE decal;
    decal.materialAssetPath = "materials/warning.uemat";
    entityManager.AddComponentUVE<Decal3DComponentUVE>(source, decal);
    entityManager.AddComponentUVE<LodGroup3DComponentUVE>(source, LodGroup3DComponentUVE{});
    entityManager.AddComponentUVE<Occluder3DComponentUVE>(source, Occluder3DComponentUVE{});
    entityManager.AddComponentUVE<VisibilityRegion3DComponentUVE>(source, VisibilityRegion3DComponentUVE{});
    SpawnPoint3DComponentUVE spawn;
    spawn.spawnTag = "player_start";
    entityManager.AddComponentUVE<SpawnPoint3DComponentUVE>(source, spawn);
    PlayerComponentUVE player;
    player.lookSensitivity = 0.2F;
    entityManager.AddComponentUVE<PlayerComponentUVE>(source, player);
    HealthComponentUVE health;
    health.maxHealth = 75.0F;
    health.health = 12.0F;
    health.invulnerable = true;
    entityManager.AddComponentUVE<HealthComponentUVE>(source, health);
    LevelStreamer3DComponentUVE streamer;
    streamer.levelPath = "levels/courtyard.uvscene";
    streamer.enabled = true;
    entityManager.AddComponentUVE<LevelStreamer3DComponentUVE>(source, streamer);
    entityManager.AddComponentUVE<WorldPartition3DComponentUVE>(source, WorldPartition3DComponentUVE{});

    const std::optional<SceneSnapshotUVE> snapshot =
        serializer.CaptureUVE(entityManager, {source}, SceneAssetTypeUVE::Scene);
    ASSERT_TRUE(snapshot.has_value());
    const std::vector<EntityUVE> restoredRoots = serializer.RestoreUVE(entityManager, *snapshot);
    ASSERT_EQ(restoredRoots.size(), 1U);
    const EntityUVE restored = restoredRoots.front();

    EXPECT_NE(restored, source);
    EXPECT_TRUE(entityManager.GetComponentUVE<TransformComponentUVE>(restored).localPosition == transform.localPosition);
    EXPECT_EQ(entityManager.GetComponentUVE<MeshComponentUVE>(restored).meshGuid, Asset::AssetGuidUVE{11});
    ASSERT_TRUE(entityManager.HasComponentUVE<PrimitiveMeshComponentUVE>(restored));
    EXPECT_EQ(entityManager.GetComponentUVE<PrimitiveMeshComponentUVE>(restored).kind,
              PrimitiveMeshKindUVE::UVSphere);
    EXPECT_EQ(entityManager.GetComponentUVE<PrimitiveMeshComponentUVE>(restored).baseColor,
              (Math::Vector3UVE{0.2F, 0.5F, 0.8F}));
    EXPECT_EQ(entityManager.GetComponentUVE<LightComponentUVE>(restored).type, LightTypeUVE::Spot);
    EXPECT_FLOAT_EQ(entityManager.GetComponentUVE<CameraComponentUVE>(restored).farPlane, 250.0F);
    EXPECT_EQ(entityManager.GetComponentUVE<NameComponentUVE>(restored).name, "Complete Snapshot");
    EXPECT_FLOAT_EQ(entityManager.GetComponentUVE<ColliderComponentUVE>(restored).friction, 0.25F);
    EXPECT_FLOAT_EQ(entityManager.GetComponentUVE<Rigid3DComponentUVE>(restored).mass, 9.5F);
    EXPECT_TRUE(entityManager.GetComponentUVE<Rigid3DComponentUVE>(restored).isKinematic);
    EXPECT_EQ(entityManager.GetComponentUVE<AudioSourceComponentUVE>(restored).audioAssetPath,
              "sounds/lifecycle.wav");
    EXPECT_TRUE(entityManager.GetComponentUVE<AudioSourceComponentUVE>(restored).looping);
    EXPECT_EQ(entityManager.GetComponentUVE<ScriptComponentUVE>(restored).scriptAssetPath, "scripts/lifecycle.lua");
    EXPECT_EQ(entityManager.GetComponentUVE<ScriptComponentUVE>(restored).exportValues,
              (std::map<std::string, std::string>{{"label", "hero"}, {"speed", "9.5"}}));
    EXPECT_EQ(entityManager.GetComponentUVE<ParticleEmitterComponentUVE>(restored).maxParticles, 128U);
    EXPECT_EQ(entityManager.GetComponentUVE<PrefabInstanceComponentUVE>(restored).sourcePrefabGuid,
              Asset::AssetGuidUVE{9001});
    ASSERT_TRUE(entityManager.HasComponentUVE<PhysicsInterpolationComponentUVE>(restored));
    EXPECT_EQ(entityManager.GetComponentUVE<PhysicsInterpolationComponentUVE>(restored).mode,
              PoseSmoothingUVE::Blended);
    // The pose fields are runtime state and must NOT survive the round trip: a restored entity
    // starts as if freshly spawned, never with a possibly-stale pose from whatever session wrote
    // the file.
    EXPECT_FALSE(entityManager.GetComponentUVE<PhysicsInterpolationComponentUVE>(restored).hasPreviousPose);
    EXPECT_EQ(entityManager.GetComponentUVE<PhysicsInterpolationComponentUVE>(restored).currentPosition,
              Math::Vector3UVE{});
    EXPECT_EQ(entityManager.GetComponentUVE<EditorDescriptionComponentUVE>(restored).description,
              "Why this object exists.");

    ASSERT_TRUE(entityManager.HasComponentUVE<ProcessComponentUVE>(restored));
    EXPECT_EQ(entityManager.GetComponentUVE<ProcessComponentUVE>(restored).mode, TickModeUVE::PausedOnly);
    EXPECT_EQ(entityManager.GetComponentUVE<ProcessComponentUVE>(restored).priority, -5);
    EXPECT_EQ(entityManager.GetComponentUVE<ProcessComponentUVE>(restored).physicsPriority, 12);
    // Same rule as the interpolation pose above: the resolved answer is derived from the hierarchy
    // every update, so persisting it would restore something already being replaced.
    EXPECT_EQ(entityManager.GetComponentUVE<ProcessComponentUVE>(restored).resolvedModeInHierarchy,
              ProcessComponentUVE{}.resolvedModeInHierarchy);
    EXPECT_EQ(entityManager.GetComponentUVE<ThreadGroupComponentUVE>(restored).mode,
              ThreadGroupModeUVE::SubThread);
    EXPECT_EQ(entityManager.GetComponentUVE<ThreadGroupComponentUVE>(restored).order, 3);
    EXPECT_EQ(entityManager.GetComponentUVE<ThreadGroupComponentUVE>(restored).resolvedModeInHierarchy,
              ThreadGroupComponentUVE{}.resolvedModeInHierarchy);
    EXPECT_EQ(entityManager.GetComponentUVE<AutoTranslateComponentUVE>(restored).mode,
              LocalizeModeUVE::Literal);
    // Authored order survives, which is why the entries serialize as an array rather than as a
    // JSON object whose member order a reader is not obliged to keep.
    // Every value, type included, comes back exactly: a ObjectPath stays a ObjectPath rather than
    // decaying into a plain string, and the float inside the nested array keeps every digit.
    EXPECT_EQ(entityManager.GetComponentUVE<ObjectMetadataComponentUVE>(restored).entries, objectMetadata.entries);
    EXPECT_FALSE(entityManager.GetComponentUVE<AreaComponentUVE>(restored).monitoring);
    EXPECT_FALSE(entityManager.GetComponentUVE<AreaComponentUVE>(restored).monitorable);
    EXPECT_FLOAT_EQ(entityManager.GetComponentUVE<RayCast3DComponentUVE>(restored).length, 42.0F);
    // Exclusions are entity references and are round-tripped by the tests below, which save the
    // objects they name alongside the ray. Here the point is that an unauthored list comes back
    // empty - eight empty slots - rather than as index 0 written out eight times.
    EXPECT_EQ(CountRayCast3DExclusionsUVE(entityManager.GetComponentUVE<RayCast3DComponentUVE>(restored)), 0U);
    // Both navigation objects come back whole: the region's bake settings and layers are authored
    // data, and the agent's are the measurements and schedule the step reads.
    const NavMeshVolume3DComponentUVE& restoredRegion =
        entityManager.GetComponentUVE<NavMeshVolume3DComponentUVE>(restored);
    EXPECT_EQ(restoredRegion.navigationMeshAssetPath, "navigation/courtyard.uvnav");
    EXPECT_EQ(restoredRegion.boundsHalfExtents, (Math::Vector3UVE{12.0F, 3.0F, 9.0F}));
    EXPECT_EQ(restoredRegion.navigationLayers, 0x5U);
    EXPECT_FLOAT_EQ(restoredRegion.cellSize, 0.25F);
    EXPECT_FLOAT_EQ(restoredRegion.agentRadius, 0.35F);
    EXPECT_FLOAT_EQ(restoredRegion.agentHeight, 1.6F);
    EXPECT_FLOAT_EQ(restoredRegion.maximumSlopeDegrees, 38.0F);
    EXPECT_FLOAT_EQ(restoredRegion.maximumStepHeight, 0.3F);
    const NavSeeker3DComponentUVE& restoredAgent = entityManager.GetComponentUVE<NavSeeker3DComponentUVE>(restored);
    EXPECT_EQ(restoredAgent.targetPosition, (Math::Vector3UVE{8.0F, 0.0F, -4.0F}));
    EXPECT_FLOAT_EQ(restoredAgent.radius, 0.35F);
    EXPECT_FLOAT_EQ(restoredAgent.height, 1.6F);
    EXPECT_FLOAT_EQ(restoredAgent.maxSpeed, 5.5F);
    EXPECT_FLOAT_EQ(restoredAgent.acceleration, 12.0F);
    EXPECT_FLOAT_EQ(restoredAgent.pathUpdateInterval, 0.25F);
    EXPECT_FLOAT_EQ(restoredAgent.waypointRadius, 0.6F);
    EXPECT_FLOAT_EQ(restoredAgent.targetTolerance, 1.2F);
    EXPECT_FLOAT_EQ(restoredAgent.slowDownRadius, 2.5F);
    EXPECT_FLOAT_EQ(restoredAgent.avoidanceRadius, 3.5F);
    EXPECT_FALSE(restoredAgent.avoidanceEnabled);
    EXPECT_EQ(restoredAgent.navigationLayers, 0x5U);
    // ...and the route does not: it is what the last step computed, so a load starts from scratch and
    // the agent finds its own way from its target.
    EXPECT_TRUE(restoredAgent.nextPathPosition == Math::Vector3UVE{});
    EXPECT_TRUE(restoredAgent.desiredVelocity == Math::Vector3UVE{});
    EXPECT_EQ(restoredAgent.pathStatus, NavigationAgentPathStatusUVE::Idle);
    EXPECT_FALSE(restoredAgent.pathChanged);
    EXPECT_FALSE(restoredAgent.targetReached);
    EXPECT_EQ(entityManager.GetComponentUVE<Skeleton3DComponentUVE>(restored).bones.size(), 1U);
    EXPECT_EQ(entityManager.GetComponentUVE<Hitbox3DComponentUVE>(restored).damageChannel, "melee");
    EXPECT_EQ(entityManager.GetComponentUVE<InteractionArea3DComponentUVE>(restored).interactionTag, "door");
    EXPECT_EQ(entityManager.GetComponentUVE<WorldEnvironment3DComponentUVE>(restored).skyAssetPath,
              "environment/day.uesky");
    EXPECT_EQ(entityManager.GetComponentUVE<Decal3DComponentUVE>(restored).materialAssetPath,
              "materials/warning.uemat");
    EXPECT_EQ(entityManager.GetComponentUVE<SpawnPoint3DComponentUVE>(restored).spawnTag, "player_start");
    EXPECT_FLOAT_EQ(entityManager.GetComponentUVE<PlayerComponentUVE>(restored).lookSensitivity, 0.2F);
    ASSERT_TRUE(entityManager.HasComponentUVE<HealthComponentUVE>(restored));
    EXPECT_FLOAT_EQ(entityManager.GetComponentUVE<HealthComponentUVE>(restored).maxHealth, 75.0F);
    EXPECT_TRUE(entityManager.GetComponentUVE<HealthComponentUVE>(restored).invulnerable);
    EXPECT_FLOAT_EQ(entityManager.GetComponentUVE<HealthComponentUVE>(restored).health, 75.0F);
    EXPECT_TRUE(entityManager.GetComponentUVE<LevelStreamer3DComponentUVE>(restored).enabled);
    EXPECT_TRUE(entityManager.HasComponentUVE<Kinematic3DComponentUVE>(restored));
    EXPECT_TRUE(entityManager.HasComponentUVE<NavSeeker3DComponentUVE>(restored));
    EXPECT_TRUE(entityManager.HasComponentUVE<BoneAttachment3DComponentUVE>(restored));
    EXPECT_TRUE(entityManager.HasComponentUVE<SpringArm3DComponentUVE>(restored));
    EXPECT_TRUE(entityManager.HasComponentUVE<Marker3DComponentUVE>(restored));
    EXPECT_TRUE(entityManager.HasComponentUVE<Hurtbox3DComponentUVE>(restored));
    EXPECT_TRUE(entityManager.HasComponentUVE<Projectile3DComponentUVE>(restored));
    EXPECT_TRUE(entityManager.HasComponentUVE<ReflectionProbe3DComponentUVE>(restored));
    EXPECT_TRUE(entityManager.HasComponentUVE<LodGroup3DComponentUVE>(restored));
    EXPECT_TRUE(entityManager.HasComponentUVE<Occluder3DComponentUVE>(restored));
    EXPECT_TRUE(entityManager.HasComponentUVE<VisibilityRegion3DComponentUVE>(restored));
    EXPECT_TRUE(entityManager.HasComponentUVE<WorldPartition3DComponentUVE>(restored));
    EXPECT_TRUE(entityManager.HasComponentUVE<WorldTransformComponentUVE>(restored));
}

TEST_F(SceneSerializerUVETest, BoneAttachmentSkeletonReferenceRoundTripsThroughTheFileLocalId) {
    // The skeleton is a real entity reference in the component, and a file has no entities - only
    // file-local ids. Saved, it goes out as an id and comes back as the RESTORED skeleton, not as the
    // handle it held when the scene was written, which names a slot in a manager that no longer holds
    // that entity.
    const EntityUVE skeletonEntity = entityManager.CreateEntityUVE();
    Skeleton3DComponentUVE skeleton;
    skeleton.skeletonAssetPath = "assets/character.uvskel";
    skeleton.bones.push_back(SkeletonBoneUVE{"root", -1, {}, {}, {1.0F, 1.0F, 1.0F}});
    entityManager.AddComponentUVE<Skeleton3DComponentUVE>(skeletonEntity, skeleton);
    const EntityUVE attachmentEntity = entityManager.CreateEntityUVE();
    BoneAttachment3DComponentUVE attachment;
    attachment.skeleton = skeletonEntity;
    attachment.boneName = "root";
    attachment.localPosition = Math::Vector3UVE{0.0F, 0.25F, 0.0F};
    entityManager.AddComponentUVE<BoneAttachment3DComponentUVE>(attachmentEntity, attachment);

    const std::optional<SceneSnapshotUVE> snapshot =
        serializer.CaptureUVE(entityManager, {skeletonEntity, attachmentEntity}, SceneAssetTypeUVE::Scene);
    ASSERT_TRUE(snapshot.has_value());
    EntityManagerUVE fresh{memoryManager.GetDefaultAllocatorUVE(), eventSystem};
    const std::vector<EntityUVE> restoredRoots = serializer.RestoreUVE(fresh, *snapshot);
    ASSERT_EQ(restoredRoots.size(), 2U);

    EntityUVE restoredSkeleton = kInvalidEntityUVE;
    EntityUVE restoredAttachment = kInvalidEntityUVE;
    for (const EntityUVE entity : restoredRoots) {
        if (fresh.HasComponentUVE<Skeleton3DComponentUVE>(entity)) {
            restoredSkeleton = entity;
        }
        if (fresh.HasComponentUVE<BoneAttachment3DComponentUVE>(entity)) {
            restoredAttachment = entity;
        }
    }
    ASSERT_NE(restoredSkeleton, kInvalidEntityUVE);
    ASSERT_NE(restoredAttachment, kInvalidEntityUVE);
    const BoneAttachment3DComponentUVE restored =
        fresh.GetComponentUVE<BoneAttachment3DComponentUVE>(restoredAttachment);
    EXPECT_EQ(restored.skeleton, restoredSkeleton) << "the id in the file is remapped to the restored skeleton";
    EXPECT_TRUE(fresh.IsAliveUVE(restored.skeleton))
        << "what the load left behind is a reference that resolves in the loaded scene, not a stale handle";
    EXPECT_EQ(restored.boneName, "root");
    EXPECT_FLOAT_EQ(restored.localPosition.y, 0.25F);
}

TEST_F(SceneSerializerUVETest, BoneAttachmentPointingOutsideTheSavedSetLoadsInert) {
    // The skeleton was not part of what was saved, so there is no id for it in the file. The
    // attachment must load pointing at nothing rather than at whichever entity happens to share the
    // slot it used to occupy.
    const EntityUVE skeletonEntity = entityManager.CreateEntityUVE();
    entityManager.AddComponentUVE<Skeleton3DComponentUVE>(skeletonEntity, Skeleton3DComponentUVE{});
    const EntityUVE attachmentEntity = entityManager.CreateEntityUVE();
    BoneAttachment3DComponentUVE attachment;
    attachment.skeleton = skeletonEntity;
    attachment.boneName = "root";
    entityManager.AddComponentUVE<BoneAttachment3DComponentUVE>(attachmentEntity, attachment);

    const std::optional<SceneSnapshotUVE> snapshot =
        serializer.CaptureUVE(entityManager, {attachmentEntity}, SceneAssetTypeUVE::Scene);
    ASSERT_TRUE(snapshot.has_value());
    EntityManagerUVE fresh{memoryManager.GetDefaultAllocatorUVE(), eventSystem};
    const std::vector<EntityUVE> restoredRoots = serializer.RestoreUVE(fresh, *snapshot);
    ASSERT_EQ(restoredRoots.size(), 1U);
    const BoneAttachment3DComponentUVE restored =
        fresh.GetComponentUVE<BoneAttachment3DComponentUVE>(restoredRoots.front());
    EXPECT_EQ(restored.skeleton, kInvalidEntityUVE);
    EXPECT_EQ(restored.boneName, "root");
    EXPECT_FALSE(IsBoneAttachment3DObjectComponentResolvableUVE(restored))
        << "an attachment with nowhere to bind is inert, which is what keeps it from teleporting";
}

TEST_F(SceneSerializerUVETest, LoadUVE_ABoneAttachmentFromBeforeTheReferenceLoadsInertOrBound) {
    // Documents written while the skeleton was still a bare numeric id keep that key. An id this file
    // does not contain leaves the attachment inert; one it does contain binds it. Neither may read
    // the number as an entity slot.
    const std::string payloadText =
        R"({"entities":[{"localId":0,"components":{"Skeleton3DComponentUVE":{"skeletonAssetPath":"Hero.fbx","bones":[{"name":"root","parentIndex":-1,"localPosition":[0.0,0.0,0.0],"localRotation":[0.0,0.0,0.0,1.0],"localScale":[1.0,1.0,1.0]}],"enabled":true}}},{"localId":1,"components":{"BoneAttachment3DComponentUVE":{"skeletonLocalId":0,"boneIndex":4294967295,"boneName":"root","localPosition":[0.0,0.1,0.0],"localRotation":[0.0,0.0,0.0,1.0],"localScale":[1.0,1.0,1.0],"enabled":true}}},{"localId":2,"components":{"BoneAttachment3DComponentUVE":{"skeletonLocalId":7,"boneName":"root","localPosition":[0.0,0.0,0.0],"localRotation":[0.0,0.0,0.0,1.0],"localScale":[1.0,1.0,1.0],"enabled":true}}}]})";
    const auto* const payloadBytesPtr = reinterpret_cast<const std::byte*>(payloadText.data());
    const std::vector<std::byte> payloadBytes(payloadBytesPtr, payloadBytesPtr + payloadText.size());

    const std::filesystem::path path = "uve_scene_serializer_tests_bone_attachment_legacy_reference.uvscene";
    std::filesystem::remove(path);
    ASSERT_TRUE(Asset::WriteUveFileUVE(path, SceneAssetTypeUVE::Scene, payloadBytes));

    const std::vector<EntityUVE> roots = serializer.LoadUVE(entityManager, path);
    ASSERT_EQ(roots.size(), 3U);
    const EntityUVE skeletonEntity = roots[0];
    const BoneAttachment3DComponentUVE bound =
        entityManager.GetComponentUVE<BoneAttachment3DComponentUVE>(roots[1]);
    EXPECT_EQ(bound.skeleton, skeletonEntity) << "id 0 names the skeleton saved alongside it";
    EXPECT_EQ(bound.boneName, "root");
    const BoneAttachment3DComponentUVE dangling =
        entityManager.GetComponentUVE<BoneAttachment3DComponentUVE>(roots[2]);
    EXPECT_EQ(dangling.skeleton, kInvalidEntityUVE) << "id 7 names nothing in this file";

    std::filesystem::remove(path);
}

TEST_F(SceneSerializerUVETest, TwoBoneIKChainReferencesRoundTripThroughTheFileLocalIds) {
    // Three references, three keys: the skeleton whose pose it corrects, the object it reaches for and
    // the point that picks which way the joint bends. A file has no entities, only file-local ids, so
    // every one of them has to come back as the RESTORED entity rather than as the handle it held when
    // the scene was written.
    const EntityUVE skeletonEntity = entityManager.CreateEntityUVE();
    Skeleton3DComponentUVE skeleton;
    skeleton.skeletonAssetPath = "assets/archer.uvskel";
    skeleton.bones.push_back(SkeletonBoneUVE{"UpperArm", -1, {}, {}, {1.0F, 1.0F, 1.0F}});
    entityManager.AddComponentUVE<Skeleton3DComponentUVE>(skeletonEntity, skeleton);
    const EntityUVE targetEntity = entityManager.CreateEntityUVE();
    TransformComponentUVE targetTransform;
    targetTransform.localPosition = Math::Vector3UVE{1.0F, 0.0F, 0.0F};
    entityManager.AddComponentUVE<TransformComponentUVE>(targetEntity, targetTransform);
    const EntityUVE poleEntity = entityManager.CreateEntityUVE();
    TransformComponentUVE poleTransform;
    poleTransform.localPosition = Math::Vector3UVE{0.0F, 0.0F, 2.0F};
    entityManager.AddComponentUVE<TransformComponentUVE>(poleEntity, poleTransform);
    const EntityUVE chainEntity = entityManager.CreateEntityUVE();
    TwoBoneIK3DComponentUVE chain;
    chain.skeleton = skeletonEntity;
    chain.rootBoneIndex = 3U;
    chain.rootBoneName = "UpperArm";
    chain.middleBoneIndex = 4294967295U;
    chain.middleBoneName = "Forearm";
    chain.endBoneName = "Hand";
    chain.target = targetEntity;
    chain.targetPosition = Math::Vector3UVE{0.25F, 1.0F, 0.0F};
    chain.poleTarget = poleEntity;
    chain.poleDirection = Math::Vector3UVE{0.0F, 0.0F, 1.0F};
    chain.enabled = false;
    // Runtime answers on the source: none of them may reach the file - a restored chain has been
    // solved zero times, and a stale `reached` would have the Inspector report a solve that never ran.
    chain.solved = true;
    chain.reached = true;
    chain.endToTargetDistanceMetres = 0.4F;
    chain.resolvedRootBoneIndex = 3U;
    chain.resolvedMiddleBoneIndex = 4U;
    chain.resolvedEndBoneIndex = 5U;
    entityManager.AddComponentUVE<TwoBoneIK3DComponentUVE>(chainEntity, chain);

    const std::optional<SceneSnapshotUVE> snapshot = serializer.CaptureUVE(
        entityManager, {skeletonEntity, targetEntity, poleEntity, chainEntity}, SceneAssetTypeUVE::Scene);
    ASSERT_TRUE(snapshot.has_value());
    EntityManagerUVE fresh{memoryManager.GetDefaultAllocatorUVE(), eventSystem};
    const std::vector<EntityUVE> restoredRoots = serializer.RestoreUVE(fresh, *snapshot);
    ASSERT_EQ(restoredRoots.size(), 4U);

    // Named by what each object carries rather than by restore order, so a swapped pair of references
    // cannot pass by both entities having been restored.
    EntityUVE restoredSkeleton = kInvalidEntityUVE;
    EntityUVE restoredTarget = kInvalidEntityUVE;
    EntityUVE restoredPole = kInvalidEntityUVE;
    EntityUVE restoredChain = kInvalidEntityUVE;
    for (const EntityUVE entity : restoredRoots) {
        if (fresh.HasComponentUVE<Skeleton3DComponentUVE>(entity)) {
            restoredSkeleton = entity;
            continue;
        }
        if (fresh.HasComponentUVE<TwoBoneIK3DComponentUVE>(entity)) {
            restoredChain = entity;
            continue;
        }
        const Math::Vector3UVE position = fresh.GetComponentUVE<TransformComponentUVE>(entity).localPosition;
        if (position.x == 1.0F) {
            restoredTarget = entity;
        } else if (position.z == 2.0F) {
            restoredPole = entity;
        }
    }
    ASSERT_NE(restoredSkeleton, kInvalidEntityUVE);
    ASSERT_NE(restoredTarget, kInvalidEntityUVE);
    ASSERT_NE(restoredPole, kInvalidEntityUVE);
    ASSERT_NE(restoredChain, kInvalidEntityUVE);

    const TwoBoneIK3DComponentUVE restored = fresh.GetComponentUVE<TwoBoneIK3DComponentUVE>(restoredChain);
    EXPECT_EQ(restored.skeleton, restoredSkeleton) << "id in the file -> restored skeleton";
    EXPECT_EQ(restored.target, restoredTarget);
    EXPECT_EQ(restored.poleTarget, restoredPole);
    EXPECT_NE(restored.target, restoredPole) << "two references that named different objects still do";
    EXPECT_EQ(restored.rootBoneIndex, 3U);
    EXPECT_EQ(restored.rootBoneName, "UpperArm");
    EXPECT_EQ(restored.middleBoneIndex, kInvalidSkeletonBoneIndexUVE);
    EXPECT_EQ(restored.middleBoneName, "Forearm");
    EXPECT_EQ(restored.endBoneName, "Hand");
    EXPECT_FLOAT_EQ(restored.targetPosition.x, 0.25F);
    EXPECT_FLOAT_EQ(restored.poleDirection.z, 1.0F);
    EXPECT_FALSE(restored.enabled);
    EXPECT_FALSE(restored.solved) << "the file carries authored data, never the last frame's answer";
    EXPECT_FALSE(restored.reached);
    EXPECT_FLOAT_EQ(restored.endToTargetDistanceMetres, 0.0F);
    EXPECT_EQ(restored.resolvedRootBoneIndex, kInvalidSkeletonBoneIndexUVE);
    EXPECT_EQ(restored.resolvedMiddleBoneIndex, kInvalidSkeletonBoneIndexUVE);
    EXPECT_EQ(restored.resolvedEndBoneIndex, kInvalidSkeletonBoneIndexUVE);
}

TEST_F(SceneSerializerUVETest, TwoBoneIKPointingOutsideTheSavedSetLoadsInert) {
    // Nothing the chain names was part of what was saved, so there are no ids for them in the file.
    // Each reference has to load pointing at nothing rather than at whichever entity happens to share
    // the slot it used to occupy, and the chain has to be inert rather than aimed somewhere.
    const EntityUVE skeletonEntity = entityManager.CreateEntityUVE();
    entityManager.AddComponentUVE<Skeleton3DComponentUVE>(skeletonEntity, Skeleton3DComponentUVE{});
    const EntityUVE targetEntity = entityManager.CreateEntityUVE();
    const EntityUVE chainEntity = entityManager.CreateEntityUVE();
    TwoBoneIK3DComponentUVE chain;
    chain.skeleton = skeletonEntity;
    chain.target = targetEntity;
    chain.rootBoneName = "UpperArm";
    chain.middleBoneName = "Forearm";
    chain.endBoneName = "Hand";
    entityManager.AddComponentUVE<TwoBoneIK3DComponentUVE>(chainEntity, chain);

    const std::optional<SceneSnapshotUVE> snapshot =
        serializer.CaptureUVE(entityManager, {chainEntity}, SceneAssetTypeUVE::Scene);
    ASSERT_TRUE(snapshot.has_value());
    EntityManagerUVE fresh{memoryManager.GetDefaultAllocatorUVE(), eventSystem};
    const std::vector<EntityUVE> restoredRoots = serializer.RestoreUVE(fresh, *snapshot);
    ASSERT_EQ(restoredRoots.size(), 1U);
    const TwoBoneIK3DComponentUVE restored = fresh.GetComponentUVE<TwoBoneIK3DComponentUVE>(restoredRoots.front());
    EXPECT_EQ(restored.skeleton, kInvalidEntityUVE);
    EXPECT_EQ(restored.target, kInvalidEntityUVE);
    EXPECT_EQ(restored.poleTarget, kInvalidEntityUVE);
    EXPECT_EQ(restored.rootBoneName, "UpperArm") << "the authored names survive the missing references";
    EXPECT_FALSE(IsTwoBoneIK3DObjectComponentResolvableUVE(restored))
        << "a chain with nowhere to bind is inert, which is what keeps it from driving a stranger's bones";
}

TEST_F(SceneSerializerUVETest, LoadUVE_ATwoBoneIKChainBindsTheIdsTheFileHas) {
    // The three reference keys as a document spells them, including a pole id that names nothing in
    // this file: the chain still binds its skeleton and target, and solves toward the authored point
    // because the pole reference it could not resolve is the sentinel, not a wrong object.
    const std::string payloadText =
        R"({"entities":[{"localId":0,"components":{"Skeleton3DComponentUVE":{"skeletonAssetPath":"Archer.fbx","bones":[{"name":"UpperArm","parentIndex":-1,"localPosition":[0.0,0.0,0.0],"localRotation":[0.0,0.0,0.0,1.0],"localScale":[1.0,1.0,1.0]}],"enabled":true}}},{"localId":1,"components":{"TwoBoneIK3DComponentUVE":{"skeletonLocalId":0,"targetLocalId":2,"poleLocalId":9,"rootBoneIndex":4294967295,"rootBoneName":"UpperArm","middleBoneIndex":4294967295,"middleBoneName":"Forearm","endBoneIndex":4294967295,"endBoneName":"Hand","targetPosition":[0.25,1.0,0.0],"poleDirection":[0.0,0.0,1.0],"enabled":true}}},{"localId":2,"components":{}}]})";
    const auto* const payloadBytesPtr = reinterpret_cast<const std::byte*>(payloadText.data());
    const std::vector<std::byte> payloadBytes(payloadBytesPtr, payloadBytesPtr + payloadText.size());

    const std::filesystem::path path = "uve_scene_serializer_tests_two_bone_ik_references.uvscene";
    std::filesystem::remove(path);
    ASSERT_TRUE(Asset::WriteUveFileUVE(path, SceneAssetTypeUVE::Scene, payloadBytes));

    const std::vector<EntityUVE> roots = serializer.LoadUVE(entityManager, path);
    ASSERT_EQ(roots.size(), 3U);
    const TwoBoneIK3DComponentUVE chain =
        entityManager.GetComponentUVE<TwoBoneIK3DComponentUVE>(roots[1]);
    EXPECT_EQ(chain.skeleton, roots[0]) << "id 0 names the skeleton saved beside it";
    EXPECT_EQ(chain.target, roots[2]) << "id 2 names the object the chain reaches for";
    EXPECT_EQ(chain.poleTarget, kInvalidEntityUVE) << "id 9 names nothing in this file";
    EXPECT_EQ(chain.rootBoneName, "UpperArm");
    EXPECT_TRUE(IsTwoBoneIK3DObjectComponentResolvableUVE(chain));

    std::filesystem::remove(path);
}

TEST_F(SceneSerializerUVETest, SpringArmAuthoredFieldsRoundTripAndRuntimeTruthIsReseeded) {
    // The arm's authored half is saved; its runtime half is not, and must not be: where the boom
    // happens to be pointing right now is a fact about the frame that saved the scene, not about
    // the scene. A restored arm starts at its authored reach and the first step resolves the truth.
    const EntityUVE source = entityManager.CreateEntityUVE();
    SpringArm3DComponentUVE springArm;
    springArm.armLength = 6.0F;
    springArm.margin = 0.25F;
    springArm.smoothing = 12.0F;
    springArm.collisionMask = 0x0FU;
    springArm.currentLength = 2.0F; // retracted against something, at save time
    springArm.enabled = false;
    entityManager.AddComponentUVE<SpringArm3DComponentUVE>(source, springArm);

    const std::optional<SceneSnapshotUVE> snapshot =
        serializer.CaptureUVE(entityManager, {source}, SceneAssetTypeUVE::Scene);
    ASSERT_TRUE(snapshot.has_value());
    const std::vector<EntityUVE> restoredRoots = serializer.RestoreUVE(entityManager, *snapshot);
    ASSERT_EQ(restoredRoots.size(), 1U);

    const SpringArm3DComponentUVE restored =
        entityManager.GetComponentUVE<SpringArm3DComponentUVE>(restoredRoots.front());
    EXPECT_FLOAT_EQ(restored.armLength, 6.0F);
    EXPECT_FLOAT_EQ(restored.margin, 0.25F);
    EXPECT_FLOAT_EQ(restored.smoothing, 12.0F);
    EXPECT_EQ(restored.collisionMask, 0x0FU);
    EXPECT_FALSE(restored.enabled);
    EXPECT_FLOAT_EQ(restored.currentLength, restored.armLength)
        << "runtime truth is re-derived by the next step, so a load must not restore a stale one";
}

TEST_F(SceneSerializerUVETest, Projectile3DAuthoredHitContractRoundTripsAndTheRuntimeResultIsNotSaved) {
    // The authored half of the hit contract - the policy and the two coefficients - is scene data
    // and must survive a save. The runtime half is not: where this projectile last landed and how
    // many times it bounced are facts about the session that saved the scene, and a restored
    // projectile starts its life unspent.
    const EntityUVE source = entityManager.CreateEntityUVE();
    Projectile3DComponentUVE projectile;
    projectile.velocity = Math::Vector3UVE{0.0F, 2.0F, 20.0F};
    projectile.acceleration = Math::Vector3UVE{0.0F, -9.81F, 0.0F};
    projectile.radius = 0.25F;
    projectile.maxLifetime = 4.0F;
    projectile.collisionMask = 0x03U;
    projectile.active = false;
    projectile.hitPolicy = Projectile3DHitPolicyUVE::Bounce;
    projectile.restitution = 0.75F;
    projectile.friction = 0.1F;
    // Runtime truth, at save time.
    projectile.remainingLifetime = 1.5F;
    projectile.hit = true;
    projectile.hitEntity = EntityUVE{7U, 1U};
    projectile.hitPosition = Math::Vector3UVE{1.0F, 2.0F, 3.0F};
    projectile.hitNormal = Math::Vector3UVE{0.0F, 1.0F, 0.0F};
    projectile.impactSpeed = 12.0F;
    projectile.bounceCount = 3U;
    entityManager.AddComponentUVE<Projectile3DComponentUVE>(source, projectile);

    const std::optional<SceneSnapshotUVE> snapshot =
        serializer.CaptureUVE(entityManager, {source}, SceneAssetTypeUVE::Scene);
    ASSERT_TRUE(snapshot.has_value());
    const std::vector<EntityUVE> restoredRoots = serializer.RestoreUVE(entityManager, *snapshot);
    ASSERT_EQ(restoredRoots.size(), 1U);

    const Projectile3DComponentUVE restored =
        entityManager.GetComponentUVE<Projectile3DComponentUVE>(restoredRoots.front());
    EXPECT_EQ(restored.velocity, projectile.velocity);
    EXPECT_EQ(restored.acceleration, projectile.acceleration);
    EXPECT_FLOAT_EQ(restored.radius, 0.25F);
    EXPECT_FLOAT_EQ(restored.maxLifetime, 4.0F);
    EXPECT_EQ(restored.collisionMask, 0x03U);
    EXPECT_FALSE(restored.active);
    EXPECT_EQ(restored.hitPolicy, Projectile3DHitPolicyUVE::Bounce);
    EXPECT_FLOAT_EQ(restored.restitution, 0.75F);
    EXPECT_FLOAT_EQ(restored.friction, 0.1F);
    EXPECT_EQ(restored.ignoreEntity, kInvalidEntityUVE);
    EXPECT_FLOAT_EQ(restored.remainingLifetime, restored.maxLifetime)
        << "the countdown is re-armed from the authored lifetime, not restored stale";
    EXPECT_FALSE(restored.hit);
    EXPECT_EQ(restored.hitEntity, kInvalidEntityUVE);
    EXPECT_EQ(restored.hitPosition, Math::Vector3UVE{});
    EXPECT_EQ(restored.bounceCount, 0U);
}

TEST_F(SceneSerializerUVETest, LoadUVE_AProjectileFromBeforeTheHitContractLoadsWithAuthoredDefaults) {
    // A file written before the hit contract existed has no policy and no coefficients, so the
    // component lands on the authored defaults - Stop, 0.5 and 0.2 - rather than on whatever a
    // zero-initialized enum happens to mean.
    const std::string payloadText =
        R"({"entities":[{"localId":0,"components":{"Projectile3DComponentUVE":{"velocity":[0.0,0.0,20.0],"acceleration":[0.0,-9.81,0.0],"radius":0.25,"maxLifetime":4.0,"collisionMask":4294967295,"active":true}}}]})";
    const auto* const payloadBytesPtr = reinterpret_cast<const std::byte*>(payloadText.data());
    const std::vector<std::byte> payloadBytes(payloadBytesPtr, payloadBytesPtr + payloadText.size());

    const std::filesystem::path path = "uve_scene_serializer_tests_projectile_legacy_contract.uvscene";
    std::filesystem::remove(path);
    ASSERT_TRUE(Asset::WriteUveFileUVE(path, SceneAssetTypeUVE::Scene, payloadBytes));

    const std::vector<EntityUVE> roots = serializer.LoadUVE(entityManager, path);
    ASSERT_EQ(roots.size(), 1U);
    const Projectile3DComponentUVE& loaded =
        entityManager.GetComponentUVE<Projectile3DComponentUVE>(roots[0]);
    EXPECT_FLOAT_EQ(loaded.radius, 0.25F);
    EXPECT_FLOAT_EQ(loaded.maxLifetime, 4.0F);
    EXPECT_EQ(loaded.hitPolicy, Projectile3DHitPolicyUVE::Stop);
    EXPECT_FLOAT_EQ(loaded.restitution, 0.5F);
    EXPECT_FLOAT_EQ(loaded.friction, 0.2F);
    EXPECT_TRUE(loaded.active);

    std::filesystem::remove(path);
}

TEST_F(SceneSerializerUVETest, LodGroup3DAuthoredLevelsAndBandRoundTripAndTheResolvedAnswerIsNotSaved) {
    // What the author decided - the thresholds, the per-level meshes and the band - is scene data.
    // What the renderer resolved from it on the frame that saved the scene is not: currentLevel and
    // culledByDistance describe where the camera was, and a restored scene resolves them itself.
    const EntityUVE source = entityManager.CreateEntityUVE();
    LodGroup3DComponentUVE lod;
    lod.levelCount = 3U;
    lod.distanceThresholds[0] = 12.0F;
    lod.distanceThresholds[1] = 40.0F;
    lod.distanceThresholds[2] = 90.0F;
    lod.lodMeshGuids[1] = Asset::AssetGuidUVE{77U};
    lod.lodMeshGuids[2] = Asset::AssetGuidUVE{88U};
    lod.hysteresis = 0.15F;
    // Runtime truth at save time, which must not come back.
    lod.currentLevel = 2U;
    lod.culledByDistance = true;
    entityManager.AddComponentUVE<LodGroup3DComponentUVE>(source, lod);

    const std::optional<SceneSnapshotUVE> snapshot =
        serializer.CaptureUVE(entityManager, {source}, SceneAssetTypeUVE::Scene);
    ASSERT_TRUE(snapshot.has_value());
    const std::vector<EntityUVE> restoredRoots = serializer.RestoreUVE(entityManager, *snapshot);
    ASSERT_EQ(restoredRoots.size(), 1U);

    const LodGroup3DComponentUVE restored =
        entityManager.GetComponentUVE<LodGroup3DComponentUVE>(restoredRoots.front());
    EXPECT_EQ(restored.levelCount, 3U);
    EXPECT_FLOAT_EQ(restored.distanceThresholds[0], 12.0F);
    EXPECT_FLOAT_EQ(restored.distanceThresholds[1], 40.0F);
    EXPECT_FLOAT_EQ(restored.distanceThresholds[2], 90.0F);
    EXPECT_FLOAT_EQ(restored.hysteresis, 0.15F) << "the band is authored data";
    EXPECT_EQ(restored.lodMeshGuids[0], Asset::kInvalidAssetGuidUVE)
        << "level 0 was never overridden, and must come back unassigned rather than pointing "
           "anywhere";
    EXPECT_EQ(restored.lodMeshGuids[1], Asset::AssetGuidUVE{77U});
    EXPECT_EQ(restored.lodMeshGuids[2], Asset::AssetGuidUVE{88U});
    EXPECT_EQ(restored.currentLevel, 0U) << "the resolved level is re-derived, not restored";
    EXPECT_FALSE(restored.culledByDistance);
}

TEST_F(SceneSerializerUVETest, LoadUVE_AnLodGroupFromBeforePerLevelMeshesLoadsWithNoOverridesAndNoBand) {
    // A file written before levels could name their own meshes has neither array nor band. It must
    // load as a group that overrides nothing and bands nothing - exactly what it meant when it was
    // written - rather than failing validation or inventing a band.
    const std::string payloadText =
        R"({"entities":[{"localId":0,"components":{"LodGroup3DComponentUVE":{"distanceThresholds":[12.0,40.0],"levelCount":2,"enabled":true}}}]})";
    const auto* const payloadBytesPtr = reinterpret_cast<const std::byte*>(payloadText.data());
    const std::vector<std::byte> payloadBytes(payloadBytesPtr, payloadBytesPtr + payloadText.size());

    const std::filesystem::path path = "uve_scene_serializer_tests_lod_legacy_levels.uvscene";
    std::filesystem::remove(path);
    ASSERT_TRUE(Asset::WriteUveFileUVE(path, SceneAssetTypeUVE::Scene, payloadBytes));

    const std::vector<EntityUVE> roots = serializer.LoadUVE(entityManager, path);
    ASSERT_EQ(roots.size(), 1U);
    const LodGroup3DComponentUVE& loaded =
        entityManager.GetComponentUVE<LodGroup3DComponentUVE>(roots[0]);
    EXPECT_EQ(loaded.levelCount, 2U);
    EXPECT_FLOAT_EQ(loaded.distanceThresholds[0], 12.0F);
    EXPECT_FLOAT_EQ(loaded.distanceThresholds[1], 40.0F);
    EXPECT_FLOAT_EQ(loaded.hysteresis, 0.0F);
    for (std::size_t index = 0U; index < kMaximumLodLevelsUVE; ++index) {
        EXPECT_EQ(loaded.lodMeshGuids[index], Asset::kInvalidAssetGuidUVE);
    }
    EXPECT_TRUE(IsLodGroup3DObjectComponentValidUVE(loaded));

    std::filesystem::remove(path);
}

TEST_F(SceneSerializerUVETest, SaveUVE_InvalidAuthoredTransformFailsBeforeDestinationPublication) {
    const EntityUVE entity = entityManager.CreateEntityUVE();
    TransformComponentUVE transform;
    transform.localPosition.x = std::numeric_limits<float>::quiet_NaN();
    entityManager.AddComponentUVE<TransformComponentUVE>(entity, transform);

    const std::filesystem::path path = "uve_scene_serializer_tests_invalid_authored_transform.uvscene";
    std::filesystem::remove(path);
    EXPECT_FALSE(serializer.SaveUVE(entityManager, {entity}, path, SceneAssetTypeUVE::Scene));
    EXPECT_FALSE(std::filesystem::exists(path));
    std::filesystem::remove(path);
}

TEST_F(SceneSerializerUVETest, CaptureUVE_UnregisteredComponent_ReturnsNulloptWithoutMutation) {
    const EntityUVE entity = entityManager.CreateEntityUVE();
    entityManager.AddComponentUVE<UnregisteredSnapshotComponentUVE>(entity, UnregisteredSnapshotComponentUVE{42});
    const std::size_t entityCountBefore = entityManager.GetEntityCountUVE();

    const std::optional<SceneSnapshotUVE> snapshot =
        serializer.CaptureUVE(entityManager, {entity}, SceneAssetTypeUVE::Scene);

    EXPECT_FALSE(snapshot.has_value());
    EXPECT_TRUE(entityManager.IsAliveUVE(entity));
    EXPECT_EQ(entityManager.GetEntityCountUVE(), entityCountBefore);
}

TEST(NameComponentUVETest, IsNameComponentValidUVE_BoundsBytesAndRejectsEmbeddedNul) {
    EXPECT_TRUE(IsNameComponentValidUVE(NameComponentUVE{}));
    EXPECT_TRUE(IsNameComponentValidUVE(
        NameComponentUVE{std::string(kMaximumEntityNameBytesUVE, 'N')}));
    EXPECT_FALSE(IsNameComponentValidUVE(
        NameComponentUVE{std::string(kMaximumEntityNameBytesUVE + 1U, 'N')}));
    EXPECT_FALSE(IsNameComponentValidUVE(NameComponentUVE{std::string("Visible") + '\0' + "Hidden"}));
}

TEST_F(SceneSerializerUVETest, RestoreUVE_OversizedNamePayload_RollsBackCreatedEntities) {
    const EntityUVE existing = entityManager.CreateEntityUVE();
    const std::size_t entityCountBefore = entityManager.GetEntityCountUVE();
    const std::string oversizedName(kMaximumEntityNameBytesUVE + 1U, 'N');
    const std::string payloadText =
        R"({"entities":[{"localId":0,"components":{"NameComponentUVE":{"name":")" +
        oversizedName + R"("}}}]})";
    const auto* const payloadBytes = reinterpret_cast<const std::byte*>(payloadText.data());
    const SceneSnapshotUVE snapshot{
        Asset::EncodeUveFileEnvelopeUVE(SceneAssetTypeUVE::Scene,
                                        std::vector<std::byte>{payloadBytes, payloadBytes + payloadText.size()}),
        SceneAssetTypeUVE::Scene};

    const std::vector<EntityUVE> roots = serializer.RestoreUVE(entityManager, snapshot);

    EXPECT_TRUE(roots.empty());
    EXPECT_TRUE(entityManager.IsAliveUVE(existing));
    EXPECT_EQ(entityManager.GetEntityCountUVE(), entityCountBefore);
}

TEST_F(SceneSerializerUVETest, RestoreUVE_CyclicHierarchy_RollsBackBeforeEntityPublication) {
    const EntityUVE existing = entityManager.CreateEntityUVE();
    const std::size_t entityCountBefore = entityManager.GetEntityCountUVE();
    const std::string payloadText =
        R"({"entities":[{"localId":0,"components":{"TransformComponentUVE":{"localPosition":[0.0,0.0,0.0],"localRotation":[0.0,0.0,0.0,1.0],"localScale":[1.0,1.0,1.0]},"HierarchyComponentUVE":{"parentLocalId":1}}},{"localId":1,"components":{"TransformComponentUVE":{"localPosition":[0.0,0.0,0.0],"localRotation":[0.0,0.0,0.0,1.0],"localScale":[1.0,1.0,1.0]},"HierarchyComponentUVE":{"parentLocalId":0}}}]})";
    const auto* const payloadBytes = reinterpret_cast<const std::byte*>(payloadText.data());
    const SceneSnapshotUVE snapshot{
        Asset::EncodeUveFileEnvelopeUVE(SceneAssetTypeUVE::Scene,
                                        std::vector<std::byte>{payloadBytes, payloadBytes + payloadText.size()}),
        SceneAssetTypeUVE::Scene};

    const std::vector<EntityUVE> roots = serializer.RestoreUVE(entityManager, snapshot);

    EXPECT_TRUE(roots.empty());
    EXPECT_TRUE(entityManager.IsAliveUVE(existing));
    EXPECT_EQ(entityManager.GetEntityCountUVE(), entityCountBefore);
}

TEST_F(SceneSerializerUVETest, RestoreUVE_OutOfRangeHierarchyParentId_RollsBackCreatedEntities) {
    const EntityUVE existing = entityManager.CreateEntityUVE();
    const std::size_t entityCountBefore = entityManager.GetEntityCountUVE();

    for (const std::string& invalidParentId : {std::string("-2"), std::string("4294967296")}) {
        SCOPED_TRACE(invalidParentId);
        const std::string payloadText =
            R"({"entities":[{"localId":0,"components":{"HierarchyComponentUVE":{"parentLocalId":)" +
            invalidParentId + R"(}}}]})";
        const auto* const payloadBytes = reinterpret_cast<const std::byte*>(payloadText.data());
        const SceneSnapshotUVE snapshot{
            Asset::EncodeUveFileEnvelopeUVE(SceneAssetTypeUVE::Scene,
                                            std::vector<std::byte>{payloadBytes, payloadBytes + payloadText.size()}),
            SceneAssetTypeUVE::Scene};

        const std::vector<EntityUVE> roots = serializer.RestoreUVE(entityManager, snapshot);

        EXPECT_TRUE(roots.empty());
        EXPECT_TRUE(entityManager.IsAliveUVE(existing));
        EXPECT_EQ(entityManager.GetEntityCountUVE(), entityCountBefore);
    }
}

TEST_F(SceneSerializerUVETest, RestoreUVE_UnknownComponent_RollsBackCreatedEntities) {
    const EntityUVE existing = entityManager.CreateEntityUVE();
    const std::size_t entityCountBefore = entityManager.GetEntityCountUVE();
    const std::string payloadText =
        R"({"entities":[{"localId":0,"components":{"UnknownComponentUVE":{}}}]})";
    const auto* const payloadBytes = reinterpret_cast<const std::byte*>(payloadText.data());
    const SceneSnapshotUVE snapshot{
        Asset::EncodeUveFileEnvelopeUVE(SceneAssetTypeUVE::Scene,
                                        std::vector<std::byte>{payloadBytes, payloadBytes + payloadText.size()}),
        SceneAssetTypeUVE::Scene};

    const std::vector<EntityUVE> roots = serializer.RestoreUVE(entityManager, snapshot);

    EXPECT_TRUE(roots.empty());
    EXPECT_TRUE(entityManager.IsAliveUVE(existing));
    EXPECT_EQ(entityManager.GetEntityCountUVE(), entityCountBefore);
}

TEST_F(SceneSerializerUVETest, RestoreUVE_MalformedComponentData_RollsBackCreatedEntities) {
    const EntityUVE existing = entityManager.CreateEntityUVE();
    const std::size_t entityCountBefore = entityManager.GetEntityCountUVE();
    const std::string payloadText =
        R"({"entities":[{"localId":0,"components":{"NameComponentUVE":{"name":"Valid"}}},{"localId":1,"components":{"MeshComponentUVE":{"meshGuid":5}}}]})";
    const auto* const payloadBytes = reinterpret_cast<const std::byte*>(payloadText.data());
    const SceneSnapshotUVE snapshot{
        Asset::EncodeUveFileEnvelopeUVE(SceneAssetTypeUVE::Scene,
                                        std::vector<std::byte>{payloadBytes, payloadBytes + payloadText.size()}),
        SceneAssetTypeUVE::Scene};

    const std::vector<EntityUVE> roots = serializer.RestoreUVE(entityManager, snapshot);

    EXPECT_TRUE(roots.empty());
    EXPECT_TRUE(entityManager.IsAliveUVE(existing));
    EXPECT_EQ(entityManager.GetEntityCountUVE(), entityCountBefore);
}

TEST_F(SceneSerializerUVETest, RestoreUVE_InvalidMeshPayload_RollsBackCreatedEntities) {
    const EntityUVE existing = entityManager.CreateEntityUVE();
    const std::size_t entityCountBefore = entityManager.GetEntityCountUVE();
    const std::string payloadText =
        R"({"entities":[{"localId":0,"components":{"MeshComponentUVE":{"meshGuid":0,"materialGuid":5}}}]})";
    const auto* const payloadBytes = reinterpret_cast<const std::byte*>(payloadText.data());
    const SceneSnapshotUVE snapshot{
        Asset::EncodeUveFileEnvelopeUVE(SceneAssetTypeUVE::Scene,
                                        std::vector<std::byte>{payloadBytes, payloadBytes + payloadText.size()}),
        SceneAssetTypeUVE::Scene};

    const std::vector<EntityUVE> roots = serializer.RestoreUVE(entityManager, snapshot);

    EXPECT_TRUE(roots.empty());
    EXPECT_TRUE(entityManager.IsAliveUVE(existing));
    EXPECT_EQ(entityManager.GetEntityCountUVE(), entityCountBefore);
}

TEST_F(SceneSerializerUVETest, RestoreUVE_InvalidTransformPayload_RollsBackCreatedEntities) {
    const EntityUVE existing = entityManager.CreateEntityUVE();
    const std::size_t entityCountBefore = entityManager.GetEntityCountUVE();
    const std::string payloadText =
        R"({"entities":[{"localId":0,"components":{"TransformComponentUVE":{"localPosition":[0.0,0.0,0.0],"localRotation":[0.0,0.0,0.0,0.5],"localScale":[1.0,1.0,1.0]}}}]})";
    const auto* const payloadBytes = reinterpret_cast<const std::byte*>(payloadText.data());
    const SceneSnapshotUVE snapshot{
        Asset::EncodeUveFileEnvelopeUVE(SceneAssetTypeUVE::Scene,
                                        std::vector<std::byte>{payloadBytes, payloadBytes + payloadText.size()}),
        SceneAssetTypeUVE::Scene};

    const std::vector<EntityUVE> roots = serializer.RestoreUVE(entityManager, snapshot);

    EXPECT_TRUE(roots.empty());
    EXPECT_TRUE(entityManager.IsAliveUVE(existing));
    EXPECT_EQ(entityManager.GetEntityCountUVE(), entityCountBefore);
}

TEST_F(SceneSerializerUVETest, RestoreUVE_InvalidPrimitivePayload_RollsBackCreatedEntities) {
    const EntityUVE existing = entityManager.CreateEntityUVE();
    const std::size_t entityCountBefore = entityManager.GetEntityCountUVE();
    const std::string payloadText =
        R"({"entities":[{"localId":0,"components":{"PrimitiveMeshComponentUVE":{"kind":99,"baseColor":[0.5,0.5,0.5]}}}]})";
    const auto* const payloadBytes = reinterpret_cast<const std::byte*>(payloadText.data());
    const SceneSnapshotUVE snapshot{
        Asset::EncodeUveFileEnvelopeUVE(SceneAssetTypeUVE::Scene,
                                        std::vector<std::byte>{payloadBytes, payloadBytes + payloadText.size()}),
        SceneAssetTypeUVE::Scene};

    const std::vector<EntityUVE> roots = serializer.RestoreUVE(entityManager, snapshot);

    EXPECT_TRUE(roots.empty());
    EXPECT_TRUE(entityManager.IsAliveUVE(existing));
    EXPECT_EQ(entityManager.GetEntityCountUVE(), entityCountBefore);
}

TEST_F(SceneSerializerUVETest, RestoreUVE_InvalidCameraPayload_RollsBackCreatedEntities) {
    const EntityUVE existing = entityManager.CreateEntityUVE();
    const std::size_t entityCountBefore = entityManager.GetEntityCountUVE();
    const std::string payloadText =
        R"({"entities":[{"localId":0,"components":{"CameraComponentUVE":{"fieldOfViewDegrees":180.0,"nearPlane":0.1,"farPlane":100.0}}}]})";
    const auto* const payloadBytes = reinterpret_cast<const std::byte*>(payloadText.data());
    const SceneSnapshotUVE snapshot{
        Asset::EncodeUveFileEnvelopeUVE(SceneAssetTypeUVE::Scene,
                                        std::vector<std::byte>{payloadBytes, payloadBytes + payloadText.size()}),
        SceneAssetTypeUVE::Scene};

    const std::vector<EntityUVE> roots = serializer.RestoreUVE(entityManager, snapshot);

    EXPECT_TRUE(roots.empty());
    EXPECT_TRUE(entityManager.IsAliveUVE(existing));
    EXPECT_EQ(entityManager.GetEntityCountUVE(), entityCountBefore);
}

TEST_F(SceneSerializerUVETest, RestoreUVE_InvalidLightPayload_RollsBackCreatedEntities) {
    const EntityUVE existing = entityManager.CreateEntityUVE();
    const std::size_t entityCountBefore = entityManager.GetEntityCountUVE();
    const std::string payloadText =
        R"({"entities":[{"localId":0,"components":{"LightComponentUVE":{"color":[1.0,1.0,-0.1],"intensity":2.0,"type":0,"range":10.0,"spotAngleDegrees":45.0}}}]})";
    const auto* const payloadBytes = reinterpret_cast<const std::byte*>(payloadText.data());
    const SceneSnapshotUVE snapshot{
        Asset::EncodeUveFileEnvelopeUVE(SceneAssetTypeUVE::Scene,
                                        std::vector<std::byte>{payloadBytes, payloadBytes + payloadText.size()}),
        SceneAssetTypeUVE::Scene};

    const std::vector<EntityUVE> roots = serializer.RestoreUVE(entityManager, snapshot);

    EXPECT_TRUE(roots.empty());
    EXPECT_TRUE(entityManager.IsAliveUVE(existing));
    EXPECT_EQ(entityManager.GetEntityCountUVE(), entityCountBefore);
}

TEST_F(SceneSerializerUVETest, RestoreUVE_InvalidColliderPayload_RollsBackCreatedEntities) {
    const EntityUVE existing = entityManager.CreateEntityUVE();
    const std::size_t entityCountBefore = entityManager.GetEntityCountUVE();
    const std::string payloadText =
        R"({"entities":[{"localId":0,"components":{"ColliderComponentUVE":{"halfExtents":[0.5,0.0,0.5],"collisionLayer":1,"collisionMask":4294967295,"friction":0.0,"restitution":0.0,"density":1.0}}}]})";
    const auto* const payloadBytes = reinterpret_cast<const std::byte*>(payloadText.data());
    const SceneSnapshotUVE snapshot{
        Asset::EncodeUveFileEnvelopeUVE(SceneAssetTypeUVE::Scene,
                                        std::vector<std::byte>{payloadBytes, payloadBytes + payloadText.size()}),
        SceneAssetTypeUVE::Scene};

    const std::vector<EntityUVE> roots = serializer.RestoreUVE(entityManager, snapshot);

    EXPECT_TRUE(roots.empty());
    EXPECT_TRUE(entityManager.IsAliveUVE(existing));
    EXPECT_EQ(entityManager.GetEntityCountUVE(), entityCountBefore);
}

TEST_F(SceneSerializerUVETest, RestoreUVE_InvalidRigid3DPayload_RollsBackCreatedEntities) {
    const EntityUVE existing = entityManager.CreateEntityUVE();
    const std::size_t entityCountBefore = entityManager.GetEntityCountUVE();
    const std::string payloadText =
        R"({"entities":[{"localId":0,"components":{"Rigid3DComponentUVE":{"mass":1.0,"isKinematic":false,"velocity":[0.0,0.0,0.0],"drag":-0.1,"gravityScale":1.0}}}]})";
    const auto* const payloadBytes = reinterpret_cast<const std::byte*>(payloadText.data());
    const SceneSnapshotUVE snapshot{
        Asset::EncodeUveFileEnvelopeUVE(SceneAssetTypeUVE::Scene,
                                        std::vector<std::byte>{payloadBytes, payloadBytes + payloadText.size()}),
        SceneAssetTypeUVE::Scene};

    const std::vector<EntityUVE> roots = serializer.RestoreUVE(entityManager, snapshot);

    EXPECT_TRUE(roots.empty());
    EXPECT_TRUE(entityManager.IsAliveUVE(existing));
    EXPECT_EQ(entityManager.GetEntityCountUVE(), entityCountBefore);
}

TEST_F(SceneSerializerUVETest, RestoreUVE_InvalidAudioSourcePayload_RollsBackCreatedEntities) {
    const EntityUVE existing = entityManager.CreateEntityUVE();
    const std::size_t entityCountBefore = entityManager.GetEntityCountUVE();
    const std::string payloadText =
        R"({"entities":[{"localId":0,"components":{"AudioSourceComponentUVE":{"audioAssetPath":"","volume":1.0,"looping":false,"pitch":1.0,"spatial":true,"minDistance":1.0,"maxDistance":1.0,"attenuationCurve":0,"playOnAwake":true}}}]})";
    const auto* const payloadBytes = reinterpret_cast<const std::byte*>(payloadText.data());
    const SceneSnapshotUVE snapshot{
        Asset::EncodeUveFileEnvelopeUVE(SceneAssetTypeUVE::Scene,
                                        std::vector<std::byte>{payloadBytes, payloadBytes + payloadText.size()}),
        SceneAssetTypeUVE::Scene};

    const std::vector<EntityUVE> roots = serializer.RestoreUVE(entityManager, snapshot);

    EXPECT_TRUE(roots.empty());
    EXPECT_TRUE(entityManager.IsAliveUVE(existing));
    EXPECT_EQ(entityManager.GetEntityCountUVE(), entityCountBefore);
}

TEST_F(SceneSerializerUVETest, RestoreUVE_AnimationTargetsRemapToTheRestoredEntities) {
    // A door (Object3D) with an AnimationSequencer and an AnimationGraph beside it, both aimed at it.
    const EntityUVE door = entityManager.CreateEntityUVE();
    entityManager.AddComponentUVE<TransformComponentUVE>(door, TransformComponentUVE{});
    entityManager.AddComponentUVE<HierarchyComponentUVE>(door, HierarchyComponentUVE{});
    const EntityUVE player = entityManager.CreateEntityUVE();
    entityManager.AddComponentUVE<HierarchyComponentUVE>(player, HierarchyComponentUVE{door});
    AnimationDriverComponentUVE mixer;
    mixer.target = door;
    mixer.speedScale = 0.5F;
    mixer.animateScale = false;
    mixer.processCallback = AnimationProcessCallbackUVE::Physics;
    entityManager.AddComponentUVE<AnimationDriverComponentUVE>(player, mixer);
    AnimationSequencerComponentUVE animations;
    animations.clip = Asset::AssetGuidUVE{21U};
    animations.library = {Asset::AssetGuidUVE{20U}, Asset::AssetGuidUVE{21U}, Asset::AssetGuidUVE{22U}};
    entityManager.AddComponentUVE<AnimationSequencerComponentUVE>(player, animations);
    // A state machine with a transition and a parameter, so the whole graph goes through the file.
    AnimationGraphComponentUVE blend;
    blend.parameters = {AnimationParameterUVE{"speed", AnimationParameterTypeUVE::Float, 0.25F},
                        AnimationParameterUVE{"jump", AnimationParameterTypeUVE::Trigger, 0.0F}};
    AnimationGraphNodeUVE machine;
    machine.id = 3U;
    machine.kind = AnimationGraphNodeKindUVE::StateMachine;
    machine.name = "Locomotion";
    machine.position = Math::Vector2UVE{12.0F, -4.0F};
    machine.inputs = {2U, 0U};
    AnimationGraphTransitionUVE transition;
    transition.fromState = kAnyAnimationStateUVE;
    transition.toState = 1U;
    transition.conditions = {AnimationTransitionConditionUVE{AnimationConditionUVE::Triggered, "jump", 0.0F},
                             AnimationTransitionConditionUVE{AnimationConditionUVE::ParameterLess, "speed", 2.5F}};
    transition.exitPhase = 0.6F;
    transition.start = AnimationTransitionStartUVE::InStep;
    transition.curve = AnimationTransitionCurveUVE::EaseInOut;
    transition.interruptible = false;
    transition.enabled = false;
    transition.fadeSeconds = 0.05F;
    machine.transitions = {transition};
    machine.statePositions = {Math::Vector2UVE{40.0F, -20.0F}};
    machine.entryPosition = Math::Vector2UVE{-300.0F, 10.0F};
    machine.anyPosition = Math::Vector2UVE{-300.0F, 90.0F};
    blend.nodes[0].inputs = {3U};
    blend.nodes[1].clip = Asset::AssetGuidUVE{77U};
    blend.nodes[1].loop = false;
    blend.nodes[1].sync = true; // round-trips even where it has no effect
    blend.nodes.push_back(machine);
    // The later kinds' own fields, on loose objects (a graph half-built is still saved).
    AnimationGraphNodeUVE space;
    space.id = 4U;
    space.kind = AnimationGraphNodeKindUVE::BlendSpace2D;
    space.blendPoints = {AnimationBlendPointUVE{{0.0F, 0.0F}, Asset::AssetGuidUVE{21U}, 1.25F, false},
                         AnimationBlendPointUVE{{-1.5F, 2.0F}, Asset::AssetGuidUVE{22U}}};
    space.parameterY = "speed";
    space.valueY = 0.75F;
    space.blendMode = AnimationBlendModeUVE::NearestInStep;
    space.smoothingSeconds = 0.15F;
    blend.nodes.push_back(space);
    AnimationGraphNodeUVE layered;
    layered.id = 5U;
    layered.kind = AnimationGraphNodeKindUVE::LayeredBlend;
    layered.inputs = {0U, 0U};
    layered.bones = {"Spine", "LeftShoulder"};
    layered.restart = false;
    blend.nodes.push_back(layered);
    ASSERT_TRUE(IsAnimationGraphComponentValidUVE(blend)) << DescribeAnimationGraphProblemUVE(blend);
    entityManager.AddComponentUVE<AnimationGraphComponentUVE>(player, blend);

    const std::optional<SceneSnapshotUVE> snapshot = serializer.CaptureUVE(entityManager, {door}, SceneAssetTypeUVE::Scene);
    ASSERT_TRUE(snapshot.has_value());
    EntityManagerUVE restoredManager(memoryManager.GetDefaultAllocatorUVE(), eventSystem);
    const std::vector<EntityUVE> roots = serializer.RestoreUVE(restoredManager, *snapshot);
    ASSERT_EQ(roots.size(), 1U);
    const EntityUVE restoredDoor = roots[0];
    EntityUVE restoredPlayer = kInvalidEntityUVE;
    restoredManager.ForEachUVE<AnimationSequencerComponentUVE>(
        [&restoredPlayer](const EntityUVE entity, const AnimationSequencerComponentUVE&) { restoredPlayer = entity; });
    ASSERT_NE(restoredPlayer, kInvalidEntityUVE);
    AnimationDriverComponentUVE expectedMixer = mixer;
    expectedMixer.target = restoredDoor; // the one authored field that is remapped
    EXPECT_EQ(restoredManager.GetComponentUVE<AnimationDriverComponentUVE>(restoredPlayer), expectedMixer);
    EXPECT_TRUE(restoredManager.GetComponentUVE<AnimationSequencerComponentUVE>(restoredPlayer).HasSameSettingsUVE(animations))
        << "the clip and the player's whole animation list, in order";
    EXPECT_TRUE(restoredManager.GetComponentUVE<AnimationGraphComponentUVE>(restoredPlayer).HasSameSettingsUVE(blend));
    // A pure Object stays one: no transform appears on the way through the file.
    EXPECT_FALSE(restoredManager.HasComponentUVE<TransformComponentUVE>(restoredPlayer));
}

TEST_F(SceneSerializerUVETest, CaptureThenRestoreUVE_RayCastExclusionsFollowTheObjectsTheyName) {
    // Two objects in one file: the ray names the other in its exclusions. After the round trip the
    // reference must point at the RESTORED object, not at a raw index that may now belong to
    // something else entirely - which is exactly what a saved handle would have meant.
    const EntityUVE target = entityManager.CreateEntityUVE();
    entityManager.AddComponentUVE<TransformComponentUVE>(target, TransformComponentUVE{});
    entityManager.AddComponentUVE<HierarchyComponentUVE>(target, HierarchyComponentUVE{});
    const EntityUVE rayEntity = entityManager.CreateEntityUVE();
    entityManager.AddComponentUVE<TransformComponentUVE>(rayEntity, TransformComponentUVE{});
    entityManager.AddComponentUVE<HierarchyComponentUVE>(rayEntity, HierarchyComponentUVE{});
    RayCast3DComponentUVE ray;
    ray.length = 12.0F;
    ray.exclusions[0] = target;
    entityManager.AddComponentUVE<RayCast3DComponentUVE>(rayEntity, ray);

    const std::optional<SceneSnapshotUVE> snapshot =
        serializer.CaptureUVE(entityManager, {rayEntity, target}, SceneAssetTypeUVE::Scene);
    ASSERT_TRUE(snapshot.has_value());
    EntityManagerUVE restoredManager(memoryManager.GetDefaultAllocatorUVE(), eventSystem);
    const std::vector<EntityUVE> roots = serializer.RestoreUVE(restoredManager, *snapshot);
    ASSERT_EQ(roots.size(), 2U);
    const EntityUVE restoredRay = roots[0];
    const EntityUVE restoredTarget = roots[1];
    ASSERT_NE(restoredTarget, target);
    ASSERT_TRUE(restoredManager.HasComponentUVE<RayCast3DComponentUVE>(restoredRay));
    const RayCast3DComponentUVE& restored =
        restoredManager.GetComponentUVE<RayCast3DComponentUVE>(restoredRay);
    EXPECT_FLOAT_EQ(restored.length, 12.0F);
    EXPECT_EQ(CountRayCast3DExclusionsUVE(restored), 1U);
    EXPECT_EQ(restored.exclusions[0], restoredTarget);

    // Saving the restored pair again reproduces the same reference, so the remap is stable rather
    // than correct only once.
    const std::optional<SceneSnapshotUVE> second =
        serializer.CaptureUVE(restoredManager, {restoredRay, restoredTarget}, SceneAssetTypeUVE::Scene);
    ASSERT_TRUE(second.has_value());
    EntityManagerUVE secondManager(memoryManager.GetDefaultAllocatorUVE(), eventSystem);
    const std::vector<EntityUVE> secondRoots = serializer.RestoreUVE(secondManager, *second);
    ASSERT_EQ(secondRoots.size(), 2U);
    const RayCast3DComponentUVE& secondRay =
        secondManager.GetComponentUVE<RayCast3DComponentUVE>(secondRoots[0]);
    EXPECT_EQ(CountRayCast3DExclusionsUVE(secondRay), 1U);
    EXPECT_EQ(secondRay.exclusions[0], secondRoots[1]);
}

TEST_F(SceneSerializerUVETest, CaptureThenRestoreUVE_HitboxIgnoreFollowsTheObjectItNames) {
    const EntityUVE owner = entityManager.CreateEntityUVE();
    entityManager.AddComponentUVE<TransformComponentUVE>(owner, TransformComponentUVE{});
    entityManager.AddComponentUVE<HierarchyComponentUVE>(owner, HierarchyComponentUVE{});
    const EntityUVE hitboxEntity = entityManager.CreateEntityUVE();
    entityManager.AddComponentUVE<TransformComponentUVE>(hitboxEntity, TransformComponentUVE{});
    entityManager.AddComponentUVE<HierarchyComponentUVE>(hitboxEntity, HierarchyComponentUVE{});
    Hitbox3DComponentUVE hitbox;
    hitbox.damageChannel = "melee";
    hitbox.ignoreEntity = owner;
    entityManager.AddComponentUVE<Hitbox3DComponentUVE>(hitboxEntity, hitbox);

    const std::optional<SceneSnapshotUVE> snapshot =
        serializer.CaptureUVE(entityManager, {hitboxEntity, owner}, SceneAssetTypeUVE::Scene);
    ASSERT_TRUE(snapshot.has_value());
    EntityManagerUVE restoredManager(memoryManager.GetDefaultAllocatorUVE(), eventSystem);
    const std::vector<EntityUVE> roots = serializer.RestoreUVE(restoredManager, *snapshot);
    ASSERT_EQ(roots.size(), 2U);
    EntityUVE restoredHitbox = kInvalidEntityUVE;
    EntityUVE restoredOwner = kInvalidEntityUVE;
    for (const EntityUVE root : roots) {
        if (restoredManager.HasComponentUVE<Hitbox3DComponentUVE>(root)) {
            restoredHitbox = root;
        } else {
            restoredOwner = root;
        }
    }
    ASSERT_NE(restoredHitbox, kInvalidEntityUVE);
    ASSERT_NE(restoredOwner, kInvalidEntityUVE);
    ASSERT_NE(restoredOwner, owner);
    const Hitbox3DComponentUVE& restored = restoredManager.GetComponentUVE<Hitbox3DComponentUVE>(restoredHitbox);
    EXPECT_EQ(restored.damageChannel, "melee");
    EXPECT_EQ(restored.ignoreEntity, restoredOwner);
}

TEST_F(SceneSerializerUVETest, CaptureThenRestoreUVE_HurtboxIgnoreFollowsTheObjectItNames) {
    const EntityUVE owner = entityManager.CreateEntityUVE();
    entityManager.AddComponentUVE<TransformComponentUVE>(owner, TransformComponentUVE{});
    entityManager.AddComponentUVE<HierarchyComponentUVE>(owner, HierarchyComponentUVE{});
    const EntityUVE hurtboxEntity = entityManager.CreateEntityUVE();
    entityManager.AddComponentUVE<TransformComponentUVE>(hurtboxEntity, TransformComponentUVE{});
    entityManager.AddComponentUVE<HierarchyComponentUVE>(hurtboxEntity, HierarchyComponentUVE{});
    Hurtbox3DComponentUVE hurtbox;
    hurtbox.damageChannel = "player";
    hurtbox.ignoreEntity = owner;
    entityManager.AddComponentUVE<Hurtbox3DComponentUVE>(hurtboxEntity, hurtbox);

    const std::optional<SceneSnapshotUVE> snapshot =
        serializer.CaptureUVE(entityManager, {hurtboxEntity, owner}, SceneAssetTypeUVE::Scene);
    ASSERT_TRUE(snapshot.has_value());
    EntityManagerUVE restoredManager(memoryManager.GetDefaultAllocatorUVE(), eventSystem);
    const std::vector<EntityUVE> roots = serializer.RestoreUVE(restoredManager, *snapshot);
    ASSERT_EQ(roots.size(), 2U);
    EntityUVE restoredHurtbox = kInvalidEntityUVE;
    EntityUVE restoredOwner = kInvalidEntityUVE;
    for (const EntityUVE root : roots) {
        if (restoredManager.HasComponentUVE<Hurtbox3DComponentUVE>(root)) {
            restoredHurtbox = root;
        } else {
            restoredOwner = root;
        }
    }
    ASSERT_NE(restoredHurtbox, kInvalidEntityUVE);
    ASSERT_NE(restoredOwner, kInvalidEntityUVE);
    ASSERT_NE(restoredOwner, owner);
    const Hurtbox3DComponentUVE& restored = restoredManager.GetComponentUVE<Hurtbox3DComponentUVE>(restoredHurtbox);
    EXPECT_EQ(restored.damageChannel, "player");
    EXPECT_EQ(restored.ignoreEntity, restoredOwner);
}

TEST_F(SceneSerializerUVETest, CaptureThenRestoreUVE_InteractionAreaIgnoreFollowsTheObjectItNames) {
    const EntityUVE owner = entityManager.CreateEntityUVE();
    entityManager.AddComponentUVE<TransformComponentUVE>(owner, TransformComponentUVE{});
    entityManager.AddComponentUVE<HierarchyComponentUVE>(owner, HierarchyComponentUVE{});
    const EntityUVE areaEntity = entityManager.CreateEntityUVE();
    entityManager.AddComponentUVE<TransformComponentUVE>(areaEntity, TransformComponentUVE{});
    entityManager.AddComponentUVE<HierarchyComponentUVE>(areaEntity, HierarchyComponentUVE{});
    InteractionArea3DComponentUVE area;
    area.interactionTag = "door";
    area.ignoreEntity = owner;
    entityManager.AddComponentUVE<InteractionArea3DComponentUVE>(areaEntity, area);

    const std::optional<SceneSnapshotUVE> snapshot =
        serializer.CaptureUVE(entityManager, {areaEntity, owner}, SceneAssetTypeUVE::Scene);
    ASSERT_TRUE(snapshot.has_value());
    EntityManagerUVE restoredManager(memoryManager.GetDefaultAllocatorUVE(), eventSystem);
    const std::vector<EntityUVE> roots = serializer.RestoreUVE(restoredManager, *snapshot);
    ASSERT_EQ(roots.size(), 2U);
    EntityUVE restoredArea = kInvalidEntityUVE;
    EntityUVE restoredOwner = kInvalidEntityUVE;
    for (const EntityUVE root : roots) {
        if (restoredManager.HasComponentUVE<InteractionArea3DComponentUVE>(root)) {
            restoredArea = root;
        } else {
            restoredOwner = root;
        }
    }
    ASSERT_NE(restoredArea, kInvalidEntityUVE);
    ASSERT_NE(restoredOwner, kInvalidEntityUVE);
    ASSERT_NE(restoredOwner, owner);
    const InteractionArea3DComponentUVE& restored =
        restoredManager.GetComponentUVE<InteractionArea3DComponentUVE>(restoredArea);
    EXPECT_EQ(restored.interactionTag, "door");
    EXPECT_EQ(restored.ignoreEntity, restoredOwner);
}

TEST_F(SceneSerializerUVETest, CaptureThenRestoreUVE_ProjectileIgnoreFollowsTheObjectItNames) {
    const EntityUVE owner = entityManager.CreateEntityUVE();
    entityManager.AddComponentUVE<TransformComponentUVE>(owner, TransformComponentUVE{});
    entityManager.AddComponentUVE<HierarchyComponentUVE>(owner, HierarchyComponentUVE{});
    const EntityUVE projectileEntity = entityManager.CreateEntityUVE();
    entityManager.AddComponentUVE<TransformComponentUVE>(projectileEntity, TransformComponentUVE{});
    entityManager.AddComponentUVE<HierarchyComponentUVE>(projectileEntity, HierarchyComponentUVE{});
    Projectile3DComponentUVE projectile;
    projectile.velocity = Math::Vector3UVE{0.0F, 0.0F, 20.0F};
    projectile.ignoreEntity = owner;
    entityManager.AddComponentUVE<Projectile3DComponentUVE>(projectileEntity, projectile);

    const std::optional<SceneSnapshotUVE> snapshot =
        serializer.CaptureUVE(entityManager, {projectileEntity, owner}, SceneAssetTypeUVE::Scene);
    ASSERT_TRUE(snapshot.has_value());
    EntityManagerUVE restoredManager(memoryManager.GetDefaultAllocatorUVE(), eventSystem);
    const std::vector<EntityUVE> roots = serializer.RestoreUVE(restoredManager, *snapshot);
    ASSERT_EQ(roots.size(), 2U);
    EntityUVE restoredProjectile = kInvalidEntityUVE;
    EntityUVE restoredOwner = kInvalidEntityUVE;
    for (const EntityUVE root : roots) {
        if (restoredManager.HasComponentUVE<Projectile3DComponentUVE>(root)) {
            restoredProjectile = root;
        } else {
            restoredOwner = root;
        }
    }
    ASSERT_NE(restoredProjectile, kInvalidEntityUVE);
    ASSERT_NE(restoredOwner, kInvalidEntityUVE);
    ASSERT_NE(restoredOwner, owner);
    const Projectile3DComponentUVE& restored =
        restoredManager.GetComponentUVE<Projectile3DComponentUVE>(restoredProjectile);
    EXPECT_EQ(restored.velocity, projectile.velocity);
    EXPECT_EQ(restored.ignoreEntity, restoredOwner);
}

TEST_F(SceneSerializerUVETest, CaptureThenRestoreUVE_AnExclusionOutsideTheSavedSetIsDroppedRatherThanRenumbered) {
    // The excluded object lives in another part of the level, so this file does not contain it.
    // Writing its handle out would be writing a number the next load reads as whatever entity
    // happens to occupy that slot - so the reference is dropped instead, and the ray still loads
    // with everything else it owns.
    const EntityUVE outsider = entityManager.CreateEntityUVE();
    entityManager.AddComponentUVE<TransformComponentUVE>(outsider, TransformComponentUVE{});
    entityManager.AddComponentUVE<HierarchyComponentUVE>(outsider, HierarchyComponentUVE{});
    const EntityUVE rayEntity = entityManager.CreateEntityUVE();
    entityManager.AddComponentUVE<TransformComponentUVE>(rayEntity, TransformComponentUVE{});
    entityManager.AddComponentUVE<HierarchyComponentUVE>(rayEntity, HierarchyComponentUVE{});
    RayCast3DComponentUVE ray;
    ray.length = 12.0F;
    ray.exclusions[0] = outsider;
    entityManager.AddComponentUVE<RayCast3DComponentUVE>(rayEntity, ray);

    const std::optional<SceneSnapshotUVE> snapshot =
        serializer.CaptureUVE(entityManager, {rayEntity}, SceneAssetTypeUVE::Scene);
    ASSERT_TRUE(snapshot.has_value());
    EntityManagerUVE restoredManager(memoryManager.GetDefaultAllocatorUVE(), eventSystem);
    const std::vector<EntityUVE> roots = serializer.RestoreUVE(restoredManager, *snapshot);
    ASSERT_EQ(roots.size(), 1U);
    const RayCast3DComponentUVE& restored =
        restoredManager.GetComponentUVE<RayCast3DComponentUVE>(roots[0]);
    EXPECT_FLOAT_EQ(restored.length, 12.0F);
    EXPECT_EQ(CountRayCast3DExclusionsUVE(restored), 0U);

    // Dropped once means dropped for good: a second save of what was restored cannot resurrect a
    // reference the file never held.
    const std::optional<SceneSnapshotUVE> second =
        serializer.CaptureUVE(restoredManager, {roots[0]}, SceneAssetTypeUVE::Scene);
    ASSERT_TRUE(second.has_value());
    EntityManagerUVE secondManager(memoryManager.GetDefaultAllocatorUVE(), eventSystem);
    const std::vector<EntityUVE> secondRoots = serializer.RestoreUVE(secondManager, *second);
    ASSERT_EQ(secondRoots.size(), 1U);
    EXPECT_EQ(CountRayCast3DExclusionsUVE(
                  secondManager.GetComponentUVE<RayCast3DComponentUVE>(secondRoots[0])),
              0U);
}

TEST_F(SceneSerializerUVETest, LoadUVE_ALegacyRawExclusionListIsDroppedRatherThanMisread) {
    // Files written while `exclusions` held raw entity indices are still readable - the component
    // loads with every real field - but the numbers are NOT revived as handles: they were indices
    // into a pool that no longer exists, and guessing at them would aim a ray at a stranger.
    const std::string payloadText =
        R"({"entities":[{"localId":0,"components":{"RayCast3DComponentUVE":{"direction":[0.0,-1.0,0.0],"length":8.0,"collisionMask":4294967295,"enabled":true,"exclusions":[3]}}}]})";
    const auto* const payloadBytesPtr = reinterpret_cast<const std::byte*>(payloadText.data());
    const std::vector<std::byte> payloadBytes(payloadBytesPtr, payloadBytesPtr + payloadText.size());

    const std::filesystem::path path = "uve_scene_serializer_tests_raycast_legacy_exclusions.uvscene";
    std::filesystem::remove(path);
    ASSERT_TRUE(Asset::WriteUveFileUVE(path, SceneAssetTypeUVE::Scene, payloadBytes));

    const std::vector<EntityUVE> roots = serializer.LoadUVE(entityManager, path);
    ASSERT_EQ(roots.size(), 1U);
    const RayCast3DComponentUVE& loaded =
        entityManager.GetComponentUVE<RayCast3DComponentUVE>(roots[0]);
    EXPECT_FLOAT_EQ(loaded.length, 8.0F);
    EXPECT_TRUE(loaded.enabled);
    EXPECT_EQ(loaded.collisionMask, 0xFFFFFFFFU);
    EXPECT_EQ(CountRayCast3DExclusionsUVE(loaded), 0U);

    std::filesystem::remove(path);
}

TEST_F(SceneSerializerUVETest, RestoreUVE_TwoClipAnimationGraphBecomesABlendGraph) {
    const std::string payloadText =
        R"({"entities":[{"localId":0,"components":{"AnimationGraphComponentUVE":{"active":true,"clipA":11,"clipB":12,"blend":0.75,"speed":1.5}}}]})";
    const auto* const payloadBytes = reinterpret_cast<const std::byte*>(payloadText.data());
    const SceneSnapshotUVE snapshot{
        Asset::EncodeUveFileEnvelopeUVE(SceneAssetTypeUVE::Scene,
                                        std::vector<std::byte>{payloadBytes, payloadBytes + payloadText.size()}),
        SceneAssetTypeUVE::Scene};
    const std::vector<EntityUVE> roots = serializer.RestoreUVE(entityManager, snapshot);
    ASSERT_EQ(roots.size(), 1U);
    const AnimationGraphComponentUVE& tree = entityManager.GetComponentUVE<AnimationGraphComponentUVE>(roots[0]);
    ASSERT_EQ(tree.parameters.size(), 1U);
    EXPECT_EQ(tree.parameters[0].name, "blend");
    EXPECT_FLOAT_EQ(tree.parameters[0].value, 0.75F);
    ASSERT_EQ(tree.nodes.size(), 4U);
    EXPECT_EQ(tree.nodes[1].kind, AnimationGraphNodeKindUVE::Blend2);
    EXPECT_EQ(tree.nodes[1].parameter, "blend");
    EXPECT_EQ(tree.nodes[2].clip.value, 11U);
    EXPECT_EQ(tree.nodes[3].clip.value, 12U);
    EXPECT_FLOAT_EQ(tree.nodes[3].speed, 1.5F);
}

TEST_F(SceneSerializerUVETest, RestoreUVE_OldBlendSpaceInputsBecomeItsOwnPoints) {
    // Saved before blend spaces held their animations: Clips wired into slots, placed by "points".
    const std::string payloadText =
        R"({"entities":[{"localId":0,"components":{"AnimationGraphComponentUVE":{"parameters":[],"nodes":[)"
        R"({"id":1,"kind":0,"inputs":[2]},)"
        R"({"id":2,"kind":3,"inputs":[3,4],"points":[1.0,6.0],"parameter":"speed"},)"
        R"({"id":3,"kind":1,"clip":31,"speed":1.25,"loop":true},)"
        R"({"id":4,"kind":1,"clip":32,"loop":false}]}}}]})";
    const auto* const payloadBytes = reinterpret_cast<const std::byte*>(payloadText.data());
    const SceneSnapshotUVE snapshot{
        Asset::EncodeUveFileEnvelopeUVE(SceneAssetTypeUVE::Scene,
                                        std::vector<std::byte>{payloadBytes, payloadBytes + payloadText.size()}),
        SceneAssetTypeUVE::Scene};
    const std::vector<EntityUVE> roots = serializer.RestoreUVE(entityManager, snapshot);
    ASSERT_EQ(roots.size(), 1U);
    const AnimationGraphComponentUVE& tree = entityManager.GetComponentUVE<AnimationGraphComponentUVE>(roots[0]);
    EXPECT_TRUE(DescribeAnimationGraphProblemUVE(tree).empty()) << DescribeAnimationGraphProblemUVE(tree);
    ASSERT_EQ(tree.nodes.size(), 2U) << "the two Clips were folded into the space";
    const AnimationGraphNodeUVE& space = tree.nodes[1];
    EXPECT_TRUE(space.inputs.empty());
    ASSERT_EQ(space.blendPoints.size(), 2U);
    EXPECT_EQ(space.blendPoints[0].clip.value, 31U);
    EXPECT_FLOAT_EQ(space.blendPoints[0].speed, 1.25F);
    EXPECT_FLOAT_EQ(space.blendPoints[1].position.x, 6.0F);
    EXPECT_FALSE(space.blendPoints[1].loop);
    EXPECT_LT(space.areaMin.x, 1.0F) << "an area around the old points";
    EXPECT_GT(space.areaMax.x, 6.0F);
}

TEST_F(SceneSerializerUVETest, RestoreUVE_OldSingleConditionTransitionsBecomeAList) {
    // Saved when a transition had one condition in its own fields.
    const std::string payloadText =
        R"({"entities":[{"localId":0,"components":{"AnimationGraphComponentUVE":{"parameters":[],"nodes":[)"
        R"({"id":1,"kind":0,"inputs":[2]},)"
        R"({"id":2,"kind":7,"inputs":[0,0],"transitions":[)"
        R"({"from":0,"to":1,"condition":2,"parameter":"speed","threshold":0.5,"fadeSeconds":0.3},)"
        R"({"from":1,"to":0,"condition":0,"fadeSeconds":0.1}]}]}}}]})";
    const auto* const payloadBytes = reinterpret_cast<const std::byte*>(payloadText.data());
    const SceneSnapshotUVE snapshot{
        Asset::EncodeUveFileEnvelopeUVE(SceneAssetTypeUVE::Scene,
                                        std::vector<std::byte>{payloadBytes, payloadBytes + payloadText.size()}),
        SceneAssetTypeUVE::Scene};
    const std::vector<EntityUVE> roots = serializer.RestoreUVE(entityManager, snapshot);
    ASSERT_EQ(roots.size(), 1U);
    const AnimationGraphComponentUVE& tree = entityManager.GetComponentUVE<AnimationGraphComponentUVE>(roots[0]);
    EXPECT_TRUE(DescribeAnimationGraphProblemUVE(tree).empty()) << DescribeAnimationGraphProblemUVE(tree);
    const std::vector<AnimationGraphTransitionUVE>& transitions = tree.nodes[1].transitions;
    ASSERT_EQ(transitions.size(), 2U);
    ASSERT_EQ(transitions[0].conditions.size(), 1U);
    EXPECT_EQ(transitions[0].conditions[0].condition, AnimationConditionUVE::ParameterGreater);
    EXPECT_EQ(transitions[0].conditions[0].parameter, "speed");
    EXPECT_FLOAT_EQ(transitions[0].conditions[0].threshold, 0.5F);
    EXPECT_FLOAT_EQ(transitions[0].fadeSeconds, 0.3F);
    EXPECT_TRUE(transitions[1].conditions.empty()) << "Always is no condition";
    EXPECT_LT(transitions[1].exitPhase, 0.0F);
    EXPECT_TRUE(transitions[1].enabled);
    EXPECT_TRUE(transitions[1].interruptible);
}

TEST_F(SceneSerializerUVETest, RestoreUVE_LegacyAnimationSequencerFieldsCarryOver) {
    const std::string payloadText =
        R"({"entities":[{"localId":0,"components":{"AnimationSequencerComponentUVE":{"clipAssetPath":"anims/run.uvclip","playbackSpeed":2.0,"looping":false,"playOnAwake":true,"enabled":false}}}]})";
    const auto* const payloadBytes = reinterpret_cast<const std::byte*>(payloadText.data());
    const SceneSnapshotUVE snapshot{
        Asset::EncodeUveFileEnvelopeUVE(SceneAssetTypeUVE::Scene,
                                        std::vector<std::byte>{payloadBytes, payloadBytes + payloadText.size()}),
        SceneAssetTypeUVE::Scene};
    const std::vector<EntityUVE> roots = serializer.RestoreUVE(entityManager, snapshot);
    ASSERT_EQ(roots.size(), 1U);
    const AnimationSequencerComponentUVE& loaded = entityManager.GetComponentUVE<AnimationSequencerComponentUVE>(roots[0]);
    EXPECT_FLOAT_EQ(loaded.speed, 2.0F);
    EXPECT_EQ(loaded.loopMode, AnimationLoopModeUVE::Once);
    EXPECT_FALSE(loaded.autoplay); // it was disabled
    EXPECT_EQ(loaded.clip, Asset::kInvalidAssetGuidUVE);
    ASSERT_TRUE(entityManager.HasComponentUVE<AnimationDriverComponentUVE>(roots[0])) << "an old player gains its base";
    EXPECT_EQ(entityManager.GetComponentUVE<AnimationDriverComponentUVE>(roots[0]).target, kInvalidEntityUVE);
}

TEST_F(SceneSerializerUVETest, RestoreUVE_SequencerSavedBeforeTheAnimationDriverBaseMovesItsSettingsIntoOne) {
    // A door and a player aimed at it, saved when target, masks and clock lived on the player.
    const std::string payloadText =
        R"({"entities":[{"localId":0,"components":{"HierarchyComponentUVE":{"parentLocalId":-1}}},)"
        R"({"localId":1,"components":{"HierarchyComponentUVE":{"parentLocalId":0},"AnimationSequencerComponentUVE":)"
        R"({"clip":5,"animateScale":false,"processCallback":1,"targetLocalId":0}}}]})";
    const auto* const payloadBytes = reinterpret_cast<const std::byte*>(payloadText.data());
    const SceneSnapshotUVE snapshot{
        Asset::EncodeUveFileEnvelopeUVE(SceneAssetTypeUVE::Scene,
                                        std::vector<std::byte>{payloadBytes, payloadBytes + payloadText.size()}),
        SceneAssetTypeUVE::Scene};
    const std::vector<EntityUVE> roots = serializer.RestoreUVE(entityManager, snapshot);
    ASSERT_EQ(roots.size(), 1U);
    EntityUVE player = kInvalidEntityUVE;
    entityManager.ForEachUVE<AnimationSequencerComponentUVE>(
        [&player](const EntityUVE entity, const AnimationSequencerComponentUVE&) { player = entity; });
    ASSERT_NE(player, kInvalidEntityUVE);
    ASSERT_TRUE(entityManager.HasComponentUVE<AnimationDriverComponentUVE>(player));
    const AnimationDriverComponentUVE& mixer = entityManager.GetComponentUVE<AnimationDriverComponentUVE>(player);
    EXPECT_EQ(mixer.target, roots[0]);
    EXPECT_FALSE(mixer.animateScale);
    EXPECT_TRUE(mixer.animatePosition);
    EXPECT_EQ(mixer.processCallback, AnimationProcessCallbackUVE::Physics);
    EXPECT_EQ(entityManager.GetComponentUVE<AnimationSequencerComponentUVE>(player).clip.value, 5U);
}

TEST(AudioSourceComponentUVE, IsAudioSourceComponentValidUVE_EnforcesBoundedNulFreePath) {
    AudioSourceComponentUVE valid;
    valid.audioAssetPath = "sounds/player.wav";
    EXPECT_TRUE(IsAudioSourceComponentValidUVE(valid));
    valid.audioAssetPath.clear();
    EXPECT_TRUE(IsAudioSourceComponentValidUVE(valid));
    valid.audioAssetPath.assign(kMaximumAudioAssetPathBytesUVE + 1U, 'x');
    EXPECT_FALSE(IsAudioSourceComponentValidUVE(valid));
    valid.audioAssetPath.assign("sounds/player");
    valid.audioAssetPath.push_back('\0');
    valid.audioAssetPath += ".wav";
    EXPECT_FALSE(IsAudioSourceComponentValidUVE(valid));
}

TEST(ScriptComponentUVE, IsScriptComponentValidUVE_AllowsEmptyAndCanonicalRelativePaths) {
    EXPECT_TRUE(IsScriptComponentValidUVE(ScriptComponentUVE{""}));
    EXPECT_TRUE(IsScriptComponentValidUVE(ScriptComponentUVE{"scripts/player.lua"}));
    EXPECT_FALSE(IsScriptComponentValidUVE(ScriptComponentUVE{"/scripts/player.lua"}));
    EXPECT_FALSE(IsScriptComponentValidUVE(ScriptComponentUVE{"scripts/../player.lua"}));
    EXPECT_FALSE(IsScriptComponentValidUVE(ScriptComponentUVE{"scripts\\player.lua"}));
    EXPECT_FALSE(IsScriptComponentValidUVE(ScriptComponentUVE{"C:/scripts/player.lua"}));
    EXPECT_FALSE(IsScriptComponentValidUVE(ScriptComponentUVE{"file://scripts/player.lua"}));
    EXPECT_FALSE(IsScriptComponentValidUVE(ScriptComponentUVE{"scripts/player.lua:alternate"}));

    std::string embeddedNulPath{"scripts/player"};
    embeddedNulPath.push_back('\0');
    embeddedNulPath += ".lua";
    EXPECT_FALSE(IsScriptComponentValidUVE(ScriptComponentUVE{embeddedNulPath}));
    EXPECT_FALSE(IsScriptComponentValidUVE(
        ScriptComponentUVE{std::string(kMaximumScriptAssetPathBytesUVE + 1U, 'x')}));
}

TEST(ParticleEmitterComponentUVE, IsParticleEmitterComponentValidUVE_EnforcesBoundedBudget) {
    EXPECT_FALSE(IsParticleEmitterComponentValidUVE(ParticleEmitterComponentUVE{0U}));
    EXPECT_TRUE(IsParticleEmitterComponentValidUVE(ParticleEmitterComponentUVE{1U}));
    EXPECT_TRUE(IsParticleEmitterComponentValidUVE(
        ParticleEmitterComponentUVE{kMaximumParticleEmitterParticlesUVE}));
    EXPECT_FALSE(IsParticleEmitterComponentValidUVE(
        ParticleEmitterComponentUVE{kMaximumParticleEmitterParticlesUVE + 1U}));
}

TEST(ParticleEmitterComponentUVE, IsParticleEmitterComponentValidUVE_EnforcesRateAndLifetime) {
    ParticleEmitterComponentUVE component{};
    component.maxParticles = 8U;
    EXPECT_TRUE(IsParticleEmitterComponentValidUVE(component));
    component.emissionRate = -1.0F;
    EXPECT_FALSE(IsParticleEmitterComponentValidUVE(component));
    component.emissionRate = 10.0F;
    component.lifetimeSeconds = 0.0F;
    EXPECT_FALSE(IsParticleEmitterComponentValidUVE(component));
    component.lifetimeSeconds = kMaximumParticleEmitterLifetimeSecondsUVE + 1.0F;
    EXPECT_FALSE(IsParticleEmitterComponentValidUVE(component));
}

TEST(ParticleEmitterComponentUVE, ConsumeParticleEmitterAutoEmitCountUVE_SpendsRateAcrossFrames) {
    ParticleEmitterComponentUVE component{};
    component.maxParticles = 8U;
    component.emissionRate = 10.0F;
    float remainder = 0.0F;
    EXPECT_EQ(ConsumeParticleEmitterAutoEmitCountUVE(remainder, component, 0.05F, 0U), 0U);
    EXPECT_EQ(ConsumeParticleEmitterAutoEmitCountUVE(remainder, component, 0.05F, 0U), 1U);
    EXPECT_NEAR(remainder, 0.0F, 1.0e-5F);
    component.emitting = false;
    EXPECT_EQ(ConsumeParticleEmitterAutoEmitCountUVE(remainder, component, 1.0F, 0U), 0U);
}

TEST(ParticleEmitterComponentUVE, ConsumeParticleEmitterAutoEmitCountUVE_StopsAtTheBudget) {
    ParticleEmitterComponentUVE component{};
    component.maxParticles = 2U;
    component.emissionRate = 100.0F;
    float remainder = 0.0F;
    EXPECT_EQ(ConsumeParticleEmitterAutoEmitCountUVE(remainder, component, 1.0F, 0U), 2U);
    EXPECT_EQ(ConsumeParticleEmitterAutoEmitCountUVE(remainder, component, 1.0F, 2U), 0U);
}

TEST_F(SceneSerializerUVETest, RestoreUVE_ScriptSavedBeforeExportValuesLoadsWithNone) {
    const std::string payloadText =
        R"({"entities":[{"localId":0,"components":{"ScriptComponentUVE":{"scriptAssetPath":"scripts/a.uvs"}}}]})";
    const auto* const payloadBytes = reinterpret_cast<const std::byte*>(payloadText.data());
    const SceneSnapshotUVE snapshot{
        Asset::EncodeUveFileEnvelopeUVE(SceneAssetTypeUVE::Scene,
                                        std::vector<std::byte>{payloadBytes, payloadBytes + payloadText.size()}),
        SceneAssetTypeUVE::Scene};
    const std::vector<EntityUVE> roots = serializer.RestoreUVE(entityManager, snapshot);
    ASSERT_EQ(roots.size(), 1U);
    EXPECT_EQ(entityManager.GetComponentUVE<ScriptComponentUVE>(roots.front()).scriptAssetPath, "scripts/a.uvs");
    EXPECT_TRUE(entityManager.GetComponentUVE<ScriptComponentUVE>(roots.front()).exportValues.empty());
}

TEST(ScriptComponentUVE, IsScriptComponentValidUVE_BoundsExportValues) {
    EXPECT_TRUE(IsScriptComponentValidUVE(ScriptComponentUVE{"scripts/a.uvs", {{"speed", "1.0"}}}));
    EXPECT_FALSE(IsScriptComponentValidUVE(ScriptComponentUVE{"scripts/a.uvs", {{"", "1.0"}}}));
    EXPECT_FALSE(IsScriptComponentValidUVE(
        ScriptComponentUVE{"scripts/a.uvs", {{"speed", std::string(kMaximumScriptExportValueBytesUVE + 1U, 'x')}}}));
}

TEST_F(SceneSerializerUVETest, RestoreUVE_InvalidScriptPayload_RollsBackCreatedEntities) {
    const EntityUVE existing = entityManager.CreateEntityUVE();
    const std::size_t entityCountBefore = entityManager.GetEntityCountUVE();
    const std::string payloadText =
        R"({"entities":[{"localId":0,"components":{"ScriptComponentUVE":{"scriptAssetPath":"../escape.lua"}}}]})";
    const auto* const payloadBytes = reinterpret_cast<const std::byte*>(payloadText.data());
    const SceneSnapshotUVE snapshot{
        Asset::EncodeUveFileEnvelopeUVE(SceneAssetTypeUVE::Scene,
                                        std::vector<std::byte>{payloadBytes, payloadBytes + payloadText.size()}),
        SceneAssetTypeUVE::Scene};
    const std::vector<EntityUVE> roots = serializer.RestoreUVE(entityManager, snapshot);
    EXPECT_TRUE(roots.empty());
    EXPECT_TRUE(entityManager.IsAliveUVE(existing));
    EXPECT_EQ(entityManager.GetEntityCountUVE(), entityCountBefore);
}

TEST_F(SceneSerializerUVETest, RestoreUVE_ParticleEmitterSavedBeforeAutoEmitLoadsDefaults) {
    const std::string payloadText =
        R"({"entities":[{"localId":0,"components":{"ParticleEmitterComponentUVE":{"maxParticles":128}}}]})";
    const auto* const payloadBytes = reinterpret_cast<const std::byte*>(payloadText.data());
    const SceneSnapshotUVE snapshot{
        Asset::EncodeUveFileEnvelopeUVE(SceneAssetTypeUVE::Scene,
                                        std::vector<std::byte>{payloadBytes, payloadBytes + payloadText.size()}),
        SceneAssetTypeUVE::Scene};
    const std::vector<EntityUVE> roots = serializer.RestoreUVE(entityManager, snapshot);
    ASSERT_EQ(roots.size(), 1U);
    const ParticleEmitterComponentUVE& restored =
        entityManager.GetComponentUVE<ParticleEmitterComponentUVE>(roots.front());
    EXPECT_EQ(restored.maxParticles, 128U);
    EXPECT_TRUE(restored.emitting);
    EXPECT_FLOAT_EQ(restored.emissionRate, ParticleEmitterComponentUVE{}.emissionRate);
    EXPECT_FLOAT_EQ(restored.lifetimeSeconds, ParticleEmitterComponentUVE{}.lifetimeSeconds);
}

TEST_F(SceneSerializerUVETest, RestoreUVE_InvalidParticlePayload_RollsBackCreatedEntities) {
    const EntityUVE existing = entityManager.CreateEntityUVE();
    const std::size_t entityCountBefore = entityManager.GetEntityCountUVE();
    const std::string payloadText =
        R"({"entities":[{"localId":0,"components":{"ParticleEmitterComponentUVE":{"maxParticles":0}}}]})";
    const auto* const payloadBytes = reinterpret_cast<const std::byte*>(payloadText.data());
    const SceneSnapshotUVE snapshot{
        Asset::EncodeUveFileEnvelopeUVE(SceneAssetTypeUVE::Scene,
                                        std::vector<std::byte>{payloadBytes, payloadBytes + payloadText.size()}),
        SceneAssetTypeUVE::Scene};
    const std::vector<EntityUVE> roots = serializer.RestoreUVE(entityManager, snapshot);
    EXPECT_TRUE(roots.empty());
    EXPECT_TRUE(entityManager.IsAliveUVE(existing));
    EXPECT_EQ(entityManager.GetEntityCountUVE(), entityCountBefore);
}

TEST_F(SceneSerializerUVETest, RestoreUVE_InvalidEditorDescriptionPayload_RollsBackCreatedEntities) {
    const EntityUVE existing = entityManager.CreateEntityUVE();
    const std::size_t entityCountBefore = entityManager.GetEntityCountUVE();
    const std::string oversizedDescription(kMaximumEditorDescriptionBytesUVE + 1U, 'x');
    const std::string payloadText =
        R"({"entities":[{"localId":0,"components":{"EditorDescriptionComponentUVE":{"description":")" +
        oversizedDescription + R"("}}}]})";
    const auto* const payloadBytes = reinterpret_cast<const std::byte*>(payloadText.data());
    const SceneSnapshotUVE snapshot{
        Asset::EncodeUveFileEnvelopeUVE(SceneAssetTypeUVE::Scene,
                                        std::vector<std::byte>{payloadBytes, payloadBytes + payloadText.size()}),
        SceneAssetTypeUVE::Scene};
    const std::vector<EntityUVE> roots = serializer.RestoreUVE(entityManager, snapshot);
    EXPECT_TRUE(roots.empty());
    EXPECT_TRUE(entityManager.IsAliveUVE(existing));
    EXPECT_EQ(entityManager.GetEntityCountUVE(), entityCountBefore);
}

TEST_F(SceneSerializerUVETest, CaptureThenRestore_AbstractObjectBasesKeepTheirAuthoredValues) {
    const EntityUVE source = entityManager.CreateEntityUVE();
    entityManager.AddComponentUVE<HierarchyComponentUVE>(source, HierarchyComponentUVE{kInvalidEntityUVE});
    entityManager.AddComponentUVE<BoneModifierComponentUVE>(source, BoneModifierComponentUVE{false, 0.25F});
    PhysicsObjectComponentUVE object{};
    object.disableMode = PhysicsObjectDisableModeUVE::KeepActive;
    object.collisionPriority = 3.5F;
    object.inputRayPickable = false;
    object.inputCaptureOnDrag = true;
    entityManager.AddComponentUVE<PhysicsObjectComponentUVE>(source, object);
    entityManager.AddComponentUVE<RenderInstanceComponentUVE>(source, RenderInstanceComponentUVE{0x3U, -1.5F, false});

    const std::optional<SceneSnapshotUVE> snapshot = serializer.CaptureUVE(entityManager, {source}, SceneAssetTypeUVE::Scene);
    ASSERT_TRUE(snapshot.has_value());
    const std::vector<EntityUVE> roots = serializer.RestoreUVE(entityManager, *snapshot);
    ASSERT_EQ(roots.size(), 1U);
    EXPECT_EQ(entityManager.GetComponentUVE<BoneModifierComponentUVE>(roots.front()), (BoneModifierComponentUVE{false, 0.25F}));
    EXPECT_EQ(entityManager.GetComponentUVE<PhysicsObjectComponentUVE>(roots.front()), object);
    EXPECT_EQ(entityManager.GetComponentUVE<RenderInstanceComponentUVE>(roots.front()),
              (RenderInstanceComponentUVE{0x3U, -1.5F, false}));
}

TEST_F(SceneSerializerUVETest, RoundTripUVE_RenderInstanceFamilyKeepsEveryField) {
    const EntityUVE source = entityManager.CreateEntityUVE();
    SurfaceInstanceComponentUVE surface{};
    surface.materialOverridePath = "materials/red.uvmat";
    surface.transparency = 0.4F;
    surface.castShadow = SurfaceShadowModeUVE::ShadowsOnly;
    surface.visibilityRangeBegin = 2.0F;
    surface.visibilityRangeEnd = 50.0F;
    surface.visibilityRangeFadeMode = SurfaceFadeModeUVE::Self;
    LightEmitterComponentUVE light{};
    light.color = {1.0F, 0.5F, 0.25F};
    light.energy = 3.0F;
    light.shadowEnabled = true;
    light.shadowBlur = 2.0F;
    light.bakeMode = LightBakeModeUVE::Static;
    light.cullMask = 0x5U;
    Decal3DComponentUVE decal{};
    decal.modulate = {0.5F, 0.5F, 1.0F};
    decal.normalFade = 0.3F;
    decal.distanceFadeEnabled = true;
    decal.cullMask = 0x2U;
    FogVolume3DComponentUVE fog{};
    fog.shape = FogVolumeShapeUVE::Ellipsoid;
    fog.density = -0.5F;
    fog.emission = {0.1F, 0.2F, 0.3F};
    fog.edgeFade = 0.5F;
    entityManager.AddComponentUVE<SurfaceInstanceComponentUVE>(source, surface);
    entityManager.AddComponentUVE<LightEmitterComponentUVE>(source, light);
    entityManager.AddComponentUVE<Decal3DComponentUVE>(source, decal);
    entityManager.AddComponentUVE<FogVolume3DComponentUVE>(source, fog);

    const std::optional<SceneSnapshotUVE> snapshot = serializer.CaptureUVE(entityManager, {source}, SceneAssetTypeUVE::Scene);
    ASSERT_TRUE(snapshot.has_value());
    const std::vector<EntityUVE> roots = serializer.RestoreUVE(entityManager, *snapshot);
    ASSERT_EQ(roots.size(), 1U);
    EXPECT_EQ(entityManager.GetComponentUVE<SurfaceInstanceComponentUVE>(roots.front()), surface);
    EXPECT_EQ(entityManager.GetComponentUVE<LightEmitterComponentUVE>(roots.front()), light);
    EXPECT_EQ(entityManager.GetComponentUVE<Decal3DComponentUVE>(roots.front()), decal);
    EXPECT_EQ(entityManager.GetComponentUVE<FogVolume3DComponentUVE>(roots.front()), fog);
}

TEST_F(SceneSerializerUVETest, Decal3DLifetimeIsReArmedOnLoadAndTheCountdownIsNotSaved) {
    // The authored lifetime is scene data. The countdown that was left of it - and the fact that it
    // had already run out - are facts about the session that saved the scene, so a restored decal
    // starts its life whole rather than resuming a life that was nearly over. The same rule a
    // restored projectile's remaining flight follows.
    const EntityUVE source = entityManager.CreateEntityUVE();
    Decal3DComponentUVE decal{};
    decal.materialAssetPath = "materials/scorch.uemat";
    decal.size = Math::Vector3UVE{2.0F, 2.0F, 2.0F};
    decal.lifetime = 3.0F;
    // Runtime truth at save time.
    decal.remainingLifetime = 0.25F;
    decal.expired = true;
    entityManager.AddComponentUVE<Decal3DComponentUVE>(source, decal);

    const std::optional<SceneSnapshotUVE> snapshot =
        serializer.CaptureUVE(entityManager, {source}, SceneAssetTypeUVE::Scene);
    ASSERT_TRUE(snapshot.has_value());
    const std::vector<EntityUVE> restoredRoots = serializer.RestoreUVE(entityManager, *snapshot);
    ASSERT_EQ(restoredRoots.size(), 1U);

    const Decal3DComponentUVE restored =
        entityManager.GetComponentUVE<Decal3DComponentUVE>(restoredRoots.front());
    EXPECT_EQ(restored.materialAssetPath, "materials/scorch.uemat");
    EXPECT_EQ(restored.size, (Math::Vector3UVE{2.0F, 2.0F, 2.0F}));
    EXPECT_FLOAT_EQ(restored.lifetime, 3.0F);
    EXPECT_FLOAT_EQ(restored.remainingLifetime, 3.0F) << "a restored decal starts its life whole";
    EXPECT_FALSE(restored.expired) << "a saved countdown that had run out does not come back spent";
}

TEST_F(SceneSerializerUVETest, LoadUVE_ADecalFromBeforeTheCountdownLoadsPermanentAndAlive) {
    // A decal saved before the runtime countdown existed has no lifetime at all, which means the
    // default: permanent. It must load alive and unarmed rather than expired.
    const std::string payloadText =
        R"({"entities":[{"localId":0,"components":{"Decal3DComponentUVE":{"materialAssetPath":"materials/old_mark.uemat","size":[1.0,1.0,1.0],"projection":0}}}]})";
    const auto* const payloadBytesPtr = reinterpret_cast<const std::byte*>(payloadText.data());
    const std::vector<std::byte> payloadBytes(payloadBytesPtr, payloadBytesPtr + payloadText.size());

    const std::filesystem::path path = "uve_scene_serializer_tests_decal_legacy_countdown.uvscene";
    std::filesystem::remove(path);
    ASSERT_TRUE(Asset::WriteUveFileUVE(path, SceneAssetTypeUVE::Scene, payloadBytes));

    const std::vector<EntityUVE> roots = serializer.LoadUVE(entityManager, path);
    ASSERT_EQ(roots.size(), 1U);
    const Decal3DComponentUVE& loaded = entityManager.GetComponentUVE<Decal3DComponentUVE>(roots[0]);
    EXPECT_FLOAT_EQ(loaded.lifetime, 0.0F) << "the default is permanent";
    EXPECT_FLOAT_EQ(loaded.remainingLifetime, 0.0F);
    EXPECT_FALSE(loaded.expired);
    EXPECT_TRUE(IsDecal3DPaintingUVE(loaded));
    EXPECT_TRUE(IsDecal3DObjectComponentValidUVE(loaded));

    std::filesystem::remove(path);
}

TEST_F(SceneSerializerUVETest, RestoreUVE_DecalSavedBeforeItsNewFieldsLoadsWithDefaults) {
    const std::string payloadText =
        R"({"entities":[{"localId":0,"components":{"Decal3DComponentUVE":)"
        R"({"materialAssetPath":"decals/hole.uvmat","size":[2,1,2],"projection":1,"lifetime":5,"enabled":true}}}]})";
    const auto* const payloadBytes = reinterpret_cast<const std::byte*>(payloadText.data());
    const SceneSnapshotUVE snapshot{
        Asset::EncodeUveFileEnvelopeUVE(SceneAssetTypeUVE::Scene,
                                        std::vector<std::byte>{payloadBytes, payloadBytes + payloadText.size()}),
        SceneAssetTypeUVE::Scene};
    const std::vector<EntityUVE> roots = serializer.RestoreUVE(entityManager, snapshot);
    ASSERT_EQ(roots.size(), 1U);
    Decal3DComponentUVE expected{};
    expected.materialAssetPath = "decals/hole.uvmat";
    expected.size = {2.0F, 1.0F, 2.0F};
    expected.projection = DecalProjectionModeUVE::Cylinder;
    expected.lifetime = 5.0F;
    // Loading re-arms the runtime countdown from the authored lifetime, so the whole-component
    // comparison has to expect a life about to start rather than a life at zero - which is exactly
    // what a decal that has just been authored or loaded should be.
    expected.remainingLifetime = 5.0F;
    EXPECT_FALSE(expected.expired);
    const Decal3DComponentUVE& restored = entityManager.GetComponentUVE<Decal3DComponentUVE>(roots.front());
    EXPECT_FLOAT_EQ(restored.remainingLifetime, 5.0F);
    EXPECT_EQ(restored, expected);
}

TEST_F(SceneSerializerUVETest, RestoreUVE_MetadataSavedAsPlainStringsLoadsAsStringValues) {
    // The format before metadata values were typed. It must keep loading, not fail the scene.
    const std::string payloadText =
        R"({"entities":[{"localId":0,"components":{"ObjectMetadataComponentUVE":)"
        R"({"entries":[{"key":"door north","value":"open"}]}}}]})";
    const auto* const payloadBytes = reinterpret_cast<const std::byte*>(payloadText.data());
    const SceneSnapshotUVE snapshot{
        Asset::EncodeUveFileEnvelopeUVE(SceneAssetTypeUVE::Scene,
                                        std::vector<std::byte>{payloadBytes, payloadBytes + payloadText.size()}),
        SceneAssetTypeUVE::Scene};

    const std::vector<EntityUVE> roots = serializer.RestoreUVE(entityManager, snapshot);
    ASSERT_EQ(roots.size(), 1U);
    const ObjectMetadataComponentUVE& metadata = entityManager.GetComponentUVE<ObjectMetadataComponentUVE>(roots.front());
    ASSERT_EQ(metadata.entries.size(), 1U);
    EXPECT_EQ(metadata.entries.front().key, "door north");
    EXPECT_EQ(metadata.entries.front().value, Core::VariantUVE::MakeTextUVE(Core::VariantTypeUVE::String, "open"));
}

TEST_F(SceneSerializerUVETest, RestoreUVE_MalformedMetadataValueRollsBackCreatedEntities) {
    const EntityUVE existing = entityManager.CreateEntityUVE();
    const std::size_t entityCountBefore = entityManager.GetEntityCountUVE();
    // An unknown type name, a vector with the wrong arity, and a float written as null (what a NaN
    // would have become): each must fail the load cleanly rather than half-restore a scene.
    for (const std::string& value : {std::string{R"({"type":"Matrix7","value":0})"},
                                    std::string{R"({"type":"Vector3","value":[1,2]})"},
                                    std::string{R"({"type":"float","value":null})"}}) {
        const std::string payloadText =
            R"({"entities":[{"localId":0,"components":{"ObjectMetadataComponentUVE":{"entries":[{"key":"k","value":)" +
            value + R"(}]}}}]})";
        const auto* const payloadBytes = reinterpret_cast<const std::byte*>(payloadText.data());
        const SceneSnapshotUVE snapshot{
            Asset::EncodeUveFileEnvelopeUVE(SceneAssetTypeUVE::Scene,
                                            std::vector<std::byte>{payloadBytes, payloadBytes + payloadText.size()}),
            SceneAssetTypeUVE::Scene};
        EXPECT_TRUE(serializer.RestoreUVE(entityManager, snapshot).empty()) << value;
        EXPECT_TRUE(entityManager.IsAliveUVE(existing));
        EXPECT_EQ(entityManager.GetEntityCountUVE(), entityCountBefore) << value;
    }
}

TEST_F(SceneSerializerUVETest, RestoreUVE_InvalidPrefabInstancePayload_RollsBackCreatedEntities) {
    const EntityUVE existing = entityManager.CreateEntityUVE();
    const std::size_t entityCountBefore = entityManager.GetEntityCountUVE();
    const std::string payloadText =
        R"({"entities":[{"localId":0,"components":{"PrefabInstanceComponentUVE":{"sourcePrefabGuid":0}}}]})";
    const auto* const payloadBytes = reinterpret_cast<const std::byte*>(payloadText.data());
    const SceneSnapshotUVE snapshot{
        Asset::EncodeUveFileEnvelopeUVE(SceneAssetTypeUVE::Scene,
                                        std::vector<std::byte>{payloadBytes, payloadBytes + payloadText.size()}),
        SceneAssetTypeUVE::Scene};
    const std::vector<EntityUVE> roots = serializer.RestoreUVE(entityManager, snapshot);
    EXPECT_TRUE(roots.empty());
    EXPECT_TRUE(entityManager.IsAliveUVE(existing));
    EXPECT_EQ(entityManager.GetEntityCountUVE(), entityCountBefore);
}

TEST_F(SceneSerializerUVETest, RestoreUVE_LegacyPrefabInstanceDefaultsRevisions) {
    const std::string payloadText =
        R"({"entities":[{"localId":0,"components":{"PrefabInstanceComponentUVE":{"sourcePrefabGuid":77,"overrides":[]}}}]})";
    const auto* const payloadBytes = reinterpret_cast<const std::byte*>(payloadText.data());
    const SceneSnapshotUVE snapshot{
        Asset::EncodeUveFileEnvelopeUVE(SceneAssetTypeUVE::Scene,
                                        std::vector<std::byte>{payloadBytes, payloadBytes + payloadText.size()}),
        SceneAssetTypeUVE::Scene};

    const std::vector<EntityUVE> roots = serializer.RestoreUVE(entityManager, snapshot);
    ASSERT_EQ(roots.size(), 1U);
    const PrefabInstanceComponentUVE& component =
        entityManager.GetComponentUVE<PrefabInstanceComponentUVE>(roots.front());
    EXPECT_EQ(component.sourcePrefabGuid, Asset::AssetGuidUVE{77U});
    EXPECT_EQ(component.sourceRevision, 1U);
    EXPECT_EQ(component.instanceRevision, 1U);
    EXPECT_TRUE(component.overrides.empty());
}

TEST_F(SceneSerializerUVETest, SaveThenLoad_PrefabInstanceOverrides_RoundTripsDeterministically) {
    const EntityUVE entity = entityManager.CreateEntityUVE();
    const PrefabInstanceComponentUVE component{
        Asset::AssetGuidUVE{77U},
        {{"Transform.position", "[1,2,3]"}, {"Transform.rotation", "[0,0,0,1]"}},
        5U,
        6U};
    entityManager.AddComponentUVE<PrefabInstanceComponentUVE>(entity, component);

    const std::filesystem::path path = "uve_scene_serializer_tests_prefab_overrides.uvscene";
    std::filesystem::remove(path);
    ASSERT_TRUE(serializer.SaveUVE(entityManager, {entity}, path, SceneAssetTypeUVE::Scene));

    EntityManagerUVE loadedManager(memoryManager.GetDefaultAllocatorUVE(), eventSystem);
    const std::vector<EntityUVE> roots = serializer.LoadUVE(loadedManager, path);
    ASSERT_EQ(roots.size(), 1U);
    const PrefabInstanceComponentUVE& loaded =
        loadedManager.GetComponentUVE<PrefabInstanceComponentUVE>(roots.front());
    ASSERT_EQ(loaded.sourcePrefabGuid, Asset::AssetGuidUVE{77U});
    EXPECT_EQ(loaded.sourceRevision, 5U);
    EXPECT_EQ(loaded.instanceRevision, 6U);
    ASSERT_EQ(loaded.overrides.size(), 2U);
    EXPECT_EQ(loaded.overrides[0].propertyPath, "Transform.position");
    EXPECT_EQ(loaded.overrides[0].serializedValue, "[1,2,3]");
    EXPECT_EQ(loaded.overrides[1].propertyPath, "Transform.rotation");
    EXPECT_EQ(loaded.overrides[1].serializedValue, "[0,0,0,1]");

    std::filesystem::remove(path);
}

TEST_F(SceneSerializerUVETest, RestoreUVE_UnsortedPrefabOverrides_RollsBackCreatedEntities) {
    const EntityUVE existing = entityManager.CreateEntityUVE();
    const std::size_t entityCountBefore = entityManager.GetEntityCountUVE();
    const std::string payloadText =
        R"({"entities":[{"localId":0,"components":{"PrefabInstanceComponentUVE":{"sourcePrefabGuid":77,"overrides":[{"propertyPath":"Transform.rotation","serializedValue":"[0,0,0,1]"},{"propertyPath":"Transform.position","serializedValue":"[1,2,3]"}]}}}]})";
    const auto* const payloadBytes = reinterpret_cast<const std::byte*>(payloadText.data());
    const SceneSnapshotUVE snapshot{
        Asset::EncodeUveFileEnvelopeUVE(SceneAssetTypeUVE::Scene,
                                        std::vector<std::byte>{payloadBytes, payloadBytes + payloadText.size()}),
        SceneAssetTypeUVE::Scene};
    const std::vector<EntityUVE> roots = serializer.RestoreUVE(entityManager, snapshot);
    EXPECT_TRUE(roots.empty());
    EXPECT_TRUE(entityManager.IsAliveUVE(existing));
    EXPECT_EQ(entityManager.GetEntityCountUVE(), entityCountBefore);
}

TEST_F(SceneSerializerUVETest, SaveThenLoad_SingleEntityWithMultipleComponents_RoundTripsExactly) {
    const EntityUVE entity = entityManager.CreateEntityUVE();
    entityManager.AddComponentUVE<MeshComponentUVE>(
        entity, MeshComponentUVE{Asset::AssetGuidUVE{111}, Asset::AssetGuidUVE{222}});
    entityManager.AddComponentUVE<LightComponentUVE>(
        entity, LightComponentUVE{Math::Vector3UVE{0.2F, 0.4F, 0.6F}, 2.5F});
    entityManager.AddComponentUVE<Rigid3DComponentUVE>(entity, Rigid3DComponentUVE{5.0F, true});

    const std::filesystem::path path = "uve_scene_serializer_tests_single.uvscene";
    std::filesystem::remove(path);
    ASSERT_TRUE(serializer.SaveUVE(entityManager, {entity}, path, SceneAssetTypeUVE::Scene));

    EntityManagerUVE loadedManager(memoryManager.GetDefaultAllocatorUVE(), eventSystem);
    const std::vector<EntityUVE> roots = serializer.LoadUVE(loadedManager, path);
    ASSERT_EQ(roots.size(), 1U);
    const EntityUVE loaded = roots[0];

    EXPECT_EQ(loadedManager.GetComponentUVE<MeshComponentUVE>(loaded).meshGuid, Asset::AssetGuidUVE{111});
    EXPECT_EQ(loadedManager.GetComponentUVE<MeshComponentUVE>(loaded).materialGuid, Asset::AssetGuidUVE{222});
    EXPECT_FLOAT_EQ(loadedManager.GetComponentUVE<LightComponentUVE>(loaded).intensity, 2.5F);
    const Math::Vector3UVE expectedColor{0.2F, 0.4F, 0.6F};
    EXPECT_TRUE(loadedManager.GetComponentUVE<LightComponentUVE>(loaded).color == expectedColor);
    EXPECT_FLOAT_EQ(loadedManager.GetComponentUVE<Rigid3DComponentUVE>(loaded).mass, 5.0F);
    EXPECT_TRUE(loadedManager.GetComponentUVE<Rigid3DComponentUVE>(loaded).isKinematic);

    std::filesystem::remove(path);
}

TEST_F(SceneSerializerUVETest, SaveThenLoad_CharacterControllerComponentUVE_RoundTripsExactly) {
    const EntityUVE entity = entityManager.CreateEntityUVE();
    CharacterControllerComponentUVE characterController{};
    characterController.moveSpeed = 6.5F;
    characterController.jumpHeight = 2.25F;
    characterController.gravityScale = 1.5F;
    characterController.motionMode = CharacterMotionModeUVE::Floating;
    characterController.builtInMovement = false;
    characterController.airControl = 0.25F;
    characterController.coyoteTimeSeconds = 0.2F;
    characterController.jumpBufferSeconds = 0.15F;
    characterController.floorMaxAngleDegrees = 55.0F;
    characterController.wallMinSlideAngleDegrees = 20.0F;
    characterController.safeMargin = 0.004F;
    characterController.floorStopOnSlope = false;
    characterController.floorConstantSpeed = true;
    characterController.floorSnapLength = 0.3F;
    characterController.maxStepHeight = 0.45F;
    characterController.minStepWidth = 0.05F;
    characterController.floorBlockOnWall = true;
    characterController.platformOnLeave = CharacterPlatformLeaveModeUVE::AddUpwardVelocity;
    characterController.maximumPlatformSpeed = 12.0F;
    characterController.slideOnCeiling = false;
    characterController.upDirection = Math::Vector3UVE{0.0F, 0.0F, 1.0F};
    characterController.pushRigidBodies = true;
    characterController.pushStrength = 2.0F;
    characterController.maxPushSpeed = 7.0F;
    characterController.maxSlides = 12U;
    characterController.maximumContacts = 24U;
    characterController.velocity = Math::Vector3UVE{1.0F, -3.0F, 2.0F};
    characterController.grounded = true;
    entityManager.AddComponentUVE<CharacterControllerComponentUVE>(entity, characterController);

    const std::filesystem::path path = "uve_scene_serializer_tests_character_controller.uvscene";
    std::filesystem::remove(path);
    ASSERT_TRUE(serializer.SaveUVE(entityManager, {entity}, path, SceneAssetTypeUVE::Scene));

    EntityManagerUVE loadedManager(memoryManager.GetDefaultAllocatorUVE(), eventSystem);
    const std::vector<EntityUVE> roots = serializer.LoadUVE(loadedManager, path);
    ASSERT_EQ(roots.size(), 1U);
    const EntityUVE loaded = roots[0];

    const CharacterControllerComponentUVE& loadedController =
        loadedManager.GetComponentUVE<CharacterControllerComponentUVE>(loaded);
    EXPECT_FLOAT_EQ(loadedController.moveSpeed, 6.5F);
    EXPECT_FLOAT_EQ(loadedController.jumpHeight, 2.25F);
    EXPECT_FLOAT_EQ(loadedController.gravityScale, 1.5F);
    EXPECT_EQ(loadedController.motionMode, CharacterMotionModeUVE::Floating);
    EXPECT_FALSE(loadedController.builtInMovement);
    EXPECT_FLOAT_EQ(loadedController.airControl, 0.25F);
    EXPECT_FLOAT_EQ(loadedController.coyoteTimeSeconds, 0.2F);
    EXPECT_FLOAT_EQ(loadedController.jumpBufferSeconds, 0.15F);
    EXPECT_FLOAT_EQ(loadedController.floorMaxAngleDegrees, 55.0F);
    EXPECT_FLOAT_EQ(loadedController.wallMinSlideAngleDegrees, 20.0F);
    EXPECT_FLOAT_EQ(loadedController.safeMargin, 0.004F);
    EXPECT_FALSE(loadedController.floorStopOnSlope);
    EXPECT_TRUE(loadedController.floorConstantSpeed);
    EXPECT_FLOAT_EQ(loadedController.floorSnapLength, 0.3F);
    EXPECT_FLOAT_EQ(loadedController.maxStepHeight, 0.45F);
    EXPECT_FLOAT_EQ(loadedController.minStepWidth, 0.05F);
    EXPECT_TRUE(loadedController.floorBlockOnWall);
    EXPECT_EQ(loadedController.platformOnLeave, CharacterPlatformLeaveModeUVE::AddUpwardVelocity);
    EXPECT_FLOAT_EQ(loadedController.maximumPlatformSpeed, 12.0F);
    EXPECT_FALSE(loadedController.slideOnCeiling);
    EXPECT_EQ(loadedController.upDirection, (Math::Vector3UVE{0.0F, 0.0F, 1.0F}));
    EXPECT_TRUE(loadedController.pushRigidBodies);
    EXPECT_FLOAT_EQ(loadedController.pushStrength, 2.0F);
    EXPECT_FLOAT_EQ(loadedController.maxPushSpeed, 7.0F);
    EXPECT_EQ(loadedController.maxSlides, 12U);
    EXPECT_EQ(loadedController.maximumContacts, 24U);
    EXPECT_EQ(loadedController.velocity, (Math::Vector3UVE{1.0F, -3.0F, 2.0F}));
    EXPECT_TRUE(loadedController.grounded);

    std::filesystem::remove(path);
}

TEST_F(SceneSerializerUVETest, Load_OlderCharacterControllerPayloadKeepsItsValuesAndDefaultsTheRest) {
    // Before Character3D had its full set of settings, only these five were saved.
    const std::string payload =
        R"({"entities":[{"localId":0,"components":{"CharacterControllerComponentUVE":)"
        R"({"moveSpeed":6.5,"jumpHeight":2.25,"gravityScale":1.5,"verticalVelocity":-3.0,"isGrounded":true}}}]})";
    const auto* const bytes = reinterpret_cast<const std::byte*>(payload.data());
    const SceneSnapshotUVE snapshot{Asset::EncodeUveFileEnvelopeUVE(SceneAssetTypeUVE::Scene,
                                                                    std::vector<std::byte>{bytes, bytes + payload.size()}),
                                    SceneAssetTypeUVE::Scene};
    const std::vector<EntityUVE> roots = serializer.RestoreUVE(entityManager, snapshot);
    ASSERT_EQ(roots.size(), 1U);
    const CharacterControllerComponentUVE& loaded =
        entityManager.GetComponentUVE<CharacterControllerComponentUVE>(roots.front());
    EXPECT_FLOAT_EQ(loaded.moveSpeed, 6.5F);
    EXPECT_FLOAT_EQ(loaded.jumpHeight, 2.25F);
    EXPECT_FLOAT_EQ(loaded.gravityScale, 1.5F);
    EXPECT_EQ(loaded.velocity, (Math::Vector3UVE{0.0F, -3.0F, 0.0F}));
    EXPECT_TRUE(loaded.grounded);
    const CharacterControllerComponentUVE defaults{};
    EXPECT_EQ(loaded.motionMode, defaults.motionMode);
    EXPECT_EQ(loaded.builtInMovement, defaults.builtInMovement);
    EXPECT_FLOAT_EQ(loaded.maxStepHeight, defaults.maxStepHeight);
    EXPECT_FLOAT_EQ(loaded.floorMaxAngleDegrees, defaults.floorMaxAngleDegrees);
    EXPECT_FLOAT_EQ(loaded.safeMargin, defaults.safeMargin);
    EXPECT_FLOAT_EQ(loaded.minStepWidth, defaults.minStepWidth);
    EXPECT_EQ(loaded.platformOnLeave, defaults.platformOnLeave);
    EXPECT_EQ(loaded.maxSlides, defaults.maxSlides);
    EXPECT_EQ(loaded.maximumContacts, defaults.maximumContacts);
}

TEST_F(SceneSerializerUVETest, SaveThenLoad_UIComponentsUVE_RoundTripExactly) {
    const EntityUVE entity = entityManager.CreateEntityUVE();
    CanvasComponentUVE canvas{};
    canvas.visible = false;
    canvas.sortOrder = -5;
    entityManager.AddComponentUVE<CanvasComponentUVE>(entity, canvas);

    UITextComponentUVE text{};
    text.text = "Lives: 3";
    text.positionPixels = Math::Vector2UVE{10.0F, 20.0F};
    text.fontSize = 18.0F;
    text.color = Math::Vector3UVE{0.2F, 0.4F, 0.6F};
    text.alpha = 0.75F;
    entityManager.AddComponentUVE<UITextComponentUVE>(entity, text);

    UIImageComponentUVE image{};
    image.textureAssetGuid = Asset::AssetGuidUVE{0x4040U};
    image.positionPixels = Math::Vector2UVE{5.0F, 6.0F};
    image.sizePixels = Math::Vector2UVE{128.0F, 64.0F};
    image.tintColor = Math::Vector3UVE{0.9F, 0.1F, 0.5F};
    image.alpha = 0.5F;
    entityManager.AddComponentUVE<UIImageComponentUVE>(entity, image);

    UIButtonComponentUVE button{};
    button.positionPixels = Math::Vector2UVE{1.0F, 2.0F};
    button.sizePixels = Math::Vector2UVE{150.0F, 40.0F};
    button.normalColor = Math::Vector3UVE{0.1F, 0.1F, 0.1F};
    button.hoverColor = Math::Vector3UVE{0.2F, 0.2F, 0.2F};
    button.pressedColor = Math::Vector3UVE{0.3F, 0.3F, 0.3F};
    button.isHovered = true;
    button.wasClickedThisFrame = false;
    entityManager.AddComponentUVE<UIButtonComponentUVE>(entity, button);

    const std::filesystem::path path = "uve_scene_serializer_tests_ui_components.uvscene";
    std::filesystem::remove(path);
    ASSERT_TRUE(serializer.SaveUVE(entityManager, {entity}, path, SceneAssetTypeUVE::Scene));

    EntityManagerUVE loadedManager(memoryManager.GetDefaultAllocatorUVE(), eventSystem);
    const std::vector<EntityUVE> roots = serializer.LoadUVE(loadedManager, path);
    ASSERT_EQ(roots.size(), 1U);
    const EntityUVE loaded = roots[0];

    const CanvasComponentUVE& loadedCanvas = loadedManager.GetComponentUVE<CanvasComponentUVE>(loaded);
    EXPECT_FALSE(loadedCanvas.visible);
    EXPECT_EQ(loadedCanvas.sortOrder, -5);

    const UITextComponentUVE& loadedText = loadedManager.GetComponentUVE<UITextComponentUVE>(loaded);
    EXPECT_EQ(loadedText.text, "Lives: 3");
    EXPECT_FLOAT_EQ(loadedText.positionPixels.x, 10.0F);
    EXPECT_FLOAT_EQ(loadedText.fontSize, 18.0F);
    EXPECT_FLOAT_EQ(loadedText.color.z, 0.6F);
    EXPECT_FLOAT_EQ(loadedText.alpha, 0.75F);

    const UIImageComponentUVE& loadedImage = loadedManager.GetComponentUVE<UIImageComponentUVE>(loaded);
    EXPECT_EQ(loadedImage.textureAssetGuid.value, 0x4040U);
    EXPECT_FLOAT_EQ(loadedImage.sizePixels.y, 64.0F);
    EXPECT_FLOAT_EQ(loadedImage.tintColor.y, 0.1F);
    EXPECT_FLOAT_EQ(loadedImage.alpha, 0.5F);

    const UIButtonComponentUVE& loadedButton = loadedManager.GetComponentUVE<UIButtonComponentUVE>(loaded);
    EXPECT_FLOAT_EQ(loadedButton.sizePixels.x, 150.0F);
    EXPECT_FLOAT_EQ(loadedButton.hoverColor.x, 0.2F);
    EXPECT_TRUE(loadedButton.isHovered);
    EXPECT_FALSE(loadedButton.wasClickedThisFrame);

    std::filesystem::remove(path);
}

TEST_F(SceneSerializerUVETest, SaveThenLoad_AnimationSequencerComponentUVE_RoundTripsExactly) {
    const EntityUVE entity = entityManager.CreateEntityUVE();
    AnimationSequencerComponentUVE animation;
    animation.clip = Asset::AssetGuidUVE{0x1234U};
    animation.autoplay = false;
    animation.speed = -1.25F;
    animation.loopMode = AnimationLoopModeUVE::PingPong;
    animation.onFinish = AnimationFinishActionUVE::ReturnToStart;
    animation.startOffsetSeconds = 0.5F;
    animation.blendInSeconds = 0.25F;
    animation.relative = true;
    animation.isPlaying = true; // runtime state: must not be saved
    animation.currentTimeSeconds = 3.0F;
    entityManager.AddComponentUVE<AnimationSequencerComponentUVE>(entity, animation);

    const std::filesystem::path path = "uve_scene_serializer_tests_animation_player.uvscene";
    std::filesystem::remove(path);
    ASSERT_TRUE(serializer.SaveUVE(entityManager, {entity}, path, SceneAssetTypeUVE::Scene));

    EntityManagerUVE loadedManager(memoryManager.GetDefaultAllocatorUVE(), eventSystem);
    const std::vector<EntityUVE> roots = serializer.LoadUVE(loadedManager, path);
    ASSERT_EQ(roots.size(), 1U);
    const AnimationSequencerComponentUVE& loaded =
        loadedManager.GetComponentUVE<AnimationSequencerComponentUVE>(roots[0]);
    EXPECT_TRUE(loaded.HasSameSettingsUVE(animation));
    EXPECT_FALSE(loaded.isPlaying);
    EXPECT_EQ(loaded.currentTimeSeconds, 0.0F);

    std::filesystem::remove(path);
}

TEST_F(SceneSerializerUVETest, SaveThenLoad_Rigid3DAngularState_RoundTripsExactly) {
    const EntityUVE entity = entityManager.CreateEntityUVE();
    Rigid3DComponentUVE rigidBody;
    rigidBody.mass = 4.0F;
    rigidBody.angularVelocity = Math::Vector3UVE{1.0F, 2.0F, 3.0F};
    rigidBody.torque = Math::Vector3UVE{0.5F, 0.25F, 0.125F};
    rigidBody.inverseInertia = Math::Vector3UVE{0.2F, 0.3F, 0.4F};
    entityManager.AddComponentUVE<Rigid3DComponentUVE>(entity, rigidBody);

    const std::filesystem::path path = "uve_scene_serializer_tests_rigidbody_angular.uvscene";
    std::filesystem::remove(path);
    ASSERT_TRUE(serializer.SaveUVE(entityManager, {entity}, path, SceneAssetTypeUVE::Scene));

    EntityManagerUVE loadedManager(memoryManager.GetDefaultAllocatorUVE(), eventSystem);
    const std::vector<EntityUVE> roots = serializer.LoadUVE(loadedManager, path);
    ASSERT_EQ(roots.size(), 1U);
    const Rigid3DComponentUVE& loaded = loadedManager.GetComponentUVE<Rigid3DComponentUVE>(roots[0]);
    EXPECT_EQ(loaded.angularVelocity, rigidBody.angularVelocity);
    EXPECT_EQ(loaded.torque, rigidBody.torque);
    EXPECT_EQ(loaded.inverseInertia, rigidBody.inverseInertia);

    std::filesystem::remove(path);
}

TEST_F(SceneSerializerUVETest, LoadUVE_LegacyRigid3DWithoutAngularFields_UsesZeroDefaults) {
    const std::string payloadText =
        R"({"entities":[{"localId":0,"components":{"Rigid3DComponentUVE":{"mass":1.0,"isKinematic":false,"velocity":[0.0,0.0,0.0],"drag":0.0,"gravityScale":1.0}}}]})";
    const auto* const payloadBytesPtr = reinterpret_cast<const std::byte*>(payloadText.data());
    const std::vector<std::byte> payloadBytes(payloadBytesPtr, payloadBytesPtr + payloadText.size());

    const std::filesystem::path path = "uve_scene_serializer_tests_rigidbody_angular_legacy.uvscene";
    std::filesystem::remove(path);
    ASSERT_TRUE(Asset::WriteUveFileUVE(path, SceneAssetTypeUVE::Scene, payloadBytes));

    const std::vector<EntityUVE> roots = serializer.LoadUVE(entityManager, path);
    ASSERT_EQ(roots.size(), 1U);
    const Rigid3DComponentUVE& loaded = entityManager.GetComponentUVE<Rigid3DComponentUVE>(roots[0]);
    EXPECT_EQ(loaded.angularVelocity, Math::Vector3UVE{});
    EXPECT_EQ(loaded.torque, Math::Vector3UVE{});
    EXPECT_EQ(loaded.inverseInertia, Math::Vector3UVE{});

    std::filesystem::remove(path);
}

TEST_F(SceneSerializerUVETest, SaveThenLoad_NameComponentUVE_RoundTripsExactly) {
    const EntityUVE entity = entityManager.CreateEntityUVE();
    entityManager.AddComponentUVE<NameComponentUVE>(entity, NameComponentUVE{"Gameplay Root"});

    const std::filesystem::path path = "uve_scene_serializer_tests_name.uvscene";
    std::filesystem::remove(path);
    ASSERT_TRUE(serializer.SaveUVE(entityManager, {entity}, path, SceneAssetTypeUVE::Scene));

    EntityManagerUVE loadedManager(memoryManager.GetDefaultAllocatorUVE(), eventSystem);
    const std::vector<EntityUVE> roots = serializer.LoadUVE(loadedManager, path);
    ASSERT_EQ(roots.size(), 1U);
    ASSERT_TRUE(loadedManager.HasComponentUVE<NameComponentUVE>(roots[0]));
    EXPECT_EQ(loadedManager.GetComponentUVE<NameComponentUVE>(roots[0]).name, "Gameplay Root");

    std::filesystem::remove(path);
}

TEST_F(SceneSerializerUVETest, LoadUVE_LegacyDocumentWithoutNameComponent_RemainsValid) {
    const std::string payloadText =
        R"({"entities":[{"localId":0,"components":{"TransformComponentUVE":{"localPosition":[0.0,0.0,0.0],"localRotation":[0.0,0.0,0.0,1.0],"localScale":[1.0,1.0,1.0]}}}]})";
    const auto* const payloadBytesPtr = reinterpret_cast<const std::byte*>(payloadText.data());
    const std::vector<std::byte> payloadBytes(payloadBytesPtr, payloadBytesPtr + payloadText.size());

    const std::filesystem::path path = "uve_scene_serializer_tests_name_legacy.uvscene";
    std::filesystem::remove(path);
    ASSERT_TRUE(Asset::WriteUveFileUVE(path, SceneAssetTypeUVE::Scene, payloadBytes));

    const std::vector<EntityUVE> roots = serializer.LoadUVE(entityManager, path);
    ASSERT_EQ(roots.size(), 1U);
    EXPECT_FALSE(entityManager.HasComponentUVE<NameComponentUVE>(roots[0]));

    std::filesystem::remove(path);
}

TEST_F(SceneSerializerUVETest, SaveThenLoad_AreaComponentUVE_RoundTripsExtentsAndMasks) {
    const EntityUVE entity = entityManager.CreateEntityUVE();
    AreaComponentUVE area{Math::Vector3UVE{2.0F, 3.0F, 4.0F}, 4U, 0x0000FFFFU};
    area.gravityOverride = AreaSpaceOverrideModeUVE::Replace;
    area.gravityDirection = Math::Vector3UVE{0.0F, 1.0F, 0.0F};
    area.gravityMagnitude = 4.5F;
    area.gravityPoint = true;
    area.gravityPointOffset = Math::Vector3UVE{1.0F, 0.0F, -1.0F};
    area.gravityPointUnitDistance = 3.0F;
    area.linearDampOverride = AreaSpaceOverrideModeUVE::Combine;
    area.linearDamp = 0.4F;
    area.angularDampOverride = AreaSpaceOverrideModeUVE::ReplaceCombine;
    area.angularDamp = 0.25F;
    area.priority = 7;
    area.overlappingBodyCount = 3U;
    area.overlappingBodiesTruncated = true;
    entityManager.AddComponentUVE<AreaComponentUVE>(entity, area);

    const std::filesystem::path path = "uve_scene_serializer_tests_area.uvscene";
    std::filesystem::remove(path);
    ASSERT_TRUE(serializer.SaveUVE(entityManager, {entity}, path, SceneAssetTypeUVE::Scene));

    EntityManagerUVE loadedManager(memoryManager.GetDefaultAllocatorUVE(), eventSystem);
    const std::vector<EntityUVE> roots = serializer.LoadUVE(loadedManager, path);
    ASSERT_EQ(roots.size(), 1U);
    const AreaComponentUVE& loaded = loadedManager.GetComponentUVE<AreaComponentUVE>(roots[0]);

    EXPECT_EQ(loaded.halfExtents, area.halfExtents);
    EXPECT_EQ(loaded.collisionLayer, 4U);
    EXPECT_EQ(loaded.collisionMask, 0x0000FFFFU);
    EXPECT_EQ(loaded.gravityOverride, AreaSpaceOverrideModeUVE::Replace);
    EXPECT_EQ(loaded.gravityDirection, area.gravityDirection);
    EXPECT_FLOAT_EQ(loaded.gravityMagnitude, 4.5F);
    EXPECT_TRUE(loaded.gravityPoint);
    EXPECT_EQ(loaded.gravityPointOffset, area.gravityPointOffset);
    EXPECT_FLOAT_EQ(loaded.gravityPointUnitDistance, 3.0F);
    EXPECT_EQ(loaded.linearDampOverride, AreaSpaceOverrideModeUVE::Combine);
    EXPECT_FLOAT_EQ(loaded.linearDamp, 0.4F);
    EXPECT_EQ(loaded.angularDampOverride, AreaSpaceOverrideModeUVE::ReplaceCombine);
    EXPECT_FLOAT_EQ(loaded.angularDamp, 0.25F);
    EXPECT_EQ(loaded.priority, 7);
    EXPECT_EQ(loaded.overlappingBodyCount, 0U);
    EXPECT_FALSE(loaded.overlappingBodiesTruncated);

    std::filesystem::remove(path);
}

TEST_F(SceneSerializerUVETest, SaveThenLoad_ColliderComponentUVE_RoundTripsFrictionRestitutionDensity) {
    const EntityUVE entity = entityManager.CreateEntityUVE();
    ColliderComponentUVE collider{Math::Vector3UVE{0.5F, 0.5F, 0.5F}};
    collider.collisionLayer = 2;
    collider.collisionMask = 0x0000FFFFU;
    collider.friction = 0.4F;
    collider.restitution = 0.9F;
    collider.density = 2.5F;
    collider.disabled = true;
    entityManager.AddComponentUVE<ColliderComponentUVE>(entity, collider);

    const std::filesystem::path path = "uve_scene_serializer_tests_collider.uvscene";
    std::filesystem::remove(path);
    ASSERT_TRUE(serializer.SaveUVE(entityManager, {entity}, path, SceneAssetTypeUVE::Scene));

    EntityManagerUVE loadedManager(memoryManager.GetDefaultAllocatorUVE(), eventSystem);
    const std::vector<EntityUVE> roots = serializer.LoadUVE(loadedManager, path);
    ASSERT_EQ(roots.size(), 1U);
    const ColliderComponentUVE& loaded = loadedManager.GetComponentUVE<ColliderComponentUVE>(roots[0]);

    EXPECT_EQ(loaded.collisionLayer, 2U);
    EXPECT_EQ(loaded.collisionMask, 0x0000FFFFU);
    EXPECT_FLOAT_EQ(loaded.friction, 0.4F);
    EXPECT_FLOAT_EQ(loaded.restitution, 0.9F);
    EXPECT_FLOAT_EQ(loaded.density, 2.5F);
    EXPECT_EQ(loaded.shapeType, ColliderShapeTypeUVE::Box);
    EXPECT_FLOAT_EQ(loaded.radius, 0.5F);
    EXPECT_FLOAT_EQ(loaded.height, 1.0F);
    EXPECT_TRUE(loaded.disabled);

    std::filesystem::remove(path);
}

TEST_F(SceneSerializerUVETest, SaveThenLoad_ColliderComponentUVE_RoundTripsSphereAndCapsuleDescriptors) {
    const EntityUVE sphereEntity = entityManager.CreateEntityUVE();
    ColliderComponentUVE sphere{Math::Vector3UVE{0.5F, 0.5F, 0.5F}};
    sphere.shapeType = ColliderShapeTypeUVE::Sphere;
    sphere.radius = 1.25F;
    entityManager.AddComponentUVE<ColliderComponentUVE>(sphereEntity, sphere);

    const EntityUVE capsuleEntity = entityManager.CreateEntityUVE();
    ColliderComponentUVE capsule{Math::Vector3UVE{0.5F, 0.5F, 0.5F}};
    capsule.shapeType = ColliderShapeTypeUVE::Capsule;
    capsule.radius = 0.4F;
    capsule.height = 2.4F;
    entityManager.AddComponentUVE<ColliderComponentUVE>(capsuleEntity, capsule);

    const std::filesystem::path path = "uve_scene_serializer_tests_expanded_collider.uvscene";
    std::filesystem::remove(path);
    ASSERT_TRUE(serializer.SaveUVE(entityManager, {sphereEntity, capsuleEntity}, path, SceneAssetTypeUVE::Scene));

    EntityManagerUVE loadedManager(memoryManager.GetDefaultAllocatorUVE(), eventSystem);
    const std::vector<EntityUVE> roots = serializer.LoadUVE(loadedManager, path);
    ASSERT_EQ(roots.size(), 2U);
    const ColliderComponentUVE& loadedSphere = loadedManager.GetComponentUVE<ColliderComponentUVE>(roots[0]);
    const ColliderComponentUVE& loadedCapsule = loadedManager.GetComponentUVE<ColliderComponentUVE>(roots[1]);

    EXPECT_EQ(loadedSphere.shapeType, ColliderShapeTypeUVE::Sphere);
    EXPECT_FLOAT_EQ(loadedSphere.radius, 1.25F);
    EXPECT_EQ(loadedCapsule.shapeType, ColliderShapeTypeUVE::Capsule);
    EXPECT_FLOAT_EQ(loadedCapsule.radius, 0.4F);
    EXPECT_FLOAT_EQ(loadedCapsule.height, 2.4F);

    std::filesystem::remove(path);
}

TEST_F(SceneSerializerUVETest, Capture_InvalidExpandedColliderShapeFailsBeforeSnapshotPublication) {
    const EntityUVE entity = entityManager.CreateEntityUVE();
    ColliderComponentUVE collider{Math::Vector3UVE{0.5F, 0.5F, 0.5F}};
    collider.shapeType = ColliderShapeTypeUVE::Sphere;
    collider.radius = 0.0F;
    entityManager.AddComponentUVE<ColliderComponentUVE>(entity, collider);
    const std::size_t entityCountBefore = entityManager.GetEntityCountUVE();

    EXPECT_FALSE(serializer.CaptureUVE(entityManager, {entity}, SceneAssetTypeUVE::Scene).has_value());
    EXPECT_EQ(entityManager.GetEntityCountUVE(), entityCountBefore);
    EXPECT_TRUE(entityManager.IsAliveUVE(entity));
}

TEST_F(SceneSerializerUVETest, SaveThenLoad_LightComponentUVE_RoundTripsTypeRangeSpotAngle) {
    const EntityUVE entity = entityManager.CreateEntityUVE();
    LightComponentUVE light;
    light.color = Math::Vector3UVE{0.9F, 0.8F, 0.7F};
    light.intensity = 3.5F;
    light.type = LightTypeUVE::Spot;
    light.range = 15.0F;
    light.spotAngleDegrees = 30.0F;
    entityManager.AddComponentUVE<LightComponentUVE>(entity, light);

    const std::filesystem::path path = "uve_scene_serializer_tests_light.uvscene";
    std::filesystem::remove(path);
    ASSERT_TRUE(serializer.SaveUVE(entityManager, {entity}, path, SceneAssetTypeUVE::Scene));

    EntityManagerUVE loadedManager(memoryManager.GetDefaultAllocatorUVE(), eventSystem);
    const std::vector<EntityUVE> roots = serializer.LoadUVE(loadedManager, path);
    ASSERT_EQ(roots.size(), 1U);
    const LightComponentUVE& loaded = loadedManager.GetComponentUVE<LightComponentUVE>(roots[0]);

    EXPECT_TRUE(loaded.color == light.color);
    EXPECT_FLOAT_EQ(loaded.intensity, 3.5F);
    EXPECT_EQ(loaded.type, LightTypeUVE::Spot);
    EXPECT_FLOAT_EQ(loaded.range, 15.0F);
    EXPECT_FLOAT_EQ(loaded.spotAngleDegrees, 30.0F);

    std::filesystem::remove(path);
}

TEST_F(SceneSerializerUVETest, LoadUVE_OldFormatLightComponentUVEMissingNewFields_FillsInDefaults) {
    // Hand-built payload matching the pre-Increment-25 LightComponentUVE JSON shape (only
    // "color"/"intensity" - no "type"/"range"/"spotAngleDegrees" keys), proving old saves still
    // load correctly via the fromJson lambda's json.value(...) backward-compat defaults. Written
    // as a raw string literal (not nlohmann::json) since nlohmann_json is deliberately linked
    // PRIVATE to uve_scene, confined to scene_serializer_uve.cpp - not available to test code.
    const std::string payloadText =
        R"({"entities":[{"localId":0,"components":{"LightComponentUVE":{"color":[0.1,0.2,0.3],"intensity":4.0}}}]})";
    const auto* const payloadBytesPtr = reinterpret_cast<const std::byte*>(payloadText.data());
    const std::vector<std::byte> payloadBytes(payloadBytesPtr, payloadBytesPtr + payloadText.size());

    const std::filesystem::path path = "uve_scene_serializer_tests_light_old_format.uvscene";
    std::filesystem::remove(path);
    ASSERT_TRUE(Asset::WriteUveFileUVE(path, SceneAssetTypeUVE::Scene, payloadBytes));

    const std::vector<EntityUVE> roots = serializer.LoadUVE(entityManager, path);
    ASSERT_EQ(roots.size(), 1U);
    const LightComponentUVE& loaded = entityManager.GetComponentUVE<LightComponentUVE>(roots[0]);

    EXPECT_TRUE(loaded.color == (Math::Vector3UVE{0.1F, 0.2F, 0.3F}));
    EXPECT_FLOAT_EQ(loaded.intensity, 4.0F);
    EXPECT_EQ(loaded.type, LightTypeUVE::Directional); // default
    EXPECT_FLOAT_EQ(loaded.range, 10.0F);               // default
    EXPECT_FLOAT_EQ(loaded.spotAngleDegrees, 45.0F);     // default

    std::filesystem::remove(path);
}

TEST_F(SceneSerializerUVETest, SaveThenLoad_AudioSourceComponentUVE_RoundTripsAllFields) {
    const EntityUVE entity = entityManager.CreateEntityUVE();
    AudioSourceComponentUVE audioSource;
    audioSource.audioAssetPath = "sounds/explosion.wav";
    audioSource.volume = 0.6F;
    audioSource.looping = true;
    audioSource.pitch = 1.5F;
    audioSource.spatial = false;
    audioSource.minDistance = 2.0F;
    audioSource.maxDistance = 50.0F;
    audioSource.attenuationCurve = AudioAttenuationCurveUVE::InverseSquare;
    audioSource.mixerGroup = "SFX";
    audioSource.playOnAwake = false;
    entityManager.AddComponentUVE<AudioSourceComponentUVE>(entity, audioSource);

    const std::filesystem::path path = "uve_scene_serializer_tests_audio_source.uvscene";
    std::filesystem::remove(path);
    ASSERT_TRUE(serializer.SaveUVE(entityManager, {entity}, path, SceneAssetTypeUVE::Scene));

    EntityManagerUVE loadedManager(memoryManager.GetDefaultAllocatorUVE(), eventSystem);
    const std::vector<EntityUVE> roots = serializer.LoadUVE(loadedManager, path);
    ASSERT_EQ(roots.size(), 1U);
    const AudioSourceComponentUVE& loaded = loadedManager.GetComponentUVE<AudioSourceComponentUVE>(roots[0]);

    EXPECT_EQ(loaded.audioAssetPath, "sounds/explosion.wav");
    EXPECT_FLOAT_EQ(loaded.volume, 0.6F);
    EXPECT_TRUE(loaded.looping);
    EXPECT_FLOAT_EQ(loaded.pitch, 1.5F);
    EXPECT_FALSE(loaded.spatial);
    EXPECT_FLOAT_EQ(loaded.minDistance, 2.0F);
    EXPECT_FLOAT_EQ(loaded.maxDistance, 50.0F);
    EXPECT_EQ(loaded.attenuationCurve, AudioAttenuationCurveUVE::InverseSquare);
    EXPECT_EQ(loaded.mixerGroup, "SFX");
    EXPECT_FALSE(loaded.playOnAwake);

    std::filesystem::remove(path);
}

TEST_F(SceneSerializerUVETest, SaveThenLoad_AudioSourceComponentUVE_DefaultsRoundTripCorrectly) {
    // Confirms every new field's default survives a save/load cycle, matching what a scene
    // serialized before this increment's fields existed would fall back to on load, via the
    // json.value(key, default) idiom in the fromJson registration.
    const EntityUVE entity = entityManager.CreateEntityUVE();
    entityManager.AddComponentUVE<AudioSourceComponentUVE>(entity, AudioSourceComponentUVE{});

    const std::filesystem::path path = "uve_scene_serializer_tests_audio_source_defaults.uvscene";
    std::filesystem::remove(path);
    ASSERT_TRUE(serializer.SaveUVE(entityManager, {entity}, path, SceneAssetTypeUVE::Scene));

    EntityManagerUVE loadedManager(memoryManager.GetDefaultAllocatorUVE(), eventSystem);
    const std::vector<EntityUVE> roots = serializer.LoadUVE(loadedManager, path);
    ASSERT_EQ(roots.size(), 1U);
    const AudioSourceComponentUVE& loaded = loadedManager.GetComponentUVE<AudioSourceComponentUVE>(roots[0]);

    EXPECT_FALSE(loaded.looping);
    EXPECT_FLOAT_EQ(loaded.pitch, 1.0F);
    EXPECT_TRUE(loaded.spatial);
    EXPECT_FLOAT_EQ(loaded.minDistance, 1.0F);
    EXPECT_FLOAT_EQ(loaded.maxDistance, 25.0F);
    EXPECT_EQ(loaded.attenuationCurve, AudioAttenuationCurveUVE::Linear);
    EXPECT_TRUE(loaded.mixerGroup.empty());
    EXPECT_TRUE(loaded.playOnAwake);

    std::filesystem::remove(path);
}

TEST_F(SceneSerializerUVETest, SaveThenLoad_Hierarchy_RemapsParentCorrectly) {
    const EntityUVE parent = entityManager.CreateEntityUVE();
    entityManager.AddComponentUVE<HierarchyComponentUVE>(parent, HierarchyComponentUVE{kInvalidEntityUVE});
    const EntityUVE child = entityManager.CreateEntityUVE();
    entityManager.AddComponentUVE<HierarchyComponentUVE>(child, HierarchyComponentUVE{parent});

    const std::filesystem::path path = "uve_scene_serializer_tests_hierarchy.uvscene";
    std::filesystem::remove(path);
    ASSERT_TRUE(serializer.SaveUVE(entityManager, {parent}, path, SceneAssetTypeUVE::Scene));

    EntityManagerUVE loadedManager(memoryManager.GetDefaultAllocatorUVE(), eventSystem);
    const std::vector<EntityUVE> roots = serializer.LoadUVE(loadedManager, path);
    ASSERT_EQ(roots.size(), 1U);
    const EntityUVE loadedParent = roots[0];

    EntityUVE loadedChild = kInvalidEntityUVE;
    loadedManager.ForEachUVE<HierarchyComponentUVE>(
        [&loadedChild, loadedParent](EntityUVE entity, HierarchyComponentUVE& hierarchy) {
            if (hierarchy.parent == loadedParent) {
                loadedChild = entity;
            }
        });
    EXPECT_NE(loadedChild, kInvalidEntityUVE);

    std::filesystem::remove(path);
}

TEST_F(SceneSerializerUVETest, PrefabSerializationRequiresSingleRootBeforeEntityPublication) {
    const EntityUVE rootA = entityManager.CreateEntityUVE();
    entityManager.AddComponentUVE<MeshComponentUVE>(rootA, MeshComponentUVE{Asset::AssetGuidUVE{301}, Asset::AssetGuidUVE{302}});
    const EntityUVE rootB = entityManager.CreateEntityUVE();
    entityManager.AddComponentUVE<MeshComponentUVE>(rootB, MeshComponentUVE{Asset::AssetGuidUVE{303}, Asset::AssetGuidUVE{304}});

    const std::filesystem::path rejectedSavePath = "uve_scene_serializer_tests_multi_root_prefab.uvprefab";
    const std::filesystem::path malformedPath = "uve_scene_serializer_tests_malformed_multi_root.uvprefab";
    std::filesystem::remove(rejectedSavePath);
    std::filesystem::remove(malformedPath);
    EXPECT_FALSE(serializer.SaveUVE(entityManager, {rootA, rootB}, rejectedSavePath, SceneAssetTypeUVE::Prefab));
    EXPECT_FALSE(std::filesystem::exists(rejectedSavePath));

    const std::optional<SceneSnapshotUVE> sceneSnapshot =
        serializer.CaptureUVE(entityManager, {rootA, rootB}, SceneAssetTypeUVE::Scene);
    ASSERT_TRUE(sceneSnapshot.has_value());
    const auto decoded = Asset::DecodeUveFileEnvelopeUVE(sceneSnapshot->bytes, "multi-root prefab test");
    ASSERT_TRUE(decoded.has_value());
    ASSERT_TRUE(Asset::WriteUveFileUVE(malformedPath, Asset::AssetKindUVE::Prefab, decoded->second));

    const std::size_t entityCountBeforeLoad = entityManager.GetEntityCountUVE();
    EXPECT_TRUE(serializer.LoadUVE(entityManager, malformedPath).empty());
    EXPECT_EQ(entityManager.GetEntityCountUVE(), entityCountBeforeLoad);

    std::filesystem::remove(rejectedSavePath);
    std::filesystem::remove(malformedPath);
}

TEST_F(SceneSerializerUVETest, SaveUVE_FailedTemporaryPublicationPreservesExistingDestination) {
    const EntityUVE root = entityManager.CreateEntityUVE();
    entityManager.AddComponentUVE<MeshComponentUVE>(root, MeshComponentUVE{Asset::AssetGuidUVE{101}, Asset::AssetGuidUVE{202}});

    const std::filesystem::path path = "uve_scene_serializer_tests_atomic_destination.uvscene";
    const std::filesystem::path temporaryPath = path.string() + ".uve_scene_tmp";
    std::filesystem::remove(path);
    std::filesystem::remove_all(temporaryPath);
    ASSERT_TRUE(serializer.SaveUVE(entityManager, {root}, path, SceneAssetTypeUVE::Scene));
    const auto before = Asset::ReadUveFileUVE(path);
    ASSERT_TRUE(before.has_value());

    ASSERT_TRUE(std::filesystem::create_directory(temporaryPath));
    {
        std::ofstream lockFile(temporaryPath / "lock");
        ASSERT_TRUE(lockFile.is_open());
        lockFile << "keep temporary path non-empty";
    }

    EXPECT_FALSE(serializer.SaveUVE(entityManager, {root}, path, SceneAssetTypeUVE::Scene));
    const auto after = Asset::ReadUveFileUVE(path);
    ASSERT_TRUE(after.has_value());
    EXPECT_EQ(after->first.assetType, before->first.assetType);
    EXPECT_EQ(after->second, before->second);

    std::filesystem::remove(path);
    std::filesystem::remove_all(temporaryPath);
}

TEST_F(SceneSerializerUVETest, SaveThenLoad_MultipleRoots_AllPresentInFileOrder) {
    const EntityUVE rootA = entityManager.CreateEntityUVE();
    entityManager.AddComponentUVE<MeshComponentUVE>(rootA, MeshComponentUVE{Asset::AssetGuidUVE{1}, Asset::AssetGuidUVE{2}});
    const EntityUVE rootB = entityManager.CreateEntityUVE();
    entityManager.AddComponentUVE<MeshComponentUVE>(rootB, MeshComponentUVE{Asset::AssetGuidUVE{3}, Asset::AssetGuidUVE{4}});

    const std::filesystem::path path = "uve_scene_serializer_tests_multi_root.uvscene";
    std::filesystem::remove(path);
    ASSERT_TRUE(serializer.SaveUVE(entityManager, {rootA, rootB}, path, SceneAssetTypeUVE::Scene));

    EntityManagerUVE loadedManager(memoryManager.GetDefaultAllocatorUVE(), eventSystem);
    const std::vector<EntityUVE> roots = serializer.LoadUVE(loadedManager, path);
    ASSERT_EQ(roots.size(), 2U);
    EXPECT_EQ(loadedManager.GetComponentUVE<MeshComponentUVE>(roots[0]).meshGuid, Asset::AssetGuidUVE{1});
    EXPECT_EQ(loadedManager.GetComponentUVE<MeshComponentUVE>(roots[1]).meshGuid, Asset::AssetGuidUVE{3});

    std::filesystem::remove(path);
}

TEST_F(SceneSerializerUVETest, SaveUVE_NeverSerializesWorldTransformComponent_AndSceneGraphRecomputesAfterLoad) {
    SceneGraphUVE sceneGraph;
    const EntityUVE entity = entityManager.CreateEntityUVE();
    TransformComponentUVE local;
    local.localPosition = Math::Vector3UVE{1.0F, 2.0F, 3.0F};
    sceneGraph.AttachTransformUVE(entityManager, entity, local);
    sceneGraph.UpdateUVE(entityManager);

    const std::filesystem::path path = "uve_scene_serializer_tests_no_world_transform.uvscene";
    std::filesystem::remove(path);
    ASSERT_TRUE(serializer.SaveUVE(entityManager, {entity}, path, SceneAssetTypeUVE::Scene));

    std::ifstream rawFile(path, std::ios::binary);
    const std::string contents((std::istreambuf_iterator<char>(rawFile)), std::istreambuf_iterator<char>());
    EXPECT_EQ(contents.find("WorldTransformComponentUVE"), std::string::npos);

    EntityManagerUVE loadedManager(memoryManager.GetDefaultAllocatorUVE(), eventSystem);
    const std::vector<EntityUVE> roots = serializer.LoadUVE(loadedManager, path);
    ASSERT_EQ(roots.size(), 1U);
    sceneGraph.UpdateUVE(loadedManager);

    const WorldTransformComponentUVE& world =
        loadedManager.GetComponentUVE<WorldTransformComponentUVE>(roots[0]);
    EXPECT_TRUE(world.worldPosition == local.localPosition);

    std::filesystem::remove(path);
}

TEST_F(SceneSerializerUVETest, LoadUVE_MissingFile_ReturnsEmptyVector) {
    const std::vector<EntityUVE> roots =
        serializer.LoadUVE(entityManager, "uve_scene_serializer_tests_nonexistent.uvscene");
    EXPECT_TRUE(roots.empty());
}

TEST_F(SceneSerializerUVETest, LoadUVE_BadMagic_ReturnsEmptyAndLogsError) {
    const std::filesystem::path path = "uve_scene_serializer_tests_bad_magic.uvscene";
    {
        std::ofstream file(path, std::ios::binary);
        file << "NOT A VALID UVE FILE AT ALL";
    }

    Debug::LoggerUVE logger;
    logger.Init(Debug::LogLevelUVE::Trace);
    auto memorySink = std::make_unique<Debug::MemorySinkUVE>();
    Debug::MemorySinkUVE* const memorySinkPtr = memorySink.get();
    logger.AddSink(std::move(memorySink));

    const std::vector<EntityUVE> roots = serializer.LoadUVE(entityManager, path);
    EXPECT_TRUE(roots.empty());

    const std::vector<Debug::LogMessageUVE> messages = memorySinkPtr->GetMessagesUVE();
    const bool foundError =
        std::any_of(messages.begin(), messages.end(), [](const Debug::LogMessageUVE& message) {
            return message.level == Debug::LogLevelUVE::Error &&
                   message.message.contains("bad magic");
        });
    EXPECT_TRUE(foundError);

    logger.Shutdown();
    std::filesystem::remove(path);
}

} // namespace

TEST_F(SceneSerializerUVETest, SaveLoadUVE_SceneRootMarkerRoundTrips) {
    // The scene root's marker component must survive a save/load cycle like every other
    // component: a captured document root carrying it restores with the marker intact, and
    // the restored hierarchy still has exactly one root.
    SceneGraphUVE sceneGraph;
    const EntityUVE root = entityManager.CreateEntityUVE();
    sceneGraph.AttachTransformUVE(entityManager, root, TransformComponentUVE{});
    entityManager.AddComponentUVE<NameComponentUVE>(root, NameComponentUVE{"SceneRoot"});
    entityManager.AddComponentUVE<SceneRootComponentUVE>(root, SceneRootComponentUVE{});
    const EntityUVE child = entityManager.CreateEntityUVE();
    sceneGraph.AttachTransformUVE(entityManager, child, TransformComponentUVE{});
    entityManager.AddComponentUVE<NameComponentUVE>(child, NameComponentUVE{"Empty"});
    sceneGraph.SetParentUVE(entityManager, child, root);

    const std::filesystem::path path = "uve_scene_serializer_tests_scene_root_marker.uvscene";
    std::filesystem::remove(path);
    ASSERT_TRUE(serializer.SaveUVE(entityManager, {root}, path, Asset::AssetKindUVE::Scene));

    EntityManagerUVE loadedManager{memoryManager.GetDefaultAllocatorUVE(), eventSystem};
    const std::vector<EntityUVE> restoredRoots = serializer.LoadUVE(loadedManager, path);
    ASSERT_EQ(restoredRoots.size(), 1U);
    EXPECT_TRUE(loadedManager.HasComponentUVE<SceneRootComponentUVE>(restoredRoots[0U]));
    std::filesystem::remove(path);
}

TEST_F(SceneSerializerUVETest, CaptureRestoreUVE_KeepsSiblingOrder) {
    // The file stores no order number: siblings are written in order and read back in sequence.
    SceneGraphUVE sceneGraph;
    const EntityUVE root = entityManager.CreateEntityUVE();
    sceneGraph.AttachTransformUVE(entityManager, root, TransformComponentUVE{});
    entityManager.AddComponentUVE<NameComponentUVE>(root, NameComponentUVE{"Root"});
    std::vector<EntityUVE> children;
    for (const char* const name : {"A", "B", "C"}) {
        const EntityUVE child = entityManager.CreateEntityUVE();
        sceneGraph.AttachTransformUVE(entityManager, child, TransformComponentUVE{});
        entityManager.AddComponentUVE<NameComponentUVE>(child, NameComponentUVE{name});
        sceneGraph.SetParentUVE(entityManager, child, root);
        children.push_back(child);
    }
    ASSERT_TRUE(sceneGraph.SetSiblingIndexUVE(entityManager, children[2], 0U)); // C, A, B

    const std::optional<SceneSnapshotUVE> snapshot = serializer.CaptureUVE(entityManager, {root}, SceneAssetTypeUVE::Scene);
    ASSERT_TRUE(snapshot.has_value());
    const std::vector<EntityUVE> restored = serializer.RestoreUVE(entityManager, *snapshot);
    ASSERT_EQ(restored.size(), 1U);
    std::vector<std::string> names;
    for (const EntityUVE child : sceneGraph.GetChildrenUVE(entityManager, restored.front())) {
        names.push_back(entityManager.GetComponentUVE<NameComponentUVE>(child).name);
    }
    EXPECT_EQ(names, (std::vector<std::string>{"C", "A", "B"}));
}

TEST_F(SceneSerializerUVETest, SaveLoadUVE_VisibilityRoundTripsTheAuthoredFlagOnly) {
    // Hiding an object has to survive a save. It also has to survive WITHOUT carrying the derived
    // field across: visibleInHierarchy depends on the entity's ancestors, so persisting it would
    // store an answer that is wrong the moment an object is saved under one parent and loaded under
    // another.
    SceneGraphUVE sceneGraph;
    const EntityUVE source = entityManager.CreateEntityUVE();
    sceneGraph.AttachTransformUVE(entityManager, source, TransformComponentUVE{});
    entityManager.AddComponentUVE<VisibilityComponentUVE>(
        source, VisibilityComponentUVE{/*visible=*/false, /*visibleInHierarchy=*/false});

    const std::filesystem::path path = "uve_scene_serializer_tests_visibility.uvscene";
    std::filesystem::remove(path);
    ASSERT_TRUE(serializer.SaveUVE(entityManager, {source}, path, Asset::AssetKindUVE::Scene));

    EntityManagerUVE loadedManager{memoryManager.GetDefaultAllocatorUVE(), eventSystem};
    const std::vector<EntityUVE> restored = serializer.LoadUVE(loadedManager, path);
    ASSERT_EQ(restored.size(), 1U);
    ASSERT_TRUE(loadedManager.HasComponentUVE<VisibilityComponentUVE>(restored[0U]));
    const VisibilityComponentUVE& loaded = loadedManager.GetComponentUVE<VisibilityComponentUVE>(restored[0U]);
    EXPECT_FALSE(loaded.visible) << "the authored switch must survive the round trip";
    // Seeded from the authored value so nothing is briefly drawn between load and the first
    // scene-graph update, which then overwrites it with the inherited answer.
    EXPECT_FALSE(loaded.visibleInHierarchy);

    std::filesystem::remove(path);
}

TEST_F(SceneSerializerUVETest, RestoreUVE_AnEntitySavedWithoutVisibilityRestoresVisible) {
    // Every scene written before this component existed carries no VisibilityComponentUVE at all.
    // Those entities must come back visible, and specifically must come back with NO component -
    // inventing one on load would rewrite documents behind the author's back, and an absent
    // component already means visible everywhere that reads it.
    //
    // This is the migration case that matters. The "component present but its key absent" variant
    // is handled by json.value("visible", true) in the loader; testing it would mean hand-editing
    // the wrapped .uve container, which tests the envelope format rather than the default.
    SceneGraphUVE sceneGraph;
    const EntityUVE source = entityManager.CreateEntityUVE();
    sceneGraph.AttachTransformUVE(entityManager, source, TransformComponentUVE{});
    entityManager.AddComponentUVE<NameComponentUVE>(source, NameComponentUVE{"LegacyObject"});

    const std::optional<SceneSnapshotUVE> snapshot =
        serializer.CaptureUVE(entityManager, {source}, SceneAssetTypeUVE::Scene);
    ASSERT_TRUE(snapshot.has_value());

    EntityManagerUVE loadedManager{memoryManager.GetDefaultAllocatorUVE(), eventSystem};
    const std::vector<EntityUVE> restored = serializer.RestoreUVE(loadedManager, *snapshot);
    ASSERT_EQ(restored.size(), 1U);
    EXPECT_FALSE(loadedManager.HasComponentUVE<VisibilityComponentUVE>(restored[0U]))
        << "loading must not invent a component the document never had";

    // And the renderer's rule for that case: no component means visible.
    EXPECT_TRUE(VisibilityComponentUVE{}.visible);
    EXPECT_TRUE(VisibilityComponentUVE{}.visibleInHierarchy);
}

TEST_F(SceneSerializerUVETest, SaveLoadUVE_VisibilityParentIsRemappedToTheRestoredEntity) {
    // An entity reference cannot be written as a raw EntityUVE: indices are reassigned on load, so
    // a saved handle would point at whatever happens to occupy that slot. It goes through the same
    // file-local id remapping HierarchyComponentUVE uses, and this proves the restored reference
    // points at the restored TARGET rather than at a coincidence.
    SceneGraphUVE sceneGraph;
    const EntityUVE root = entityManager.CreateEntityUVE();
    sceneGraph.AttachTransformUVE(entityManager, root, TransformComponentUVE{});
    entityManager.AddComponentUVE<NameComponentUVE>(root, NameComponentUVE{"Root"});
    entityManager.AddComponentUVE<VisibilityComponentUVE>(root, VisibilityComponentUVE{});

    const EntityUVE target = entityManager.CreateEntityUVE();
    sceneGraph.AttachTransformUVE(entityManager, target, TransformComponentUVE{});
    entityManager.AddComponentUVE<NameComponentUVE>(target, NameComponentUVE{"Target"});
    entityManager.AddComponentUVE<VisibilityComponentUVE>(target, VisibilityComponentUVE{});
    sceneGraph.SetParentUVE(entityManager, target, root);

    const EntityUVE follower = entityManager.CreateEntityUVE();
    sceneGraph.AttachTransformUVE(entityManager, follower, TransformComponentUVE{});
    entityManager.AddComponentUVE<NameComponentUVE>(follower, NameComponentUVE{"Follower"});
    VisibilityComponentUVE redirect;
    redirect.visibilityParent = target;
    entityManager.AddComponentUVE<VisibilityComponentUVE>(follower, redirect);
    sceneGraph.SetParentUVE(entityManager, follower, root);

    const std::filesystem::path path = "uve_scene_serializer_tests_visibility_parent.uvscene";
    std::filesystem::remove(path);
    ASSERT_TRUE(serializer.SaveUVE(entityManager, {root}, path, Asset::AssetKindUVE::Scene));

    EntityManagerUVE loadedManager{memoryManager.GetDefaultAllocatorUVE(), eventSystem};
    const std::vector<EntityUVE> restoredRoots = serializer.LoadUVE(loadedManager, path);
    ASSERT_EQ(restoredRoots.size(), 1U);

    EntityUVE restoredTarget = kInvalidEntityUVE;
    EntityUVE restoredFollower = kInvalidEntityUVE;
    loadedManager.ForEachUVE<NameComponentUVE>([&](const EntityUVE entity, const NameComponentUVE& name) {
        if (name.name == "Target") {
            restoredTarget = entity;
        } else if (name.name == "Follower") {
            restoredFollower = entity;
        }
    });
    ASSERT_NE(restoredTarget, kInvalidEntityUVE);
    ASSERT_NE(restoredFollower, kInvalidEntityUVE);
    ASSERT_TRUE(loadedManager.HasComponentUVE<VisibilityComponentUVE>(restoredFollower));
    EXPECT_EQ(loadedManager.GetComponentUVE<VisibilityComponentUVE>(restoredFollower).visibilityParent,
              restoredTarget)
        << "the redirect must be remapped to the restored target, not a stale index";

    // And it still works: hiding the target hides the follower in the loaded scene.
    SceneGraphUVE loadedGraph;
    loadedManager.GetComponentUVE<VisibilityComponentUVE>(restoredTarget).visible = false;
    loadedGraph.UpdateUVE(loadedManager);
    EXPECT_FALSE(loadedManager.GetComponentUVE<VisibilityComponentUVE>(restoredFollower).visibleInHierarchy);

    std::filesystem::remove(path);
}

} // namespace UVE::Scene::Tests
