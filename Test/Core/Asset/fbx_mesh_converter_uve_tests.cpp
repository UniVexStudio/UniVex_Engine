// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/asset/fbx_mesh_converter_uve.h"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <gtest/gtest.h>

#include "Support/test_scratch_uve.h"

#include "uve/asset/asset_database_uve.h"
#include "uve/asset/asset_importer_uve.h"
#include "uve/asset/animation_clip_asset_uve.h"
#include "uve/asset/mesh_asset_uve.h"
#include "uve/asset/mesh_skinning_uve.h"

namespace UVE::Asset::Tests {
namespace {

// An ASCII FBX 7.4 file authored the way many DCC tools write them: Z up, and in centimetres
// (UnitScaleFactor 1 means one file unit is one centimetre).
[[nodiscard]] std::string MakeFbxUVE(const std::string_view objects, const std::string_view connections) {
    std::string text = R"(; FBX 7.4.0 project file
FBXHeaderExtension:  {
	FBXHeaderVersion: 1003
	FBXVersion: 7400
}
GlobalSettings:  {
	Version: 1000
	Properties70:  {
		P: "UpAxis", "int", "Integer", "",2
		P: "UpAxisSign", "int", "Integer", "",1
		P: "FrontAxis", "int", "Integer", "",1
		P: "FrontAxisSign", "int", "Integer", "",-1
		P: "CoordAxis", "int", "Integer", "",0
		P: "CoordAxisSign", "int", "Integer", "",1
		P: "UnitScaleFactor", "double", "Number", "",1
	}
}
Objects:  {
)";
    text += objects;
    text += "}\nConnections:  {\n";
    text += connections;
    text += "}\n";
    return text;
}

// A one-metre square lying on the ground (the XY plane, in a Z-up file), in centimetres.
constexpr std::string_view kFloorGeometryUVE = R"(	Geometry: 1000, "Geometry::Floor", "Mesh" {
		Vertices: *12 {
			a: 0,0,0,100,0,0,100,100,0,0,100,0
		}
		PolygonVertexIndex: *4 {
			a: 0,1,2,-4
		}
		GeometryVersion: 124
	}
	Model: 2000, "Model::Floor", "Mesh" {
		Version: 232
	}
)";
constexpr std::string_view kFloorConnectionsUVE = "\tC: \"OO\",1000,2000\n\tC: \"OO\",2000,0\n";

[[nodiscard]] std::span<const std::byte> BytesUVE(const std::string& text) {
    return std::as_bytes(std::span<const char>(text.data(), text.size()));
}

[[nodiscard]] bool NearUVE(const float value, const float expected) {
    return std::abs(value - expected) < 1.0e-5F;
}

TEST(FbxMeshConverterUVETest, AZUpCentimetreFileArrivesInMetresStandingOnY) {
    const std::string fbx = MakeFbxUVE(kFloorGeometryUVE, kFloorConnectionsUVE);
    MeshAssetUVE mesh;
    ASSERT_TRUE(ConvertFbxMeshUVE(BytesUVE(fbx), mesh));

    // One quad: two triangles over four shared corners.
    EXPECT_EQ(mesh.vertices.size(), 4U);
    EXPECT_EQ(mesh.indices.size(), 6U);
    // 100 cm is one metre, and the Z-up ground plane is the Y-up ground plane.
    EXPECT_TRUE(NearUVE(mesh.localBounds.max.x - mesh.localBounds.min.x, 1.0F));
    EXPECT_TRUE(NearUVE(mesh.localBounds.max.y - mesh.localBounds.min.y, 0.0F));
    EXPECT_TRUE(NearUVE(mesh.localBounds.max.z - mesh.localBounds.min.z, 1.0F));
    for (const MeshVertexUVE& vertex : mesh.vertices) {
        // No normals in the file: generated, and facing up.
        EXPECT_TRUE(NearUVE(vertex.normal.y, 1.0F)) << vertex.normal.x << ", " << vertex.normal.y;
        EXPECT_TRUE(std::isfinite(vertex.tangent.x) && std::isfinite(vertex.tangentHandedness));
    }
}

