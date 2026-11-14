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

using Math::Matrix4x4UVE;
using Math::Vector3UVE;

[[nodiscard]] const Asset::AnimationAssetPoseUVE* PoseAtUVE(const Asset::AnimationAssetBoneTrackUVE& track, const double time) {
    const Asset::AnimationAssetSampleUVE* best = &track.samples.front();
    for (const Asset::AnimationAssetSampleUVE& sample : track.samples) {
        if (sample.timeSeconds <= time + 1e-6) {
            best = &sample;
        }
    }
    return &best->pose;
}

/// Each joint's world matrix for `clip` at `time` on a skeleton whose rest is `rest` (locals),
/// taking translation from the clip only where `playback` says so.
[[nodiscard]] std::vector<Matrix4x4UVE> PoseUVE(const RetargetSkeletonUVE& rest, const Asset::AnimationClipAssetUVE& clip, const double time,
                                               const ConformedPlaybackUVE& playback) {
    std::vector<Matrix4x4UVE> world(rest.bones.size());
    for (std::size_t bone = 0U; bone < rest.bones.size(); ++bone) {
        const RetargetBoneUVE& b = rest.bones[bone];
        Vector3UVE position = b.position;
        Math::QuaternionUVE rotation = b.rotation;
        for (const Asset::AnimationAssetBoneTrackUVE& track : clip.bones) {
            if (track.bone == b.name && !track.samples.empty()) {
                const Asset::AnimationAssetPoseUVE& pose = *PoseAtUVE(track, time);
                rotation = pose.rotation;
                position = playback.PositionUVE(b.name, pose.position, b.position);
                break;
            }
        }
        const Matrix4x4UVE local = Matrix4x4UVE::ComposeTrsUVE(position, rotation, Vector3UVE{b.scale, b.scale, b.scale});
        world[bone] = b.parent < 0 ? local : world[static_cast<std::size_t>(b.parent)] * local;
    }
    return world;
}

[[nodiscard]] std::vector<Vector3UVE> SkinUVE(const Asset::MeshAssetUVE& mesh, const std::vector<Matrix4x4UVE>& world) {
    std::vector<Vector3UVE> out(mesh.vertices.size());
    for (std::size_t v = 0U; v < mesh.vertices.size(); ++v) {
        const Asset::MeshSkinningInfluenceUVE& influence = mesh.skinningInfluences[v];
        Vector3UVE sum{};
        float total = 0.0F;
        for (std::size_t slot = 0U; slot < Asset::kMaxJointInfluencesUVE; ++slot) {
            const float w = influence.weights[slot];
            const std::uint32_t joint = influence.joints[slot];
            if (w <= 0.0F || joint >= world.size()) {
                continue;
            }
            const Matrix4x4UVE skin = world[joint] * mesh.joints[joint].inverseBindMatrix;
            sum += Math::TransformPointUVE(skin, mesh.vertices[v].position) * w;
            total += w;
        }
        out[v] = total > 0.0F ? sum * (1.0F / total) : mesh.vertices[v].position;
    }
    return out;
}

[[nodiscard]] std::vector<RestBoneViewUVE> ViewsUVE(const RetargetSkeletonUVE& skeleton) {
    std::vector<RestBoneViewUVE> views;
    for (const RetargetBoneUVE& bone : skeleton.bones) {
        views.push_back(RestBoneViewUVE{bone.name, bone.parent, bone.position, bone.rotation});
    }
    return views;
}

