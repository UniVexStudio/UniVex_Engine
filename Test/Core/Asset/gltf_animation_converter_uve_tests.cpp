// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "Support/test_scratch_uve.h"

#include "uve/asset/animation_clip_asset_uve.h"
#include "uve/asset/gltf_animation_converter_uve.h"
#include "uve/asset/gltf_skin_converter_uve.h"
#include "uve/asset/mesh_skinning_uve.h"
#include "uve/asset/skeleton_asset_uve.h"
#include "uve/math/matrix4x4_uve.h"

namespace UVE::Asset::Tests {
namespace {

std::filesystem::path TestPathUVE(const char* const name) {
    return ::UVE::Tests::ScratchRootUVE() / name;
}

void AppendU32LittleEndianUVE(std::vector<std::byte>& bytes, const std::uint32_t value) {
    for (unsigned int shift = 0U; shift < 32U; shift += 8U) {
        bytes.push_back(std::byte{static_cast<unsigned char>((value >> shift) & 0xFFU)});
    }
}

void AppendFloatLittleEndianUVE(std::vector<std::byte>& bytes, const float value) {
    AppendU32LittleEndianUVE(bytes, std::bit_cast<std::uint32_t>(value));
}

SkeletonAssetUVE MakeTwoJointSkeletonAssetUVE() {
    SkeletonJointUVE root;
    root.name = "Root";
    root.parent = -1;
    SkeletonJointUVE child;
    child.name = "Child";
    child.parent = 0;
    child.position = Math::Vector3UVE{0.0F, 1.0F, 0.0F};
    SkeletonAssetUVE skeleton;
    skeleton.skeletonId = "Rig";
    skeleton.joints = {root, child};
    return skeleton;
}

struct HandBuiltAnimationUVE final {
    std::vector<std::vector<std::byte>> buffers;
    GltfAnimationSourceUVE source;
};

// Moving a vector keeps its heap buffer at the same address, so spans taken here stay valid
// across later AddChannelUVE calls even when `buffers` reallocates.
void AddChannelUVE(HandBuiltAnimationUVE& anim, const char* bone, const GltfAnimationPathUVE path,
                   const GltfAnimationInterpolationUVE interpolation, const std::vector<float>& times,
                   const std::vector<float>& flatValues) {
    std::vector<std::byte> timeBytes;
    std::vector<std::byte> valueBytes;
    for (const float time : times) {
        AppendFloatLittleEndianUVE(timeBytes, time);
    }
    for (const float value : flatValues) {
        AppendFloatLittleEndianUVE(valueBytes, value);
    }
    anim.buffers.push_back(std::move(timeBytes));
    anim.buffers.push_back(std::move(valueBytes));
    const std::size_t count = times.size();
    GltfAnimationChannelSourceUVE channel;
    channel.bone = bone;
    channel.path = path;
    channel.interpolation = interpolation;
    channel.times = GltfAccessorViewUVE{std::span<const std::byte>{anim.buffers[anim.buffers.size() - 2U]},
                                        0U, static_cast<std::uint64_t>(count), 0U,
                                        GltfComponentTypeUVE::Float};
    channel.values = GltfAccessorViewUVE{std::span<const std::byte>{anim.buffers[anim.buffers.size() - 1U]},
                                         0U, static_cast<std::uint64_t>(count), 0U,
                                         GltfComponentTypeUVE::Float};
    anim.source.channels.push_back(std::move(channel));
}

const AnimationAssetBoneTrackUVE* FindTrackUVE(const AnimationClipAssetUVE& clip, const char* bone) {
    for (const AnimationAssetBoneTrackUVE& track : clip.bones) {
        if (track.bone == bone) {
            return &track;
        }
    }
    return nullptr;
}

const AnimationAssetSampleUVE* FindSampleUVE(const AnimationAssetBoneTrackUVE& track, const double time) {
    for (const AnimationAssetSampleUVE& sample : track.samples) {
        if (sample.timeSeconds == time) {
            return &sample;
        }
    }
    return nullptr;
}

void AppendIdentityMatrixUVE(std::vector<std::byte>& bytes) {
    for (int i = 0; i < 16; ++i) {
        AppendFloatLittleEndianUVE(bytes, i % 5 == 0 ? 1.0F : 0.0F);
    }
}

void AppendColumnMajorTranslationUVE(std::vector<std::byte>& bytes, const float x, const float y,
                                     const float z) {
    for (int i = 0; i < 16; ++i) {
        float value = 0.0F;
        if (i == 0 || i == 5 || i == 10 || i == 15) {
            value = 1.0F;
        } else if (i == 12) {
            value = x;
        } else if (i == 13) {
            value = y;
        } else if (i == 14) {
            value = z;
        }
        AppendFloatLittleEndianUVE(bytes, value);
    }
}

// The end-to-end fixture: a two-joint rig ("Root" at origin, "Child" at (0,1,0)) with correct
// inverse bind matrices (identity for the root, T(0,-1,0) cancelling the child's bind pose),
// one triangle (v0 on the root, v1/v2 on the child), and a one-second clip rotating the child
// 90 degrees about Z. Buffer: IBM 128B @0, POSITION 36B @128, JOINTS_0 12B @164,
// WEIGHTS_0 48B @176, TIME 8B @224, ROT 32B @232.
std::vector<std::byte> MakeEndToEndBufferUVE() {
    std::vector<std::byte> buffer;
    AppendIdentityMatrixUVE(buffer);
    AppendColumnMajorTranslationUVE(buffer, 0.0F, -1.0F, 0.0F);
    for (const float v : {0.0F, 0.0F, 0.0F, 1.0F, 1.0F, 0.0F, 1.0F, 2.0F, 0.0F}) {
        AppendFloatLittleEndianUVE(buffer, v);
    }
    for (const std::uint32_t j : {0U, 0U, 0U, 0U, 1U, 0U, 0U, 0U, 1U, 0U, 0U, 0U}) {
        buffer.push_back(std::byte{static_cast<unsigned char>(j)});
    }
    for (int i = 0; i < 3; ++i) {
        AppendFloatLittleEndianUVE(buffer, 1.0F);
        AppendFloatLittleEndianUVE(buffer, 0.0F);
        AppendFloatLittleEndianUVE(buffer, 0.0F);
        AppendFloatLittleEndianUVE(buffer, 0.0F);
    }
    AppendFloatLittleEndianUVE(buffer, 0.0F);
    AppendFloatLittleEndianUVE(buffer, 1.0F);
    for (const float q : {0.0F, 0.0F, 0.0F, 1.0F, 0.0F, 0.0F, 0.70710678F, 0.70710678F}) {
        AppendFloatLittleEndianUVE(buffer, q);
    }
    return buffer;
}

constexpr const char* kEndToEndDocumentUVE = R"json({
  "asset": {"version": "2.0"},
  "nodes": [
    {"name": "Root", "children": [1]},
    {"name": "Child", "translation": [0.0, 1.0, 0.0]}
  ],
  "skins": [{"name": "Rig", "joints": [0, 1], "inverseBindMatrices": 0}],
  "meshes": [{"primitives": [{"attributes": {"POSITION": 1, "JOINTS_0": 2, "WEIGHTS_0": 3}}]}],
  "animations": [{
    "name": "Wave",
    "samplers": [{"input": 4, "output": 5, "interpolation": "LINEAR"}],
    "channels": [{"sampler": 0, "target": {"node": 1, "path": "rotation"}}]
  }],
  "accessors": [
    {"bufferView": 0, "componentType": 5126, "count": 2, "type": "MAT4"},
    {"bufferView": 1, "componentType": 5126, "count": 3, "type": "VEC3"},
    {"bufferView": 2, "componentType": 5121, "count": 3, "type": "VEC4"},
    {"bufferView": 3, "componentType": 5126, "count": 3, "type": "VEC4"},
    {"bufferView": 4, "componentType": 5126, "count": 2, "type": "SCALAR"},
    {"bufferView": 5, "componentType": 5126, "count": 2, "type": "VEC4"}
  ],
  "bufferViews": [
    {"buffer": 0, "byteOffset": 0, "byteLength": 128},
    {"buffer": 0, "byteOffset": 128, "byteLength": 36},
    {"buffer": 0, "byteOffset": 164, "byteLength": 12},
    {"buffer": 0, "byteOffset": 176, "byteLength": 48},
    {"buffer": 0, "byteOffset": 224, "byteLength": 8},
    {"buffer": 0, "byteOffset": 232, "byteLength": 32}
  ],
  "buffers": [{"byteLength": 264}]
})json";

} // namespace