TEST(FbxMeshConverterUVETest, EveryInstanceIsMergedWhereItsObjectPlacesItAndAMirroredOneStaysFacingOut) {
    const std::string objects = std::string(kFloorGeometryUVE) + R"(	Model: 2001, "Model::Mirrored", "Mesh" {
		Version: 232
		Properties70:  {
			P: "Lcl Translation", "Lcl Translation", "", "A",300,0,0
			P: "Lcl Scaling", "Lcl Scaling", "", "A",-1,1,1
		}
	}
)";
    const std::string fbx =
        MakeFbxUVE(objects, std::string(kFloorConnectionsUVE) + "\tC: \"OO\",1000,2001\n\tC: \"OO\",2001,0\n");
    MeshAssetUVE mesh;
    ASSERT_TRUE(ConvertFbxMeshUVE(BytesUVE(fbx), mesh));

    EXPECT_EQ(mesh.vertices.size(), 8U);
    EXPECT_EQ(mesh.indices.size(), 12U);
    // The second copy sits 3 m away, flipped back over it: 0..1 m and 2..3 m along X.
    EXPECT_TRUE(NearUVE(mesh.localBounds.min.x, 0.0F));
    EXPECT_TRUE(NearUVE(mesh.localBounds.max.x, 3.0F));
    // Each triangle's winding agrees with its normals, the mirrored copy's included.
    for (std::size_t first = 0U; first < mesh.indices.size(); first += 3U) {
        const Math::Vector3UVE& a = mesh.vertices[mesh.indices[first]].position;
        const Math::Vector3UVE& b = mesh.vertices[mesh.indices[first + 1U]].position;
        const Math::Vector3UVE& c = mesh.vertices[mesh.indices[first + 2U]].position;
        const float faceY = ((b.z - a.z) * (c.x - a.x)) - ((b.x - a.x) * (c.z - a.z));
        const float normalY = mesh.vertices[mesh.indices[first]].normal.y;
        EXPECT_GT(faceY * normalY, 0.0F) << "triangle " << first / 3U << " is inside out";
    }
}

