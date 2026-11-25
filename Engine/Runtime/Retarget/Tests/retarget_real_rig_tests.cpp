// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
//
// A real character and its clip, before and after retargeting, compared vertex by vertex and joint
// by joint over every frame. Opt in with two files (an imported model and a clip made for it):
//   UVE_RETARGET_MODEL=<path to .uvmodel>  UVE_RETARGET_CLIP=<path to .uvanim>
// Skipped when they are not given, so the suite never depends on a file outside the repository.

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <numeric>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <gtest/gtest.h>

#include "animation_clip_asset.h"
#include "matrix4x4.h"
#include "mesh_asset.h"
#include "retarget_brain.h"
#include "retarget_conform.h"
#include "retarget_humanoid.h"
#include "retarget_match.h"
#include "retarget_playback.h"

namespace UVE::Retarget {
namespace {

using Math::Matrix4x4;
using Math::Vector3;

[[nodiscard]] const Asset::AnimationAssetPose* PoseAt(const Asset::AnimationAssetBoneTrack& track, const double time) {
    const Asset::AnimationAssetSample* best = &track.samples.front();
    for (const Asset::AnimationAssetSample& sample : track.samples) {
        if (sample.timeSeconds <= time + 1e-6) {
            best = &sample;
        }
    }
    return &best->pose;
}

/// Each joint's world matrix for `clip` at `time` on a skeleton whose rest is `rest` (locals),
/// taking translation from the clip only where `playback` says so.
[[nodiscard]] std::vector<Matrix4x4> Pose(const RetargetSkeleton& rest, const Asset::AnimationClipAsset& clip, const double time,
                                               const ConformedPlayback& playback) {
    std::vector<Matrix4x4> world(rest.bones.size());
    for (std::size_t bone = 0U; bone < rest.bones.size(); ++bone) {
        const RetargetBone& b = rest.bones[bone];
        Vector3 position = b.position;
        Math::Quaternion rotation = b.rotation;
        for (const Asset::AnimationAssetBoneTrack& track : clip.bones) {
            if (track.bone == b.name && !track.samples.empty()) {
                const Asset::AnimationAssetPose& pose = *PoseAt(track, time);
                rotation = pose.rotation;
                position = playback.Position(b.name, pose.position, b.position);
                break;
            }
        }
        const Matrix4x4 local = Matrix4x4::ComposeTrs(position, rotation, Vector3{b.scale, b.scale, b.scale});
        world[bone] = b.parent < 0 ? local : world[static_cast<std::size_t>(b.parent)] * local;
    }
    return world;
}

[[nodiscard]] std::vector<Vector3> Skin(const Asset::MeshAsset& mesh, const std::vector<Matrix4x4>& world) {
    std::vector<Vector3> out(mesh.vertices.size());
    for (std::size_t v = 0U; v < mesh.vertices.size(); ++v) {
        const Asset::MeshSkinningInfluence& influence = mesh.skinningInfluences[v];
        Vector3 sum{};
        float total = 0.0F;
        for (std::size_t slot = 0U; slot < Asset::kMaxJointInfluences; ++slot) {
            const float w = influence.weights[slot];
            const std::uint32_t joint = influence.joints[slot];
            if (w <= 0.0F || joint >= world.size()) {
                continue;
            }
            const Matrix4x4 skin = world[joint] * mesh.joints[joint].inverseBindMatrix;
            sum += Math::TransformPoint(skin, mesh.vertices[v].position) * w;
            total += w;
        }
        out[v] = total > 0.0F ? sum * (1.0F / total) : mesh.vertices[v].position;
    }
    return out;
}

[[nodiscard]] std::vector<RestBoneView> Views(const RetargetSkeleton& skeleton) {
    std::vector<RestBoneView> views;
    for (const RetargetBone& bone : skeleton.bones) {
        views.push_back(RestBoneView{bone.name, bone.parent, bone.position, bone.rotation});
    }
    return views;
}

TEST(RetargetRealRigTest, TheAnimationAndTheMeshComeThroughRetargetingUnchanged) {
    const char* const modelPath = std::getenv("UVE_RETARGET_MODEL");
    const char* const clipPath = std::getenv("UVE_RETARGET_CLIP");
    if (modelPath == nullptr || clipPath == nullptr) {
        GTEST_SKIP() << "set UVE_RETARGET_MODEL and UVE_RETARGET_CLIP to compare a real character";
    }
    Asset::MeshAsset mesh;
    Asset::AnimationClipAsset clip;
    ASSERT_TRUE(Asset::LoadMeshAsset(modelPath, mesh)) << modelPath;
    ASSERT_TRUE(Asset::LoadAnimationClipAsset(clipPath, clip)) << clipPath;
    ASSERT_TRUE(mesh.IsSkinned());
    const std::optional<RetargetSkeleton> rig = RigFromMesh(mesh);
    ASSERT_TRUE(rig.has_value());
    const HumanoidReference& reference = GetHumanoidReference();

    // ---- Retarget, as Generate does (in memory) --------------------------------------------------
    const HumanoidMatch match = MatchHumanoidWithBrain(*rig, reference, GetDefaultRigBrain());
    std::printf("[ real rig ] %zu bones: %zu good, %zu check, %zu broken, %zu missing\n", rig->bones.size(),
                match.Count(JointStatus::Good), match.Count(JointStatus::Warning), match.Count(JointStatus::Broken),
                match.Count(JointStatus::Missing));
    std::string error;
    const std::optional<ConformedRig> conformed = ConformSkeleton(*rig, match, reference, &error);
    ASSERT_TRUE(conformed.has_value()) << error;
    Asset::MeshAsset after = mesh;
    // UVE_RETARGET_MODE=bake: the character takes the A-pose as its rest (the default Generate).
    const char* const modeText = std::getenv("UVE_RETARGET_MODE");
    // bake: vertices moved into the A-pose; keep: own rest; default (apose): the skeleton rests in
    // the A-pose, the vertices stay where they were made.
    const std::string_view mode = modeText == nullptr ? std::string_view{"apose"} : std::string_view{modeText};
    const bool bake = mode == "bake";
    ASSERT_TRUE(ConformMesh(after, *conformed, &error, SkinBlend::DualQuaternion,
                               bake ? ConformMeshMode::BakeAPose : ConformMeshMode::KeepRestPose))
        << error;
    const MeshDistortion distortion = MeasureMeshDistortion(mesh, after);
    std::printf("[ real rig ] %s: worst edge change %.1f%%, %.2f%% of edges over 10%%\n", bake ? "vertices moved into the A-pose" : "vertices untouched",
                distortion.maximumEdgeChange * 100.0, distortion.fractionOverTenPercent * 100.0);
    // The clip's rig: its own rest skeleton when it has one, else the character's.
    RetargetSkeleton clipRig = *rig;
    if (!clip.rest.empty()) {
        clipRig.bones.clear();
        for (const Asset::AnimationAssetRestBone& rest : clip.rest) {
            clipRig.bones.push_back(RetargetBone{rest.bone, rest.parent, rest.position, rest.rotation, rest.scale.x});
        }
    }
    const HumanoidMatch clipMatch = MatchHumanoidWithBrain(clipRig, reference, GetDefaultRigBrain());
    const std::optional<ConformedRig> clipConformed = ConformSkeleton(clipRig, clipMatch, reference, &error);
    ASSERT_TRUE(clipConformed.has_value()) << error;
    const std::optional<Asset::AnimationClipAsset> conformedClip = ConformClip(clip, clipRig, *clipConformed, reference, &error);
    ASSERT_TRUE(conformedClip.has_value()) << error;

    // ---- The mesh at rest: not one vertex moved -------------------------------------------------------
    float restMoved = 0.0F;
    for (std::size_t v = 0U; v < mesh.vertices.size(); ++v) {
        restMoved = std::max(restMoved, Math::Length(after.vertices[v].position - Math::RotateVector(conformed->orientation, mesh.vertices[v].position)));
    }
    if (!bake) {
        EXPECT_LT(restMoved, 1e-5F) << "no vertex of the file moves";
    }

    // ---- A picture of the rest pose and the retarget pose ------------------------------------------
    if (const char* const dump = std::getenv("UVE_RETARGET_DUMP"); dump != nullptr) {
        // The character at its own rest, and posed in the humanoid's A-pose (what a conformed clip's
        // rest pose does to it), the mesh skinned both times; and the humanoid's joints.
        std::vector<Matrix4x4> apose;
        for (const WorldTransform& w : ComputeWorldTransforms(conformed->skeleton)) {
            apose.push_back(Matrix4x4::ComposeTrs(w.position, w.rotation, Vector3{1.0F, 1.0F, 1.0F}));
        }
        const std::vector<Vector3> posed = Skin(after, apose);
        if (FILE* const file = std::fopen((std::string{dump} + "/apose.txt").c_str(), "w"); file != nullptr) {
            for (std::size_t v = 0U; v < posed.size(); v += 5U) {
                const Vector3 a = after.vertices[v].position;
                std::fprintf(file, "%f %f %f %f %f %f\n", a.x, a.y, a.z, posed[v].x, posed[v].y, posed[v].z);
            }
            std::fclose(file);
        }
        if (FILE* const file = std::fopen((std::string{dump} + "/reference.txt").c_str(), "w"); file != nullptr) {
            const std::vector<WorldTransform> world = ComputeWorldTransforms(reference.skeleton);
            for (std::size_t bone = 0U; bone < world.size(); ++bone) {
                const std::int32_t parent = reference.skeleton.bones[bone].parent;
                if (parent < 0 || reference.info[bone].optional) {
                    continue;
                }
                const Vector3& a = world[static_cast<std::size_t>(parent)].position;
                const Vector3& b = world[bone].position;
                std::fprintf(file, "%f %f %f %f %f %f\n", a.x, a.y, a.z, b.x, b.y, b.z);
            }
            std::fclose(file);
        }
    }

    // ---- Every frame: the character played before and after ----------------------------------------
    // The skeleton the character rests in: the A-pose (what Retarget writes as its skeleton), or the
    // mesh's own bind.
    const RetargetSkeleton afterRest = mode == "apose" ? conformed->skeleton : *RigFromMesh(after);
    const std::vector<RestBoneView> afterViews = Views(afterRest);
    const ConformedPlayback playback = PlanConformedPlayback(*conformedClip, afterViews);
    const ConformedPlayback plain{};
    float low = 1e30F;
    float high = -1e30F;
    for (const Asset::MeshVertex& vertex : mesh.vertices) {
        low = std::min(low, vertex.position.y);
        high = std::max(high, vertex.position.y);
    }
    const float height = std::max(high - low, 1e-6F);
    double worstVertex = 0.0;
    double meanVertex = 0.0;
    std::size_t samples = 0U;
    const double duration = std::max(clip.durationSeconds, 1e-3);
    constexpr int kFrames = 24;
    for (int frame = 0; frame <= kFrames; ++frame) {
        const double time = duration * frame / kFrames;
        const std::vector<Vector3> before = Skin(mesh, Pose(*rig, clip, time, plain));
        const std::vector<Vector3> now = Skin(after, Pose(afterRest, *conformedClip, time, playback));
        // Compared about the hips (the clip may move the whole body; heights are the same character).
        const Vector3 beforeCentre = std::accumulate(before.begin(), before.end(), Vector3{}) * (1.0F / static_cast<float>(before.size()));
        const Vector3 nowCentre = std::accumulate(now.begin(), now.end(), Vector3{}) * (1.0F / static_cast<float>(now.size()));
        if (const char* const dump = std::getenv("UVE_RETARGET_DUMP"); dump != nullptr && frame % 6 == 0) {
            // Points for a picture: before | after, every 13th vertex, about the centre.
            if (FILE* const file = std::fopen((std::string{dump} + "/frame" + std::to_string(frame) + ".txt").c_str(), "w"); file != nullptr) {
                for (std::size_t v = 0U; v < before.size(); v += 13U) {
                    const Vector3 a = Math::RotateVector(conformed->orientation, before[v] - beforeCentre);
                    const Vector3 b = now[v] - nowCentre;
                    std::fprintf(file, "%f %f %f %f %f %f\n", a.x, a.y, a.z, b.x, b.y, b.z);
                }
                std::fclose(file);
            }
        }
        for (std::size_t v = 0U; v < before.size(); v += 7U) {
            const Vector3 a = Math::RotateVector(conformed->orientation, before[v] - beforeCentre);
            const Vector3 b = now[v] - nowCentre;
            const double error = Math::Length(a - b) / height;
            worstVertex = std::max(worstVertex, error);
            meanVertex += error;
            ++samples;
        }
    }
    meanVertex /= static_cast<double>(std::max<std::size_t>(samples, 1U));
    std::printf("[ real rig ] over %d frames: vertices off by %.3f%% of the height on average, %.3f%% at worst\n", kFrames + 1,
                meanVertex * 100.0, worstVertex * 100.0);
    EXPECT_LT(meanVertex, 0.005) << "the animation plays the same on the retargeted character";
    EXPECT_LT(worstVertex, 0.03);
}

} // namespace
} // namespace UVE::Retarget