TEST(GltfAnimationConverterUVETest, MergesChannelsOntoUnionTimesInSkeletonOrder) {
    HandBuiltAnimationUVE anim;
    anim.source.clipId = "Jump";
    // Inserted child-first to prove tracks come out in skeleton order, not insertion order.
    AddChannelUVE(anim, "Child", GltfAnimationPathUVE::Translation,
                  GltfAnimationInterpolationUVE::Linear, {0.0F, 1.0F}, {0.0F, 1.0F, 0.0F, 0.0F, 2.0F, 0.0F});
    AddChannelUVE(anim, "Child", GltfAnimationPathUVE::Rotation,
                  GltfAnimationInterpolationUVE::Linear, {0.5F}, {0.0F, 0.0F, 0.0F, 1.0F});
    AddChannelUVE(anim, "Root", GltfAnimationPathUVE::Translation,
                  GltfAnimationInterpolationUVE::Linear, {0.0F, 1.0F}, {0.0F, 0.0F, 0.0F, 0.0F, 0.0F, 0.0F});

    AnimationClipAssetUVE clip;
    ASSERT_TRUE(ConvertGltfAnimationUVE(anim.source, MakeTwoJointSkeletonAssetUVE(), clip));
    EXPECT_EQ(clip.clipId, "Jump");
    EXPECT_DOUBLE_EQ(clip.durationSeconds, 1.0);
    EXPECT_TRUE(IsAnimationClipAssetValidUVE(clip));

    ASSERT_EQ(clip.bones.size(), 2U);
    EXPECT_EQ(clip.bones[0].bone, "Root");
    EXPECT_EQ(clip.bones[1].bone, "Child");
    ASSERT_EQ(clip.rest.size(), 2U);
    EXPECT_EQ(clip.rest[1].bone, "Child");
    EXPECT_EQ(clip.rest[1].parent, 0);

    const AnimationAssetBoneTrackUVE* child = FindTrackUVE(clip, "Child");
    ASSERT_NE(child, nullptr);
    ASSERT_EQ(child->samples.size(), 3U);
    EXPECT_DOUBLE_EQ(child->samples[0].timeSeconds, 0.0);
    EXPECT_DOUBLE_EQ(child->samples[1].timeSeconds, 0.5);
    EXPECT_DOUBLE_EQ(child->samples[2].timeSeconds, 1.0);
    // The union time 0.5 sits between translation keys: the baked position lerps, the rotation
    // is the exact rotation key, and the unbaked scale falls back to the rest pose.
    EXPECT_FLOAT_EQ(child->samples[1].pose.position.x, 0.0F);
    EXPECT_FLOAT_EQ(child->samples[1].pose.position.y, 1.5F);
    EXPECT_FLOAT_EQ(child->samples[1].pose.rotation.w, 1.0F);
    EXPECT_FLOAT_EQ(child->samples[1].pose.scale.x, 1.0F);
    EXPECT_FLOAT_EQ(child->samples[2].pose.position.y, 2.0F);
}