TEST(FbxMeshConverterUVETest, ASkinIsWhatMakesItRigged) {
    const std::string objects = std::string(kFloorGeometryUVE) + R"(	Model: 3000, "Model::Bone", "LimbNode" {
		Version: 232
	}
	NodeAttribute: 3100, "NodeAttribute::Bone", "LimbNode" {
		TypeFlags: "Skeleton"
	}
	Deformer: 4000, "Deformer::Skin", "Skin" {
		Version: 101
	}
	Deformer: 4100, "SubDeformer::Cluster", "Cluster" {
		Version: 100
		Indexes: *4 {
			a: 0,1,2,3
		}
		Weights: *4 {
			a: 1,1,1,1
		}
		Transform: *16 {
			a: 1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1
		}
		TransformLink: *16 {
			a: 1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1
		}
	}
)";
    const std::string rigged = MakeFbxUVE(
        objects, std::string(kFloorConnectionsUVE) +
                     "\tC: \"OO\",4000,1000\n\tC: \"OO\",4100,4000\n\tC: \"OO\",3000,4100\n\tC: \"OO\",3100,3000\n"
                     "\tC: \"OO\",3000,0\n");
    const std::optional<FbxSourceSummaryUVE> riggedSummary = DescribeFbxSourceUVE(BytesUVE(rigged));
    ASSERT_TRUE(riggedSummary.has_value());
    EXPECT_TRUE(riggedSummary->hasSkin);
    EXPECT_EQ(riggedSummary->meshCount, 1U);
    EXPECT_EQ(riggedSummary->boneCount, 1U);
    EXPECT_FALSE(riggedSummary->IsAnimationOnlyUVE());
    const std::optional<FbxSourceSummaryUVE> staticSummary =
        DescribeFbxSourceUVE(BytesUVE(MakeFbxUVE(kFloorGeometryUVE, kFloorConnectionsUVE)));
    ASSERT_TRUE(staticSummary.has_value());
    EXPECT_FALSE(staticSummary->hasSkin);
    EXPECT_FALSE(staticSummary->IsAnimationOnlyUVE());

    // The mesh imports with its skin: one joint per bone, named like the skeleton's.
    MeshAssetUVE mesh;
    ASSERT_TRUE(ConvertFbxMeshUVE(BytesUVE(rigged), mesh));
    EXPECT_EQ(mesh.vertices.size(), 4U);
    ASSERT_TRUE(mesh.IsSkinnedUVE());
    EXPECT_TRUE(IsMeshSkinningDataValidUVE(mesh));
    const std::optional<GltfSkeletonUVE> skeleton = ReadFbxSkeletonUVE(BytesUVE(rigged), 16U);
    ASSERT_TRUE(skeleton.has_value());
    ASSERT_EQ(mesh.joints.size(), skeleton->joints.size());
    EXPECT_EQ(mesh.joints[0].name, skeleton->joints[0].name);
    for (const MeshSkinningInfluenceUVE& influence : mesh.skinningInfluences) {
        EXPECT_EQ(influence.joints[0], 0U);
        EXPECT_FLOAT_EQ(influence.weights[0], 1.0F);
    }

    // Skinned in the skeleton's rest pose, the floor stays exactly where the static import puts it.
    std::vector<Math::Matrix4x4UVE> localPose;
    for (const GltfJointUVE& joint : skeleton->joints) {
        localPose.push_back(Math::Matrix4x4UVE::ComposeTrsUVE(joint.translation, joint.rotation, joint.scale));
    }
    std::vector<Math::Matrix4x4UVE> skinning;
    ASSERT_TRUE(TryResolvePoseUVE(mesh.joints, localPose, skinning));
    std::vector<MeshVertexUVE> posed;
    ASSERT_TRUE(TrySkinMeshUVE(mesh, skinning, posed));
    MeshAssetUVE plain;
    ASSERT_TRUE(ConvertFbxMeshUVE(BytesUVE(MakeFbxUVE(kFloorGeometryUVE, kFloorConnectionsUVE)), plain));
    ASSERT_EQ(posed.size(), plain.vertices.size());
    for (std::size_t index = 0U; index < posed.size(); ++index) {
        EXPECT_NEAR(posed[index].position.x, plain.vertices[index].position.x, 1.0e-4F);
        EXPECT_NEAR(posed[index].position.y, plain.vertices[index].position.y, 1.0e-4F);
        EXPECT_NEAR(posed[index].position.z, plain.vertices[index].position.z, 1.0e-4F);
    }

    // Lifting the bone lifts the floor with it.
    localPose[0] = Math::Matrix4x4UVE::ComposeTrsUVE(
        Math::Vector3UVE{skeleton->joints[0].translation.x, skeleton->joints[0].translation.y + 1.0F,
                         skeleton->joints[0].translation.z},
        skeleton->joints[0].rotation, skeleton->joints[0].scale);
    ASSERT_TRUE(TryResolvePoseUVE(mesh.joints, localPose, skinning));
    ASSERT_TRUE(TrySkinMeshUVE(mesh, skinning, posed));
    EXPECT_NEAR(posed[0].position.y, plain.vertices[0].position.y + 1.0F, 1.0e-4F);

    // And the skin survives a save and load, names included.
    const std::filesystem::path path = ::UVE::Tests::ScratchRootUVE() / "uve_fbx_skinned_floor.uvmodel";
    ASSERT_TRUE(SaveMeshAssetUVE(mesh, path));
    MeshAssetUVE loaded;
    ASSERT_TRUE(LoadMeshAssetUVE(path, loaded));
    ASSERT_EQ(loaded.joints.size(), mesh.joints.size());
    EXPECT_EQ(loaded.joints[0].name, mesh.joints[0].name);
    EXPECT_EQ(loaded.skinningInfluences.size(), mesh.skinningInfluences.size());
    std::filesystem::remove(path);
}

TEST(FbxMeshConverterUVETest, ASkeletonWithAnimationAndNoMeshIsAnAnimationNotAModel) {
    // The shape exported motion usually takes: bones and a take, nothing to draw. One second long
    // (FBX time counts 46186158000 ticks a second).
    const std::string fbx = MakeFbxUVE(R"(	Model: 3000, "Model::Hips", "LimbNode" {
		Version: 232
	}
	NodeAttribute: 3100, "NodeAttribute::Hips", "LimbNode" {
		TypeFlags: "Skeleton"
	}
	AnimationStack: 5000, "AnimStack::Strafe", "" {
		Properties70:  {
			P: "LocalStart", "KTime", "Time", "",0
			P: "LocalStop", "KTime", "Time", "",46186158000
		}
	}
	AnimationLayer: 5100, "AnimLayer::Base", "" {
	}
)",
                                       "\tC: \"OO\",5100,5000\n\tC: \"OO\",3100,3000\n\tC: \"OO\",3000,0\n");
    const std::optional<FbxSourceSummaryUVE> summary = DescribeFbxSourceUVE(BytesUVE(fbx));
    ASSERT_TRUE(summary.has_value());
    EXPECT_EQ(summary->meshCount, 0U);
    EXPECT_EQ(summary->boneCount, 1U);
    EXPECT_EQ(summary->animationCount, 1U);
    EXPECT_NEAR(summary->longestAnimationSeconds, 1.0, 1.0e-6);
    EXPECT_FALSE(summary->hasSkin);
    EXPECT_TRUE(summary->IsAnimationOnlyUVE());

    // And there is no mesh in it to convert.
    MeshAssetUVE mesh;
    EXPECT_FALSE(ConvertFbxMeshUVE(BytesUVE(fbx), mesh));
}