TEST(RetargetRealRigUVETest, TheAnimationAndTheMeshComeThroughRetargetingUnchanged) {
    const char* const modelPath = std::getenv("UVE_RETARGET_MODEL");
    const char* const clipPath = std::getenv("UVE_RETARGET_CLIP");
    if (modelPath == nullptr || clipPath == nullptr) {
        GTEST_SKIP() << "set UVE_RETARGET_MODEL and UVE_RETARGET_CLIP to compare a real character";
    }
    Asset::MeshAssetUVE mesh;
    Asset::AnimationClipAssetUVE clip;
    ASSERT_TRUE(Asset::LoadMeshAssetUVE(modelPath, mesh)) << modelPath;
    ASSERT_TRUE(Asset::LoadAnimationClipAssetUVE(clipPath, clip)) << clipPath;
    ASSERT_TRUE(mesh.IsSkinnedUVE());
    const std::optional<RetargetSkeletonUVE> rig = RigFromMeshUVE(mesh);
    ASSERT_TRUE(rig.has_value());
    const HumanoidReferenceUVE& reference = GetHumanoidReferenceUVE();

    // ---- Retarget, as Generate does (in memory) --------------------------------------------------
    const HumanoidMatchUVE match = MatchHumanoidWithBrainUVE(*rig, reference, GetDefaultRigBrainUVE());
    std::printf("[ real rig ] %zu bones: %zu good, %zu check, %zu broken, %zu missing\n", rig->bones.size(),
                match.CountUVE(JointStatusUVE::Good), match.CountUVE(JointStatusUVE::Warning), match.CountUVE(JointStatusUVE::Broken),
                match.CountUVE(JointStatusUVE::Missing));
    std::string error;
    const std::optional<ConformedRigUVE> conformed = ConformSkeletonUVE(*rig, match, reference, &error);
    ASSERT_TRUE(conformed.has_value()) << error;
    Asset::MeshAssetUVE after = mesh;
    // UVE_RETARGET_MODE=bake: the character takes the A-pose as its rest (the default Generate).
    const char* const modeText = std::getenv("UVE_RETARGET_MODE");
    // bake: vertices moved into the A-pose; keep: own rest; default (apose): the skeleton rests in
    // the A-pose, the vertices stay where they were made.
    const std::string_view mode = modeText == nullptr ? std::string_view{"apose"} : std::string_view{modeText};
    const bool bake = mode == "bake";
    ASSERT_TRUE(ConformMeshUVE(after, *conformed, &error, SkinBlendUVE::DualQuaternion,
                               bake ? ConformMeshModeUVE::BakeAPose : ConformMeshModeUVE::KeepRestPose))
        << error;
    const MeshDistortionUVE distortion = MeasureMeshDistortionUVE(mesh, after);
    std::printf("[ real rig ] %s: worst edge change %.1f%%, %.2f%% of edges over 10%%\n", bake ? "vertices moved into the A-pose" : "vertices untouched",
                distortion.maximumEdgeChange * 100.0, distortion.fractionOverTenPercent * 100.0);
    // The clip's rig: its own rest skeleton when it has one, else the character's.
    RetargetSkeletonUVE clipRig = *rig;
    if (!clip.rest.empty()) {
        clipRig.bones.clear();
        for (const Asset::AnimationAssetRestBoneUVE& rest : clip.rest) {
            clipRig.bones.push_back(RetargetBoneUVE{rest.bone, rest.parent, rest.position, rest.rotation, rest.scale.x});
        }
    }
    const HumanoidMatchUVE clipMatch = MatchHumanoidWithBrainUVE(clipRig, reference, GetDefaultRigBrainUVE());
    const std::optional<ConformedRigUVE> clipConformed = ConformSkeletonUVE(clipRig, clipMatch, reference, &error);
    ASSERT_TRUE(clipConformed.has_value()) << error;
    const std::optional<Asset::AnimationClipAssetUVE> conformedClip = ConformClipUVE(clip, clipRig, *clipConformed, reference, &error);
    ASSERT_TRUE(conformedClip.has_value()) << error;

    // ---- The mesh at rest: not one vertex moved -------------------------------------------------------
    float restMoved = 0.0F;
    for (std::size_t v = 0U; v < mesh.vertices.size(); ++v) {
        restMoved = std::max(restMoved, Math::LengthUVE(after.vertices[v].position - Math::RotateVectorUVE(conformed->orientation, mesh.vertices[v].position)));
    }
    if (!bake) {
        EXPECT_LT(restMoved, 1e-5F) << "no vertex of the file moves";
    }

    // ---- A picture of the rest pose and the retarget pose ------------------------------------------
    if (const char* const dump = std::getenv("UVE_RETARGET_DUMP"); dump != nullptr) {
        // The character at its own rest, and posed in the humanoid's A-pose (what a conformed clip's
        // rest pose does to it), the mesh skinned both times; and the humanoid's joints.
        std::vector<Matrix4x4UVE> apose;
        for (const WorldTransformUVE& w : ComputeWorldTransformsUVE(conformed->skeleton)) {
            apose.push_back(Matrix4x4UVE::ComposeTrsUVE(w.position, w.rotation, Vector3UVE{1.0F, 1.0F, 1.0F}));
        }
        const std::vector<Vector3UVE> posed = SkinUVE(after, apose);
        if (FILE* const file = std::fopen((std::string{dump} + "/apose.txt").c_str(), "w"); file != nullptr) {
            for (std::size_t v = 0U; v < posed.size(); v += 5U) {
                const Vector3UVE a = after.vertices[v].position;
                std::fprintf(file, "%f %f %f %f %f %f\n", a.x, a.y, a.z, posed[v].x, posed[v].y, posed[v].z);
            }
            std::fclose(file);
        }
        if (FILE* const file = std::fopen((std::string{dump} + "/reference.txt").c_str(), "w"); file != nullptr) {
            const std::vector<WorldTransformUVE> world = ComputeWorldTransformsUVE(reference.skeleton);
            for (std::size_t bone = 0U; bone < world.size(); ++bone) {
                const std::int32_t parent = reference.skeleton.bones[bone].parent;
                if (parent < 0 || reference.info[bone].optional) {
                    continue;
                }
                const Vector3UVE& a = world[static_cast<std::size_t>(parent)].position;
                const Vector3UVE& b = world[bone].position;
                std::fprintf(file, "%f %f %f %f %f %f\n", a.x, a.y, a.z, b.x, b.y, b.z);
            }
            std::fclose(file);
        }
    }

    // ---- Every frame: the character played before and after ----------------------------------------
    // The skeleton the character rests in: the A-pose (what Retarget writes as its skeleton), or the
    // mesh's own bind.
    const RetargetSkeletonUVE afterRest = mode == "apose" ? conformed->skeleton : *RigFromMeshUVE(after);
    const std::vector<RestBoneViewUVE> afterViews = ViewsUVE(afterRest);
    const ConformedPlaybackUVE playback = PlanConformedPlaybackUVE(*conformedClip, afterViews);
    const ConformedPlaybackUVE plain{};
    float low = 1e30F;
    float high = -1e30F;
    for (const Asset::MeshVertexUVE& vertex : mesh.vertices) {
        low = std::min(low, vertex.position.y);
        high = std::max(high, vertex.position.y);
    }
    const float height = std::max(high - low, 1e-6F);
    double worstVertex = 0.0;
    double meanVertex = 0.0;
    std::size_t samples = 0U;
    const double duration = std::max(clip.durationSeconds, 1e-3);
    constexpr int kFramesUVE = 24;
    for (int frame = 0; frame <= kFramesUVE; ++frame) {
        const double time = duration * frame / kFramesUVE;
        const std::vector<Vector3UVE> before = SkinUVE(mesh, PoseUVE(*rig, clip, time, plain));
        const std::vector<Vector3UVE> now = SkinUVE(after, PoseUVE(afterRest, *conformedClip, time, playback));
        // Compared about the hips (the clip may move the whole body; heights are the same character).
        const Vector3UVE beforeCentre = std::accumulate(before.begin(), before.end(), Vector3UVE{}) * (1.0F / static_cast<float>(before.size()));
        const Vector3UVE nowCentre = std::accumulate(now.begin(), now.end(), Vector3UVE{}) * (1.0F / static_cast<float>(now.size()));
        if (const char* const dump = std::getenv("UVE_RETARGET_DUMP"); dump != nullptr && frame % 6 == 0) {
            // Points for a picture: before | after, every 13th vertex, about the centre.
            if (FILE* const file = std::fopen((std::string{dump} + "/frame" + std::to_string(frame) + ".txt").c_str(), "w"); file != nullptr) {
                for (std::size_t v = 0U; v < before.size(); v += 13U) {
                    const Vector3UVE a = Math::RotateVectorUVE(conformed->orientation, before[v] - beforeCentre);
                    const Vector3UVE b = now[v] - nowCentre;
                    std::fprintf(file, "%f %f %f %f %f %f\n", a.x, a.y, a.z, b.x, b.y, b.z);
                }
                std::fclose(file);
            }
        }
        for (std::size_t v = 0U; v < before.size(); v += 7U) {
            const Vector3UVE a = Math::RotateVectorUVE(conformed->orientation, before[v] - beforeCentre);
            const Vector3UVE b = now[v] - nowCentre;
            const double error = Math::LengthUVE(a - b) / height;
            worstVertex = std::max(worstVertex, error);
            meanVertex += error;
            ++samples;
        }
    }
    meanVertex /= static_cast<double>(std::max<std::size_t>(samples, 1U));
    std::printf("[ real rig ] over %d frames: vertices off by %.3f%% of the height on average, %.3f%% at worst\n", kFramesUVE + 1,
                meanVertex * 100.0, worstVertex * 100.0);
    EXPECT_LT(meanVertex, 0.005) << "the animation plays the same on the retargeted character";
    EXPECT_LT(worstVertex, 0.03);
}

} // namespace
} // namespace UVE::Retarget