TEST(GltfAnimationConverterUVETest, SlerpBakesRotationBetweenKeys) {
    HandBuiltAnimationUVE anim;
    anim.source.clipId = "Turn";
    // Identity to 180 degrees about Y: the midpoint must be 90 degrees about Y.
    AddChannelUVE(anim, "Child", GltfAnimationPathUVE::Rotation,
                  GltfAnimationInterpolationUVE::Linear, {0.0F, 1.0F},
                  {0.0F, 0.0F, 0.0F, 1.0F, 0.0F, 1.0F, 0.0F, 0.0F});
    AddChannelUVE(anim, "Child", GltfAnimationPathUVE::Translation,
                  GltfAnimationInterpolationUVE::Linear, {0.5F}, {0.0F, 1.0F, 0.0F});

    AnimationClipAssetUVE clip;
    ASSERT_TRUE(ConvertGltfAnimationUVE(anim.source, MakeTwoJointSkeletonAssetUVE(), clip));
    const AnimationAssetBoneTrackUVE* child = FindTrackUVE(clip, "Child");
    ASSERT_NE(child, nullptr);
    ASSERT_EQ(child->samples.size(), 3U);
    const Math::QuaternionUVE& mid = child->samples[1].pose.rotation;
    EXPECT_NEAR(mid.x, 0.0F, 1.0e-5F);
    EXPECT_NEAR(mid.y, 0.70710678F, 1.0e-5F);
    EXPECT_NEAR(mid.z, 0.0F, 1.0e-5F);
    EXPECT_NEAR(mid.w, 0.70710678F, 1.0e-5F);
    EXPECT_NEAR(mid.x * mid.x + mid.y * mid.y + mid.z * mid.z + mid.w * mid.w, 1.0F, 1.0e-5F);
}