TEST(FbxMeshConverterUVETest, TheSkeletonComesOutInMetresUprightWithParentsFirst) {
    // Hips one metre up (100 cm along Z in this Z-up file) with a spine child, under a null the
    // exporter put between them: the null is folded into the spine's pose so the chain still meets.
    const std::string fbx = MakeFbxUVE(R"(	Model: 3000, "Model::Hips", "LimbNode" {
		Version: 232
		Properties70:  {
			P: "Lcl Translation", "Lcl Translation", "", "A",0,0,100
		}
	}
	NodeAttribute: 3100, "NodeAttribute::Hips", "LimbNode" {
		TypeFlags: "Skeleton"
	}
	Model: 3200, "Model::Offset", "Null" {
		Version: 232
		Properties70:  {
			P: "Lcl Translation", "Lcl Translation", "", "A",0,0,10
		}
	}
	Model: 3300, "Model::Hips", "LimbNode" {
		Version: 232
		Properties70:  {
			P: "Lcl Translation", "Lcl Translation", "", "A",0,0,20
		}
	}
	NodeAttribute: 3400, "NodeAttribute::Spine", "LimbNode" {
		TypeFlags: "Skeleton"
	}
)",
                                       "\tC: \"OO\",3100,3000\n\tC: \"OO\",3400,3300\n\tC: \"OO\",3300,3200\n"
                                       "\tC: \"OO\",3200,3000\n\tC: \"OO\",3000,0\n");
    const std::optional<GltfSkeletonUVE> skeleton = ReadFbxSkeletonUVE(BytesUVE(fbx), 16U);
    ASSERT_TRUE(skeleton.has_value());
    ASSERT_EQ(skeleton->joints.size(), 2U);
    EXPECT_EQ(skeleton->skinCount, 0U);
    const GltfJointUVE& hips = skeleton->joints[0];
    const GltfJointUVE& spine = skeleton->joints[1];
    EXPECT_EQ(hips.parentIndex, -1);
    EXPECT_EQ(spine.parentIndex, 0);
    // Two bones with one name: the second is made unique, the way the glTF reader does it.
    EXPECT_EQ(hips.name, "Hips");
    EXPECT_EQ(spine.name, "Hips_1");

    // Hips stand one metre up, along +Y, and the spine continues straight up from them.
    const auto rotate = [](const Math::QuaternionUVE& q, const Math::Vector3UVE& v) {
        const Math::Vector3UVE u{q.x, q.y, q.z};
        const Math::Vector3UVE t{2.0F * ((u.y * v.z) - (u.z * v.y)), 2.0F * ((u.z * v.x) - (u.x * v.z)),
                                 2.0F * ((u.x * v.y) - (u.y * v.x))};
        return Math::Vector3UVE{v.x + (q.w * t.x) + ((u.y * t.z) - (u.z * t.y)),
                                v.y + (q.w * t.y) + ((u.z * t.x) - (u.x * t.z)),
                                v.z + (q.w * t.z) + ((u.x * t.y) - (u.y * t.x))};
    };
    EXPECT_NEAR(hips.translation.x, 0.0F, 1.0e-4F);
    EXPECT_NEAR(hips.translation.y, 1.0F, 1.0e-4F);
    EXPECT_NEAR(hips.translation.z, 0.0F, 1.0e-4F);
    // 0.1 m of null and 0.2 m of spine, in the hips' frame.
    const Math::Vector3UVE spineOffset = rotate(hips.rotation, spine.translation);
    EXPECT_NEAR(spineOffset.x, 0.0F, 1.0e-4F);
    EXPECT_NEAR(spineOffset.y, 0.3F, 1.0e-4F);
    EXPECT_NEAR(spineOffset.z, 0.0F, 1.0e-4F);

    // A limit it does not fit in, bytes that are not FBX, and a file with no bones: nothing.
    EXPECT_FALSE(ReadFbxSkeletonUVE(BytesUVE(fbx), 1U).has_value());
    const std::string garbage = "not an fbx";
    EXPECT_FALSE(ReadFbxSkeletonUVE(BytesUVE(garbage), 16U).has_value());
    EXPECT_FALSE(ReadFbxSkeletonUVE(BytesUVE(MakeFbxUVE(kFloorGeometryUVE, kFloorConnectionsUVE)), 16U).has_value());
}