TEST(GltfAnimationConverterUVETest, StepHoldsPreviousKey) {
    HandBuiltAnimationUVE anim;
    anim.source.clipId = "Snap";
    AddChannelUVE(anim, "Child", GltfAnimationPathUVE::Translation,
                  GltfAnimationInterpolationUVE::Step, {0.0F, 1.0F},
                  {0.0F, 0.0F, 0.0F, 0.0F, 9.0F, 0.0F});
    AddChannelUVE(anim, "Child", GltfAnimationPathUVE::Scale,
                  GltfAnimationInterpolationUVE::Linear, {0.5F}, {1.0F, 1.0F, 1.0F});

    AnimationClipAssetUVE clip;
    ASSERT_TRUE(ConvertGltfAnimationUVE(anim.source, MakeTwoJointSkeletonAssetUVE(), clip));
    const AnimationAssetBoneTrackUVE* child = FindTrackUVE(clip, "Child");
    ASSERT_NE(child, nullptr);
    ASSERT_EQ(child->samples.size(), 3U);
    EXPECT_FLOAT_EQ(child->samples[1].pose.position.y, 0.0F);
    EXPECT_FLOAT_EQ(child->samples[2].pose.position.y, 9.0F);
}

TEST(GltfAnimationConverterUVETest, RejectsUnknownBone) {
    HandBuiltAnimationUVE anim;
    anim.source.clipId = "Bad";
    AddChannelUVE(anim, "Ghost", GltfAnimationPathUVE::Translation,
                  GltfAnimationInterpolationUVE::Linear, {0.0F, 1.0F}, {0.0F, 0.0F, 0.0F, 0.0F, 0.0F, 0.0F});
    AnimationClipAssetUVE clip;
    EXPECT_FALSE(ConvertGltfAnimationUVE(anim.source, MakeTwoJointSkeletonAssetUVE(), clip));
}

TEST(GltfAnimationConverterUVETest, RejectsDuplicateBonePath) {
    HandBuiltAnimationUVE anim;
    anim.source.clipId = "Bad";
    AddChannelUVE(anim, "Child", GltfAnimationPathUVE::Translation,
                  GltfAnimationInterpolationUVE::Linear, {0.0F, 1.0F}, {0.0F, 0.0F, 0.0F, 0.0F, 0.0F, 0.0F});
    AddChannelUVE(anim, "Child", GltfAnimationPathUVE::Translation,
                  GltfAnimationInterpolationUVE::Step, {0.0F, 1.0F}, {0.0F, 0.0F, 0.0F, 0.0F, 0.0F, 0.0F});
    AnimationClipAssetUVE clip;
    EXPECT_FALSE(ConvertGltfAnimationUVE(anim.source, MakeTwoJointSkeletonAssetUVE(), clip));
}

TEST(GltfAnimationConverterUVETest, RejectsRepeatedKeyTime) {
    HandBuiltAnimationUVE anim;
    anim.source.clipId = "Bad";
    AddChannelUVE(anim, "Child", GltfAnimationPathUVE::Translation,
                  GltfAnimationInterpolationUVE::Linear, {0.0F, 0.0F}, {0.0F, 0.0F, 0.0F, 0.0F, 0.0F, 0.0F});
    AnimationClipAssetUVE clip;
    EXPECT_FALSE(ConvertGltfAnimationUVE(anim.source, MakeTwoJointSkeletonAssetUVE(), clip));
}

TEST(GltfAnimationConverterUVETest, RejectsNegativeKeyTime) {
    HandBuiltAnimationUVE anim;
    anim.source.clipId = "Bad";
    AddChannelUVE(anim, "Child", GltfAnimationPathUVE::Translation,
                  GltfAnimationInterpolationUVE::Linear, {-1.0F, 1.0F},
                  {0.0F, 0.0F, 0.0F, 0.0F, 0.0F, 0.0F});
    AnimationClipAssetUVE clip;
    EXPECT_FALSE(ConvertGltfAnimationUVE(anim.source, MakeTwoJointSkeletonAssetUVE(), clip));
}

TEST(GltfAnimationConverterUVETest, RejectsZeroDuration) {
    HandBuiltAnimationUVE anim;
    anim.source.clipId = "Bad";
    AddChannelUVE(anim, "Child", GltfAnimationPathUVE::Translation,
                  GltfAnimationInterpolationUVE::Linear, {0.0F}, {0.0F, 0.0F, 0.0F});
    AnimationClipAssetUVE clip;
    EXPECT_FALSE(ConvertGltfAnimationUVE(anim.source, MakeTwoJointSkeletonAssetUVE(), clip));
}

TEST(GltfAnimationConverterUVETest, RejectsZeroQuaternion) {
    HandBuiltAnimationUVE anim;
    anim.source.clipId = "Bad";
    AddChannelUVE(anim, "Child", GltfAnimationPathUVE::Rotation,
                  GltfAnimationInterpolationUVE::Linear, {0.0F, 1.0F},
                  {0.0F, 0.0F, 0.0F, 1.0F, 0.0F, 0.0F, 0.0F, 0.0F});
    AnimationClipAssetUVE clip;
    EXPECT_FALSE(ConvertGltfAnimationUVE(anim.source, MakeTwoJointSkeletonAssetUVE(), clip));
}

TEST(GltfAnimationConverterUVETest, RejectsValueCountMismatch) {
    HandBuiltAnimationUVE anim;
    anim.source.clipId = "Bad";
    AddChannelUVE(anim, "Child", GltfAnimationPathUVE::Translation,
                  GltfAnimationInterpolationUVE::Linear, {0.0F, 1.0F},
                  {0.0F, 0.0F, 0.0F, 0.0F, 0.0F, 0.0F});
    anim.source.channels.back().values.elementCount = 1U;
    AnimationClipAssetUVE clip;
    EXPECT_FALSE(ConvertGltfAnimationUVE(anim.source, MakeTwoJointSkeletonAssetUVE(), clip));
}

TEST(GltfAnimationConverterUVETest, RejectsEmptyChannels) {
    GltfAnimationSourceUVE source;
    source.clipId = "Empty";
    AnimationClipAssetUVE clip;
    EXPECT_FALSE(ConvertGltfAnimationUVE(source, MakeTwoJointSkeletonAssetUVE(), clip));
}