TEST(FbxMeshConverterUVETest, EveryTakeBecomesASkeletalClipOnTheSkeletonsBones) {
    // Hips one metre up with a spine above it; the take "Armature|Walk" slides the hips 100 cm
    // along the file's X over one second. The spine never moves.
    const std::string fbx = MakeFbxUVE(R"(	Model: 3000, "Model::Hips", "LimbNode" {
		Version: 232
		Properties70:  {
			P: "Lcl Translation", "Lcl Translation", "", "A",0,0,100
		}
	}
	NodeAttribute: 3100, "NodeAttribute::Hips", "LimbNode" {
		TypeFlags: "Skeleton"
	}
	Model: 3300, "Model::Spine", "LimbNode" {
		Version: 232
		Properties70:  {
			P: "Lcl Translation", "Lcl Translation", "", "A",0,0,20
		}
	}
	NodeAttribute: 3400, "NodeAttribute::Spine", "LimbNode" {
		TypeFlags: "Skeleton"
	}
	AnimationStack: 5000, "AnimStack::Armature|Walk", "" {
		Properties70:  {
			P: "LocalStart", "KTime", "Time", "",0
			P: "LocalStop", "KTime", "Time", "",46186158000
		}
	}
	AnimationLayer: 5100, "AnimLayer::Base", "" {
	}
	AnimationCurveNode: 6000, "AnimCurveObject::T", "" {
		Properties70:  {
			P: "d|X", "Number", "", "A",0
			P: "d|Y", "Number", "", "A",0
			P: "d|Z", "Number", "", "A",100
		}
	}
	AnimationCurve: 7000, "AnimCurve::", "" {
		Default: 0
		KeyVer: 4009
		KeyTime: *2 {
			a: 0,46186158000
		}
		KeyValueFloat: *2 {
			a: 0,100
		}
		KeyAttrFlags: *1 {
			a: 260
		}
		KeyAttrDataFloat: *4 {
			a: 0,0,0,0
		}
		KeyAttrRefCount: *1 {
			a: 2
		}
	}
)",
                                       "\tC: \"OO\",3100,3000\n\tC: \"OO\",3400,3300\n\tC: \"OO\",3300,3000\n"
                                       "\tC: \"OO\",3000,0\n\tC: \"OO\",5100,5000\n\tC: \"OO\",6000,5100\n"
                                       "\tC: \"OP\",6000,3000,\"Lcl Translation\"\n\tC: \"OP\",7000,6000,\"d|X\"\n");
    const std::optional<GltfSkeletonUVE> skeleton = ReadFbxSkeletonUVE(BytesUVE(fbx), 16U);
    ASSERT_TRUE(skeleton.has_value());
    const std::vector<AnimationClipAssetUVE> clips = ReadFbxAnimationsUVE(BytesUVE(fbx), 16U);
    ASSERT_EQ(clips.size(), 1U);
    const AnimationClipAssetUVE& walk = clips[0];
    EXPECT_EQ(walk.clipId, "Walk") << "the armature prefix is not part of the take's name";
    EXPECT_NEAR(walk.durationSeconds, 1.0, 1.0e-6);
    EXPECT_TRUE(walk.IsSkeletalUVE());
    EXPECT_TRUE(IsAnimationClipAssetValidUVE(walk));
    ASSERT_EQ(walk.bones.size(), skeleton->joints.size());
    for (std::size_t index = 0U; index < walk.bones.size(); ++index) {
        EXPECT_EQ(walk.bones[index].bone, skeleton->joints[index].name) << "tracks follow the skeleton's bones";
    }

    // The take carries the skeleton it was made for: the same bones, parents and rest pose.
    ASSERT_EQ(walk.rest.size(), skeleton->joints.size());
    for (std::size_t index = 0U; index < walk.rest.size(); ++index) {
        EXPECT_EQ(walk.rest[index].bone, skeleton->joints[index].name);
        EXPECT_EQ(walk.rest[index].parent, skeleton->joints[index].parentIndex);
        EXPECT_NEAR(walk.rest[index].position.y, skeleton->joints[index].translation.y, 1.0e-5F);
    }
    EXPECT_FALSE(walk.conformed);

    // The hips: sampled every frame, starting at the rest pose and ending one metre along X.
    const AnimationAssetBoneTrackUVE& hips = walk.bones[0];
    ASSERT_GT(hips.samples.size(), 2U);
    EXPECT_DOUBLE_EQ(hips.samples.front().timeSeconds, 0.0);
    EXPECT_DOUBLE_EQ(hips.samples.back().timeSeconds, walk.durationSeconds);
    EXPECT_NEAR(hips.samples.front().pose.position.x, skeleton->joints[0].translation.x, 1.0e-4F);
    EXPECT_NEAR(hips.samples.front().pose.position.y, 1.0F, 1.0e-4F) << "metres, +Y up, like the skeleton";
    EXPECT_NEAR(hips.samples.back().pose.position.x, 1.0F, 1.0e-3F);
    EXPECT_NEAR(hips.samples.back().pose.position.y, 1.0F, 1.0e-4F);

    // The spine never moves: one sample, its rest pose.
    const AnimationAssetBoneTrackUVE& spine = walk.bones[1];
    ASSERT_EQ(spine.samples.size(), 1U);
    EXPECT_NEAR(spine.samples[0].pose.position.x, skeleton->joints[1].translation.x, 1.0e-4F);
    EXPECT_NEAR(spine.samples[0].pose.position.y, skeleton->joints[1].translation.y, 1.0e-4F);
    EXPECT_NEAR(spine.samples[0].pose.position.z, skeleton->joints[1].translation.z, 1.0e-4F);

    // No bones or no FBX: no clips.
    EXPECT_TRUE(ReadFbxAnimationsUVE(BytesUVE(MakeFbxUVE(kFloorGeometryUVE, kFloorConnectionsUVE)), 16U).empty());
    const std::string garbage = "not an fbx";
    EXPECT_TRUE(ReadFbxAnimationsUVE(BytesUVE(garbage), 16U).empty());
}