TEST(GltfAnimationConverterUVETest, DocumentParseConvertsAndRoundTripsClip) {
    const std::vector<std::byte> buffer = MakeEndToEndBufferUVE();
    const std::optional<GltfAnimationSourceUVE> source =
        ParseGltfAnimationSourceUVE(kEndToEndDocumentUVE, buffer);
    ASSERT_TRUE(source.has_value());
    EXPECT_EQ(source->clipId, "Wave");
    ASSERT_EQ(source->channels.size(), 1U);
    EXPECT_EQ(source->channels[0].bone, "Child");

    AnimationClipAssetUVE clip;
    ASSERT_TRUE(ConvertGltfAnimationUVE(*source, MakeTwoJointSkeletonAssetUVE(), clip));
    EXPECT_DOUBLE_EQ(clip.durationSeconds, 1.0);
    const AnimationAssetBoneTrackUVE* child = FindTrackUVE(clip, "Child");
    ASSERT_NE(child, nullptr);
    ASSERT_EQ(child->samples.size(), 2U);

    const std::filesystem::path path = TestPathUVE("uve_gltf_animation_clip_round_trip.uvanim");
    std::filesystem::remove(path);
    ASSERT_TRUE(SaveAnimationClipAssetUVE(clip, path));
    AnimationClipAssetUVE reloaded;
    ASSERT_TRUE(LoadAnimationClipAssetUVE(path, reloaded));
    EXPECT_EQ(reloaded.clipId, "Wave");
    EXPECT_DOUBLE_EQ(reloaded.durationSeconds, 1.0);
    ASSERT_EQ(reloaded.bones.size(), 1U);
    EXPECT_EQ(reloaded.bones[0].bone, "Child");
    ASSERT_EQ(reloaded.bones[0].samples.size(), 2U);
    EXPECT_NEAR(reloaded.bones[0].samples[1].pose.rotation.z, 0.70710678F, 1.0e-6F);
    std::filesystem::remove(path);
}

TEST(GltfAnimationConverterUVETest, RejectsDocumentWithCubicspline) {
    std::string document = kEndToEndDocumentUVE;
    ASSERT_NE(document.find("LINEAR"), std::string::npos);
    document.replace(document.find("LINEAR"), 6U, "CUBICSPLINE");
    EXPECT_FALSE(ParseGltfAnimationSourceUVE(document, MakeEndToEndBufferUVE()).has_value());
}

TEST(GltfAnimationConverterUVETest, RejectsDocumentWithMorphWeightsPath) {
    std::string document = kEndToEndDocumentUVE;
    ASSERT_NE(document.find("\"rotation\""), std::string::npos);
    document.replace(document.find("\"rotation\""), 10U, "\"weights\"");
    EXPECT_FALSE(ParseGltfAnimationSourceUVE(document, MakeEndToEndBufferUVE()).has_value());
}

TEST(GltfAnimationConverterUVETest, RejectsDocumentWithNonJointTarget) {
    std::string document = kEndToEndDocumentUVE;
    ASSERT_NE(document.find("\"node\": 1"), std::string::npos);
    document.replace(document.find("\"node\": 1"), 8U, "\"node\": 7");
    EXPECT_FALSE(ParseGltfAnimationSourceUVE(document, MakeEndToEndBufferUVE()).has_value());
}

TEST(GltfAnimationConverterUVETest, RejectsDocumentWithoutAnimation) {
    constexpr const char* kNoAnimation = R"json({
  "asset": {"version": "2.0"},
  "nodes": [{"name": "Root"}],
  "skins": [{"joints": [0]}]
})json";
    EXPECT_FALSE(ParseGltfAnimationSourceUVE(kNoAnimation, MakeEndToEndBufferUVE()).has_value());
}