TEST(FbxMeshConverterUVETest, WhatIsNotAnFbxWithTrianglesIsRefusedAndLeavesTheMeshAlone) {
    MeshAssetUVE mesh;
    mesh.indices = {7U};
    EXPECT_FALSE(ConvertFbxMeshUVE({}, mesh));
    const std::string garbage = "definitely not an fbx file";
    EXPECT_FALSE(ConvertFbxMeshUVE(BytesUVE(garbage), mesh));
    // OBJ has its own importer; the FBX path does not quietly take it.
    const std::string obj = "v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n";
    EXPECT_FALSE(ConvertFbxMeshUVE(BytesUVE(obj), mesh));
    // A valid FBX with nothing to draw.
    const std::string empty = MakeFbxUVE("", "");
    EXPECT_FALSE(ConvertFbxMeshUVE(BytesUVE(empty), mesh));
    EXPECT_FALSE(DescribeFbxSourceUVE(BytesUVE(garbage)).has_value());
    EXPECT_EQ(mesh.indices, (std::vector<std::uint32_t>{7U}));
}

TEST(FbxMeshConverterUVETest, TheImporterPublishesAUveModelAndRegistersIt) {
    const std::filesystem::path sourcePath = "uve_fbx_importer_tests_floor.fbx";
    const std::filesystem::path destinationPath = "uve_fbx_importer_tests_floor.uvmodel";
    std::filesystem::remove(destinationPath);
    {
        const std::string fbx = MakeFbxUVE(kFloorGeometryUVE, kFloorConnectionsUVE);
        std::ofstream output(sourcePath, std::ios::binary | std::ios::trunc);
        output.write(fbx.data(), static_cast<std::streamsize>(fbx.size()));
        ASSERT_TRUE(output.good());
    }

    AssetImporterUVE importer;
    AssetDatabaseUVE database;
    const AssetGuidUVE guid = importer.ImportUVE(sourcePath, destinationPath, database);
    ASSERT_NE(guid, kInvalidAssetGuidUVE);
    EXPECT_EQ(database.ResolveUVE(guid), destinationPath);
    MeshAssetUVE mesh;
    ASSERT_TRUE(LoadMeshAssetUVE(destinationPath, mesh));
    EXPECT_EQ(mesh.vertices.size(), 4U);
    EXPECT_EQ(mesh.indices.size(), 6U);

    // A destination that is not a .uvmodel is refused before anything is written.
    EXPECT_EQ(importer.ImportUVE(sourcePath, "uve_fbx_importer_tests_floor.uvtex", database), kInvalidAssetGuidUVE);

    std::filesystem::remove(sourcePath);
    std::filesystem::remove(destinationPath);
}

} // namespace
} // namespace UVE::Asset::Tests