// The Tier 2.6 proof: a glTF skinned mesh animates. The end-to-end document imports through both
// converters and both asset round-trips, then the clip's own samples drive the existing CPU pose
// resolution and skinning. Mid-key sampling is Tier 3.1's runtime sampler; here the samples are
// the clip's exact keys.
TEST(GltfAnimationConverterUVETest, SkinnedMeshAnimatesEndToEnd) {
    const std::vector<std::byte> buffer = MakeEndToEndBufferUVE();

    const std::optional<GltfSkinSourceUVE> skinSource =
        ParseGltfSkinSourceUVE(kEndToEndDocumentUVE, buffer);
    ASSERT_TRUE(skinSource.has_value());
    GltfSkinResultUVE skinResult;
    ASSERT_TRUE(ConvertGltfSkinUVE(*skinSource, skinResult));
    const std::filesystem::path skeletonPath = TestPathUVE("uve_gltf_end_to_end_skeleton.uvskel");
    std::filesystem::remove(skeletonPath);
    ASSERT_TRUE(SaveSkeletonAssetUVE(skinResult.skeleton, skeletonPath));
    SkeletonAssetUVE skeleton;
    ASSERT_TRUE(LoadSkeletonAssetUVE(skeletonPath, skeleton));
    EXPECT_EQ(skeleton, skinResult.skeleton);
    std::filesystem::remove(skeletonPath);

    const std::optional<GltfAnimationSourceUVE> animSource =
        ParseGltfAnimationSourceUVE(kEndToEndDocumentUVE, buffer);
    ASSERT_TRUE(animSource.has_value());
    AnimationClipAssetUVE clip;
    ASSERT_TRUE(ConvertGltfAnimationUVE(*animSource, skeleton, clip));

    MeshAssetUVE mesh;
    mesh.vertices = {
        MeshVertexUVE{.position = Math::Vector3UVE{0.0F, 0.0F, 0.0F},
                      .normal = Math::Vector3UVE{0.0F, 0.0F, 1.0F}},
        MeshVertexUVE{.position = Math::Vector3UVE{1.0F, 1.0F, 0.0F},
                      .normal = Math::Vector3UVE{0.0F, 0.0F, 1.0F}},
        MeshVertexUVE{.position = Math::Vector3UVE{1.0F, 2.0F, 0.0F},
                      .normal = Math::Vector3UVE{0.0F, 0.0F, 1.0F}},
    };
    mesh.indices = {0U, 1U, 2U};
    mesh.skinningInfluences = skinResult.influences;
    mesh.joints = skinResult.joints;
    ASSERT_TRUE(IsMeshSkinningDataValidUVE(mesh));

    const AnimationAssetBoneTrackUVE* child = FindTrackUVE(clip, "Child");
    ASSERT_NE(child, nullptr);
    ASSERT_EQ(clip.rest.size(), 2U);

    for (const double time : {0.0, 1.0}) {
        const AnimationAssetSampleUVE* sample = FindSampleUVE(*child, time);
        ASSERT_NE(sample, nullptr) << "no baked sample at t=" << time;
        std::vector<Math::Matrix4x4UVE> localPose;
        localPose.push_back(Math::Matrix4x4UVE::ComposeTrsUVE(
            clip.rest[0].position, clip.rest[0].rotation, clip.rest[0].scale));
        localPose.push_back(Math::Matrix4x4UVE::ComposeTrsUVE(sample->pose.position,
                                                              sample->pose.rotation, sample->pose.scale));
        std::vector<Math::Matrix4x4UVE> skinningMatrices;
        ASSERT_TRUE(TryResolvePoseUVE(mesh.joints, localPose, skinningMatrices));
        std::vector<MeshVertexUVE> skinned;
        ASSERT_TRUE(TrySkinMeshUVE(mesh, skinningMatrices, skinned));
        ASSERT_EQ(skinned.size(), 3U);
        if (time == 0.0) {
            EXPECT_NEAR(skinned[0].position.x, 0.0F, 1.0e-4F);
            EXPECT_NEAR(skinned[1].position.x, 1.0F, 1.0e-4F);
            EXPECT_NEAR(skinned[1].position.y, 1.0F, 1.0e-4F);
            EXPECT_NEAR(skinned[2].position.x, 1.0F, 1.0e-4F);
            EXPECT_NEAR(skinned[2].position.y, 2.0F, 1.0e-4F);
        } else {
            // The child turned 90 degrees about Z around its own center (0,1,0): v0
            // (root-bound) stays, while v1 orbits from (1,1,0) to (0,2,0) and v2 from
            // (1,2,0) to (-1,2,0).
            EXPECT_NEAR(skinned[0].position.x, 0.0F, 1.0e-4F);
            EXPECT_NEAR(skinned[0].position.y, 0.0F, 1.0e-4F);
            EXPECT_NEAR(skinned[1].position.x, 0.0F, 1.0e-4F);
            EXPECT_NEAR(skinned[1].position.y, 2.0F, 1.0e-4F);
            EXPECT_NEAR(skinned[2].position.x, -1.0F, 1.0e-4F);
            EXPECT_NEAR(skinned[2].position.y, 2.0F, 1.0e-4F);
            EXPECT_GT(std::abs(skinned[1].position.x - 1.0F), 0.5F);
        }
    }
}

} // namespace UVE::Asset::Tests
